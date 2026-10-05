#include "../support/decode_eapi_request.h"
#include "../support/fake_http.h"
#include "application/backend/backend_facade.h"
#include "application/music/music_query_service.h"
#include "infrastructure/netease/music/music_gateway.h"
#include "infrastructure/netease/music/music_mapper.h"
#include "infrastructure/netease/music/playlist_discovery_mapper.h"
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
QList<MusicQuery> queries()
{
    return {RecommendedPlaylistsQuery{2},
            PlaylistListQuery{"R&B/Soul", {2, 10}, PlaylistOrder::New},
            PremiumPlaylistsQuery{"全部", 2, 300},
            PlaylistTagsQuery{},
            PlaylistTagsQuery{PlaylistTagsKind::Hot},
            PlaylistTagsQuery{PlaylistTagsKind::Premium}};
}
// Synthetic metadata only; never retain real playlist/creator/library responses here.
QList<QByteArray> responses()
{
    return {
        R"({"code":200,"result":[{"id":"9007199254740993","name":"synthetic","picUrl":"https:\/\/example.invalid\/cover","copywriter":"synthetic recommendation","playCount":123.5,"trackCount":3}]})",
        R"({"code":200,"playlists":[{"id":1,"updateTime":200}],"total":12,"more":true})",
        R"({"code":200,"playlists":[{"id":2,"updateTime":100}],"lasttime":100,"more":true})",
        R"({"code":200,"all":{"name":"全部","category":4},"sub":[{"name":"华语","category":0,"hot":false}],"categories":{"0":"语种","4":"主题"}})",
        R"({"code":200,"tags":[{"name":"流行","category":1}]})",
        R"({"code":200,"tags":[{"name":"爵士"}]})"};
}
QJsonObject json(const QByteArray& b) { return QJsonDocument::fromJson(b).object(); }
MusicResult mapped(int index, const QJsonObject& body)
{
    return mapMusicResponse(queries()[index], body);
}
MusicResult result(const QSignalSpy& s) { return qvariant_cast<MusicResult>(s.last()[2]); }
struct Harness {
    SessionCoordinator session;
    FakeManager* http = nullptr;
    SessionApiClient client;
    MusicGateway gateway{client};
    MusicQueryService service{session, gateway};
    static std::unique_ptr<HttpTransport> transport(QList<Plan> plans, FakeManager*& out)
    {
        auto m = std::make_unique<FakeManager>();
        out = m.get();
        m->plans = plans;
        return std::make_unique<HttpTransport>(std::move(m));
    }
    explicit Harness(QList<Plan> plans, bool auth = false)
        : client(transport(plans, http), "synthetic-playlist-discovery")
    {
        QVERIFY(client.advanceTo(session.snapshot().token));
        client.setAnonymousToken("synthetic-anon");
        QObject::connect(&session, &SessionCoordinator::changed, &client,
                         [this](SessionSnapshot s) { QVERIFY(client.advanceTo(s.token)); });
        if (!auth)
            return;
        const auto prepared = session.prepareAuthentication(session.beginAuthentication(), "101");
        QVERIFY(prepared);
        QVERIFY(session.completeAuthentication(*prepared));
        Plan seed{R"({"code":200,"unikey":"synthetic"})"};
        seed.headers.append(std::pair<QByteArray, QByteArray>{
            "Set-Cookie", "MUSIC_U=synthetic-original; Domain=.music.163.com; Path=/"});
        http->plans.prepend(seed);
        QSignalSpy done(&client, &SessionApiClient::completed);
        QVERIFY(client.submit(session.snapshot().token, ApiOperation::QrKey));
        QTRY_COMPARE(done.size(), 1);
    }
};
} // namespace
class PlaylistDiscoveryTest : public QObject
{
    Q_OBJECT
  private slots:
    void fixedPayloadsAndDefaults()
    {
        QCOMPARE(RecommendedPlaylistsQuery{}.limit, 30);
        QCOMPARE(PlaylistListQuery{}.page.limit, 50);
        QCOMPARE(PlaylistListQuery{}.category, QString("全部"));
        QCOMPARE(PlaylistListQuery{}.order, PlaylistOrder::Hot);
        QCOMPARE(PremiumPlaylistsQuery{}.before, 0);
        QCOMPARE(PremiumPlaylistsQuery{}.limit, 50);
        QCOMPARE(PlaylistTagsQuery{}.kind, PlaylistTagsKind::Catalogue);
        const QStringList paths{"/api/personalized/playlist",     "/api/playlist/list",
                                "/api/playlist/highquality/list", "/api/playlist/catalogue",
                                "/api/playlist/hottags",          "/api/playlist/highquality/tags"};
        for (int i = 0; i < 6; ++i) {
            const auto q = queries()[i];
            QVERIFY(isPlaylistDiscoveryQuery(q));
            QVERIFY(!requiresAuthenticatedMusicQuery(q));
            QVERIFY(!musicQueryAccount(q));
            const auto r = playlistDiscoveryRequest(q);
            QVERIFY(r);
            const auto c = operationContract(r->operation);
            QVERIFY(c);
            QCOMPARE(c->logicalPath, paths[i]);
            QCOMPARE(c->protocol, i == 3 ? Protocol::Eapi : Protocol::Weapi);
            QCOMPARE(c->responseLimit, i == 3 || i == 5 ? 1024 * 1024
                                       : i == 4         ? 512 * 1024
                                                        : 4 * 1024 * 1024);
            QCOMPARE(c->responseEncoding, ResponseEncoding::Json);
            QCOMPARE(c->destination, OfficialDestination::Default);
            QVERIFY(!requiresAccountCookie(r->operation));
            const auto p = operationPayload(r->operation, r->parameters);
            QVERIFY(p);
            if (i < 3)
                QCOMPARE(p->value("total"), QJsonValue(true));
            else
                QVERIFY(p->isEmpty());
            if (i == 0) {
                QCOMPARE(p->value("n"), QJsonValue(1000));
                QVERIFY(!p->contains("offset"));
            }
            if (i == 1) {
                QCOMPARE(p->value("order"), QJsonValue("new"));
                QCOMPARE(p->value("cat"), QJsonValue("R&B/Soul"));
                QCOMPARE(p->value("offset").toInt(), 10);
            }
            if (i == 2) {
                QCOMPARE(p->value("lasttime").toInteger(), 300);
                QVERIFY(!p->contains("before"));
                QVERIFY(!p->contains("offset"));
            }
            auto extra = r->parameters;
            extra.insert("host", "https://example.invalid");
            QVERIFY(!operationPayload(r->operation, extra));
        }
        const auto premium =
            playlistDiscoveryRequest(PremiumPlaylistsQuery{"全部", 100, MaximumPlaylistCursor});
        QVERIFY(premium);
        QCOMPARE(operationPayload(premium->operation, premium->parameters)
                     ->value("lasttime")
                     .toInteger(),
                 MaximumPlaylistCursor);
    }
    void invalidInputs()
    {
        for (const MusicQuery q :
             {MusicQuery(RecommendedPlaylistsQuery{0}), MusicQuery(RecommendedPlaylistsQuery{101}),
              MusicQuery(PlaylistListQuery{"", {1, 0}}),
              MusicQuery(PlaylistListQuery{" 全部", {1, 0}}),
              MusicQuery(PlaylistListQuery{"全部", {1, -1}}),
              MusicQuery(PlaylistListQuery{"全部", {101, 0}}),
              MusicQuery(PlaylistListQuery{"全部", {1, 0}, static_cast<PlaylistOrder>(2)}),
              MusicQuery(PremiumPlaylistsQuery{"全部", 1, -1}),
              MusicQuery(PremiumPlaylistsQuery{"全部", 1, MaximumPlaylistCursor + 1}),
              MusicQuery(PlaylistTagsQuery{static_cast<PlaylistTagsKind>(3)})}) {
            QVERIFY(!validMusicQuery(q));
            QVERIFY(!playlistDiscoveryRequest(q));
        }
        QVERIFY(!validPlaylistCategory(QString(129, u'x')));
        QVERIFY(!validPlaylistCategory("a\nb"));
        QVERIFY(!validPlaylistCategory(QString(QChar(0xd800))));
        QVERIFY(validPlaylistCategory("R&B/Soul"));
        QVERIFY(validPlaylistCategory("全部"));
        for (int i = 0; i < 3; ++i) {
            const auto r = *playlistDiscoveryRequest(queries()[i]);
            auto p = r.parameters;
            p.insert("limit", 1.5);
            QVERIFY(!operationPayload(r.operation, p));
            p = r.parameters;
            p.insert("limit", true);
            QVERIFY(!operationPayload(r.operation, p));
            p = r.parameters;
            p.insert("offset", 1.5);
            QVERIFY(!operationPayload(r.operation, p));
        }
        auto p = playlistDiscoveryRequest(queries()[2])->parameters;
        p.insert("before", 1.5);
        QVERIFY(!operationPayload(ApiOperation::PremiumPlaylists, p));
        p.insert("before", true);
        QVERIFY(!operationPayload(ApiOperation::PremiumPlaylists, p));
    }
    void recommendationMetadata()
    {
        const auto r = mapped(0, json(responses()[0]));
        QVERIFY(std::holds_alternative<RecommendedPlaylists>(r));
        const auto p = std::get<RecommendedPlaylists>(r);
        QCOMPARE(p.query.limit, 2);
        QCOMPARE(p.playlists.size(), 1);
        const auto item = p.playlists.first();
        QCOMPARE(item.id.value, "9007199254740993");
        QCOMPARE(item.playCount, std::optional<double>(123.5));
        QCOMPARE(item.trackCount, std::optional<qint64>(3));
        QCOMPARE(item.summary, std::optional<QString>("synthetic recommendation"));
        QCOMPARE(item.cover, QUrl("https://example.invalid/cover"));
        QVERIFY(!item.providerUpdateTime);
        const auto duplicate = mapped(0, json(R"({"code":200,"result":[{"id":1},{"id":1}]})"));
        QVERIFY(std::holds_alternative<RecommendedPlaylists>(duplicate));
        QCOMPARE(std::get<RecommendedPlaylists>(duplicate).playlists.size(), 2);
        const auto& unknown = std::get<RecommendedPlaylists>(duplicate).playlists.first();
        QVERIFY(!unknown.playCount);
        QVERIFY(!unknown.trackCount);
        QVERIFY(!unknown.summary);
    }
    void distinctPaginationAndUnknowns()
    {
        const auto topResult = mapped(1, json(responses()[1]));
        QVERIFY(std::holds_alternative<PlaylistListPage>(topResult));
        const auto top = std::get<PlaylistListPage>(topResult);
        QCOMPARE(top.query.page.offset, 10);
        QCOMPARE(top.total, std::optional<qint64>(12));
        QCOMPARE(top.hasMore, std::optional<bool>(true));
        const auto premiumResult = mapped(2, json(responses()[2]));
        QVERIFY(std::holds_alternative<PremiumPlaylistsPage>(premiumResult));
        const auto premium = std::get<PremiumPlaylistsPage>(premiumResult);
        QCOMPARE(premium.query.before, 300);
        QCOMPARE(premium.providerLastTime, std::optional<qint64>(100));
        QCOMPARE(premium.playlists.last().providerUpdateTime, std::optional<qint64>(100));
        QVERIFY(!premium.total);
        const PremiumPlaylistsQuery next{premium.query.category, premium.query.limit,
                                         *premium.playlists.last().providerUpdateTime};
        QCOMPARE(playlistDiscoveryRequest(next)->parameters.value("before").toInteger(), 100);
        const auto missing = mapped(2, json(R"({"code":200,"playlists":[{"id":1}]})"));
        QVERIFY(std::holds_alternative<PremiumPlaylistsPage>(missing));
        const auto unknown = std::get<PremiumPlaylistsPage>(missing);
        QVERIFY(!unknown.providerLastTime);
        QVERIFY(!unknown.hasMore);
        QVERIFY(!unknown.playlists.first().providerUpdateTime);
        const auto empty = mapped(1, json(R"({"code":200,"playlists":[],"more":false,"total":0})"));
        QVERIFY(std::holds_alternative<PlaylistListPage>(empty));
        QCOMPARE(std::get<PlaylistListPage>(empty).hasMore, std::optional<bool>(false));
        auto wrong = json(R"({"code":200,"result":[],"playlists":[{"id":1}]})");
        QVERIFY(std::get<RecommendedPlaylists>(mapped(0, wrong)).playlists.isEmpty());
        QCOMPARE(std::get<PlaylistListPage>(mapped(1, wrong)).playlists.size(), 1);
    }
    void categoryKindsRemainDistinct()
    {
        for (int i = 3; i < 6; ++i) {
            const auto r = mapped(i, json(responses()[i]));
            QVERIFY(std::holds_alternative<PlaylistTags>(r));
            const auto tags = std::get<PlaylistTags>(r);
            QCOMPARE(tags.query.kind, std::get<PlaylistTagsQuery>(queries()[i]).kind);
            QCOMPARE(tags.tags.size(), 1);
            if (i == 3) {
                QVERIFY(tags.all);
                QCOMPARE(tags.all->name, QString("全部"));
                QVERIFY(tags.categories);
                QCOMPARE(tags.categories->value(0), QString("语种"));
                QCOMPARE(tags.tags.first().hot, std::optional<bool>(false));
            } else {
                QVERIFY(!tags.all);
                QVERIFY(!tags.categories);
                QVERIFY(!tags.tags.first().hot);
            }
        }
        const auto unknown = mapped(
            3,
            json(
                R"({"code":200,"sub":[{"name":"other","category":99},{"name":"other"}],"categories":{"0":"语种"}})"));
        QVERIFY(std::holds_alternative<PlaylistTags>(unknown));
        QCOMPARE(std::get<PlaylistTags>(unknown).tags.size(), 2);
        QCOMPARE(std::get<PlaylistTags>(unknown).tags.first().categoryCode,
                 std::optional<qint64>(99));
        for (int i = 3; i < 6; ++i) {
            const auto r =
                mapped(i, json(i == 3 ? R"({"code":200,"sub":[]})" : R"({"code":200,"tags":[]})"));
            QVERIFY(std::holds_alternative<PlaylistTags>(r));
            QVERIFY(std::get<PlaylistTags>(r).tags.isEmpty());
        }
    }
    void malformed_data()
    {
        QTest::addColumn<int>("index");
        QTest::addColumn<QByteArray>("body");
        QTest::newRow("missing-rec-array") << 0 << QByteArray(R"({"code":200,"playlists":[]})");
        QTest::newRow("null-rec") << 0 << QByteArray(R"({"code":200,"result":null})");
        QTest::newRow("unsafe-id")
            << 0 << QByteArray(R"({"code":200,"result":[{"id":9007199254740993}]})");
        QTest::newRow("wrong-summary")
            << 0 << QByteArray(R"({"code":200,"result":[{"id":1,"copywriter":true}]})");
        QTest::newRow("negative-plays")
            << 0 << QByteArray(R"({"code":200,"result":[{"id":1,"playCount":-1}]})");
        QTest::newRow("text-plays")
            << 0 << QByteArray(R"({"code":200,"result":[{"id":1,"playCount":"3"}]})");
        QTest::newRow("missing-page-array") << 1 << QByteArray(R"({"code":200,"result":[]})");
        QTest::newRow("negative-total")
            << 1 << QByteArray(R"({"code":200,"playlists":[],"total":-1})");
        QTest::newRow("wrong-more") << 1 << QByteArray(R"({"code":200,"playlists":[],"more":1})");
        QTest::newRow("contradictory-total")
            << 1 << QByteArray(R"({"code":200,"playlists":[{"id":1}],"total":0})");
        QTest::newRow("bad-trackcount")
            << 1 << QByteArray(R"({"code":200,"playlists":[{"id":1,"trackCount":1.5}]})");
        QTest::newRow("bad-updatetime")
            << 2 << QByteArray(R"({"code":200,"playlists":[{"id":1,"updateTime":true}]})");
        QTest::newRow("bad-lasttime")
            << 2 << QByteArray(R"({"code":200,"playlists":[],"lasttime":-1})");
        QTest::newRow("fractional-lasttime")
            << 2 << QByteArray(R"({"code":200,"playlists":[],"lasttime":1.5})");
        QTest::newRow("missing-sub") << 3 << QByteArray(R"({"code":200,"tags":[]})");
        QTest::newRow("null-sub") << 3 << QByteArray(R"({"code":200,"sub":null})");
        QTest::newRow("bad-tag-name") << 3 << QByteArray(R"({"code":200,"sub":[{"name":1}]})");
        QTest::newRow("bad-tag-category")
            << 3 << QByteArray(R"({"code":200,"sub":[{"name":"x","category":-1}]})");
        QTest::newRow("bad-tag-hot")
            << 3 << QByteArray(R"({"code":200,"sub":[{"name":"x","hot":"true"}]})");
        QTest::newRow("bad-all") << 3 << QByteArray(R"({"code":200,"sub":[],"all":true})");
        QTest::newRow("bad-groups") << 3 << QByteArray(R"({"code":200,"sub":[],"categories":[]})");
        QTest::newRow("bad-group-key")
            << 3 << QByteArray(R"({"code":200,"sub":[],"categories":{"00":"x"}})");
        QTest::newRow("bad-group-name")
            << 3 << QByteArray(R"({"code":200,"sub":[],"categories":{"0":1}})");
        QTest::newRow("wrong-hot-root") << 4 << QByteArray(R"({"code":200,"sub":[]})");
        QTest::newRow("wrong-premium-tags-root") << 5 << QByteArray(R"({"code":200,"sub":[]})");
        QTest::newRow("whitespace-tag") << 5 << QByteArray(R"({"code":200,"tags":[{"name":" "}]})");
        QTest::newRow("bad-code") << 0 << QByteArray(R"({"code":500,"result":[]})");
    }
    void malformed()
    {
        QFETCH(int, index);
        QFETCH(QByteArray, body);
        const auto r = mapped(index, json(body));
        QVERIFY(std::holds_alternative<MusicFailure>(r));
        QCOMPARE(std::get<MusicFailure>(r).error, MusicError::InvalidResponse);
    }
    void boundedCollectionsAndText()
    {
        for (int i = 0; i < 3; ++i) {
            QJsonArray rows;
            for (int j = 0; j < 3; ++j)
                rows.append(QJsonObject{{"id", j + 1}});
            const auto r = mapped(i, {{"code", 200}, {i == 0 ? "result" : "playlists", rows}});
            QVERIFY(std::holds_alternative<MusicFailure>(r));
        }
        for (int i = 3; i < 6; ++i) {
            const int cap = i == 4 ? PlaylistTags::MaximumHotTags : PlaylistTags::MaximumTags;
            QJsonArray tags;
            for (int j = 0; j < cap; ++j)
                tags.append(QJsonObject{{"name", "synthetic"}});
            const QString field = i == 3 ? "sub" : "tags";
            QVERIFY(
                std::holds_alternative<PlaylistTags>(mapped(i, {{"code", 200}, {field, tags}})));
            tags.append(QJsonObject{{"name", "overflow"}});
            QVERIFY(
                std::holds_alternative<MusicFailure>(mapped(i, {{"code", 200}, {field, tags}})));
        }
        QJsonObject groups;
        for (int i = 0; i < 50; ++i)
            groups.insert(QString::number(i), "group");
        QVERIFY(std::holds_alternative<PlaylistTags>(
            mapped(3, {{"code", 200}, {"sub", QJsonArray{}}, {"categories", groups}})));
        groups.insert("50", "overflow");
        QVERIFY(std::holds_alternative<MusicFailure>(
            mapped(3, {{"code", 200}, {"sub", QJsonArray{}}, {"categories", groups}})));
        for (const QString bad : {QString(129, u'x'), QString(QChar::Null), QString(QChar(0xd800))})
            QVERIFY(std::holds_alternative<MusicFailure>(
                mapped(5, {{"code", 200}, {"tags", QJsonArray{QJsonObject{{"name", bad}}}}})));
        QVERIFY(std::holds_alternative<MusicFailure>(mapped(
            0, {{"code", 200},
                {"result",
                 QJsonArray{QJsonObject{{"id", 1}, {"copywriter", QString(4097, u'x')}}}}})));
    }
    void nativeReads_data()
    {
        QTest::addColumn<int>("index");
        QTest::addColumn<bool>("auth");
        for (int i = 0; i < 6; ++i)
            for (bool auth : {false, true})
                QTest::newRow(
                    qPrintable(QString("operation-%1-%2").arg(i).arg(auth ? "account" : "guest")))
                    << i << auth;
    }
    void nativeReads()
    {
        QFETCH(int, index);
        QFETCH(bool, auth);
        Plan first{responses()[index]};
        first.headers.append(std::pair<QByteArray, QByteArray>{
            "Set-Cookie", "MUSIC_U=synthetic-rotation; Domain=.music.163.com; Path=/"});
        Harness h({first, Plan{responses()[index]}}, auth);
        QSignalSpy done(&h.service, &MusicQueryService::completed);
        const auto context = h.session.snapshot().token;
        const auto q = queries()[index];
        QVERIFY(h.service.request(context, q));
        QTRY_COMPARE(done.size(), 1);
        QVERIFY(!std::holds_alternative<MusicFailure>(result(done)));
        QCOMPARE(qvariant_cast<SessionToken>(done.last()[1]), context);
        const auto r = *playlistDiscoveryRequest(q);
        const auto c = *operationContract(r.operation);
        QCOMPARE(h.http->requests.last().url().host(),
                 index == 3 ? "interfacepc.music.163.com" : "music.163.com");
        QCOMPARE(h.http->requests.last().url().path(),
                 QString(index == 3 ? "/eapi" : "/weapi") + c.logicalPath.mid(4));
        if (index == 3) {
            const auto wire = decodeEapiRequest(h.http->bodies.last());
            QVERIFY(wire);
            QCOMPARE(wire->logicalPath, c.logicalPath);
            auto business = wire->payload;
            business.remove("header");
            business.remove("e_r");
            QVERIFY(business.isEmpty());
        }
        QVERIFY(h.service.request(context, q));
        QTRY_COMPARE(done.size(), 2);
        const auto cookie = h.http->requests.last().rawHeader("Cookie");
        QVERIFY(!cookie.contains("synthetic-rotation"));
        if (auth)
            QVERIFY(cookie.contains("MUSIC_U=synthetic-original"));
        else
            QVERIFY(!cookie.contains("MUSIC_U="));
        QCOMPARE(h.http->requests.size(), auth ? 3 : 2); // no auto pagination/hydration/feedback
    }
    void cancelStaleRateLimit_data()
    {
        QTest::addColumn<int>("index");
        for (int i = 0; i < 6; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void cancelStaleRateLimit()
    {
        QFETCH(int, index);
        const auto q = queries()[index];
        Harness h({Plan{"", 200, -1}, Plan{"", 200, -1}});
        QSignalSpy done(&h.service, &MusicQueryService::completed);
        const auto token = h.session.snapshot().token;
        const auto id = h.service.request(token, q);
        QVERIFY(id);
        QTRY_COMPARE(h.http->requests.size(), 1);
        h.service.cancel(id);
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(std::get<MusicFailure>(result(done)).error, MusicError::Canceled);
        QVERIFY(h.service.request(token, q));
        QTRY_COMPARE(h.http->requests.size(), 2);
        h.session.signOut();
        QTRY_COMPARE(done.size(), 2);
        QCOMPARE(std::get<MusicFailure>(result(done)).error, MusicError::StaleContext);
        QCOMPARE(qvariant_cast<SessionToken>(done.last()[1]), token);
        Harness rate({Plan{"", 429}});
        QSignalSpy r(&rate.service, &MusicQueryService::completed);
        QVERIFY(rate.service.request(rate.session.snapshot().token, q));
        QTRY_COMPARE(r.size(), 1);
        QCOMPARE(std::get<MusicFailure>(result(r)).error, MusicError::RateLimited);
        QCOMPARE(std::get<MusicFailure>(result(r)).retryAfterMs, std::optional<qint64>(3000));
        QTest::qWait(10);
        QCOMPARE(rate.http->requests.size(), 1);
    }
    void facadeDefaultsAndMailbox()
    {
        BackendFacade f;
        QVERIFY(!f.recommendedPlaylists());
        f.start();
        BackendEvent e;
        e.kind = BackendEvent::Kind::Started;
        f.deliver(e);
        e.kind = BackendEvent::Kind::Session;
        const SessionToken token{QUuid::createUuid(), 1};
        e.session = {token, SessionPhase::Guest};
        f.deliver(e);
        QSignalSpy commands(&f, &BackendFacade::musicRequested),
            done(&f, &BackendFacade::musicCompleted),
            reports(&f, &BackendFacade::listeningReportRequested),
            play(&f, &BackendFacade::playbackRequested);
        QVERIFY(!f.recommendedPlaylists(0));
        QVERIFY(!f.premiumPlaylists({"全部", 50, -1}));
        QVERIFY(!f.playlistTags(static_cast<PlaylistTagsKind>(3)));
        QVERIFY(f.recommendedPlaylists());
        QCOMPARE(std::get<RecommendedPlaylistsQuery>(
                     qvariant_cast<MusicCommand>(commands.last()[0]).query)
                     .limit,
                 30);
        QVERIFY(f.playlistList());
        QCOMPARE(std::get<PlaylistListQuery>(qvariant_cast<MusicCommand>(commands.last()[0]).query)
                     .page.limit,
                 50);
        QVERIFY(f.premiumPlaylists());
        QCOMPARE(
            std::get<PremiumPlaylistsQuery>(qvariant_cast<MusicCommand>(commands.last()[0]).query)
                .before,
            0);
        QVERIFY(f.playlistTags());
        QVERIFY(f.playlistTags(PlaylistTagsKind::Hot));
        QVERIFY(f.playlistTags(PlaylistTagsKind::Premium));
        const auto old = qvariant_cast<MusicCommand>(commands.first()[0]);
        for (int i = 0; i < 10; ++i)
            QVERIFY(f.recommendedPlaylists(1));
        QVERIFY(!f.playlistList());
        QVERIFY(f.signOut());
        QTRY_COMPARE(done.size(), 16);
        f.deliverMusic({old.id, old.generation, token,
                        RecommendedPlaylists{std::get<RecommendedPlaylistsQuery>(old.query), {}}});
        QCOMPARE(done.size(), 16);
        QVERIFY(reports.isEmpty());
        QVERIFY(play.isEmpty());
    }
};
QTEST_GUILESS_MAIN(PlaylistDiscoveryTest)
#include "playlist_discovery_test.moc"
