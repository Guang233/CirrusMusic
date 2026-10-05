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
    return {QUuid::createUuid(),
            {"9007199254740993"},
            {"2"},
            action,
            action == ListeningReportAction::Play ? 35 : 0};
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
        r.playlist = {};
        QVERIFY(!r.isValid());
        r = report();
        r.eventId = {};
        QVERIFY(!r.isValid());
        for (auto op : {ApiOperation::ListeningStart, ApiOperation::ListeningPlay}) {
            const auto c = operationContract(op);
            QVERIFY(c);
            QCOMPARE(c->logicalPath, "/api/feedback/weblog");
            QCOMPARE(c->responseLimit, 64 * 1024);
            QCOMPARE(c->protocol, Protocol::Eapi);
            QVERIFY(requiresAccountCookie(op));
            QCOMPARE(c->destination, OfficialDestination::ListeningLog);
            auto p = QJsonObject{{"id", "9007199254740993"}, {"sourceId", "2"}};
            if (op == ApiOperation::ListeningPlay)
                p.insert("time", 35);
            QVERIFY(operationPayload(op, p));
            p.insert("url", "arbitrary");
            QVERIFY(!operationPayload(op, p));
        }
        QVERIFY(!operationPayload(ApiOperation::ListeningPlay,
                                  {{"id", "1"}, {"sourceId", "2"}, {"time", 1.5}}));
        QVERIFY(!operationPayload(ApiOperation::ListeningPlay,
                                  {{"id", "1"}, {"sourceId", "2"}, {"time", 0}}));
        QVERIFY(!operationPayload(ApiOperation::ListeningStart, {{"id", "1"}, {"sourceId", "0"}}));
        const auto log = officialRequestUrl(Protocol::Eapi, u"/api/feedback/weblog",
                                            OfficialDestination::ListeningLog);
        QVERIFY(log);
        QCOMPARE(log->host(), "clientlog.music.163.com");
        QCOMPARE(log->path(), "/eapi/feedback/weblog");
        QVERIFY(!officialRequestUrl(Protocol::Weapi, u"/api/feedback/weblog",
                                    OfficialDestination::ListeningLog));
        QVERIFY(!officialRequestUrl(Protocol::Eapi, u"/api/song/like/get",
                                    OfficialDestination::ListeningLog));
        QCOMPARE(officialRequestUrl(Protocol::Eapi, u"/api/song/like/get")->host(),
                 "interfacepc.music.163.com");
    }
    void eachActionIsOneRequest()
    {
        Plan response{R"({"code":200})"};
        response.headers.append(std::pair<QByteArray, QByteArray>{
            "Set-Cookie", "MUSIC_U=synthetic-rotation; Domain=.music.163.com; Path=/"});
        Harness h({response, Plan{R"({"code":200})"}});
        QSignalSpy done(&h.gateway, &IListeningReportGateway::completed);
        for (auto action : {ListeningReportAction::StartPlay, ListeningReportAction::Play}) {
            const auto r = report(action);
            QVERIFY(h.submit(r));
            QTRY_COMPARE(done.size(), action == ListeningReportAction::StartPlay ? 1 : 2);
            QCOMPARE(result(done).outcome, ReportOutcome::Acknowledged);
            QCOMPARE(result(done).eventId, r.eventId);
            QCOMPARE(result(done).action, action);
            const auto& request = h.http->requests.last();
            QCOMPARE(request.url().host(), "clientlog.music.163.com");
            QCOMPARE(request.url().path(), "/eapi/feedback/weblog");
            const auto wire = decodeEapiRequest(h.http->bodies.last());
            QVERIFY(wire);
            QCOMPARE(wire->logicalPath, "/api/feedback/weblog");
            const auto header = wire->payload.value("header").toObject();
            QCOMPARE(header.value("os").toString(), "osx");
            QCOMPARE(header.value("osver").toString(), "15.5");
            QCOMPARE(header.value("appver").toString(), "3.1.10.5100");
            QCOMPARE(header.value("MUSIC_U").toString(),
                     "synthetic-original"); // response Cookies ignored
            QVERIFY(request.rawHeader("User-Agent").startsWith("Mozilla/5.0 (Macintosh;"));
            QVERIFY(request.rawHeader("Cookie").contains("os=osx"));
            const auto rows =
                QJsonDocument::fromJson(wire->payload.value("logs").toString().toUtf8()).array();
            QCOMPARE(rows.size(), 1);
            const auto row = rows.first().toObject(), json = row.value("json").toObject();
            QCOMPARE(row.value("action").toString(),
                     action == ListeningReportAction::Play ? "play" : "startplay");
            QCOMPARE(json.value("id").toString(), "9007199254740993");
            QCOMPARE(json.value("content").toString(), "id=2");
            if (action == ListeningReportAction::Play) {
                QCOMPARE(json.value("time").toInt(), 35);
                QCOMPARE(json.value("source").toString(), "list");
                QCOMPARE(json.value("sourceId").toString(), "2");
                QCOMPARE(json.value("end").toString(), "playend");
            } else
                QVERIFY(!json.contains("time"));
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
