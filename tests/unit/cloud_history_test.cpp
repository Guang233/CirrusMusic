#include "../support/fake_http.h"
#include "application/backend/backend_facade.h"
#include "application/music/music_query_service.h"
#include "infrastructure/netease/music/cloud_history_mapper.h"
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
MusicResult mapped(bool recent, const QByteArray& body)
{
    const MusicQuery q = recent ? MusicQuery(RecentSongsQuery{"101", 2})
                                : MusicQuery(ListeningRankingQuery{"101", RankingPeriod::LastWeek});
    return mapMusicResponse(q, QJsonDocument::fromJson(body).object());
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
        auto manager = std::make_unique<FakeManager>();
        out = manager.get();
        manager->plans = plans;
        return std::make_unique<HttpTransport>(std::move(manager));
    }
    Harness(QList<Plan> plans, bool auth = true)
        : client(transport(plans, http), "synthetic-cloud-history")
    {
        QVERIFY(client.advanceTo(session.snapshot().token));
        QObject::connect(&session, &SessionCoordinator::changed, &client,
                         [this](SessionSnapshot s) { QVERIFY(client.advanceTo(s.token)); });
        if (!auth)
            return;
        const auto attempt = session.beginAuthentication();
        const auto prepared = session.prepareAuthentication(attempt, "101");
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
class CloudHistoryTest : public QObject
{
    Q_OBJECT
  private slots:
    void contracts()
    {
        QVERIFY((RecentSongsQuery{"101"}.isValid()));
        QCOMPARE(RecentSongsQuery{"101"}.limit, 100);
        QVERIFY(!(RecentSongsQuery{"101", 0}.isValid()));
        QVERIFY(!(RecentSongsQuery{"101", 101}.isValid()));
        QVERIFY(!(ListeningRankingQuery{"101", static_cast<RankingPeriod>(2)}.isValid()));
        const QString wide = "9007199254740993";
        for (const MusicQuery q :
             {MusicQuery(RecentSongsQuery{wide, 25}), MusicQuery(ListeningRankingQuery{wide}),
              MusicQuery(ListeningRankingQuery{wide, RankingPeriod::LastWeek})}) {
            QCOMPARE(musicQueryAccount(q), std::optional<QString>(wide));
            QVERIFY(requiresAuthenticatedMusicQuery(q));
            QVERIFY(!libraryQueryAccount(q));
            QVERIFY(historyQueryAccount(q));
            const auto request = cloudHistoryRequest(q);
            QVERIFY(request);
            const auto c = operationContract(request->operation);
            QVERIFY(c);
            QCOMPARE(c->protocol, Protocol::Weapi);
            QCOMPARE(c->responseLimit, 4 * 1024 * 1024);
            QCOMPARE(c->destination, OfficialDestination::Default);
            QVERIFY(requiresAccountCookie(request->operation));
            const auto p = operationPayload(request->operation, request->parameters);
            QVERIFY(p);
            if (std::holds_alternative<RecentSongsQuery>(q)) {
                QCOMPARE(c->logicalPath, "/api/play-record/song/list");
                QCOMPARE(p->size(), 1);
                QCOMPARE(p->value("limit").toInt(), 25);
            } else {
                QCOMPARE(c->logicalPath, "/api/v1/play/record");
                QCOMPARE(p->value("uid").toString(), wide);
                QCOMPARE(p->value("type").toInt(),
                         std::get<ListeningRankingQuery>(q).period == RankingPeriod::LastWeek ? 1
                                                                                              : 0);
            }
        }
        QVERIFY(!operationPayload(ApiOperation::RecentSongs, {{"limit", 1.5}}));
        QVERIFY(!operationPayload(ApiOperation::RecentSongs, {{"limit", 101}}));
        QVERIFY(!operationPayload(ApiOperation::RecentSongs, {{"limit", 1}, {"offset", 1}}));
        QVERIFY(!operationPayload(ApiOperation::ListeningRanking, {{"uid", wide}, {"type", 2}}));
        QVERIFY(!operationPayload(ApiOperation::ListeningRanking, {{"uid", wide}, {"type", true}}));
        QVERIFY(!operationPayload(ApiOperation::ListeningRanking,
                                  {{"uid", wide}, {"type", 1}, {"limit", 1}}));
    }
    void recentPreservesServerEvidence()
    {
        const auto r = mapped(
            true,
            R"({"code":200,"uid":101,"data":{"total":8,"list":[{"resourceId":"9007199254740993","resourceType":"SONG","playTime":1730000000000,"data":{"id":"9007199254740993","dt":180000}},{"data":{"id":"9007199254740993"}}]}})");
        QVERIFY(std::holds_alternative<CloudRecentSongs>(r));
        const auto p = std::get<CloudRecentSongs>(r);
        QCOMPARE(p.query.accountId, "101");
        QCOMPARE(p.query.limit, 2);
        QCOMPARE(p.total, std::optional<qint64>(8));
        QCOMPARE(p.songs.size(), 2);
        QCOMPARE(p.songs[0].song.id.value, p.songs[1].song.id.value);
        QCOMPARE(p.songs[0].providerPlayTime, std::optional<qint64>(1730000000000LL));
        QCOMPARE(p.songs[0].song.durationMs, std::optional<qint64>(180000));
        QVERIFY(!p.songs[1].providerPlayTime); // no fallback to wall clock/song duration
        const auto unknown = mapped(true, R"({"code":200,"data":{"list":[]}})");
        QVERIFY(std::holds_alternative<CloudRecentSongs>(unknown));
        QVERIFY(!std::get<CloudRecentSongs>(unknown).total);
    }
    void rankingPreservesCountsAndScores()
    {
        const QJsonObject root =
            QJsonDocument::fromJson(
                R"({"code":200,"weekData":[{"song":{"id":2},"playCount":0,"score":87.5},{"song":{"id":1},"playCount":3},{"song":{"id":2},"score":0}],"allData":[{"song":{"id":3},"playCount":20,"score":100}]})")
                .object();
        const auto week =
            mapMusicResponse(ListeningRankingQuery{"101", RankingPeriod::LastWeek}, root);
        QVERIFY(std::holds_alternative<ListeningRanking>(week));
        const auto p = std::get<ListeningRanking>(week);
        QCOMPARE(p.query.period, RankingPeriod::LastWeek);
        QCOMPARE(p.songs.size(), 3);
        QCOMPARE(p.songs[0].song.id.value, "2");
        QCOMPARE(p.songs[0].playCount, std::optional<qint64>(0));
        QCOMPARE(p.songs[0].score, std::optional<double>(87.5));
        QVERIFY(!p.songs[1].score);
        QVERIFY(!p.songs[2].playCount);
        const auto all = mapMusicResponse(ListeningRankingQuery{"101"}, root);
        QVERIFY(std::holds_alternative<ListeningRanking>(all));
        QCOMPARE(std::get<ListeningRanking>(all).songs.first().song.id.value, "3");
        const auto empty = mapped(false, R"({"code":200,"weekData":[]})");
        QVERIFY(std::holds_alternative<ListeningRanking>(empty));
    }
    void malformed_data()
    {
        QTest::addColumn<bool>("recent");
        QTest::addColumn<QByteArray>("body");
        QTest::newRow("no-data") << true << QByteArray(R"({"code":200})");
        QTest::newRow("null-list") << true << QByteArray(R"({"code":200,"data":{"list":null}})");
        QTest::newRow("non-object") << true << QByteArray(R"({"code":200,"data":{"list":[1]}})");
        QTest::newRow("wrong-resource")
            << true
            << QByteArray(
                   R"({"code":200,"data":{"list":[{"resourceType":"VIDEO","data":{"id":1}}]}})");
        QTest::newRow("mismatched-resource")
            << true
            << QByteArray(R"({"code":200,"data":{"list":[{"resourceId":2,"data":{"id":1}}]}})");
        QTest::newRow("negative-playtime")
            << true
            << QByteArray(R"({"code":200,"data":{"list":[{"playTime":-1,"data":{"id":1}}]}})");
        QTest::newRow("fractional-playtime")
            << true
            << QByteArray(R"({"code":200,"data":{"list":[{"playTime":1.5,"data":{"id":1}}]}})");
        QTest::newRow("missing-song")
            << true << QByteArray(R"({"code":200,"data":{"list":[{"resourceId":1}]}})");
        QTest::newRow("unsafe-numeric-id")
            << true
            << QByteArray(R"({"code":200,"data":{"list":[{"data":{"id":9007199254740993}}]}})");
        QTest::newRow("contradictory-total")
            << true << QByteArray(R"({"code":200,"data":{"total":0,"list":[{"data":{"id":1}}]}})");
        QTest::newRow("wrong-account")
            << true << QByteArray(R"({"code":200,"uid":202,"data":{"list":[]}})");
        QTest::newRow("wrong-window") << false << QByteArray(R"({"code":200,"allData":[]})");
        QTest::newRow("missing-ranked-song")
            << false << QByteArray(R"({"code":200,"weekData":[{"playCount":1}]})");
        QTest::newRow("fractional-count")
            << false
            << QByteArray(R"({"code":200,"weekData":[{"song":{"id":1},"playCount":1.5}]})");
        QTest::newRow("negative-count")
            << false << QByteArray(R"({"code":200,"weekData":[{"song":{"id":1},"playCount":-1}]})");
        QTest::newRow("bad-score")
            << false << QByteArray(R"({"code":200,"weekData":[{"song":{"id":1},"score":"100"}]})");
        QTest::newRow("negative-score")
            << false << QByteArray(R"({"code":200,"weekData":[{"song":{"id":1},"score":-1}]})");
        QTest::newRow("bad-code") << false << QByteArray(R"({"code":500,"weekData":[]})");
    }
    void malformed()
    {
        QFETCH(bool, recent);
        QFETCH(QByteArray, body);
        const auto r = mapped(recent, body);
        QVERIFY(std::holds_alternative<MusicFailure>(r));
        QCOMPARE(std::get<MusicFailure>(r).error, MusicError::InvalidResponse);
    }
    void boundedLists()
    {
        QJsonArray recentRows;
        for (int i = 0; i < 3; ++i)
            recentRows.append(QJsonObject{{"data", QJsonObject{{"id", i + 1}}}});
        recentRows.append(1); // past the limit: not read
        // The service ignores the limit and sends its whole list: the first rows are kept.
        const auto recent =
            mapMusicResponse(RecentSongsQuery{"101", 2},
                             {{"code", 200}, {"data", QJsonObject{{"list", recentRows}}}});
        QVERIFY(std::holds_alternative<CloudRecentSongs>(recent));
        const auto& kept = std::get<CloudRecentSongs>(recent).songs;
        QCOMPARE(kept.size(), 2);
        QCOMPARE(kept.at(0).song.id.value, QStringLiteral("1"));
        QCOMPARE(kept.at(1).song.id.value, QStringLiteral("2"));
        QJsonArray ranks;
        for (int i = 0; i < 1000; ++i)
            ranks.append(QJsonObject{{"song", QJsonObject{{"id", i + 1}}}});
        const auto q = ListeningRankingQuery{"101"};
        const auto accepted = mapMusicResponse(q, {{"code", 200}, {"allData", ranks}});
        QVERIFY(std::holds_alternative<ListeningRanking>(accepted));
        QCOMPARE(std::get<ListeningRanking>(accepted).songs.size(), 1000);
        ranks.append(QJsonObject{{"song", QJsonObject{{"id", 1001}}}});
        QVERIFY(std::holds_alternative<MusicFailure>(
            mapMusicResponse(q, {{"code", 200}, {"allData", ranks}})));
    }
    void nativeReadChainAndCookies()
    {
        Plan recent{R"({"code":200,"data":{"list":[{"data":{"id":1}}]}})"};
        recent.headers.append(std::pair<QByteArray, QByteArray>{
            "Set-Cookie", "MUSIC_U=synthetic-rotation; Domain=.music.163.com; Path=/"});
        Harness h({recent, Plan{R"({"code":200,"allData":[{"song":{"id":2},"playCount":3}]})"}});
        QSignalSpy done(&h.service, &MusicQueryService::completed);
        QVERIFY(h.service.request(h.session.snapshot().token, RecentSongsQuery{"101", 1}));
        QTRY_COMPARE(done.size(), 1);
        QVERIFY(std::holds_alternative<CloudRecentSongs>(result(done)));
        QCOMPARE(h.http->requests.last().url().path(), "/weapi/play-record/song/list");
        QVERIFY(h.service.request(h.session.snapshot().token, ListeningRankingQuery{"101"}));
        QTRY_COMPARE(done.size(), 2);
        QVERIFY(std::holds_alternative<ListeningRanking>(result(done)));
        QCOMPARE(h.http->requests.last().url().path(), "/weapi/v1/play/record");
        QVERIFY(h.http->requests.last().rawHeader("Cookie").contains("MUSIC_U=synthetic-original"));
        QCOMPARE(h.http->requests.size(), 3); // seed + two reads; no hydration/reporting
    }
    void authenticationAndAccountGate()
    {
        Harness guest({}, false);
        QSignalSpy g(&guest.service, &MusicQueryService::completed);
        const auto context = guest.session.snapshot().token;
        for (const MusicQuery q :
             {MusicQuery(RecentSongsQuery{"101"}), MusicQuery(ListeningRankingQuery{"101"})})
            QVERIFY(guest.service.request(context, q));
        QTRY_COMPARE(g.size(), 2);
        for (const auto& args : g)
            QCOMPARE(std::get<MusicFailure>(qvariant_cast<MusicResult>(args[2])).error,
                     MusicError::AuthenticationRequired);
        QVERIFY(guest.http->requests.isEmpty());
        guest.client.setAnonymousToken("synthetic-anonymous");
        QSignalSpy raw(&guest.client, &SessionApiClient::completed);
        QVERIFY(guest.client.submit(context, ApiOperation::RecentSongs, {{"limit", 1}}));
        QVERIFY(guest.client.submit(context, ApiOperation::ListeningRanking,
                                    {{"uid", "101"}, {"type", 0}}));
        QTRY_COMPARE(raw.size(), 2);
        for (const auto& args : raw)
            QCOMPARE(qvariant_cast<ApiResult>(args[2]).error, ApiError::Cookie);
        QVERIFY(guest.http->requests.isEmpty());
        Harness h({});
        QSignalSpy done(&h.service, &MusicQueryService::completed);
        QVERIFY(h.service.request(h.session.snapshot().token, RecentSongsQuery{"202"}));
        QVERIFY(h.service.request(h.session.snapshot().token, ListeningRankingQuery{"202"}));
        QTRY_COMPARE(done.size(), 2);
        for (const auto& args : done)
            QCOMPARE(std::get<MusicFailure>(qvariant_cast<MusicResult>(args[2])).error,
                     MusicError::InvalidRequest);
        QCOMPARE(h.http->requests.size(), 1);
    }
    void cancelAccountChangeAndRateLimit()
    {
        Harness h({Plan{"", 200, -1}, Plan{"", 200, -1}, Plan{"", 429}});
        QSignalSpy done(&h.service, &MusicQueryService::completed);
        const auto context = h.session.snapshot().token;
        const auto id = h.service.request(context, RecentSongsQuery{"101"});
        QVERIFY(id);
        QTRY_COMPARE(h.http->requests.size(), 2);
        h.service.cancel(id);
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(std::get<MusicFailure>(result(done)).error, MusicError::Canceled);
        QVERIFY(h.service.request(context, ListeningRankingQuery{"101"}));
        QTRY_COMPARE(h.http->requests.size(), 3);
        h.session.signOut();
        QTRY_COMPARE(done.size(), 2);
        QCOMPARE(std::get<MusicFailure>(result(done)).error, MusicError::StaleContext);
        QCOMPARE(qvariant_cast<SessionToken>(done.last()[1]), context);
        Harness limited({Plan{"", 429}});
        QSignalSpy rate(&limited.service, &MusicQueryService::completed);
        QVERIFY(limited.service.request(limited.session.snapshot().token, RecentSongsQuery{"101"}));
        QTRY_COMPARE(rate.size(), 1);
        QCOMPARE(std::get<MusicFailure>(result(rate)).error, MusicError::RateLimited);
        QCOMPARE(std::get<MusicFailure>(result(rate)).retryAfterMs, std::optional<qint64>(3000));
        QTest::qWait(10);
        QCOMPARE(limited.http->requests.size(), 2);
    }
    void facadeDefaultsBoundsAndLogout()
    {
        BackendFacade f;
        f.start();
        BackendEvent e;
        e.kind = BackendEvent::Kind::Started;
        f.deliver(e);
        const SessionToken token{QUuid::createUuid(), 1};
        e.kind = BackendEvent::Kind::Session;
        e.session = {token, SessionPhase::Guest};
        f.deliver(e);
        QSignalSpy commands(&f, &BackendFacade::musicRequested),
            done(&f, &BackendFacade::musicCompleted),
            reports(&f, &BackendFacade::listeningReportRequested);
        QVERIFY(!f.recentSongs());
        QVERIFY(!f.listeningRanking());
        QVERIFY(commands.isEmpty());
        e.session = {token, SessionPhase::Authenticated, "101"};
        f.deliver(e);
        QVERIFY(!f.recentSongs(101));
        QVERIFY(!f.listeningRanking(static_cast<RankingPeriod>(2)));
        const auto id = f.recentSongs();
        QVERIFY(id);
        const auto c = qvariant_cast<MusicCommand>(commands.last()[0]);
        QCOMPARE(std::get<RecentSongsQuery>(c.query).accountId, "101");
        QCOMPARE(std::get<RecentSongsQuery>(c.query).limit, 100);
        f.deliverMusic({id, c.generation, c.context,
                        CloudRecentSongs{std::get<RecentSongsQuery>(c.query), {}, {}}});
        QCOMPARE(done.size(), 1);
        QVERIFY(f.listeningRanking());
        const auto rank = qvariant_cast<MusicCommand>(commands.last()[0]);
        QCOMPARE(std::get<ListeningRankingQuery>(rank.query).period, RankingPeriod::AllTime);
        for (int i = 0; i < 15; ++i)
            QVERIFY(f.recentSongs(1));
        QVERIFY(!f.recentSongs());
        QVERIFY(f.signOut());
        QTRY_COMPARE(done.size(), 17);
        f.deliverMusic({rank.id, rank.generation, token,
                        ListeningRanking{std::get<ListeningRankingQuery>(rank.query), {}}});
        QCOMPARE(done.size(), 17);
        QCOMPARE(std::get<MusicFailure>(result(done)).error, MusicError::StaleContext);
        QVERIFY(reports.isEmpty());
    }
};
QTEST_GUILESS_MAIN(CloudHistoryTest)
#include "cloud_history_test.moc"
