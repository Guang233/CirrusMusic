#include "../support/decode_eapi_request.h"
#include "../support/fake_http.h"
#include "application/backend/backend_facade.h"
#include "application/music/music_query_service.h"
#include "infrastructure/netease/music/library_mapper.h"
#include "infrastructure/netease/music/music_gateway.h"
#include "infrastructure/netease/music/music_mapper.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QtTest>
using namespace cirrus::domain;
using namespace cirrus::application;
using namespace cirrus::netease;
using namespace cirrus::test;
namespace
{
QJsonObject json(QByteArray value) { return QJsonDocument::fromJson(value).object(); }
MusicQuery query(int kind, QString account = "1", CatalogPage page = {2, 0})
{
    switch (kind) {
    case 0:
        return MyPlaylistsQuery{account, page};
    case 1:
        return LikedSongIdsQuery{account};
    case 2:
        return SavedAlbumsQuery{account, page};
    default:
        return SavedArtistsQuery{account, page};
    }
}
MusicResult result(const QSignalSpy& spy) { return qvariant_cast<MusicResult>(spy.last().at(2)); }
struct Harness {
    SessionCoordinator session;
    FakeManager* http = nullptr;
    SessionApiClient client;
    MusicGateway gateway{client};
    MusicQueryService service{session, gateway};
    static std::unique_ptr<HttpTransport> transport(QList<Plan> plans, FakeManager*& http)
    {
        auto m = std::make_unique<FakeManager>();
        http = m.get();
        m->plans = std::move(plans);
        return std::make_unique<HttpTransport>(std::move(m));
    }
    explicit Harness(QList<Plan> plans, bool authenticated = true, QString account = "1")
        : client(transport(std::move(plans), http), "synthetic-library")
    {
        QVERIFY(client.advanceTo(session.snapshot().token));
        QObject::connect(&session, &SessionCoordinator::changed, &client,
                         [this](SessionSnapshot s) { QVERIFY(client.advanceTo(s.token)); });
        if (authenticated) {
            const auto attempt = session.beginAuthentication();
            const auto prepared = session.prepareAuthentication(attempt, account);
            QVERIFY(prepared);
            QVERIFY(session.completeAuthentication(*prepared));
            // Synthetic Cookie seed; full authentication/promotion covered by runtime test.
            Plan seed{R"({"code":200,"unikey":"synthetic"})"};
            seed.headers.append(std::pair<QByteArray, QByteArray>{
                "Set-Cookie", "MUSIC_U=synthetic-original; Domain=.music.163.com; Path=/"});
            http->plans.prepend(seed);
            QSignalSpy done(&client, &SessionApiClient::completed);
            QVERIFY(client.submit(session.snapshot().token, ApiOperation::QrKey));
            QTRY_COMPARE(done.size(), 1);
        }
    }
};
} // namespace
class LibraryTest : public QObject
{
    Q_OBJECT
  private slots:
    void contracts()
    {
        QCOMPARE(SavedAlbumsQuery{"1"}.page.limit, 25);
        QCOMPARE(SavedArtistsQuery{"1"}.page.limit, 25);
        const QString wide = "9007199254740993";
        const QStringList paths{"/api/user/playlist", "/api/song/like/get", "/api/album/sublist",
                                "/api/artist/sublist"};
        for (int i = 0; i < 4; ++i) {
            const auto req = libraryRequest(query(i, wide, {25, 50}));
            QVERIFY(req);
            const auto contract = operationContract(req->operation);
            QVERIFY(contract);
            QCOMPARE(contract->logicalPath, paths[i]);
            QCOMPARE(contract->protocol, i == 1 ? Protocol::Eapi : Protocol::Weapi);
            QCOMPARE(contract->responseLimit, (i == 3 ? 2 : 4) * 1024 * 1024);
            QVERIFY(requiresAccountCookie(req->operation));
            const auto payload = operationPayload(req->operation, req->parameters);
            QVERIFY(payload);
            if (i < 2)
                QCOMPARE(payload->value("uid").toString(), wide);
            if (i == 0)
                QCOMPARE(payload->value("includeVideo"), QJsonValue(false));
            if (i > 1)
                QCOMPARE(payload->value("total"), QJsonValue(true));
            auto extra = req->parameters;
            extra.insert("url", "arbitrary");
            QVERIFY(!operationPayload(req->operation, extra));
            if (i != 1) {
                extra = req->parameters;
                extra.insert("limit", 1.5);
                QVERIFY(!operationPayload(req->operation, extra));
                extra.insert("limit", 101);
                QVERIFY(!operationPayload(req->operation, extra));
                QVERIFY(!libraryRequest(query(i, "1", {1, 100001})));
            } else {
                QVERIFY(!operationPayload(req->operation, {{"uid", 9007199254740992.0}}));
            }
            DecodedResponse r;
            r.businessCode = 200;
            QVERIFY(!acceptsResponseCookies(req->operation, r));
            QVERIFY(!libraryRequest(query(i, "01")));
        }
    }
    void typedPagesAndUnknownMetadata()
    {
        const auto p = std::get<MyPlaylistsPage>(mapMusicResponse(
            query(0),
            json(
                R"({"code":200,"more":true,"playlist":[{"id":"9007199254740993","name":"synthetic","creator":{"userId":1},"subscribed":false,"trackCount":0},{"id":3}]})")));
        QCOMPARE(p.query.accountId, "1");
        QCOMPARE(p.playlists.first().id.value, "9007199254740993");
        QCOMPARE(p.playlists.first().creatorId, std::optional<QString>("1"));
        QCOMPARE(p.playlists.first().subscribed, std::optional<bool>(false));
        QVERIFY(!p.playlists.last().creatorId);
        QVERIFY(!p.playlists.last().subscribed);
        QVERIFY(!p.total);
        QCOMPARE(p.hasMore, std::optional<bool>(true));
        const auto liked = std::get<LikedSongIds>(mapMusicResponse(
            query(1), json(R"({"code":200,"uid":1,"ids":["9007199254740993",3,2]})")));
        QCOMPARE(liked.ids.size(), 3);
        QCOMPARE(liked.ids.first().value, "9007199254740993");
        QCOMPARE(liked.ids.last().value, "2"); // do not sort into a claimed listening order
        const auto a = std::get<SavedAlbumsPage>(mapMusicResponse(
            query(2),
            json(
                R"({"code":200,"count":3,"hasMore":true,"data":[{"id":4,"name":"album","size":8,"publishTime":-1,"artists":[{"id":5,"name":"artist"}]}]})")));
        QCOMPARE(a.albums.first().artists.first().id->value, "5");
        QCOMPARE(a.albums.first().publishedAtMs, std::optional<qint64>(-1));
        QCOMPARE(a.total, std::optional<qint64>(3));
        const auto ar = std::get<SavedArtistsPage>(mapMusicResponse(
            query(3),
            json(
                R"({"code":200,"data":[{"id":5,"name":"artist","albumSize":2,"picUrl":"https:\/\/example.test\/cover"}]})")));
        QCOMPARE(ar.artists.first().albumCount, std::optional<qint64>(2));
        QVERIFY(!ar.total);
        QVERIFY(!ar.hasMore);
        for (int i = 0; i < 4; ++i) {
            const auto key = i == 0 ? "playlist" : i == 1 ? "ids" : "data";
            QVERIFY(!std::holds_alternative<MusicFailure>(
                mapMusicResponse(query(i), {{"code", 200}, {key, QJsonArray{}}})));
        }
    }
    void malformed_data()
    {
        QTest::addColumn<int>("kind");
        QTest::addColumn<QByteArray>("body");
        QTest::newRow("missing list") << 0 << QByteArray(R"({"code":200})");
        QTest::newRow("wrong list") << 2 << QByteArray(R"({"code":200,"data":{}})");
        QTest::newRow("unsafe id") << 1 << QByteArray(R"({"code":200,"ids":[9007199254740993]})");
        QTest::newRow("duplicate liked") << 1 << QByteArray(R"({"code":200,"ids":[1,"1"]})");
        QTest::newRow("wrong owner") << 1 << QByteArray(R"({"code":200,"uid":2,"ids":[]})");
        QTest::newRow("bad creator")
            << 0 << QByteArray(R"({"code":200,"playlist":[{"id":1,"creator":{"userId":"01"}}]})");
        QTest::newRow("bad subscribed")
            << 0 << QByteArray(R"({"code":200,"playlist":[{"id":1,"subscribed":1}]})");
        QTest::newRow("bad more") << 0 << QByteArray(R"({"code":200,"playlist":[],"more":1})");
        QTest::newRow("bad hasMore")
            << 3 << QByteArray(R"({"code":200,"data":[],"hasMore":"false"})");
        QTest::newRow("negative count") << 2 << QByteArray(R"({"code":200,"data":[],"count":-1})");
        QTest::newRow("bad album")
            << 2 << QByteArray(R"({"code":200,"data":[{"id":1,"artists":false}]})");
        QTest::newRow("bad artist")
            << 3 << QByteArray(R"({"code":200,"data":[{"id":1,"name":false}]})");
    }
    void malformed()
    {
        QFETCH(int, kind);
        QFETCH(QByteArray, body);
        QCOMPARE(std::get<MusicFailure>(mapMusicResponse(query(kind), json(body))).error,
                 MusicError::InvalidResponse);
    }
    void hardLimitsNoTruncation()
    {
        for (int kind : {0, 2, 3}) {
            const QJsonArray rows{QJsonObject{{"id", 1}}, QJsonObject{{"id", 2}}};
            QJsonObject response{{"code", 200}, {kind == 0 ? "playlist" : "data", rows}};
            QVERIFY(std::holds_alternative<MusicFailure>(
                mapMusicResponse(query(kind, "1", {1, 0}), response)));
            QVERIFY(!std::holds_alternative<MusicFailure>(mapMusicResponse(query(kind), response)));
        }
        QJsonArray ids;
        for (int i = 1; i <= LikedSongIds::MaximumIds; ++i)
            ids.append(i);
        QVERIFY(std::holds_alternative<LikedSongIds>(
            mapMusicResponse(query(1), {{"code", 200}, {"ids", ids}})));
        ids.append(100001);
        QVERIFY(std::holds_alternative<MusicFailure>(
            mapMusicResponse(query(1), {{"code", 200}, {"ids", ids}})));
        QVERIFY(std::holds_alternative<MusicFailure>(mapMusicResponse(
            query(3),
            {{"code", 200},
             {"data", QJsonArray{QJsonObject{{"id", 1}, {"name", QString(4097, 'x')}}}}})));
    }
    void authAndAccountBinding()
    {
        Harness guest({}, false);
        guest.client.setAnonymousToken("synthetic-guest");
        QSignalSpy done(&guest.service, &MusicQueryService::completed);
        QSignalSpy raw(&guest.client, &SessionApiClient::completed);
        for (int i = 0; i < 4; ++i) {
            QVERIFY(guest.service.request(guest.session.snapshot().token, query(i)));
            QTRY_COMPARE(done.size(), i + 1);
            QCOMPARE(std::get<MusicFailure>(result(done)).error,
                     MusicError::AuthenticationRequired);
            const auto req = libraryRequest(query(i));
            QVERIFY(guest.client.submit(guest.session.snapshot().token, req->operation,
                                        req->parameters));
            QTRY_COMPARE(raw.size(), i + 1);
            QCOMPARE(qvariant_cast<ApiResult>(raw.last().at(2)).error, ApiError::Cookie);
        }
        QVERIFY(guest.http->requests.isEmpty());
        Harness account({});
        QSignalSpy other(&account.service, &MusicQueryService::completed);
        for (int i = 0; i < 4; ++i) {
            QVERIFY(account.service.request(account.session.snapshot().token, query(i, "2")));
            QTRY_COMPARE(other.size(), i + 1);
            QCOMPARE(std::get<MusicFailure>(result(other)).error, MusicError::InvalidRequest);
        }
        QCOMPARE(account.http->requests.size(), 1); // only synthetic Cookie seed
    }
    void nativeReadsCookieIsolationAndWideUid()
    {
        QList<Plan> plans;
        for (const auto body : {R"({"code":200,"playlist":[]})", R"({"code":200,"ids":[2]})",
                                R"({"code":200,"data":[]})", R"({"code":200,"data":[]})",
                                R"({"code":200,"ids":[]})"}) {
            Plan p{body};
            p.headers.append(std::pair<QByteArray, QByteArray>{
                "Set-Cookie", "MUSIC_U=synthetic-untrusted; Domain=.music.163.com; Path=/"});
            plans.append(p);
        }
        const QString wide = "9007199254740993";
        Harness h(plans, true, wide);
        QSignalSpy done(&h.service, &MusicQueryService::completed);
        for (int i = 0; i < 5; ++i) {
            const int kind = i == 4 ? 1 : i;
            QVERIFY(h.service.request(h.session.snapshot().token, query(kind, wide)));
            QTRY_COMPARE(done.size(), i + 1);
            QVERIFY(!std::holds_alternative<MusicFailure>(result(done)));
            const auto cookie = h.http->requests.last().rawHeader("Cookie");
            QVERIFY(cookie.contains("MUSIC_U=synthetic-original"));
            QVERIFY(!cookie.contains("untrusted"));
            if (kind == 1) {
                const auto decoded = decodeEapiRequest(h.http->bodies.last());
                QVERIFY(decoded);
                QCOMPARE(decoded->logicalPath, "/api/song/like/get");
                QCOMPARE(decoded->payload.value("uid").toString(), wide);
            }
        }
        QCOMPARE(h.http->requests.size(), 6); // no follow-up hydration or background sync
    }
    void cancelSessionChangeAndRateLimit()
    {
        Harness h({Plan{"{}", 200, -1}, Plan{"{}", 200, -1}, Plan{R"({"code":429})", 429}});
        QSignalSpy done(&h.service, &MusicQueryService::completed);
        const auto token = h.session.snapshot().token;
        const auto id = h.service.request(token, query(0));
        QTRY_COMPARE(h.http->requests.size(), 2);
        h.service.cancel(id);
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(std::get<MusicFailure>(result(done)).error, MusicError::Canceled);
        QVERIFY(h.service.request(token, query(1)));
        QTRY_COMPARE(h.http->requests.size(), 3);
        h.session.signOut();
        QTRY_COMPARE(done.size(), 2);
        QCOMPARE(std::get<MusicFailure>(result(done)).error, MusicError::StaleContext);
        QCOMPARE(qvariant_cast<SessionToken>(done.last().at(1)), token);
        Harness limited({Plan{R"({"code":429})", 429}});
        QSignalSpy rate(&limited.service, &MusicQueryService::completed);
        QVERIFY(limited.service.request(limited.session.snapshot().token, query(2)));
        QTRY_COMPARE(rate.size(), 1);
        QCOMPARE(std::get<MusicFailure>(result(rate)).retryAfterMs, std::optional<qint64>(3000));
        QCOMPARE(std::get<MusicFailure>(result(rate)).error, MusicError::RateLimited);
        QCoreApplication::processEvents();
        QCOMPARE(limited.http->requests.size(), 2);
    }
    void facadeCapturesAccountAndInvalidates()
    {
        BackendFacade f;
        QCOMPARE(f.myPlaylists(), 0);
        QCOMPARE(f.likedSongIds(), 0);
        f.start();
        BackendEvent e;
        e.kind = BackendEvent::Kind::Started;
        f.deliver(e);
        e.kind = BackendEvent::Kind::Session;
        e.session = {{QUuid::createUuid(), 1}, SessionPhase::Guest, {}};
        f.deliver(e);
        QCOMPARE(f.savedAlbums(), 0);
        QCOMPARE(f.savedArtists(), 0);
        e.session.phase = SessionPhase::Authenticated;
        e.session.accountId = "9007199254740993";
        f.deliver(e);
        QSignalSpy commands(&f, &BackendFacade::musicRequested),
            done(&f, &BackendFacade::musicCompleted);
        QVERIFY(f.myPlaylists());
        QVERIFY(f.likedSongIds());
        QVERIFY(f.savedAlbums());
        QVERIFY(f.savedArtists());
        for (const auto& row : commands) {
            const auto cmd = qvariant_cast<MusicCommand>(row.first());
            QCOMPARE(*libraryQueryAccount(cmd.query), e.session.accountId);
        }
        QCOMPARE(f.myPlaylists({0, 0}), 0);
        for (int i = 4; i < 16; ++i)
            QVERIFY(f.likedSongIds());
        QCOMPARE(f.likedSongIds(), 0);
        const auto old = qvariant_cast<MusicCommand>(commands.first().first());
        QVERIFY(f.signOut());
        QCOMPARE(f.likedSongIds(), 0);
        f.deliverMusic({old.id, old.generation, old.context, MyPlaylistsPage{}});
        QTRY_COMPARE(done.size(), 16);
        for (const auto& row : done)
            QCOMPARE(std::get<MusicFailure>(qvariant_cast<MusicResult>(row.at(2))).error,
                     MusicError::StaleContext);
    }
};
QTEST_GUILESS_MAIN(LibraryTest)
#include "library_test.moc"
