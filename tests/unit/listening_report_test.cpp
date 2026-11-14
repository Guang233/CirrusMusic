#include "../support/decode_eapi_request.h"
#include "../support/fake_http.h"
#include "application/backend/backend_facade.h"
#include "infrastructure/netease/music/listening_report_gateway.h"
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
ListeningReport report(ListeningReportAction action = ListeningReportAction::Play)
{
    ListeningReport r{QUuid::createUuid(),
                      {"9007199254740993"},
                      QStringLiteral("2"),
                      action,
                      action == ListeningReportAction::Play ? 35 : 0};
    r.end = ListeningReportEnd::Ui;
    return r;
}
ListeningReportResult result(const QSignalSpy& s)
{
    return qvariant_cast<ListeningReportResult>(s.last().at(2));
}
struct Harness {
    SessionCoordinator session;
    FakeManager* http = nullptr;
    SessionApiClient client;
    ListeningReportGateway gateway{session, client};
    static std::unique_ptr<HttpTransport> transport(QList<Plan> plans, FakeManager*& http)
    {
        auto manager = std::make_unique<FakeManager>();
        http = manager.get();
        http->plans = plans;
        return std::make_unique<HttpTransport>(std::move(manager));
    }
    Harness(QList<Plan> plans, bool auth = true, bool cookie = true)
        : client(transport(plans, http), "synthetic-report")
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
        if (!cookie)
            return;
        Plan seed{R"({"code":200,"unikey":"synthetic"})"};
        seed.headers.append(std::pair<QByteArray, QByteArray>{
            "Set-Cookie", "MUSIC_U=synthetic-original; Domain=.music.163.com; Path=/"});
        seed.headers.append(std::pair<QByteArray, QByteArray>{
            "Set-Cookie", "__csrf=synthetic-csrf; Domain=.music.163.com; Path=/"});
        http->plans.prepend(seed);
        QSignalSpy done(&client, &SessionApiClient::completed);
        QVERIFY(client.submit(session.snapshot().token, ApiOperation::QrKey));
        QTRY_COMPARE(done.size(), 1);
    }
    quint64 submit(ListeningReport r = report(), QString account = "101")
    {
        return gateway.submit(session.snapshot().token, account, r);
    }
};
BackendEvent sessionEvent(SessionToken token, SessionPhase phase = SessionPhase::Authenticated)
{
    BackendEvent e;
    e.kind = BackendEvent::Kind::Session;
    e.session = {token, phase, phase == SessionPhase::Authenticated ? QString("101") : QString{}};
    return e;
}
void ready(BackendFacade& f, SessionPhase phase = SessionPhase::Authenticated)
{
    f.start();
    BackendEvent e;
    e.kind = BackendEvent::Kind::Started;
    f.deliver(e);
    f.deliver(sessionEvent({QUuid::createUuid(), 1}, phase));
}
} // namespace
class ListeningReportTest : public QObject
{
    Q_OBJECT
  private slots:
    void contractsAndValidation()
    {
        auto r = report();
        QVERIFY(r.isValid());
        r.playedSeconds = 604800;
        QVERIFY(r.isValid());
        r.playedSeconds++;
        QVERIFY(!r.isValid());
        r = report(ListeningReportAction::StartPlay);
        QVERIFY(r.isValid());
        r.playedSeconds = 1;
        QVERIFY(!r.isValid());
        r = report();
        r.sourceId = {};
        QVERIFY(!r.isValid());
        r.source = ListeningReportSource::DailyRecommend; // the web leaves it out
        QVERIFY(r.isValid());
        r.source = ListeningReportSource::Search;
        QVERIFY(!r.isValid());
        r.sourceId = QStringLiteral("陈奕迅 富士山下");
        QVERIFY(r.isValid());
        r.sourceId = QStringLiteral("a\nb");
        QVERIFY(!r.isValid());
        r.sourceId = QString(257, QLatin1Char('a'));
        QVERIFY(!r.isValid());
        r = report();
        r.source = ListeningReportSource::Album;
        r.sourceId = QStringLiteral("not-an-id");
        QVERIFY(!r.isValid());
        r = report();
        r.eventId = {};
        QVERIFY(!r.isValid());
        ListeningReport active; // about no song
        active.eventId = QUuid::createUuid();
        active.action = ListeningReportAction::ActiveClient;
        QVERIFY(active.isValid());
        active.playedSeconds = 1;
        QVERIFY(!active.isValid());
        for (auto op : {ApiOperation::ListeningStart, ApiOperation::ListeningPlay,
                        ApiOperation::ListeningActive}) {
            const auto c = operationContract(op);
            QVERIFY(c);
            QCOMPARE(c->logicalPath, "/api/feedback/weblog");
            QCOMPARE(c->responseLimit, 64 * 1024);
            QCOMPARE(c->protocol, Protocol::Weapi);
            QVERIFY(requiresAccountCookie(op));
            QCOMPARE(c->destination, OfficialDestination::ListeningLog);
            if (op == ApiOperation::ListeningActive) {
                QVERIFY(operationPayload(op, {}));
                QVERIFY(!operationPayload(op, {{"id", "1"}}));
                continue;
            }
            auto p = QJsonObject{{"id", "9007199254740993"}, {"source", "list"}, {"sourceId", "2"}};
            if (op == ApiOperation::ListeningPlay) {
                p.insert("time", 35);
                p.insert("end", "ui");
            }
            QVERIFY(operationPayload(op, p));
            p.insert("url", "arbitrary");
            QVERIFY(!operationPayload(op, p));
        }
        const QJsonObject play{
            {"id", "1"}, {"source", "list"}, {"sourceId", "2"}, {"time", 35}, {"end", "playend"}};
        const auto with = [&](const char* key, QJsonValue value) {
            auto p = play;
            p.insert(key, value);
            return p;
        };
        QVERIFY(operationPayload(ApiOperation::ListeningPlay, play));
        QVERIFY(!operationPayload(ApiOperation::ListeningPlay, with("time", 1.5)));
        QVERIFY(!operationPayload(ApiOperation::ListeningPlay, with("time", 0)));
        QVERIFY(!operationPayload(ApiOperation::ListeningPlay, with("end", "skip")));
        QVERIFY(!operationPayload(ApiOperation::ListeningPlay, with("source", "fm")));
        QVERIFY(!operationPayload(ApiOperation::ListeningPlay, with("sourceId", "keywords")));
        auto searched = with("source", "search");
        searched.insert("sourceId", "富士山下");
        const auto search = operationPayload(ApiOperation::ListeningPlay, searched);
        QVERIFY(search);
        const auto logs =
            QJsonDocument::fromJson(search->value("logs").toString().toUtf8()).array();
        const auto json = logs.first().toObject().value("json").toObject();
        // The web player's logs (explicit-listening-reports §4c): no source, sourceId or end.
        QCOMPARE(logs.size(), 1);
        QCOMPARE(logs.first().toObject().value("action").toString(), "play");
        QCOMPARE(json, (QJsonObject{{"mainSite", "2"},
                                    {"mainSiteWeb", "2"},
                                    {"id", "1"},
                                    {"type", "song"},
                                    {"content", ""},
                                    {"time", 35},
                                    {"wifi", 0},
                                    {"download", 0}}));
        const auto logOf = [](ApiOperation op, const QJsonObject& p) {
            return QJsonDocument::fromJson(
                       operationPayload(op, p)->value("logs").toString().toUtf8())
                .array()
                .first()
                .toObject();
        };
        const auto start = logOf(ApiOperation::ListeningStart,
                                 {{"id", "1"}, {"source", "list"}, {"sourceId", "2"}});
        QCOMPARE(start.value("action").toString(), "startPlay");
        QCOMPARE(start.value("json").toObject(), (QJsonObject{{"mainSite", "2"},
                                                              {"mainSiteWeb", "2"},
                                                              {"id", "1"},
                                                              {"type", "song"},
                                                              {"content", ""}}));
        const auto client = logOf(ApiOperation::ListeningActive, {});
        QCOMPARE(client.value("action").toString(), "activeclient");
        QCOMPARE(client.value("json").toObject(),
                 (QJsonObject{{"mainSite", "2"}, {"mainSiteWeb", "2"}}));
        QVERIFY(!operationPayload(ApiOperation::ListeningStart,
                                  {{"id", "1"}, {"source", "list"}, {"sourceId", "0"}}));
        const auto log = officialRequestUrl(Protocol::Weapi, u"/api/feedback/weblog",
                                            OfficialDestination::ListeningLog);
        QVERIFY(log);
        QCOMPARE(log->host(), "clientlogusf.music.163.com");
        QCOMPARE(log->path(), "/weapi/feedback/weblog");
        QVERIFY(!officialRequestUrl(Protocol::Eapi, u"/api/feedback/weblog",
                                    OfficialDestination::ListeningLog));
        QVERIFY(!officialRequestUrl(Protocol::Weapi, u"/api/song/like/get",
                                    OfficialDestination::ListeningLog));
        QCOMPARE(officialRequestUrl(Protocol::Eapi, u"/api/song/like/get")->host(),
                 "interfacepc.music.163.com");
    }
    void eachActionIsOneRequest()
    {
        Plan response{R"({"code":200})"};
        response.headers.append(std::pair<QByteArray, QByteArray>{
            "Set-Cookie", "MUSIC_U=synthetic-rotation; Domain=.music.163.com; Path=/"});
        Harness h({response, Plan{R"({"code":200})"}, Plan{R"({"code":200})"}});
        QSignalSpy done(&h.gateway, &IListeningReportGateway::completed);
        int sent = 0;
        for (auto action : {ListeningReportAction::StartPlay, ListeningReportAction::Play,
                            ListeningReportAction::ActiveClient}) {
            auto r = report(action);
            if (action == ListeningReportAction::ActiveClient)
                r = ListeningReport{QUuid::createUuid(), {}, {}, action, 0};
            QVERIFY(h.submit(r));
            ++sent;
            QTRY_COMPARE(done.size(), sent);
            QCOMPARE(result(done).outcome, ReportOutcome::Acknowledged);
            QCOMPARE(result(done).eventId, r.eventId);
            QCOMPARE(result(done).action, action);
            // As the web player sends its logs (explicit-listening-reports §4c); the weapi
            // body itself can't be read back here (operationPayload is checked above).
            const auto& request = h.http->requests.last();
            QCOMPARE(request.url().host(), "clientlogusf.music.163.com");
            QCOMPARE(request.url().path(), "/weapi/feedback/weblog");
            QCOMPARE(request.url().query(), "csrf_token=synthetic-csrf");
            QCOMPARE(request.rawHeader("x-music-web-os"), "web3");
            QCOMPARE(request.rawHeader("Origin"), "https://music.163.com");
            QCOMPARE(request.rawHeader("Referer"), "https://music.163.com");
            QVERIFY(request.rawHeader("User-Agent").startsWith("Mozilla/5.0 (Macintosh;"));
            const auto cookie = request.rawHeader("Cookie");
            QVERIFY(cookie.contains("MUSIC_U=synthetic-original")); // response Cookies ignored
            QVERIFY(!cookie.contains("os="));                       // no client identity: a browser
            QVERIFY(h.http->bodies.last().startsWith("params="));
            QVERIFY(h.http->bodies.last().contains("&encSecKey="));
            QCOMPARE(h.http->requests.size(), done.size() + 1); // no automatic second action
        }
    }
    void responses_data()
    {
        QTest::addColumn<QByteArray>("body");
        QTest::addColumn<int>("status");
        QTest::addColumn<ReportOutcome>("outcome");
        QTest::addColumn<ReportError>("error");
        QTest::newRow("accepted") << QByteArray(R"({"code":200})") << 200
                                  << ReportOutcome::Acknowledged << ReportError::None;
        QTest::newRow("rejected") << QByteArray(R"({"code":500})") << 200 << ReportOutcome::Rejected
                                  << ReportError::ProviderRejected;
        QTest::newRow("missing-code")
            << QByteArray("{}") << 200 << ReportOutcome::Unknown << ReportError::InvalidResponse;
        QTest::newRow("malformed") << QByteArray("broken") << 200 << ReportOutcome::Unknown
                                   << ReportError::InvalidResponse;
        QTest::newRow("http-rate-limit")
            << QByteArray("{}") << 429 << ReportOutcome::Unknown << ReportError::RateLimited;
        QTest::newRow("business-rate-limit") << QByteArray(R"({"code":429})") << 200
                                             << ReportOutcome::Rejected << ReportError::RateLimited;
        QTest::newRow("http-failure")
            << QByteArray("{}") << 502 << ReportOutcome::Unknown << ReportError::Network;
    }
    void responses()
    {
        QFETCH(QByteArray, body);
        QFETCH(int, status);
        QFETCH(ReportOutcome, outcome);
        QFETCH(ReportError, error);
        Harness h({Plan{body, status}});
        QSignalSpy done(&h.gateway, &IListeningReportGateway::completed);
        QVERIFY(h.submit());
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(result(done).outcome, outcome);
        QCOMPARE(result(done).error, error);
        if (error == ReportError::RateLimited)
            QCOMPARE(result(done).retryAfterMs, std::optional<qint64>(3000));
        QTest::qWait(10);
        QCOMPARE(h.http->requests.size(), 2);
        QCOMPARE(done.size(), 1); // no retry
    }
    void authAndAccountBeforeNetwork()
    {
        Harness guest({}, false);
        QSignalSpy g(&guest.gateway, &IListeningReportGateway::completed);
        QVERIFY(guest.submit());
        QTRY_COMPARE(g.size(), 1);
        QCOMPARE(result(g).outcome, ReportOutcome::NotSent);
        QCOMPARE(result(g).error, ReportError::AuthenticationRequired);
        QVERIFY(guest.http->requests.isEmpty());
        Harness h({});
        QSignalSpy done(&h.gateway, &IListeningReportGateway::completed);
        QVERIFY(h.submit(report(), "202"));
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(result(done).outcome, ReportOutcome::NotSent);
        QCOMPARE(result(done).error, ReportError::InvalidRequest);
        QCOMPARE(h.http->requests.size(), 1);
        Harness missing({}, true, false);
        missing.client.setAnonymousToken("synthetic-anonymous");
        QSignalSpy absent(&missing.gateway, &IListeningReportGateway::completed);
        QVERIFY(missing.submit());
        QTRY_COMPARE(absent.size(), 1);
        QCOMPARE(result(absent).outcome, ReportOutcome::NotSent);
        QVERIFY(missing.http->requests.isEmpty());
    }
    void cancellationAndSessionChange()
    {
        Harness h({Plan{"", 200, -1}, Plan{"", 200, -1}, Plan{"", 200, -1}});
        QSignalSpy done(&h.gateway, &IListeningReportGateway::completed);
        const auto id = h.submit();
        QVERIFY(id);
        QTRY_COMPARE(h.http->requests.size(), 2);
        h.gateway.cancel(id);
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(result(done).outcome, ReportOutcome::Unknown);
        QCOMPARE(result(done).error, ReportError::Canceled);
        const auto context = h.session.snapshot().token;
        QVERIFY(h.submit());
        QTRY_COMPARE(h.http->requests.size(), 3);
        QVERIFY(h.submit());
        QTRY_COMPARE(h.http->requests.size(), 4);
        h.session.signOut();
        QTRY_COMPARE(done.size(), 3);
        QCOMPARE(result(done).outcome, ReportOutcome::Unknown);
        QCOMPARE(result(done).error, ReportError::StaleContext);
        QCOMPARE(qvariant_cast<SessionToken>(done.last().at(1)), context);
    }
    void facadeCancellationRemainsUnknown()
    {
        BackendFacade f;
        ready(f);
        QSignalSpy done(&f, &BackendFacade::listeningReportCompleted);
        const auto id = f.reportListening(report());
        QVERIFY(id);
        f.cancelListeningReport(id);
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(result(done).outcome, ReportOutcome::Unknown);
        QCOMPARE(result(done).error, ReportError::Canceled);
        const auto next = f.reportListening(report());
        QVERIFY(next);
        f.deliver(sessionEvent(f.snapshot().token, SessionPhase::Guest));
        QTRY_COMPARE(done.size(), 2);
        QCOMPARE(result(done).outcome, ReportOutcome::Unknown);
        QCOMPARE(result(done).error, ReportError::StaleContext);
    }
    void gatewayBoundAndLostResponse()
    {
        QList<Plan> plans;
        for (int i = 0; i < 8; ++i)
            plans.append(Plan{"", 200, -1});
        Harness h(plans);
        QSignalSpy done(&h.gateway, &IListeningReportGateway::completed);
        for (int i = 0; i < 8; ++i)
            QVERIFY(h.submit());
        QVERIFY(!h.submit());
        QTRY_COMPARE(h.http->requests.size(), 9);
        ApiResult timeout;
        timeout.error = ApiError::Transport;
        timeout.transportError = TransportError::Timeout;
        timeout.submittedToTransport = true;
        emit h.client.completed(2, h.session.snapshot().token,
                                timeout); // synthetic lost response after dispatch
        QCOMPARE(done.size(), 1);
        QCOMPARE(result(done).outcome, ReportOutcome::Unknown);
        QCOMPARE(result(done).error, ReportError::Timeout);
        QTest::qWait(10);
        QCOMPARE(h.http->requests.size(), 9); // never retry unknown write
    }
    void cancellationBeforeClientStartIsNotSent()
    {
        Harness h({});
        QSignalSpy done(&h.gateway, &IListeningReportGateway::completed);
        const auto id = h.submit();
        QVERIFY(id);
        h.gateway.cancel(id);
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(result(done).outcome, ReportOutcome::NotSent);
        QCOMPARE(h.http->requests.size(), 1);
    }
    void facadeBoundsAndOriginalContext()
    {
        BackendFacade f;
        ready(f);
        QSignalSpy commands(&f, &BackendFacade::listeningReportRequested);
        QSignalSpy done(&f, &BackendFacade::listeningReportCompleted);
        const auto r = report();
        const auto id = f.reportListening(r);
        QVERIFY(id);
        QVERIFY(!f.reportListening(r)); // same in-flight event/action locally rejected
        const auto c = qvariant_cast<ListeningReportCommand>(commands.first().first());
        QCOMPARE(c.accountId, "101");
        QCOMPARE(c.context, f.snapshot().token);
        f.deliverListeningReport(
            {id, c.generation, c.context, {r.eventId, r.action, ReportOutcome::Acknowledged}});
        QCOMPARE(done.size(), 1);
        QCOMPARE(result(done).outcome, ReportOutcome::Acknowledged);
        for (int i = 0; i < 8; ++i)
            QVERIFY(f.reportListening(report()));
        QVERIFY(!f.reportListening(report()));
        QVERIFY(f.signOut());
        QTRY_COMPARE(done.size(), 9);
        for (int i = 1; i < done.size(); ++i) {
            QCOMPARE(qvariant_cast<ListeningReportResult>(done[i][2]).outcome,
                     ReportOutcome::Unknown);
            QCOMPARE(qvariant_cast<SessionToken>(done[i][1]), c.context);
        }
        f.deliverListeningReport(
            {id, c.generation, c.context, {r.eventId, r.action, ReportOutcome::Acknowledged}});
        QCOMPARE(done.size(), 9);
    }
    void guestFacadeAndPlaybackNeverReport()
    {
        BackendFacade f;
        ready(f, SessionPhase::Guest);
        QSignalSpy commands(&f, &BackendFacade::listeningReportRequested);
        QVERIFY(!f.reportListening(report()));
        QVERIFY(f.playSong({"1"}));
        QVERIFY(f.stopPlayback());
        f.close();
        QCOMPARE(commands.size(), 0);
    }
};
QTEST_GUILESS_MAIN(ListeningReportTest)
#include "listening_report_test.moc"
