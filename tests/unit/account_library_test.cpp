#include "../support/fake_http.h"
#include "application/backend/backend_facade.h"
#include "application/music/music_query_service.h"
#include "infrastructure/netease/music/account_library_mapper.h"
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
MusicQuery query(int kind, QString account = "1", CatalogPage page = {2, 0})
{
    if (kind == 0)
        return SubscribedPodcastsQuery{account, page};
    if (kind == 1)
        return CloudSongsQuery{account, page};
    return CloudSongDetailsQuery{account, {{"4"}, {"5"}}};
}
QByteArray body(int kind)
{
    if (kind == 0)
        return R"({"code":200,"djRadios":[{"id":3,"name":"synthetic","programCount":0}],"count":2,"hasMore":true,"time":0})";
    if (kind == 1)
        return R"({"code":200,"data":[{"songId":4,"songName":"synthetic","fileName":"synthetic.mp3","artist":"synthetic","album":null,"fileSize":0,"bitrate":128000,"addTime":100,"simpleSong":{"id":99}}],"count":2,"hasMore":true,"size":"9007199254740993","maxSize":"1048"})";
    return R"({"code":200,"data":[{"songId":5,"simpleSong":{"id":0}},{"songId":4,"simpleSong":{"id":99}}]})";
}
MusicResult result(const QSignalSpy& spy) { return qvariant_cast<MusicResult>(spy.last()[2]); }
QJsonObject json(QByteArray s) { return QJsonDocument::fromJson(s).object(); }
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
    explicit Harness(QList<Plan> plans, bool auth = true, QString account = "1")
        : client(transport(plans, http), "synthetic-account-library")
    {
        QVERIFY(client.advanceTo(session.snapshot().token));
        QObject::connect(&session, &SessionCoordinator::changed, &client,
                         [this](SessionSnapshot s) { QVERIFY(client.advanceTo(s.token)); });
        if (!auth)
            return;
        const auto p = session.prepareAuthentication(session.beginAuthentication(), account);
        QVERIFY(p);
        QVERIFY(session.completeAuthentication(*p));
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
class AccountLibraryTest : public QObject
{
    Q_OBJECT
  private slots:
    void contracts_data()
    {
        QTest::addColumn<int>("kind");
        for (int k = 0; k < 3; ++k)
            QTest::newRow(qPrintable(QString::number(k))) << k;
    }
    void contracts()
    {
        QFETCH(int, kind);
        const auto q = query(kind, "9007199254740993", {30, 100});
        const auto req = accountLibraryRequest(q);
        QVERIFY(req);
        QVERIFY(isAccountLibraryQuery(q));
        QCOMPARE(musicQueryAccount(q), std::optional<QString>("9007199254740993"));
        QVERIFY(requiresAuthenticatedMusicQuery(q));
        const QStringList paths{"/api/djradio/get/subed", "/api/v1/cloud/get",
                                "/api/v1/cloud/get/byids"};
        const auto c = operationContract(req->operation, req->parameters);
        QVERIFY(c);
        QCOMPARE(c->logicalPath, paths[kind]);
        QCOMPARE(c->protocol, Protocol::Weapi);
        QCOMPARE(c->responseEncoding, ResponseEncoding::Json);
        QCOMPARE(c->responseLimit, 4 * 1024 * 1024);
        QCOMPARE(c->destination, OfficialDestination::Default);
        QVERIFY(requiresAccountCookie(req->operation));
        const auto p = operationPayload(req->operation, req->parameters);
        QVERIFY(p);
        if (kind == 0)
            QCOMPARE(*p, (QJsonObject{{"limit", 30}, {"offset", 100}, {"total", true}}));
        else if (kind == 1)
            QCOMPARE(*p, (QJsonObject{{"limit", 30}, {"offset", 100}}));
        else
            QCOMPARE(*p, (QJsonObject{{"songIds", QJsonArray{"4", "5"}}}));
        QVERIFY(!p->contains("uid"));
        for (const QString key :
             {"uid", "host", "url", "crypto", "csrf_token", "e_r", "total", "songIds"}) {
            auto bad = req->parameters;
            bad.insert(key, "injected");
            QVERIFY(!operationPayload(req->operation, bad));
        }
        DecodedResponse r;
        r.businessCode = 200;
        r.object = {{"code", 200}};
        QVERIFY(!acceptsResponseCookies(req->operation, r));
        QVERIFY(!accountLibraryRequest(query(kind, "01")));
    }
    void boundaries()
    {
        static_assert(!std::is_same_v<CloudSongId, SongId>);
        static_assert(!std::is_constructible_v<CloudSongId, SongId>);
        QCOMPARE(SubscribedPodcastsQuery{"1"}.page.limit, 30);
        QCOMPARE(CloudSongsQuery{"1"}.page.limit, 30);
        for (int k : {0, 1}) {
            for (const CatalogPage p : {CatalogPage{0, 0}, CatalogPage{101, 0}, CatalogPage{1, -1},
                                        CatalogPage{1, 100001}})
                QVERIFY(!validMusicQuery(query(k, "1", p)));
            const auto req = *accountLibraryRequest(query(k));
            for (const QString key : {"limit", "offset"})
                for (const QJsonValue v : {QJsonValue(true), QJsonValue("1"), QJsonValue(1.5),
                                           QJsonValue(QJsonValue::Null)}) {
                    auto p = req.parameters;
                    p.insert(key, v);
                    QVERIFY(!operationPayload(req.operation, p));
                }
            QVERIFY(validMusicQuery(query(k, "1", {100, 100000})));
        }
        QVERIFY((!CloudSongDetailsQuery{"1", {}}.isValid()));
        QVERIFY((!CloudSongDetailsQuery{"1", {{"4"}, {"4"}}}.isValid()));
        for (const QString id : {"0", "01", " 4 ", "4,5", "-4", "9223372036854775808"})
            QVERIFY((!CloudSongDetailsQuery{"1", {{id}}}.isValid()));
        CloudSongDetailsQuery q{"1", {}};
        QJsonArray ids;
        for (int i = 1; i <= 100; ++i) {
            q.songs.append({QString::number(i)});
            ids.append(QString::number(i));
        }
        QVERIFY(q.isValid());
        QVERIFY(operationPayload(ApiOperation::CloudSongDetails, {{"ids", ids}}));
        q.songs.append({"101"});
        ids.append("101");
        QVERIFY(!q.isValid());
        QVERIFY(!operationPayload(ApiOperation::CloudSongDetails, {{"ids", ids}}));
        for (const QJsonValue a :
             {QJsonValue("4,5"), QJsonValue(QJsonArray{4}), QJsonValue(QJsonArray{"4", "4"}),
              QJsonValue(QJsonArray{"4", QJsonValue::Null})})
            QVERIFY(!operationPayload(ApiOperation::CloudSongDetails, {{"ids", a}}));
        const auto wide = accountLibraryRequest(CloudSongDetailsQuery{"1", {{"9007199254740993"}}});
        QVERIFY(wide);
        QCOMPARE(operationPayload(wide->operation, wide->parameters)->value("songIds"),
                 QJsonValue(QJsonArray{"9007199254740993"}));
    }
    void valuesUnknownsAndMissing()
    {
        const auto p = mapMusicResponse(query(0), json(body(0)));
        QVERIFY(std::holds_alternative<SubscribedPodcastsPage>(p));
        const auto podcasts = std::get<SubscribedPodcastsPage>(p);
        QCOMPARE(podcasts.podcasts.first().id.value, "3");
        QCOMPARE(podcasts.podcasts.first().voiceCount, std::optional<qint64>(0));
        QCOMPARE(podcasts.providerTime, std::optional<qint64>(0));
        QCOMPARE(podcasts.hasMore, std::optional<bool>(true));
        QCOMPARE(podcasts.query.accountId, "1");
        const auto c = mapMusicResponse(query(1), json(body(1)));
        QVERIFY(std::holds_alternative<CloudSongsPage>(c));
        const auto cloud = std::get<CloudSongsPage>(c);
        const auto song = cloud.songs.first();
        QCOMPARE(song.id.value, "4");
        QCOMPARE(song.catalogReference->value, "99");
        QCOMPARE(song.title, "synthetic");
        QCOMPARE(song.providerFileSize, std::optional<qint64>(0));
        QCOMPARE(song.providerAddedTime, std::optional<qint64>(100));
        QVERIFY(!song.album);
        QCOMPARE(cloud.providerUsedSpace, std::optional<qint64>(9007199254740993LL));
        QCOMPARE(cloud.providerMaximumSpace,
                 std::optional<qint64>(1048)); // over quota does not imply unusable/deleted
        const auto d = mapMusicResponse(query(2), json(body(2)));
        QVERIFY(std::holds_alternative<CloudSongDetails>(d));
        const auto detail = std::get<CloudSongDetails>(d);
        QCOMPARE(detail.songs.first().id.value, "4");
        QCOMPARE(detail.songs.last().id.value, "5");
        QVERIFY(detail.missing.isEmpty());
        QVERIFY(!detail.songs.last().catalogReference); // not aliased to cloud ID
        const CloudSongDetailsQuery partial{"1", {{"4"}, {"6"}, {"5"}}};
        const auto r = mapMusicResponse(partial, json(body(2)));
        QVERIFY(std::holds_alternative<CloudSongDetails>(r));
        QCOMPARE(std::get<CloudSongDetails>(r).missing.first().value, "6");
        for (int k = 0; k < 3; ++k) {
            const auto r = mapMusicResponse(
                query(k), {{"code", 200}, {k == 0 ? "djRadios" : "data", QJsonArray{}}});
            QVERIFY(!std::holds_alternative<MusicFailure>(r));
            if (k == 0) {
                const auto v = std::get<SubscribedPodcastsPage>(r);
                QVERIFY(!v.total);
                QVERIFY(!v.hasMore);
                QVERIFY(!v.providerTime);
            }
            if (k == 1) {
                const auto v = std::get<CloudSongsPage>(r);
                QVERIFY(!v.total);
                QVERIFY(!v.hasMore);
                QVERIFY(!v.providerUsedSpace);
            }
            if (k == 2)
                QCOMPARE(std::get<CloudSongDetails>(r).missing.size(), 2);
        }
        const QJsonArray duplicates{QJsonObject{{"songId", "9007199254740993"}},
                                    QJsonObject{{"songId", "9007199254740993"}}};
        const auto duplicate = mapMusicResponse(query(1), {{"code", 200}, {"data", duplicates}});
        QVERIFY(std::holds_alternative<CloudSongsPage>(duplicate));
        QCOMPARE(std::get<CloudSongsPage>(duplicate).songs.size(), 2);
        QVERIFY(!std::get<CloudSongsPage>(duplicate).songs.first().catalogReference);
        const auto pod = mapMusicResponse(
            query(0), {{"code", 200},
                       {"djRadios", QJsonArray{QJsonObject{{"id", 3}}, QJsonObject{{"id", 3}}}}});
        QVERIFY(std::holds_alternative<SubscribedPodcastsPage>(pod));
        QCOMPARE(std::get<SubscribedPodcastsPage>(pod).podcasts.size(), 2);
    }
    void cloudTracks()
    {
        // simpleSong reads as the song that plays; matched from the album (or matchType).
        const auto rows =
            QJsonDocument::fromJson(
                R"json([)json"
                R"json({"songId":7,"songName":"晴天","simpleSong":{"id":186016,"name":"晴天",)json"
                R"json("ar":[{"id":6452,"name":"周杰伦"}],"al":{"id":18915,"name":"叶惠美","picUrl":"https://p1.music.126.net/x.jpg"},"dt":269000}},)json"
                R"json({"songId":8,"songName":"demo","simpleSong":{"id":8,"name":"demo",)json"
                R"json("ar":[{"id":0,"name":"未知"}],"al":{"id":0,"name":""},"dt":182000}},)json"
                R"json({"songId":9,"songName":"other","matchType":"unmatched","simpleSong":{"id":9,"name":"other",)json"
                R"json("ar":[],"al":{"id":5,"name":"x"}}},)json"
                R"json({"songId":10,"songName":"odd","simpleSong":{"id":10}}])json")
                .array();
        const auto r = mapMusicResponse(query(1, "1", {30, 0}), {{"code", 200}, {"data", rows}});
        QVERIFY(std::holds_alternative<CloudSongsPage>(r));
        const auto songs = std::get<CloudSongsPage>(r).songs;
        QCOMPARE(songs.size(), 4);
        QVERIFY(songs[0].track);
        QCOMPARE(songs[0].track->id.value, "186016");
        QCOMPARE(songs[0].track->durationMs, std::optional<qint64>(269000));
        QCOMPARE(songs[0].matched, std::optional<bool>(true));
        QVERIFY(songs[1].track);
        QCOMPARE(songs[1].track->id.value, "8");
        QCOMPARE(songs[1].matched, std::optional<bool>(false));
        QCOMPARE(songs[2].matched, std::optional<bool>(false)); // matchType wins
        QVERIFY(songs[3].track); // id only: still plays; the cloud row names it
        QCOMPARE(songs[3].matched, std::optional<bool>(false));
    }
    void malformed_data()
    {
        QTest::addColumn<int>("kind");
        QTest::addColumn<QByteArray>("body");
        QTest::newRow("pod-missing") << 0 << QByteArray(R"({"code":200})");
        QTest::newRow("pod-null") << 0 << QByteArray(R"({"code":200,"djRadios":null})");
        QTest::newRow("pod-wrong-row") << 0 << QByteArray(R"({"code":200,"djRadios":[true]})");
        QTest::newRow("pod-empty-id") << 0 << QByteArray(R"({"code":200,"djRadios":[{"id":0}]})");
        QTest::newRow("pod-fraction-count")
            << 0 << QByteArray(R"({"code":200,"djRadios":[],"count":1.5})");
        QTest::newRow("pod-program-count")
            << 0 << QByteArray(R"({"code":200,"djRadios":[{"id":3,"programCount":false}]})");
        QTest::newRow("pod-more") << 0 << QByteArray(R"({"code":200,"djRadios":[],"hasMore":0})");
        QTest::newRow("pod-time") << 0 << QByteArray(R"({"code":200,"djRadios":[],"time":-1})");
        QTest::newRow("pod-owner") << 0 << QByteArray(R"({"code":200,"uid":2,"djRadios":[]})");
        QTest::newRow("pod-total")
            << 0 << QByteArray(R"({"code":200,"djRadios":[{"id":3}],"count":0})");
        QTest::newRow("cloud-missing") << 1 << QByteArray(R"({"code":200})");
        QTest::newRow("cloud-object") << 1 << QByteArray(R"({"code":200,"data":{}})");
        QTest::newRow("cloud-row") << 1 << QByteArray(R"({"code":200,"data":[null]})");
        QTest::newRow("cloud-no-fallback")
            << 1 << QByteArray(R"({"code":200,"data":[{"simpleSong":{"id":4}}]})");
        QTest::newRow("cloud-wide-number")
            << 1 << QByteArray(R"({"code":200,"data":[{"songId":9007199254740992}]})");
        QTest::newRow("cloud-title")
            << 1 << QByteArray(R"({"code":200,"data":[{"songId":4,"songName":false}]})");
        QTest::newRow("cloud-filename")
            << 1 << QByteArray(R"({"code":200,"data":[{"songId":4,"fileName":4}]})");
        QTest::newRow("cloud-artist")
            << 1 << QByteArray(R"({"code":200,"data":[{"songId":4,"artist":{}}]})");
        QTest::newRow("cloud-album")
            << 1 << QByteArray(R"({"code":200,"data":[{"songId":4,"album":[]}]})");
        QTest::newRow("cloud-size")
            << 1 << QByteArray(R"({"code":200,"data":[{"songId":4,"fileSize":-1}]})");
        QTest::newRow("cloud-bitrate")
            << 1 << QByteArray(R"({"code":200,"data":[{"songId":4,"bitrate":"128000"}]})");
        QTest::newRow("cloud-added")
            << 1 << QByteArray(R"({"code":200,"data":[{"songId":4,"addTime":true}]})");
        QTest::newRow("cloud-nested")
            << 1 << QByteArray(R"({"code":200,"data":[{"songId":4,"simpleSong":[]}]})");
        QTest::newRow("cloud-reference")
            << 1 << QByteArray(R"({"code":200,"data":[{"songId":4,"simpleSong":{"id":"01"}}]})");
        QTest::newRow("cloud-total")
            << 1 << QByteArray(R"({"code":200,"data":[{"songId":4}],"count":0})");
        QTest::newRow("cloud-owner") << 1 << QByteArray(R"({"code":200,"uid":2,"data":[]})");
        QTest::newRow("detail-missing") << 2 << QByteArray(R"({"code":200})");
        QTest::newRow("detail-duplicate")
            << 2 << QByteArray(R"({"code":200,"data":[{"songId":4},{"songId":"4"}]})");
        QTest::newRow("detail-unsolicited")
            << 2 << QByteArray(R"({"code":200,"data":[{"songId":6}]})");
        QTest::newRow("detail-owner") << 2 << QByteArray(R"({"code":200,"uid":2,"data":[]})");
        QTest::newRow("detail-unsafe-reference")
            << 2
            << QByteArray(
                   R"({"code":200,"data":[{"songId":4,"simpleSong":{"id":9007199254740992}}]})");
    }
    void malformed()
    {
        QFETCH(int, kind);
        QFETCH(QByteArray, body);
        const auto doc = QJsonDocument::fromJson(body);
        QVERIFY(doc.isObject());
        const auto r = mapMusicResponse(query(kind), doc.object());
        QVERIFY(std::holds_alternative<MusicFailure>(r));
        QCOMPARE(std::get<MusicFailure>(r).error, MusicError::InvalidResponse);
    }
    void quotaAndHardBounds()
    {
        for (const QString s :
             {"00", "01", "-1", "+1", " 1", "1 ", "1.5", "1e4", "9223372036854775808", ""})
            for (const QString key : {"size", "maxSize"}) {
                const auto r =
                    mapMusicResponse(query(1), {{"code", 200}, {"data", QJsonArray{}}, {key, s}});
                QVERIFY(std::holds_alternative<MusicFailure>(r));
            }
        for (const QJsonValue v :
             {QJsonValue(true), QJsonValue(1.5), QJsonValue(9007199254740992LL)})
            QVERIFY(std::holds_alternative<MusicFailure>(
                mapMusicResponse(query(1), {{"code", 200}, {"data", QJsonArray{}}, {"size", v}})));
        const auto zero = mapMusicResponse(query(1), {{"code", 200},
                                                      {"data", QJsonArray{}},
                                                      {"size", "0"},
                                                      {"maxSize", "9223372036854775807"}});
        QVERIFY(std::holds_alternative<CloudSongsPage>(zero));
        QCOMPARE(std::get<CloudSongsPage>(zero).providerUsedSpace, std::optional<qint64>(0));
        for (int k : {0, 1}) {
            const QString id = k == 0 ? "id" : "songId";
            QVERIFY(std::holds_alternative<MusicFailure>(
                mapMusicResponse(query(k, "1", {1, 0}),
                                 {{"code", 200},
                                  {k == 0 ? "djRadios" : "data",
                                   QJsonArray{QJsonObject{{id, 3}}, QJsonObject{{id, 4}}}}})));
        }
        for (const QString key : {"songName", "artist", "album", "fileName"}) {
            QVERIFY(std::holds_alternative<MusicFailure>(mapMusicResponse(
                query(1),
                {{"code", 200},
                 {"data", QJsonArray{QJsonObject{{"songId", 4}, {key, QString(4097, 'x')}}}}})));
            QVERIFY(std::holds_alternative<MusicFailure>(mapMusicResponse(
                query(1),
                {{"code", 200},
                 {"data", QJsonArray{QJsonObject{{"songId", 4}, {key, QString(QChar::Null)}}}}})));
        }
    }
    void accountGates_data() { contracts_data(); }
    void accountGates()
    {
        QFETCH(int, kind);
        Harness guest({}, false);
        guest.client.setAnonymousToken("synthetic-anonymous");
        QSignalSpy done(&guest.service, &MusicQueryService::completed),
            raw(&guest.client, &SessionApiClient::completed);
        QVERIFY(guest.service.request(guest.session.snapshot().token, query(kind)));
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(std::get<MusicFailure>(result(done)).error, MusicError::AuthenticationRequired);
        const auto req = *accountLibraryRequest(query(kind));
        QVERIFY(guest.client.submit(guest.session.snapshot().token, req.operation, req.parameters));
        QTRY_COMPARE(raw.size(), 1);
        QCOMPARE(qvariant_cast<ApiResult>(raw.last()[2]).error, ApiError::Cookie);
        QVERIFY(guest.http->requests.isEmpty());
        Harness auth({});
        QSignalSpy foreign(&auth.service, &MusicQueryService::completed);
        QVERIFY(auth.service.request(auth.session.snapshot().token, query(kind, "2")));
        QTRY_COMPARE(foreign.size(), 1);
        QCOMPARE(std::get<MusicFailure>(result(foreign)).error, MusicError::InvalidRequest);
        QCOMPARE(auth.http->requests.size(), 1);
    }
    void nativeReads_data() { contracts_data(); }
    void nativeReads()
    {
        QFETCH(int, kind);
        Plan p{body(kind)};
        p.headers.append(std::pair<QByteArray, QByteArray>{
            "Set-Cookie", "MUSIC_U=synthetic-rotation; Domain=.music.163.com; Path=/"});
        Harness h({p, p}, true, "9007199254740993");
        QSignalSpy done(&h.service, &MusicQueryService::completed);
        const auto token = h.session.snapshot().token;
        const QStringList paths{"/weapi/djradio/get/subed", "/weapi/v1/cloud/get",
                                "/weapi/v1/cloud/get/byids"};
        for (int i = 1; i <= 2; ++i) {
            QVERIFY(h.service.request(token, query(kind, "9007199254740993")));
            QTRY_COMPARE(done.size(), i);
            QVERIFY(!std::holds_alternative<MusicFailure>(result(done)));
            QCOMPARE(qvariant_cast<SessionToken>(done.last()[1]), token);
            const auto request = h.http->requests.last();
            QCOMPARE(request.url().host(), "music.163.com");
            QCOMPARE(request.url().path(), paths[kind]);
            QVERIFY(request.rawHeader("Cookie").contains("synthetic-original"));
            QVERIFY(!request.rawHeader("Cookie").contains("synthetic-rotation"));
            QVERIFY(h.http->bodies.last().startsWith("params="));
            QVERIFY(h.http->bodies.last().contains("&encSecKey="));
        }
        QCOMPARE(h.http->requests.size(),
                 3); // seed plus two explicit reads; no URL/hydration/pagination
    }
    void lifecycleFailures_data() { contracts_data(); }
    void lifecycleFailures()
    {
        QFETCH(int, kind);
        Harness h({Plan{"", 200, -1}, Plan{"", 200, -1}});
        QSignalSpy done(&h.service, &MusicQueryService::completed);
        const auto token = h.session.snapshot().token;
        const auto id = h.service.request(token, query(kind));
        QVERIFY(id);
        QTRY_COMPARE(h.http->requests.size(), 2);
        h.service.cancel(id);
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(std::get<MusicFailure>(result(done)).error, MusicError::Canceled);
        QVERIFY(h.service.request(token, query(kind)));
        QTRY_COMPARE(h.http->requests.size(), 3);
        h.session.signOut();
        QTRY_COMPARE(done.size(), 2);
        QCOMPARE(std::get<MusicFailure>(result(done)).error, MusicError::StaleContext);
        QCOMPARE(qvariant_cast<SessionToken>(done.last()[1]), token);
        Harness limited({Plan{"", 429}});
        QSignalSpy rate(&limited.service, &MusicQueryService::completed);
        QVERIFY(limited.service.request(limited.session.snapshot().token, query(kind)));
        QTRY_COMPARE(rate.size(), 1);
        QCOMPARE(std::get<MusicFailure>(result(rate)).error, MusicError::RateLimited);
        QCOMPARE(std::get<MusicFailure>(result(rate)).retryAfterMs, std::optional<qint64>(3000));
        QTest::qWait(10);
        QCOMPARE(limited.http->requests.size(), 2);
        Harness reject({Plan{R"({"code":-462})"}});
        QSignalSpy denied(&reject.service, &MusicQueryService::completed);
        QVERIFY(reject.service.request(reject.session.snapshot().token, query(kind)));
        QTRY_COMPARE(denied.size(), 1);
        QCOMPARE(std::get<MusicFailure>(result(denied)).error, MusicError::ProviderRejected);
        QCOMPARE(std::get<MusicFailure>(result(denied)).providerCode, std::optional<int>(-462));
        QTest::qWait(10);
        QCOMPARE(reject.http->requests.size(), 2);
    }
    void facadeIsolation()
    {
        BackendFacade f;
        f.start();
        BackendEvent e;
        e.kind = BackendEvent::Kind::Started;
        f.deliver(e);
        e.kind = BackendEvent::Kind::Session;
        e.session = {{QUuid::createUuid(), 1}, SessionPhase::Guest};
        f.deliver(e);
        QVERIFY(!f.subscribedPodcasts());
        QVERIFY(!f.cloudSongs());
        QVERIFY(!f.cloudSongDetails({{"4"}}));
        e.session.phase = SessionPhase::Authenticated;
        e.session.accountId = "9007199254740993";
        f.deliver(e);
        const auto token = e.session.token;
        QSignalSpy req(&f, &BackendFacade::musicRequested),
            done(&f, &BackendFacade::musicCompleted), play(&f, &BackendFacade::playbackRequested),
            report(&f, &BackendFacade::listeningReportRequested);
        QVERIFY(f.subscribedPodcasts());
        QVERIFY(f.cloudSongs());
        QVERIFY(f.cloudSongDetails({{"4"}, {"5"}}));
        for (const auto& row : req) {
            const auto command = qvariant_cast<MusicCommand>(row[0]);
            QCOMPARE(command.context, token);
            QCOMPARE(musicQueryAccount(command.query), std::optional<QString>(e.session.accountId));
        }
        QVERIFY(!f.cloudSongs({0, 0}));
        QVERIFY(!f.cloudSongDetails({}));
        QVERIFY(!f.cloudSongDetails({{"4"}, {"4"}}));
        while (req.size() < 16)
            QVERIFY(f.cloudSongs());
        QVERIFY(!f.subscribedPodcasts());
        const auto old = qvariant_cast<MusicCommand>(req.first()[0]);
        QVERIFY(f.signOut());
        QVERIFY(!f.cloudSongs());
        QTRY_COMPARE(done.size(), 16);
        f.deliverMusic({old.id, old.generation, old.context, SubscribedPodcastsPage{}});
        QCOMPARE(done.size(), 16);
        for (const auto& row : done) {
            QCOMPARE(qvariant_cast<SessionToken>(row[1]), token);
            QCOMPARE(std::get<MusicFailure>(qvariant_cast<MusicResult>(row[2])).error,
                     MusicError::StaleContext);
        }
        QVERIFY(play.isEmpty());
        QVERIFY(report.isEmpty());
    }
};
QTEST_GUILESS_MAIN(AccountLibraryTest)
#include "account_library_test.moc"
