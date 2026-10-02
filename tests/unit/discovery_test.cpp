#include "../support/fake_http.h"
#include "application/music/music_query_service.h"
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
    explicit Harness(QList<Plan> plans, bool authenticated = true)
        : client(transport(std::move(plans), http), "synthetic-discovery")
    {
        QVERIFY(client.advanceTo(session.snapshot().token));
        QObject::connect(&session, &SessionCoordinator::changed, &client,
                         [this](SessionSnapshot s) { QVERIFY(client.advanceTo(s.token)); });
        if (authenticated) {
            const auto attempt = session.beginAuthentication();
            const auto prepared = session.prepareAuthentication(attempt, "1");
            QVERIFY(prepared);
            QVERIFY(session.completeAuthentication(*prepared));
            // Synthetic credential fixture only; real promotion exercised in runtime test.
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
class DiscoveryTest : public QObject
{
    Q_OBJECT
  private slots:
    void contractsAndNoRefreshOrFeedback()
    {
        const auto daily = operationContract(ApiOperation::DailyRecommendations);
        const auto fm = operationContract(ApiOperation::PersonalFm);
        QVERIFY(daily && fm);
        QCOMPARE(daily->logicalPath, "/api/v3/discovery/recommend/songs");
        QCOMPARE(fm->logicalPath, "/api/v1/radio/get");
        QCOMPARE(daily->protocol, Protocol::Weapi);
        QCOMPARE(fm->protocol, Protocol::Weapi);
        QCOMPARE(*operationPayload(ApiOperation::DailyRecommendations, {}),
                 json(R"({"afresh":false})"));
        QVERIFY(operationPayload(ApiOperation::PersonalFm, {})->isEmpty());
        for (auto op : {ApiOperation::DailyRecommendations, ApiOperation::PersonalFm}) {
            QVERIFY(!operationPayload(op, {{"afresh", true}}));
            QVERIFY(!operationPayload(op, {{"id", "1"}}));
            QVERIFY(!operationPayload(op, {{"url", "arbitrary"}}));
            DecodedResponse r;
            r.businessCode = 200;
            QVERIFY(!acceptsResponseCookies(op, r));
        }
    }
    void independentSchemasReasonsAndReceiptTime()
    {
        const auto daily = std::get<DailyRecommendations>(mapMusicResponse(
            DailyRecommendationsQuery{},
            json(
                R"({"code":200,"data":{"dailySongs":[{"id":"9007199254740993","dt":12000,"ar":[{"id":2}],"reason":"inline"},{"id":3}],"recommendReasons":[{"songId":3,"reason":"for third"},{"songId":"9007199254740993","reason":"wide"}]}})")));
        QCOMPARE(daily.songs.size(), 2);
        QCOMPARE(daily.songs.first().song.id.value, "9007199254740993");
        QCOMPARE(daily.songs.first().inlineReason, std::optional<QString>("inline"));
        QVERIFY(!daily.songs.last().inlineReason);
        QCOMPARE(daily.reasons.first().song.value, "3");
        QVERIFY(!daily.receivedAtUtc.isValid()); // pure mapping does not invent a provider date
        const auto fm = std::get<PersonalFmBatch>(mapMusicResponse(
            PersonalFmQuery{},
            json(
                R"({"code":200,"data":[{"id":3,"duration":17000,"artists":[{"id":4}],"album":{"id":5}},{"id":3}]})")));
        QCOMPARE(fm.songs.size(), 2); // raw duplicate kept; dynamic source deduplicates
        QCOMPARE(fm.songs.first().durationMs, std::optional<qint64>(17000));
        QCOMPARE(fm.songs.first().album->id->value, "5");
        QVERIFY(std::get<PersonalFmBatch>(
                    mapMusicResponse(PersonalFmQuery{}, json(R"({"code":200,"data":[]})")))
                    .songs.isEmpty());
        QVERIFY(std::get<DailyRecommendations>(
                    mapMusicResponse(DailyRecommendationsQuery{},
                                     json(R"({"code":200,"data":{"dailySongs":[]}})")))
                    .songs.isEmpty());
    }
    void malformed_data()
    {
        QTest::addColumn<bool>("daily");
        QTest::addColumn<QByteArray>("body");
        QTest::newRow("missing daily list") << true << QByteArray(R"({"code":200,"data":{}})");
        QTest::newRow("null daily list")
            << true << QByteArray(R"({"code":200,"data":{"dailySongs":null}})");
        QTest::newRow("bad reason")
            << true
            << QByteArray(R"({"code":200,"data":{"dailySongs":[{"id":1,"reason":false}]}})");
        QTest::newRow("unkeyed reason")
            << true
            << QByteArray(
                   R"({"code":200,"data":{"dailySongs":[],"recommendReasons":[{"reason":"x"}]}})");
        QTest::newRow("unsafe numeric id")
            << true
            << QByteArray(R"({"code":200,"data":{"dailySongs":[{"id":9007199254740993}]}})");
        QTest::newRow("missing fm list") << false << QByteArray(R"({"code":200})");
        QTest::newRow("nested fm object")
            << false << QByteArray(R"({"code":200,"data":{"songs":[]}})");
        QTest::newRow("bad fm song") << false << QByteArray(R"({"code":200,"data":[{"id":"01"}]})");
    }
    void malformed()
    {
        QFETCH(bool, daily);
        QFETCH(QByteArray, body);
        const MusicQuery q =
            daily ? MusicQuery{DailyRecommendationsQuery{}} : MusicQuery{PersonalFmQuery{}};
        QCOMPARE(std::get<MusicFailure>(mapMusicResponse(q, json(body))).error,
                 MusicError::InvalidResponse);
    }
    void limits()
    {
        QJsonArray a;
        for (int i = 0; i < 501; ++i)
            a.append(QJsonObject{{"id", i + 1}});
        QVERIFY(std::holds_alternative<MusicFailure>(
            mapMusicResponse(DailyRecommendationsQuery{},
                             {{"code", 200}, {"data", QJsonObject{{"dailySongs", a}}}})));
        QJsonArray reasons;
        for (int i = 0; i < 501; ++i)
            reasons.append(QJsonObject{{"songId", i + 1}, {"reason", "synthetic"}});
        QVERIFY(std::holds_alternative<MusicFailure>(mapMusicResponse(
            DailyRecommendationsQuery{},
            {{"code", 200},
             {"data", QJsonObject{{"dailySongs", QJsonArray{}}, {"recommendReasons", reasons}}}})));
        a = {};
        for (int i = 0; i < 21; ++i)
            a.append(QJsonObject{{"id", i + 1}});
        QVERIFY(std::holds_alternative<MusicFailure>(
            mapMusicResponse(PersonalFmQuery{}, {{"code", 200}, {"data", a}})));
        QVERIFY(std::holds_alternative<MusicFailure>(mapMusicResponse(
            DailyRecommendationsQuery{},
            {{"code", 200},
             {"data",
              QJsonObject{{"dailySongs", QJsonArray{QJsonObject{
                                             {"id", 1}, {"reason", QString(4097, 'x')}}}}}}})));
    }
    void guestRejectedBeforeNetwork()
    {
        Harness h({}, false);
        QSignalSpy done(&h.service, &MusicQueryService::completed);
        QVERIFY(h.service.request(h.session.snapshot().token, DailyRecommendationsQuery{}));
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(std::get<MusicFailure>(result(done)).error, MusicError::AuthenticationRequired);
        QVERIFY(h.service.request(h.session.snapshot().token, PersonalFmQuery{}));
        QTRY_COMPARE(done.size(), 2);
        QCOMPARE(std::get<MusicFailure>(result(done)).error, MusicError::AuthenticationRequired);
        QCOMPARE(h.http->requests.size(), 0);
        h.client.setAnonymousToken("synthetic-guest");
        QVERIFY(h.client.hasAnonymousToken());
        QSignalSpy native(&h.client, &SessionApiClient::completed);
        QVERIFY(h.client.submit(h.session.snapshot().token, ApiOperation::PersonalFm));
        QTRY_COMPARE(native.size(), 1);
        QCOMPARE(qvariant_cast<ApiResult>(native.last().at(2)).error, ApiError::Cookie);
        QCOMPARE(h.http->requests.size(), 0);
    }
    void nativeReadsAndCookieIsolation()
    {
        Plan daily{R"({"code":200,"data":{"dailySongs":[{"id":1}]}})"};
        daily.headers.append(std::pair<QByteArray, QByteArray>{
            "Set-Cookie", "MUSIC_U=synthetic-untrusted; Domain=.music.163.com; Path=/"});
        Plan fm{R"({"code":200,"data":[{"id":2}]})"};
        fm.headers.append(std::pair<QByteArray, QByteArray>{
            "Set-Cookie", "MUSIC_U=synthetic-fm-untrusted; Domain=.music.163.com; Path=/"});
        Harness h({daily, fm, Plan{R"({"code":200,"data":[]})"}});
        QSignalSpy done(&h.service, &MusicQueryService::completed);
        const auto token = h.session.snapshot().token;
        QVERIFY(h.service.request(token, DailyRecommendationsQuery{}));
        QTRY_COMPARE(done.size(), 1);
        QVERIFY(std::get<DailyRecommendations>(result(done)).receivedAtUtc.isValid());
        QCOMPARE(h.http->requests.last().url().path(), "/weapi/v3/discovery/recommend/songs");
        QVERIFY(h.http->bodies.last().startsWith("params="));
        QVERIFY(h.service.request(token, PersonalFmQuery{}));
        QTRY_COMPARE(done.size(), 2);
        QCOMPARE(std::get<PersonalFmBatch>(result(done)).songs.first().id.value, "2");
        QCOMPARE(h.http->requests.last().url().path(), "/weapi/v1/radio/get");
        QVERIFY(h.http->requests.last().rawHeader("Cookie").contains("MUSIC_U=synthetic-original"));
        QVERIFY(!h.http->requests.last().rawHeader("Cookie").contains("untrusted"));
        QVERIFY(h.service.request(token, PersonalFmQuery{}));
        QTRY_COMPARE(done.size(), 3);
        QVERIFY(h.http->requests.last().rawHeader("Cookie").contains("MUSIC_U=synthetic-original"));
        QVERIFY(!h.http->requests.last().rawHeader("Cookie").contains("untrusted"));
        QCOMPARE(qvariant_cast<SessionToken>(done.last().at(1)), token);
    }
    void cancelAndSessionReplacement()
    {
        Harness h({Plan{"{}", 200, -1}, Plan{"{}", 200, -1}});
        QSignalSpy done(&h.service, &MusicQueryService::completed);
        const auto token = h.session.snapshot().token;
        const auto first = h.service.request(token, PersonalFmQuery{});
        QVERIFY(first);
        QTRY_COMPARE(h.http->requests.size(), 2);
        h.service.cancel(first);
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(std::get<MusicFailure>(result(done)).error, MusicError::Canceled);
        QVERIFY(h.service.request(token, DailyRecommendationsQuery{}));
        QTRY_COMPARE(h.http->requests.size(), 3);
        h.session.signOut();
        QTRY_COMPARE(done.size(), 2);
        QCOMPARE(std::get<MusicFailure>(result(done)).error, MusicError::StaleContext);
        QCOMPARE(qvariant_cast<SessionToken>(done.last().at(1)), token);
    }
    void rateLimitNoAutomaticRetry()
    {
        Plan limited{R"({"code":429})", 429};
        // Plan already has one Retry-After: 3; a duplicate would parse as "3, 3".
        Harness h({limited});
        QSignalSpy done(&h.service, &MusicQueryService::completed);
        QVERIFY(h.service.request(h.session.snapshot().token, PersonalFmQuery{}));
        QTRY_COMPARE(done.size(), 1);
        const auto e = std::get<MusicFailure>(result(done));
        QCOMPARE(e.error, MusicError::RateLimited);
        QCOMPARE(e.retryAfterMs, std::optional<qint64>(3000));
        QCoreApplication::processEvents();
        QCOMPARE(h.http->requests.size(), 2);
    }
};
QTEST_GUILESS_MAIN(DiscoveryTest)
#include "discovery_test.moc"
