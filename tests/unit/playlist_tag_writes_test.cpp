#include "../support/decode_eapi_request.h"
#include "../support/fake_http.h"
#include "application/backend/backend_facade.h"
#include "infrastructure/netease/api/collection_operation.h"
#include "infrastructure/netease/music/collection_gateway.h"
#include <QJsonArray>
#include <QSignalSpy>
#include <QtTest>
using namespace cirrus::domain;
using namespace cirrus::application;
using namespace cirrus::netease;
using namespace cirrus::test;
namespace
{
const QString first = QString::fromUtf8("合成\"标签\\名");
const QString second = QString::fromUtf8("合成🎵");
PlaylistTags catalogue()
{
    PlaylistTags source;
    source.all = PlaylistTag{QStringLiteral("全部")};
    source.tags = {{first}, {second}, {QString(128, 'x')}};
    return source;
}
CollectionMutation mutation(SessionToken token, int mode)
{
    if (mode == 1)
        return {QUuid::createUuid(), PlaylistMakePublic{{"9007199254740993"}}};
    return {QUuid::createUuid(), *PlaylistTagEdit::fromCatalogue(token, {"9007199254740993"},
                                                                 catalogue(), {second, first})};
}
CollectionResult result(const QSignalSpy& done)
{
    return qvariant_cast<CollectionResult>(done.last()[2]);
}
struct Harness {
    SessionCoordinator session;
    FakeManager* http = nullptr;
    SessionApiClient client;
    CollectionGateway gateway{session, client};
    static std::unique_ptr<HttpTransport> transport(QList<Plan> plans, FakeManager*& out)
    {
        auto manager = std::make_unique<FakeManager>();
        out = manager.get();
        out->plans = std::move(plans);
        return std::make_unique<HttpTransport>(std::move(manager));
    }
    explicit Harness(QList<Plan> plans = {}, bool auth = true, bool cookie = true)
        : client(transport(std::move(plans), http), "synthetic-tag-write")
    {
        QVERIFY(client.advanceTo(session.snapshot().token));
        QObject::connect(&session, &SessionCoordinator::changed, &client,
                         [this](SessionSnapshot s) { QVERIFY(client.advanceTo(s.token)); });
        if (!auth)
            return;
        const auto prepared = session.prepareAuthentication(session.beginAuthentication(), "101");
        QVERIFY(prepared);
        QVERIFY(session.completeAuthentication(*prepared));
        if (!cookie)
            return;
        Plan p{R"({"code":200,"unikey":"synthetic"})"};
        p.headers.append(std::pair<QByteArray, QByteArray>{
            "Set-Cookie", "MUSIC_U=synthetic-original; Domain=.music.163.com; Path=/"});
        http->plans.prepend(p);
        QSignalSpy done(&client, &SessionApiClient::completed);
        QVERIFY(client.submit(session.snapshot().token, ApiOperation::QrKey));
        QTRY_COMPARE(done.size(), 1);
    }
    quint64 submit(int mode)
    {
        return gateway.submit(session.snapshot().token, "101",
                              mutation(session.snapshot().token, mode));
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
class PlaylistTagWritesTest : public QObject
{
    Q_OBJECT
  private slots:
    void catalogueFactoryAndBounds()
    {
        static_assert(!std::is_default_constructible_v<PlaylistTagEdit>);
        const SessionToken token{QUuid::createUuid(), 1};
        const auto source = catalogue();
        const auto value = PlaylistTagEdit::fromCatalogue(token, {"1"}, source, {second, first});
        QVERIFY(value);
        QVERIFY(value->isValid());
        QCOMPARE(value->tags(), QStringList({second, first}));
        QCOMPARE(value->sourceContext(), token);
        QVERIFY(!PlaylistTagEdit::fromCatalogue({}, {"1"}, source, {first}));
        QVERIFY(!PlaylistTagEdit::fromCatalogue(token, {"0"}, source, {first}));
        QVERIFY(!PlaylistTagEdit::fromCatalogue(token, {"1"}, source, {})); // no unverified clear
        QVERIFY(!PlaylistTagEdit::fromCatalogue(token, {"1"}, source, {first, first}));
        QVERIFY(!PlaylistTagEdit::fromCatalogue(token, {"1"}, source, {"unlisted"}));
        QVERIFY(!PlaylistTagEdit::fromCatalogue(token, {"1"}, source,
                                                {first, second, QString(128, 'x'), "fourth"}));
        QVERIFY(PlaylistTagEdit::fromCatalogue(token, {"1"}, source,
                                               {first, second, QString(128, 'x')}));
        for (auto kind : {PlaylistTagsKind::Hot, PlaylistTagsKind::Premium, PlaylistTagsKind(9)}) {
            auto wrong = source;
            wrong.query.kind = kind;
            QVERIFY(!PlaylistTagEdit::fromCatalogue(token, {"1"}, wrong, {first}));
        }
        for (const QString bad :
             {QString{}, QString(" x"), QString("x "), QString("x;y"), QString("全部"),
              QString(129, 'x'), QString(1, QChar(0)), QString(1, QChar(0xd800)),
              QString(1, QChar(0x7f)), QString("x\ny")}) {
            auto wrong = source;
            wrong.tags.append(PlaylistTag{bad});
            QVERIFY(!PlaylistTagEdit::fromCatalogue(token, {"1"}, wrong, {bad}));
            QVERIFY(!validEditablePlaylistTags({bad}));
        }
        auto empty = source;
        empty.tags.clear();
        QVERIFY(!PlaylistTagEdit::fromCatalogue(token, {"1"}, empty, {first}));
        auto all = source;
        all.all = PlaylistTag{first};
        QVERIFY(!PlaylistTagEdit::fromCatalogue(token, {"1"}, all, {first}));
        auto maximum = source;
        maximum.tags = QList<PlaylistTag>(PlaylistTags::MaximumTags, {first});
        QVERIFY(PlaylistTagEdit::fromCatalogue(token, {"1"}, maximum, {first}));
        maximum.tags.append({second});
        QVERIFY(!PlaylistTagEdit::fromCatalogue(token, {"1"}, maximum, {first}));
        auto invalid = mutation(token, 1);
        invalid.eventId = {};
        QVERIFY(!collectionRequest(invalid));
        QVERIFY(!(PlaylistMakePublic{{"01"}}).isValid());
    }
    void closedPayloads()
    {
        const SessionToken token{QUuid::createUuid(), 1};
        for (int mode : {0, 1}) {
            const auto request = collectionRequest(mutation(token, mode));
            QVERIFY(request);
            QVERIFY(isCollectionOperation(request->operation));
            QVERIFY(requiresAccountCookie(request->operation));
            const auto contract = operationContract(request->operation);
            QVERIFY(contract);
            QCOMPARE(contract->logicalPath,
                     mode == 0 ? "/api/playlist/tags/update" : "/api/playlist/update/privacy");
            QCOMPARE(contract->protocol, Protocol::Eapi);
            QCOMPARE(contract->destination, OfficialDestination::Default);
            QCOMPARE(contract->responseEncoding, ResponseEncoding::Json);
            QCOMPARE(contract->responseLimit, 64 * 1024);
            const auto payload = operationPayload(request->operation, request->parameters);
            QVERIFY(payload);
            QCOMPARE(payload->value("id"), QJsonValue("9007199254740993"));
            QCOMPARE(payload->size(), 2);
            if (mode == 0)
                QCOMPARE(payload->value("tags"), QJsonValue(second + ";" + first));
            else
                QCOMPARE(payload->value("privacy"), QJsonValue(0)); // integer, no flag input
            for (const QString key :
                 {"privacy", "header", "checkToken", "crypto", "desc", "timestamp"}) {
                auto p = request->parameters;
                p[key] = "10";
                QVERIFY(!operationPayload(request->operation, p));
            }
            auto p = request->parameters;
            p["id"] = 1;
            QVERIFY(!operationPayload(request->operation, p));
        }
        for (const QJsonValue raw : {QJsonValue("one;two"), QJsonValue(QJsonArray{}),
                                     QJsonValue(QJsonArray{1}), QJsonValue(QJsonArray{"x", "x"}),
                                     QJsonValue(QJsonArray{"x;y"}), QJsonValue(QJsonArray{"全部"})})
            QVERIFY(
                !operationPayload(ApiOperation::PlaylistTagsUpdate, {{"id", "1"}, {"tags", raw}}));
        // Existing description edit/clear stays independent from tags and publishing.
        for (const QString desc : {QString{}, QString("synthetic introduction\nsecond line")}) {
            const auto r =
                collectionRequest({QUuid::createUuid(), PlaylistDescription{{"1"}, desc}});
            QVERIFY(r);
            QCOMPARE(r->operation, ApiOperation::PlaylistDescription);
            QCOMPARE(operationPayload(r->operation, r->parameters)->value("desc"),
                     QJsonValue(desc));
        }
    }
    void modes_data()
    {
        QTest::addColumn<int>("mode");
        QTest::newRow("tags") << 0;
        QTest::newRow("make-public") << 1;
    }
    void native_data() { modes_data(); }
    void native()
    {
        QFETCH(int, mode);
        Plan p{R"({"code":200,"playlist":{"id":"7"}})"};
        p.headers.append(std::pair<QByteArray, QByteArray>{
            "Set-Cookie", "MUSIC_U=synthetic-rotation; Domain=.music.163.com; Path=/"});
        Harness h({p, p});
        QSignalSpy done(&h.gateway, &ICollectionGateway::completed);
        const auto token = h.session.snapshot().token;
        for (int i = 1; i <= 2; ++i) {
            QVERIFY(h.submit(mode));
            QTRY_COMPARE(done.size(), i);
            QCOMPARE(result(done).outcome, CollectionOutcome::Acknowledged);
            QVERIFY(!result(done).createdPlaylist);
            QCOMPARE(qvariant_cast<SessionToken>(done.last()[1]), token);
            const auto request = *collectionRequest(mutation(token, mode));
            const auto wire = decodeEapiRequest(h.http->bodies.last());
            QVERIFY(wire);
            QCOMPARE(wire->logicalPath, operationContract(request.operation)->logicalPath);
            auto business = wire->payload;
            QVERIFY(business["header"].isObject());
            QCOMPARE(business.take("e_r"), QJsonValue(false));
            business.remove("header");
            QCOMPARE(business, *operationPayload(request.operation, request.parameters));
            QCOMPARE(h.http->requests.last().url().host(), "interfacepc.music.163.com");
            QCOMPARE(h.http->requests.last().url().path(), "/eapi" + wire->logicalPath.mid(4));
            const auto cookie = h.http->requests.last().rawHeader("Cookie");
            QVERIFY(cookie.contains("MUSIC_U=synthetic-original"));
            QVERIFY(!cookie.contains("synthetic-rotation"));
        }
        QCOMPARE(h.http->requests.size(), 3); // seed plus two explicit writes, no read/refresh
    }
    void responses_data()
    {
        QTest::addColumn<int>("mode");
        QTest::addColumn<QByteArray>("body");
        QTest::addColumn<int>("status");
        QTest::addColumn<CollectionOutcome>("outcome");
        QTest::addColumn<CollectionError>("error");
        for (int mode : {0, 1}) {
            const auto pre = QByteArray::number(mode) + "-";
            for (const QByteArray body :
                 {QByteArray(R"({"code":200})"), QByteArray(R"({"code":"200"})")})
                QTest::newRow(pre + body) << mode << body << 200 << CollectionOutcome::Acknowledged
                                          << CollectionError::None;
            for (int code : {201, 403, 512, -462})
                QTest::newRow(pre + QByteArray::number(code))
                    << mode << QByteArray("{\"code\":" + QByteArray::number(code) + "}") << 200
                    << CollectionOutcome::Rejected << CollectionError::ProviderRejected;
            for (const QByteArray body :
                 {QByteArray("{}"), QByteArray("broken"), QByteArray("[]"),
                  QByteArray(R"({"code":true})"), QByteArray(R"({"code":"2e2"})")})
                QTest::newRow(pre + body) << mode << body << 200 << CollectionOutcome::Unknown
                                          << CollectionError::InvalidResponse;
            QTest::newRow(pre + "provider429")
                << mode << QByteArray(R"({"code":429})") << 200 << CollectionOutcome::Rejected
                << CollectionError::RateLimited;
            QTest::newRow(pre + "http429")
                << mode << QByteArray("{}") << 429 << CollectionOutcome::Unknown
                << CollectionError::RateLimited;
            QTest::newRow(pre + "http502")
                << mode << QByteArray(R"({"code":200})") << 502 << CollectionOutcome::Unknown
                << CollectionError::Network;
        }
    }
    void responses()
    {
        QFETCH(int, mode);
        QFETCH(QByteArray, body);
        QFETCH(int, status);
        QFETCH(CollectionOutcome, outcome);
        QFETCH(CollectionError, error);
        Harness h({Plan{body, status}});
        QSignalSpy done(&h.gateway, &ICollectionGateway::completed);
        const auto token = h.session.snapshot().token;
        const auto m = mutation(token, mode);
        QVERIFY(h.gateway.submit(token, "101", m));
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(result(done).eventId, m.eventId);
        QCOMPARE(result(done).outcome, outcome);
        QCOMPARE(result(done).error, error);
        QVERIFY(!result(done).createdPlaylist);
        if (error == CollectionError::RateLimited)
            QCOMPARE(result(done).retryAfterMs, std::optional<qint64>(3000));
        QTest::qWait(1);
        QCOMPARE(h.http->requests.size(), 2);
        QCOMPARE(done.size(), 1);
    }
    void gatesAndCancellation_data() { modes_data(); }
    void gatesAndCancellation()
    {
        QFETCH(int, mode);
        Harness guest({}, false);
        QSignalSpy g(&guest.gateway, &ICollectionGateway::completed);
        QVERIFY(guest.submit(mode));
        QTRY_COMPARE(g.size(), 1);
        QCOMPARE(result(g).outcome, CollectionOutcome::NotSent);
        QVERIFY(guest.http->requests.isEmpty());
        Harness noCookie({}, true, false);
        noCookie.client.setAnonymousToken("synthetic");
        QSignalSpy c(&noCookie.gateway, &ICollectionGateway::completed);
        QVERIFY(noCookie.submit(mode));
        QTRY_COMPARE(c.size(), 1);
        QCOMPARE(result(c).error, CollectionError::AuthenticationRequired);
        QVERIFY(noCookie.http->requests.isEmpty());
        Harness h({Plan{"", 200, -1}, Plan{"", 200, -1}});
        QSignalSpy done(&h.gateway, &ICollectionGateway::completed);
        const auto token = h.session.snapshot().token;
        QVERIFY(h.gateway.submit(token, "202", mutation(token, mode)));
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(result(done).error, CollectionError::InvalidRequest);
        QVERIFY(h.gateway.submit({QUuid::createUuid(), 1}, "101", mutation(token, mode)));
        QTRY_COMPARE(done.size(), 2);
        QCOMPARE(result(done).error, CollectionError::StaleContext);
        auto id = h.submit(mode);
        QVERIFY(id);
        h.gateway.cancel(id);
        QTRY_COMPARE(done.size(), 3);
        QCOMPARE(result(done).outcome, CollectionOutcome::NotSent);
        QCOMPARE(h.http->requests.size(), 1);
        id = h.submit(mode);
        QTRY_COMPARE(h.http->requests.size(), 2);
        h.gateway.cancel(id);
        QTRY_COMPARE(done.size(), 4);
        QCOMPARE(result(done).outcome, CollectionOutcome::Unknown);
        id = h.submit(mode);
        QTRY_COMPARE(h.http->requests.size(), 3);
        h.session.signOut();
        QTRY_COMPARE(done.size(), 5);
        QCOMPARE(result(done).outcome, CollectionOutcome::Unknown);
        QCOMPARE(qvariant_cast<SessionToken>(done.last()[1]), token);
    }
    void sourceContextAndInvalidWireNotSent()
    {
        Harness h;
        QSignalSpy done(&h.gateway, &ICollectionGateway::completed);
        const auto token = h.session.snapshot().token;
        QVERIFY(h.gateway.submit(token, "101", mutation({QUuid::createUuid(), 1}, 0)));
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(result(done).error, CollectionError::StaleContext);
        QCOMPARE(result(done).outcome, CollectionOutcome::NotSent);
        QSignalSpy api(&h.client, &SessionApiClient::completed);
        QVERIFY(h.client.submit(token, ApiOperation::PlaylistMakePublic,
                                {{"id", "1"}, {"privacy", 10}}));
        QTRY_COMPARE(api.size(), 1);
        QCOMPARE(qvariant_cast<ApiResult>(api.last()[2]).error, ApiError::InvalidRequest);
        QVERIFY(!qvariant_cast<ApiResult>(api.last()[2]).submittedToTransport);
        QCOMPARE(h.http->requests.size(), 1);
        ApiResult failure;
        failure.submittedToTransport = true;
        failure.error = ApiError::Transport;
        failure.transportError = TransportError::Timeout;
        QCOMPARE(collectionResult(failure, mutation(token, 1)).outcome, CollectionOutcome::Unknown);
        QCOMPARE(collectionResult(failure, mutation(token, 1)).error, CollectionError::Timeout);
    }
    void facadeBarriers_data()
    {
        QTest::addColumn<int>("change");
        QTest::newRow("cancel") << 0;
        QTest::newRow("signout") << 1;
        QTest::newRow("same-token-guest") << 2;
        QTest::newRow("same-token-other-account") << 3;
    }
    void facadeBarriers()
    {
        QFETCH(int, change);
        BackendFacade f;
        ready(f);
        QSignalSpy requests(&f, &BackendFacade::collectionRequested),
            done(&f, &BackendFacade::collectionCompleted),
            reads(&f, &BackendFacade::musicRequested), play(&f, &BackendFacade::playbackRequested),
            reports(&f, &BackendFacade::listeningReportRequested);
        const auto token = f.snapshot().token;
        QVERIFY(!f.mutateCollection(mutation({QUuid::createUuid(), 1}, 0)));
        QList<CollectionCommand> commands;
        for (int i = 0; i < 8; ++i) {
            QVERIFY(f.mutateCollection(mutation(token, i % 2)));
            commands.append(qvariant_cast<CollectionCommand>(requests.last()[0]));
        }
        QVERIFY(!f.mutateCollection(mutation(token, 1)));
        QVERIFY(!f.mutateCollection(commands.first().mutation));
        const auto original = commands.first();
        if (change == 0) {
            for (const auto& c : commands)
                f.cancelCollection(c.id);
        } else if (change == 1) {
            QVERIFY(f.signOut());
        } else {
            BackendEvent e;
            e.kind = BackendEvent::Kind::Session;
            e.session = f.snapshot();
            if (change == 2)
                e.session.phase = SessionPhase::Guest;
            else
                e.session.accountId = "202";
            f.deliver(e);
        }
        QTRY_COMPARE(done.size(), 8);
        for (const auto& row : done) {
            QCOMPARE(qvariant_cast<SessionToken>(row[1]), token);
            QCOMPARE(qvariant_cast<CollectionResult>(row[2]).outcome, CollectionOutcome::Unknown);
        }
        CollectionResult success;
        success.outcome = CollectionOutcome::Acknowledged;
        success.createdPlaylist = PlaylistId{"7"};
        f.deliverCollection({original.id, original.generation, original.context, success});
        QCOMPARE(done.size(), 8);
        QVERIFY(reads.isEmpty());
        QVERIFY(play.isEmpty());
        QVERIFY(reports.isEmpty());
    }
};
QTEST_GUILESS_MAIN(PlaylistTagWritesTest)
#include "playlist_tag_writes_test.moc"
