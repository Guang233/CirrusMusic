#include "../support/fake_http.h"
#include "../support/synthetic_recommendation_history.h"
#include "application/backend/backend_facade.h"
#include "application/music/music_query_service.h"
#include "infrastructure/netease/music/music_gateway.h"
#include "infrastructure/netease/music/music_mapper.h"
#include "infrastructure/netease/music/recommendation_history_mapper.h"
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
QJsonObject json(const QByteArray& b) { return QJsonDocument::fromJson(b).object(); }
MusicResult result(const QSignalSpy& s) { return qvariant_cast<MusicResult>(s.last()[2]); }
DailyHistoryDates dates(QString account = "101")
{
    return {DailyHistoryDatesQuery{account}, {"2020-06-21", "2024-02-29", "2020-06-21"}};
}
MusicQuery query(SessionToken token, bool detail, QString account = "101")
{
    if (!detail)
        return DailyHistoryDatesQuery{account};
    return *DailyHistorySongsQuery::fromDates(token, dates(account), "2020-06-21");
}
QByteArray fixture(bool detail)
{
    return detail ? recommendationHistoryFixture() : recommendationDatesFixture();
}
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
    Harness(QList<Plan> plans = {}, bool auth = true, bool cookie = true)
        : client(transport(plans, http), "synthetic-history")
    {
        QVERIFY(client.advanceTo(session.snapshot().token));
        client.setAnonymousToken("synthetic-anon");
        QObject::connect(&session, &SessionCoordinator::changed, &client,
                         [this](SessionSnapshot s) { QVERIFY(client.advanceTo(s.token)); });
        if (!auth)
            return;
        const auto p = session.prepareAuthentication(session.beginAuthentication(), "101");
        QVERIFY(p);
        QVERIFY(session.completeAuthentication(*p));
        if (!cookie)
            return;
        Plan seed{R"({"code":200,"unikey":"synthetic"})"};
        seed.headers.append(std::pair<QByteArray, QByteArray>{
            "Set-Cookie", "MUSIC_U=synthetic-original; Domain=.music.163.com; Path=/"});
        http->plans.prepend(seed);
        QSignalSpy done(&client, &SessionApiClient::completed);
        QVERIFY(client.submit(session.snapshot().token, ApiOperation::QrKey));
        QTRY_COMPARE(done.size(), 1);
    }
};
void ready(BackendFacade& f)
{
    f.start();
    BackendEvent e;
    e.kind = BackendEvent::Kind::Started;
    f.deliver(e);
    e.kind = BackendEvent::Kind::Session;
    e.session = {{QUuid::createUuid(), 1}, SessionPhase::Authenticated, "101"};
    f.deliver(e);
}
} // namespace
class RecommendationHistoryTest : public QObject
{
    Q_OBJECT
  private slots:
    void contractsAndSelection()
    {
        const SessionToken token{QUuid::createUuid(), 1};
        for (const auto& label :
             QStringList{"", "2020-6-21", "2020-06-2", "2020-06-21 ", "2020-02-30", "2023-02-29",
                         "0000-01-01", "2020/06/21", "２０２０-06-21", "2020-06-21T00:00:00Z"})
            QVERIFY(!validRecommendationDate(label));
        QVERIFY(validRecommendationDate("2024-02-29"));
        QVERIFY(!DailyHistorySongsQuery::fromDates({}, dates(), "2020-06-21"));
        QVERIFY(!DailyHistorySongsQuery::fromDates(token, dates(), "2020-06-22"));
        auto bad = dates();
        bad.dates.append("invalid");
        QVERIFY(!DailyHistorySongsQuery::fromDates(token, bad, "2020-06-21"));
        bad = dates("01");
        QVERIFY(!DailyHistorySongsQuery::fromDates(token, bad, "2020-06-21"));
        bad = dates();
        bad.dates = QStringList(367, "2020-06-21");
        QVERIFY(!DailyHistorySongsQuery::fromDates(token, bad, "2020-06-21"));
        for (bool detail : {false, true}) {
            const auto q = query(token, detail);
            QVERIFY(validMusicQuery(q));
            QVERIFY(requiresAuthenticatedMusicQuery(q));
            QCOMPARE(musicQueryAccount(q), std::optional<QString>("101"));
            const auto req = recommendationHistoryRequest(q);
            QVERIFY(req);
            const auto op =
                detail ? ApiOperation::DailyHistorySongs : ApiOperation::DailyHistoryDates;
            QCOMPARE(req->operation, op);
            const auto contract = operationContract(op);
            QVERIFY(contract);
            QCOMPARE(contract->protocol, Protocol::Weapi);
            QCOMPARE(contract->responseEncoding, ResponseEncoding::Json);
            QCOMPARE(contract->responseLimit, detail ? 4 * 1024 * 1024 : 1024 * 1024);
            QVERIFY(requiresAccountCookie(op));
            const auto payload = operationPayload(op, req->parameters);
            QVERIFY(payload);
            QCOMPARE(payload->size(), detail ? 1 : 0);
            if (detail)
                QCOMPARE(payload->value("date").toString(), "2020-06-21");
            for (const auto& k : QStringList{"uid", "limit", "offset", "afresh", "e_r", "crypto",
                                             "checkToken", "header", "host"}) {
                auto extra = req->parameters;
                extra[k] = 1;
                QVERIFY(!operationPayload(op, extra));
            }
            DecodedResponse accepted;
            accepted.businessCode = 200;
            accepted.object = json(fixture(detail));
            QVERIFY(!acceptsResponseCookies(op, accepted));
        }
        QVERIFY(!operationPayload(ApiOperation::DailyHistorySongs, {}));
        QVERIFY(!operationPayload(ApiOperation::DailyHistorySongs, {{"date", ""}}));
        QVERIFY(!operationPayload(ApiOperation::DailyHistorySongs, {{"date", true}}));
        QVERIFY(!operationPayload(ApiOperation::DailyHistoryDates, {{"date", "2020-06-21"}}));
        QVERIFY(!recommendationHistoryRequest(DailyHistoryDatesQuery{"01"}));
        const auto page = std::get<DailyHistoryDates>(
            mapMusicResponse(query(token, false), json(fixture(false))));
        QCOMPARE(page.dates, dates().dates);
        QCOMPARE(page.query.accountId, "101");
        const auto songs =
            std::get<DailyHistorySongs>(mapMusicResponse(query(token, true), json(fixture(true))));
        QCOMPARE(songs.query.date(), "2020-06-21");
        QCOMPARE(songs.query.sourceContext(), token);
        QCOMPARE(songs.songs.size(), 3);
        QCOMPARE(songs.songs[0].id.value, "9007199254740993");
        QCOMPARE(songs.songs[2].id.value, songs.songs[0].id.value);
        QCOMPARE(songs.songs[0].title, "  <b>synthetic history</b>  ");
        QVERIFY(
            std::get<DailyHistoryDates>(
                mapMusicResponse(query(token, false),
                                 {{"code", 200}, {"data", QJsonObject{{"dates", QJsonArray{}}}}}))
                .dates.isEmpty());
        QVERIFY(
            std::get<DailyHistorySongs>(
                mapMusicResponse(query(token, true),
                                 {{"code", 200}, {"data", QJsonObject{{"songs", QJsonArray{}}}}}))
                .songs.isEmpty());
    }
    void badSchema_data()
    {
        QTest::addColumn<bool>("detail");
        QTest::addColumn<QJsonObject>("body");
        int n = 0;
        const auto row = [&](bool detail, QJsonObject body) {
            QTest::newRow(qPrintable(QString::number(++n))) << detail << body;
        };
        for (bool detail : {false, true}) {
            for (const auto& b :
                 QList<QByteArray>{R"({})", R"({"code":200})", R"({"code":"200","data":{}})",
                                   R"({"code":200,"data":null})", R"({"code":200,"data":[]})",
                                   R"({"code":200,"data":{}})"})
                row(detail, json(b));
            const auto k = detail ? QString("songs") : QString("dates");
            for (const auto v : QList<QJsonValue>{
                     QJsonValue(), QJsonValue(1), QJsonValue(QJsonObject{}),
                     QJsonValue(QJsonArray{QJsonValue(1)}), QJsonValue(QJsonArray{QJsonValue()})})
                row(detail, {{"code", 200}, {"data", QJsonObject{{k, v}}}});
        }
        for (const auto& label :
             QStringList{"", "2020-02-30", "2020-6-21", "2023-02-29", "2020-06-21 "})
            row(false, {{"code", 200}, {"data", QJsonObject{{"dates", QJsonArray{label}}}}});
        QJsonArray tooMany;
        for (int i = 0; i < 367; ++i)
            tooMany.append("2020-06-21");
        row(false, {{"code", 200}, {"data", QJsonObject{{"dates", tooMany}}}});
        const auto mutate = [&](QString k, QJsonValue v) {
            auto body = json(fixture(true));
            auto data = body["data"].toObject();
            auto songs = data["songs"].toArray();
            auto song = songs[0].toObject();
            song[k] = v;
            songs[0] = song;
            data["songs"] = songs;
            body["data"] = data;
            row(true, body);
        };
        for (const auto v : QList<QJsonValue>{
                 QJsonValue(), QJsonValue(0), QJsonValue(true), QJsonValue(1.5), QJsonValue("01"),
                 QJsonValue("9223372036854775808"), QJsonValue(qint64(9007199254740992LL))})
            mutate("id", v);
        for (const auto v : QList<QJsonValue>{QJsonValue(true), QJsonValue(QString(4097, 'x')),
                                              QJsonValue(QString(QChar(0xd800))),
                                              QJsonValue(QString("x") + QChar(0))})
            mutate("name", v);
        mutate("ar", QJsonArray{QJsonValue(true)});
        mutate("dt", -1);
        mutate("dt", 1.5);
        tooMany = QJsonArray{};
        for (int i = 0; i < 501; ++i)
            tooMany.append(QJsonObject{{"id", 1}, {"name", "synthetic"}});
        row(true, {{"code", 200}, {"data", QJsonObject{{"songs", tooMany}}}});
        row(true, {{"code", 200}, {"data", QJsonObject{{"dailySongs", QJsonArray{}}}}});
        row(false, {{"code", 200}, {"dates", QJsonArray{}}});
    }
    void badSchema()
    {
        QFETCH(bool, detail);
        QFETCH(QJsonObject, body);
        const auto r = mapMusicResponse(query({QUuid::createUuid(), 1}, detail), body);
        QVERIFY(std::holds_alternative<MusicFailure>(r));
        QCOMPARE(std::get<MusicFailure>(r).error, MusicError::InvalidResponse);
    }
    void nativeAndGates()
    {
        Plan list{fixture(false)}, detail{fixture(true)};
        for (auto* p : {&list, &detail})
            p->headers.append(std::pair<QByteArray, QByteArray>{
                "Set-Cookie", "MUSIC_U=synthetic-rotated; Domain=.music.163.com; Path=/"});
        Harness h({list, detail});
        QSignalSpy done(&h.service, &MusicQueryService::completed);
        const auto token = h.session.snapshot().token;
        for (bool detail : {false, true}) {
            QVERIFY(h.service.request(token, query(token, detail)));
            QTRY_COMPARE(done.size(), detail ? 2 : 1);
            QVERIFY(!std::holds_alternative<MusicFailure>(result(done)));
            QCOMPARE(qvariant_cast<SessionToken>(done.last()[1]), token);
            const auto req = h.http->requests.last();
            QCOMPARE(req.url().path(), detail ? "/weapi/discovery/recommend/songs/history/detail"
                                              : "/weapi/discovery/recommend/songs/history/recent");
            QCOMPARE(req.url().host(), "music.163.com");
            QVERIFY(req.rawHeader("Cookie").contains("MUSIC_U=synthetic-original"));
            QVERIFY(!req.rawHeader("Cookie").contains("rotated"));
            QVERIFY(!req.hasRawHeader("X-antiCheatToken"));
            QVERIFY(h.http->bodies.last().contains("encSecKey="));
        }
        QCOMPARE(h.http->requests.size(), 3);
        QVERIFY(h.service.request(token, query(token, true, "102")));
        QTRY_COMPARE(done.size(), 3);
        QCOMPARE(std::get<MusicFailure>(result(done)).error, MusicError::InvalidRequest);
        QCOMPARE(h.http->requests.size(), 3);
        auto old = token;
        ++old.epoch;
        QVERIFY(h.service.request(token, query(old, true)));
        QTRY_COMPARE(done.size(), 4);
        QCOMPARE(std::get<MusicFailure>(result(done)).error, MusicError::StaleContext);
        QCOMPARE(h.http->requests.size(), 3);
        QSignalSpy direct(&h.gateway, &MusicGateway::completed);
        QVERIFY(h.gateway.request(token, query(old, true)));
        QTRY_COMPARE(direct.size(), 1);
        QCOMPARE(std::get<MusicFailure>(result(direct)).error, MusicError::StaleContext);
        QCOMPARE(h.http->requests.size(), 3);
        for (bool detail : {false, true})
            for (int mode = 0; mode < 2; ++mode) {
                Harness denied({}, mode == 1, false);
                QSignalSpy d(&denied.service, &MusicQueryService::completed);
                const auto t = denied.session.snapshot().token;
                QVERIFY(denied.service.request(t, query(t, detail)));
                QTRY_COMPARE(d.size(), 1);
                // Guest is an application gate; missing original MUSIC_U is ApiError::Cookie.
                QCOMPARE(std::get<MusicFailure>(result(d)).error,
                         mode == 0 ? MusicError::AuthenticationRequired
                                   : MusicError::ProtocolFailure);
                QCOMPARE(denied.http->requests.size(), 0);
            }
    }
    void rejection_data()
    {
        QTest::addColumn<bool>("detail");
        QTest::addColumn<int>("mode");
        for (bool detail : {false, true})
            for (int mode = 0; mode < 9; ++mode)
                QTest::newRow(qPrintable(QString::number(detail) + "-" + QString::number(mode)))
                    << detail << mode;
    }
    void rejection()
    {
        QFETCH(bool, detail);
        QFETCH(int, mode);
        const QList<QByteArray> bodies{R"({"code":403})", R"({"code":429})",  R"({"code":-462})",
                                       R"({"code":500})", R"({"code":true})", "{bad",
                                       R"({"code":200})", fixture(detail),    fixture(detail)};
        Harness h({Plan{bodies[mode], mode == 7 ? 429 : mode == 8 ? 502 : 200}});
        QSignalSpy done(&h.service, &MusicQueryService::completed);
        const auto t = h.session.snapshot().token;
        QVERIFY(h.service.request(t, query(t, detail)));
        QTRY_COMPARE(done.size(), 1);
        const auto e = std::get<MusicFailure>(result(done));
        QCOMPARE(e.error, mode == 1 || mode == 7 ? MusicError::RateLimited
                          : mode < 4             ? MusicError::ProviderRejected
                          : mode < 7             ? MusicError::InvalidResponse
                                                 : MusicError::Network);
        if (mode == 1 || mode == 7)
            QCOMPARE(e.retryAfterMs, std::optional<qint64>(3000));
        QCOMPARE(h.http->requests.size(), 2);
    }
    void cancellation_data()
    {
        QTest::addColumn<bool>("detail");
        QTest::addColumn<int>("mode");
        for (bool detail : {false, true})
            for (int mode = 0; mode < 7; ++mode)
                QTest::newRow(qPrintable(QString::number(detail) + "-" + QString::number(mode)))
                    << detail << mode;
    }
    void cancellation()
    {
        QFETCH(bool, detail);
        QFETCH(int, mode);
        Harness h({Plan{"", 200, -1}});
        QSignalSpy done(&h.service, &MusicQueryService::completed);
        const auto original = h.session.snapshot();
        const auto id = h.service.request(original.token, query(original.token, detail));
        QVERIFY(id);
        if (mode != 0 && mode != 2)
            QTRY_COMPARE(h.http->requests.size(), 2);
        if (mode < 2)
            h.service.cancel(id);
        else if (mode < 4)
            h.session.signOut();
        else {
            auto changed = original;
            if (mode == 4)
                changed.accountId = "102";
            else
                changed.phase = mode == 5 ? SessionPhase::Guest : SessionPhase::Committing;
            emit h.session.changed(changed);
        }
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(std::get<MusicFailure>(result(done)).error,
                 mode < 2 ? MusicError::Canceled : MusicError::StaleContext);
        QCOMPARE(qvariant_cast<SessionToken>(done.last()[1]), original.token);
    }
    void clientLoss_data()
    {
        QTest::addColumn<bool>("detail");
        QTest::addColumn<bool>("sent");
        for (bool detail : {false, true})
            for (bool sent : {false, true})
                QTest::newRow(qPrintable(QString::number(detail) + "-" + QString::number(sent)))
                    << detail << sent;
    }
    void clientLoss()
    {
        QFETCH(bool, detail);
        QFETCH(bool, sent);
        Harness h({Plan{"", 200, -1}});
        // Independent client lets its destruction be controlled before the service settles.
        FakeManager* manager = nullptr;
        auto client = std::make_unique<SessionApiClient>(
            Harness::transport({Plan{"", 200, -1}}, manager), "synthetic");
        QVERIFY(client->advanceTo(h.session.snapshot().token));
        MusicGateway gateway(*client);
        QSignalSpy done(&gateway, &MusicGateway::completed);
        const auto token = h.session.snapshot().token;
        // Seed this client's account Cookie, never a real keyring/credential.
        Plan seed{R"({"code":200,"unikey":"synthetic"})"};
        seed.headers.append(std::pair<QByteArray, QByteArray>{
            "Set-Cookie", "MUSIC_U=synthetic-original; Domain=.music.163.com; Path=/"});
        manager->plans.prepend(seed);
        QSignalSpy seeded(client.get(), &SessionApiClient::completed);
        QVERIFY(client->submit(token, ApiOperation::QrKey));
        QTRY_COMPARE(seeded.size(), 1);
        QVERIFY(gateway.request(token, query(token, detail)));
        if (sent)
            QTRY_COMPARE(manager->requests.size(), 2);
        client.reset();
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(std::get<MusicFailure>(result(done)).error, MusicError::Network);
        QCOMPARE(qvariant_cast<SessionToken>(done.last()[1]), token);
    }
    void boundsAndFacade()
    {
        const SessionToken boundedToken{QUuid::createUuid(), 1};
        QJsonArray dateRows, songRows;
        for (int i = 0; i < 366; ++i)
            dateRows.append("2020-06-21");
        for (int i = 0; i < 500; ++i)
            songRows.append(QJsonObject{{"id", 1}, {"name", "synthetic"}});
        const auto maxDates = std::get<DailyHistoryDates>(
            mapMusicResponse(query(boundedToken, false),
                             {{"code", 200}, {"data", QJsonObject{{"dates", dateRows}}}}));
        QCOMPARE(maxDates.dates.size(), 366);
        QVERIFY(DailyHistorySongsQuery::fromDates(boundedToken, maxDates, "2020-06-21"));
        const auto maxSongs = std::get<DailyHistorySongs>(
            mapMusicResponse(query(boundedToken, true),
                             {{"code", 200}, {"data", QJsonObject{{"songs", songRows}}}}));
        QCOMPARE(maxSongs.songs.size(), 500);
        for (bool detail : {false, true}) {
            const auto limit = detail ? 4 * 1024 * 1024 : 1024 * 1024;
            Harness h({Plan{QByteArray(limit + 1, 'x')}});
            QSignalSpy done(&h.service, &MusicQueryService::completed);
            const auto t = h.session.snapshot().token;
            QVERIFY(h.service.request(t, query(t, detail)));
            QTRY_COMPARE(done.size(), 1);
            QVERIFY(std::holds_alternative<MusicFailure>(result(done)));
            QCOMPARE(h.http->requests.size(), 2);
        }
        BackendFacade f;
        QVERIFY(!f.dailyRecommendationHistoryDates());
        ready(f);
        QSignalSpy calls(&f, &BackendFacade::musicRequested),
            done(&f, &BackendFacade::musicCompleted), play(&f, &BackendFacade::playbackRequested),
            report(&f, &BackendFacade::listeningReportRequested),
            write(&f, &BackendFacade::collectionRequested),
            comment(&f, &BackendFacade::commentWriteRequested);
        const auto token = f.snapshot().token;
        auto selected = *DailyHistorySongsQuery::fromDates(token, dates(), "2020-06-21");
        auto old = token;
        ++old.epoch;
        QVERIFY(!f.dailyRecommendationHistorySongs(
            *DailyHistorySongsQuery::fromDates(old, dates(), "2020-06-21")));
        QVERIFY(!f.dailyRecommendationHistorySongs(
            *DailyHistorySongsQuery::fromDates(token, dates("102"), "2020-06-21")));
        for (int i = 0; i < 16; ++i)
            QVERIFY(i % 2 ? f.dailyRecommendationHistoryDates()
                          : f.dailyRecommendationHistorySongs(selected));
        QVERIFY(!f.dailyRecommendationHistoryDates());
        for (const auto& row : calls)
            f.cancelMusic(qvariant_cast<MusicCommand>(row[0]).id);
        QTRY_COMPARE(done.size(), 16);
        QVERIFY(f.dailyRecommendationHistorySongs(selected));
        const auto c = qvariant_cast<MusicCommand>(calls.last()[0]);
        BackendEvent e;
        e.kind = BackendEvent::Kind::Session;
        e.session = f.snapshot();
        e.session.accountId = "102";
        f.deliver(e);
        QTRY_COMPARE(done.size(), 17);
        QCOMPARE(std::get<MusicFailure>(result(done)).error, MusicError::StaleContext);
        f.deliverMusic(
            {c.id, c.generation, c.context, mapMusicResponse(c.query, json(fixture(true)))});
        QCOMPARE(done.size(), 17);
        QVERIFY(!f.dailyRecommendationHistorySongs(selected));
        QVERIFY(f.dailyRecommendationHistoryDates());
        f.close();
        QTRY_COMPARE(done.size(), 18);
        QCOMPARE(std::get<MusicFailure>(result(done)).error, MusicError::Canceled);
        QVERIFY(play.isEmpty());
        QVERIFY(report.isEmpty());
        QVERIFY(write.isEmpty());
        QVERIFY(comment.isEmpty());
        Harness h(QList<Plan>(16, Plan{"", 200, -1}));
        QSignalSpy s(&h.service, &MusicQueryService::completed);
        const auto t = h.session.snapshot().token;
        for (int i = 0; i < 16; ++i)
            QVERIFY(h.service.request(t, query(t, i % 2)));
        QVERIFY(!h.service.request(t, query(t, false)));
        QTRY_COMPARE(h.http->requests.size(), 17);
        h.session.signOut();
        QTRY_COMPARE(s.size(), 16);
        Harness direct(QList<Plan>(16, Plan{"", 200, -1}));
        QSignalSpy g(&direct.gateway, &MusicGateway::completed);
        QList<quint64> ids;
        const auto context = direct.session.snapshot().token;
        for (int i = 0; i < 17; ++i)
            ids.append(direct.gateway.request(context, query(context, i % 2)));
        QTRY_COMPARE(g.size(), 1);
        QCOMPARE(std::get<MusicFailure>(result(g)).error, MusicError::Busy);
        QTRY_COMPARE(direct.http->requests.size(), 17);
        for (auto id : ids)
            direct.gateway.cancel(id);
        QTRY_COMPARE(g.size(), 17);
    }
};
QTEST_GUILESS_MAIN(RecommendationHistoryTest)
#include "recommendation_history_test.moc"
