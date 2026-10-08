#include "../support/decode_eapi_request.h"
#include "../support/fake_http.h"
#include "../support/synthetic_together_http.h"
#include "application/backend/backend_facade.h"
#include "infrastructure/netease/api/together_operation.h"
#include "infrastructure/netease/music/together_gateway.h"
#include <QSignalSpy>
#include <QtTest>
using namespace cirrus::domain;
using namespace cirrus::application;
using namespace cirrus::netease;
using namespace cirrus::test;
namespace
{
TogetherRequest request(int i = 0) { return {QUuid::createUuid(), 7, togetherHttpActions()[i]}; }
TogetherResult result(const QSignalSpy& s) { return qvariant_cast<TogetherResult>(s.last()[2]); }
struct Harness {
    SessionCoordinator session;
    FakeManager* http = nullptr;
    std::unique_ptr<SessionApiClient> client;
    std::unique_ptr<TogetherGateway> gateway;
    Harness(QList<Plan> plans, bool auth = true, bool cookie = true)
    {
        auto m = std::make_unique<FakeManager>();
        http = m.get();
        http->plans = plans;
        client = std::make_unique<SessionApiClient>(std::make_unique<HttpTransport>(std::move(m)),
                                                    "synthetic-together");
        QVERIFY(client->advanceTo(session.snapshot().token));
        QObject::connect(&session, &SessionCoordinator::changed, client.get(),
                         [this](SessionSnapshot s) { QVERIFY(client->advanceTo(s.token)); });
        if (auth) {
            const auto attempt = session.beginAuthentication();
            const auto prepared = session.prepareAuthentication(attempt, "101");
            QVERIFY(prepared);
            QVERIFY(session.completeAuthentication(*prepared));
            if (cookie) {
                Plan seed{R"({"code":200,"unikey":"synthetic"})"};
                seed.headers.append(std::pair<QByteArray, QByteArray>{
                    "Set-Cookie", "MUSIC_U=synthetic-original; Domain=.music.163.com; Path=/"});
                http->plans.prepend(seed);
                QSignalSpy done(client.get(), &SessionApiClient::completed);
                QVERIFY(client->submit(session.snapshot().token, ApiOperation::QrKey));
                QTRY_COMPARE(done.size(), 1);
            }
        }
        gateway = std::make_unique<TogetherGateway>(session, *client);
    }
    quint64 submit(TogetherRequest r = request(), QString account = "101")
    {
        return gateway->submit(session.snapshot().token, account, r);
    }
};
ApiResult decoded(QJsonObject o)
{
    ApiResult a;
    a.submittedToTransport = true;
    a.decoded =
        decodeResponse(QJsonDocument(o).toJson(QJsonDocument::Compact), ResponseEncoding::Json);
    return a;
}
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
class TogetherHttpTest : public QObject
{
    Q_OBJECT
  private slots:
    void contracts_data()
    {
        QTest::addColumn<int>("index");
        for (int i = 0; i < 12; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void contracts()
    {
        QFETCH(int, index);
        const auto r = request(index);
        QVERIFY(r.isValid());
        const auto wire = togetherRequest(r, u"101");
        QVERIFY(wire);
        QVERIFY(isTogetherOperation(wire->operation));
        QVERIFY(!isTogetherOperation(ApiOperation::QrKey));
        const auto c = operationContract(wire->operation);
        QVERIFY(c);
        const QStringList paths{"room/create",
                                "room/check",
                                "play/invitation/accept",
                                "status/get",
                                "end/check",
                                "end/v2",
                                "heartbeat",
                                "play/command/report",
                                "sync/playlist/get",
                                "sync/list/command/report",
                                "restore/reconnect/info",
                                "device/reconnect/notice"};
        QCOMPARE(c->logicalPath, QString("/api/listen/together/") + paths[index]);
        QCOMPARE(c->protocol, Protocol::Eapi);
        QCOMPARE(c->destination, OfficialDestination::TogetherHttp);
        QVERIFY(requiresAccountCookie(wire->operation));
        const auto payload = operationPayload(wire->operation, wire->parameters);
        QVERIFY(payload);
        const auto url = officialRequestUrl(c->protocol, c->logicalPath, c->destination);
        QVERIFY(url);
        QCOMPARE(url->host(), "interface.music.163.com");
        QVERIFY(url->path().startsWith("/eapi/listen/together/"));
        auto bad = wire->parameters;
        bad.insert("crypto", "weapi");
        QVERIFY(!operationPayload(wire->operation, bad));
        QVERIFY(!officialRequestUrl(Protocol::Weapi, c->logicalPath, c->destination));
        QVERIFY(!acceptsResponseCookies(wire->operation,
                                        decoded(togetherHttpObject(TogetherKind(index))).decoded));
        QVERIFY(!officialRequestUrl(Protocol::Eapi, u"/api/listen/together/yunxin/token/get",
                                    c->destination));
        QCOMPARE(officialRequestUrl(Protocol::Eapi, u"/api/song/like/get")->host(),
                 "interfacepc.music.163.com");
        if (index == 9) {
            QVERIFY(payload->value("command").isString());
            QVERIFY(!payload->contains("roomId"));
            QVERIFY(!payload->contains("playlistParam"));
            const auto p =
                QJsonDocument::fromJson(payload->value("command").toString().toUtf8()).object();
            QCOMPARE(p.value("clientSeq").toInt(), 6);
            QCOMPARE(p.value("version").toArray().first().toObject().value("version").toInt(), 9);
        }
    }
    void nativeEach()
    {
        QList<Plan> plans;
        for (int i = 0; i < 12; ++i) {
            Plan p{togetherHttpFixture(TogetherKind(i))};
            p.headers.append(std::pair<QByteArray, QByteArray>{
                "Set-Cookie", "MUSIC_U=synthetic-rotation; Domain=.music.163.com; Path=/"});
            plans.append(p);
        }
        Harness h(plans);
        QSignalSpy done(h.gateway.get(), &ITogetherGateway::completed);
        for (int i = 0; i < 12; ++i) {
            const auto r = request(i);
            QVERIFY(h.submit(r));
            QTRY_COMPARE(done.size(), i + 1);
            const auto got = result(done);
            QCOMPARE(got.outcome, TogetherOutcome::Acknowledged);
            QCOMPARE(got.kind, TogetherKind(i));
            QCOMPARE(got.eventId, r.eventId);
            QCOMPARE(got.roomGeneration, r.roomGeneration);
            QCOMPARE(h.http->requests.size(), i + 2);
            const auto inspected = decodeEapiRequest(h.http->bodies.last());
            QVERIFY(inspected);
            const auto expected = togetherRequest(r, u"101");
            const auto payload = operationPayload(expected->operation, expected->parameters);
            QVERIFY(payload);
            auto actual = inspected->payload;
            const auto header = actual.take("header").toObject();
            QCOMPARE(header.value("MUSIC_U").toString(), "synthetic-original");
            QCOMPARE(header.value("os").toString(), "pc");
            QCOMPARE(actual.take("e_r"), QJsonValue(false));
            QCOMPARE(actual, *payload);
            QCOMPARE(inspected->logicalPath, operationContract(expected->operation)->logicalPath);
            QCOMPARE(h.http->requests.last().url().host(), "interface.music.163.com");
            QVERIFY(!h.http->requests.last().rawHeader("Cookie").contains("synthetic-rotation"));
            if (i == 0) {
                QVERIFY(got.room);
                QCOMPARE(got.room->id.value, "synthetic-room");
                QVERIFY(got.room->chatRoom);
                QCOMPARE(got.room->chatRoom->value, "synthetic-chat");
            }
            if (i == 1)
                QCOMPARE(got.joinable, std::optional<bool>(false));
            if (i == 8) {
                QVERIFY(got.playlistContentUnmapped);
                QVERIFY(got.reportedPlay);
                QCOMPARE(got.reportedPlay->serverSeq, 88);
            }
            if (i == 10) {
                QVERIFY(got.reconnect);
                QVERIFY(got.reconnect->canReconnect);
                QCOMPARE(got.reconnect->requiresDeviceConfirmation, std::optional<bool>(true));
            }
        }
    }
    void malformed_data()
    {
        QTest::addColumn<int>("index");
        QTest::addColumn<QJsonObject>("body");
        int row = 0;
        for (int i = 0; i < 12; ++i) {
            for (const auto& value :
                 QList<QJsonValue>{QJsonValue(), QJsonValue(1), QJsonValue(QJsonArray{})}) {
                auto o = togetherHttpObject(TogetherKind(i));
                o.insert("data", value);
                QTest::newRow(qPrintable(QString::number(row++))) << i << o;
            }
        }
        auto add = [&](int i, QJsonObject data) {
            QTest::newRow(qPrintable(QString::number(row++)))
                << i << QJsonObject{{"code", 200}, {"data", data}};
        };
        add(0, {{"roomInfo", QJsonObject{{"roomId", 1}}}});
        add(0, {{"roomInfo", QJsonObject{{"roomId", "synthetic-room"}, {"chatRoomId", 1}}}});
        add(2, {{"roomInfo", QJsonObject{{"roomId", "other-room"}}}});
        add(1, {{"roomInfo", QJsonObject{{"roomId", "synthetic-room"}}}});
        add(1, {{"joinable", "true"}});
        add(4, {{"result", 1}});
        add(6, {{"result", "true"}});
        add(7, {});
        add(9, {});
        add(3, {{"inRoom", 1}});
        add(3, {{"inRoom", false}, {"roomInfo", QJsonObject{}}});
        add(5, {{"success", true}});
        add(5, {{"code", "200"}});
        add(8, {{"playlist", QJsonObject{}}});
        add(8, {{"playList", QJsonObject{{"playlist", QJsonArray{}}}}});
        auto queue = togetherHttpObject(TogetherKind::Queue).value("data").toObject();
        auto wrapper = queue.value("playList").toObject();
        auto play = wrapper.value("playCommand").toObject();
        play.insert("progress", 0.5);
        wrapper.insert("playCommand", play);
        queue.insert("playList", wrapper);
        add(8, queue);
        add(10, {{"canReconnect", true}});
        add(10, {{"canReconnect", "true"}});
        add(11, {{"result", 1}});
    }
    void malformed()
    {
        QFETCH(int, index);
        QFETCH(QJsonObject, body);
        const auto r = togetherResult(request(index), decoded(body));
        QCOMPARE(r.outcome, TogetherOutcome::Unknown);
        QCOMPARE(r.error, TogetherError::InvalidResponse);
        QVERIFY(!r.room);
        QVERIFY(!r.reconnect);
        QVERIFY(!r.reportedPlay);
        QVERIFY(!r.playlistContentUnmapped);
    }
    void negativeWireAndBounds()
    {
        auto r = request();
        r.eventId = {};
        QVERIFY(!r.isValid());
        r = request();
        r.roomGeneration = 0;
        QVERIFY(!r.isValid());
        QVERIFY(!TogetherRoomId{QStringLiteral("bad/room")}.isValid());
        QVERIFY(!TogetherRoomId{QString(129, u'a')}.isValid());
        auto p = std::get<TogetherPlay>(request(7).action);
        for (const auto& bad : QStringList{"", "0", "01", "9223372036854775808"}) {
            auto q = p;
            q.target.value = bad;
            QVERIFY(!validTogetherAction(q));
        }
        p.clientSeq = 9007199254740992LL;
        QVERIFY(!validTogetherAction(p));
        auto q = std::get<TogetherReplaceQueue>(request(9).action);
        q.random.removeLast();
        QVERIFY(!validTogetherAction(q));
        q = std::get<TogetherReplaceQueue>(request(9).action);
        q.random[0] = {"5"};
        QVERIFY(!validTogetherAction(q));
        for (int i = 0; i < 6; ++i) {
            auto r = request(7);
            std::get<TogetherPlay>(r.action).action = TogetherPlayAction(i);
            const auto w = togetherRequest(r, u"101");
            QVERIFY(w);
            QVERIFY(operationPayload(w->operation, w->parameters));
        }
        auto w = togetherRequest(request(7), u"101");
        auto c = QJsonDocument::fromJson(w->parameters.value("commandInfo").toString().toUtf8())
                     .object();
        c.insert("commandType", "seek");
        w->parameters.insert("commandInfo", QString::fromUtf8(QJsonDocument(c).toJson()));
        QVERIFY(!operationPayload(w->operation, w->parameters));
        w = togetherRequest(request(9), u"101");
        w->parameters.insert("playlistParam", w->parameters.take("command"));
        QVERIFY(!operationPayload(w->operation, w->parameters));
    }
    void rejection_data()
    {
        QTest::addColumn<int>("code");
        for (int code : {400, 488, 500, 429, -462})
            QTest::newRow(qPrintable(QString::number(code))) << code;
    }
    void rejection()
    {
        QFETCH(int, code);
        Harness h({Plan{QJsonDocument(QJsonObject{{"code", code}}).toJson()}});
        QSignalSpy done(h.gateway.get(), &ITogetherGateway::completed);
        QVERIFY(h.submit());
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(result(done).outcome, TogetherOutcome::Rejected);
        QCOMPARE(result(done).providerCode, std::optional<qint64>(code));
        QCOMPARE(h.http->requests.size(), 2); // no retry or host/protocol fallback
    }
    void cancellation_data()
    {
        QTest::addColumn<int>("index");
        QTest::addColumn<bool>("dispatched");
        for (int i = 0; i < 12; ++i)
            for (bool sent : {false, true})
                QTest::newRow(qPrintable(QString::number(i) + "-" + QString::number(sent)))
                    << i << sent;
    }
    void cancellation()
    {
        QFETCH(int, index);
        QFETCH(bool, dispatched);
        Harness h({Plan{"", 200, -1}});
        QSignalSpy done(h.gateway.get(), &ITogetherGateway::completed);
        const auto id = h.submit(request(index));
        QVERIFY(id);
        if (dispatched)
            QTRY_COMPARE(h.http->requests.size(), 2);
        h.gateway->cancel(id);
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(result(done).error, TogetherError::Canceled);
        QCOMPARE(result(done).outcome,
                 dispatched ? TogetherOutcome::Unknown : TogetherOutcome::NotSent);
    }
    void guardsAndLoss()
    {
        for (bool auth : {false, true}) {
            Harness h({}, auth, false);
            QSignalSpy done(h.gateway.get(), &ITogetherGateway::completed);
            QVERIFY(h.submit());
            QTRY_COMPARE(done.size(), 1);
            QCOMPARE(result(done).outcome, TogetherOutcome::NotSent);
            QCOMPARE(result(done).error,
                     auth ? TogetherError::ProtocolFailure : TogetherError::AuthenticationRequired);
            QVERIFY(h.http->requests.isEmpty());
        }
        Harness h({Plan{"", 200, -1}});
        QSignalSpy done(h.gateway.get(), &ITogetherGateway::completed);
        QVERIFY(h.submit(request(), "102"));
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(result(done).error, TogetherError::InvalidRequest);
        const auto duplicate = request();
        const auto id = h.submit(duplicate);
        QVERIFY(id);
        QVERIFY(!h.submit(duplicate));
        QTRY_COMPARE(h.http->requests.size(), 2);
        auto announced = h.session.snapshot();
        announced.accountId = "102";
        h.session.changed(announced);
        QTRY_COMPARE(done.size(), 2);
        QCOMPARE(result(done).error, TogetherError::StaleContext);
        QVERIFY(!result(done).room);
        Harness loss({Plan{"", 200, -1}});
        QSignalSpy lost(loss.gateway.get(), &ITogetherGateway::completed);
        QVERIFY(loss.submit());
        QTRY_COMPARE(loss.http->requests.size(), 2);
        loss.client.reset();
        QTRY_COMPARE(lost.size(), 1);
        QCOMPARE(result(lost).error, TogetherError::Network);
        QCOMPARE(result(lost).outcome, TogetherOutcome::Unknown);
    }
    void scopeChanges_data()
    {
        QTest::addColumn<int>("change");
        QTest::addColumn<bool>("sent");
        for (int change = 0; change < 3; ++change)
            for (bool sent : {false, true})
                QTest::newRow(qPrintable(QString::number(change) + "-" + QString::number(sent)))
                    << change << sent;
    }
    void scopeChanges()
    {
        QFETCH(int, change);
        QFETCH(bool, sent);
        Harness h({Plan{"", 200, -1}});
        QSignalSpy done(h.gateway.get(), &ITogetherGateway::completed);
        QVERIFY(h.submit());
        if (sent)
            QTRY_COMPARE(h.http->requests.size(), 2);
        auto s = h.session.snapshot();
        if (change == 0)
            s.accountId = "102";
        else if (change == 1)
            s.phase = SessionPhase::Guest;
        else
            ++s.token.epoch;
        h.session.changed(s);
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(result(done).error, TogetherError::StaleContext);
        QCOMPARE(result(done).outcome, sent ? TogetherOutcome::Unknown : TogetherOutcome::NotSent);
        QVERIFY(!result(done).room);
        QVERIFY(!result(done).reconnect);
    }
    void slotsClientLossAndResultBoundaries()
    {
        Harness h({Plan{"", 200, -1}});
        QSignalSpy done(h.gateway.get(), &ITogetherGateway::completed);
        QList<quint64> ids;
        for (int i = 0; i < 8; ++i) {
            ids.append(h.submit());
            QVERIFY(ids.last());
        }
        QVERIFY(!h.submit());
        for (auto id : ids)
            h.gateway->cancel(id);
        QTRY_COMPARE(done.size(), 8);
        QCOMPARE(h.http->requests.size(), 1);
        Harness loss({});
        QSignalSpy lost(loss.gateway.get(), &ITogetherGateway::completed);
        QVERIFY(loss.submit());
        loss.client.reset();
        QTRY_COMPARE(lost.size(), 1);
        QCOMPARE(result(lost).outcome, TogetherOutcome::NotSent);
        QCOMPARE(result(lost).error, TogetherError::Network);
        auto o = togetherHttpObject(TogetherKind::Play);
        o.insert("data", QJsonObject{{"result", false}});
        auto r = togetherResult(request(7), decoded(o));
        QCOMPARE(r.outcome, TogetherOutcome::Rejected);
        QCOMPARE(r.error, TogetherError::ProviderRejected);
        o = {{"code", 200}, {"data", QJsonObject{{"inRoom", false}}}};
        r = togetherResult(request(3), decoded(o));
        QCOMPARE(r.outcome, TogetherOutcome::Acknowledged);
        QCOMPARE(r.inRoom, std::optional<bool>(false));
        QVERIFY(!r.room);
        ApiResult timeout;
        timeout.error = ApiError::Transport;
        timeout.transportError = TransportError::Timeout;
        timeout.submittedToTransport = true;
        r = togetherResult(request(), timeout);
        QCOMPARE(r.outcome, TogetherOutcome::Unknown);
        QCOMPARE(r.error, TogetherError::Timeout);
        timeout.httpStatus = 429;
        timeout.retryAfter = "3";
        r = togetherResult(request(), timeout);
        QCOMPARE(r.error, TogetherError::RateLimited);
        QCOMPARE(r.retryAfterMs, std::optional<qint64>(3000));
        timeout.retryAfter = "3, 3";
        r = togetherResult(request(), timeout);
        QVERIFY(!r.retryAfterMs);
        for (auto pair : {std::pair{ApiError::InvalidRequest, TogetherError::InvalidRequest},
                          std::pair{ApiError::Crypto, TogetherError::ProtocolFailure},
                          std::pair{ApiError::Busy, TogetherError::Busy},
                          std::pair{ApiError::Decode, TogetherError::InvalidResponse}}) {
            ApiResult a;
            a.error = pair.first;
            r = togetherResult(request(), a);
            QCOMPARE(r.outcome, TogetherOutcome::NotSent);
            QCOMPARE(r.error, pair.second);
        }
    }
    void facade()
    {
        BackendFacade f;
        QVERIFY(!f.requestTogether(request()));
        ready(f);
        QSignalSpy done(&f, &BackendFacade::togetherCompleted),
            calls(&f, &BackendFacade::togetherRequested),
            play(&f, &BackendFacade::playbackRequested);
        QList<quint64> ids;
        for (int i = 0; i < 8; ++i) {
            ids.append(f.requestTogether(request(i)));
            QVERIFY(ids.last());
        }
        QVERIFY(!f.requestTogether(request()));
        QCOMPARE(calls.size(), 8);
        const auto c = qvariant_cast<TogetherCommand>(calls.first()[0]);
        QCOMPARE(c.accountId, "101");
        auto bad = togetherResult(c.request, decoded(togetherHttpObject(TogetherKind::Create)));
        bad.roomGeneration++;
        f.deliverTogether({c.id, c.generation, c.context, bad});
        QCOMPARE(done.size(), 1);
        QCOMPARE(result(done).error, TogetherError::InvalidResponse);
        BackendEvent e;
        e.kind = BackendEvent::Kind::Session;
        e.generation = c.generation;
        e.session = f.snapshot();
        e.session.accountId = "102";
        f.deliver(e);
        QTRY_COMPARE(done.size(), 8);
        QVERIFY(play.isEmpty());
        f.close();
    }
};
QTEST_GUILESS_MAIN(TogetherHttpTest)
#include "together_http_test.moc"
