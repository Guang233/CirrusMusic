#include "../support/decode_eapi_request.h"
#include "../support/fake_http.h"
#include "application/backend/backend_facade.h"
#include "infrastructure/netease/api/collection_operation.h"
#include "infrastructure/netease/music/collection_gateway.h"
#include <QJsonDocument>
#include <QSignalSpy>
#include <QtTest>
using namespace cirrus::domain;
using namespace cirrus::application;
using namespace cirrus::netease;
using namespace cirrus::test;
namespace
{
constexpr auto wide = "9007199254740993";
const QString name = QString::fromUtf8("  合成\"歌单\\名称  ");
const QString description = QString::fromUtf8("合成\"描述\\\n\t第二行");
const char* names[] = {"create-public", "create-private", "rename", "description", "clear"};
const char* paths[] = {"/api/playlist/create", "/api/playlist/create", "/api/playlist/update/name",
                       "/api/playlist/desc/update", "/api/playlist/desc/update"};
const ApiOperation operations[] = {ApiOperation::PlaylistCreate, ApiOperation::PlaylistCreate,
                                   ApiOperation::PlaylistRename, ApiOperation::PlaylistDescription,
                                   ApiOperation::PlaylistDescription};
CollectionMutation mutation(int mode = 0)
{
    CollectionChange c;
    if (mode < 2)
        c = PlaylistCreate{name, mode == 0 ? PlaylistPrivacy::Public : PlaylistPrivacy::Private};
    else if (mode == 2)
        c = PlaylistRename{{wide}, name};
    else
        c = PlaylistDescription{{wide}, mode == 3 ? description : QString{}};
    return {QUuid::createUuid(), c};
}
QByteArray ack(int mode, bool stringCode = false)
{
    return mode < 2
               ? (stringCode ? QByteArray(R"({"code":"200","playlist":{"id":"9007199254740993"}})")
                             : QByteArray(R"({"code":200,"playlist":{"id":"9007199254740993"}})"))
               : (stringCode ? QByteArray(R"({"code":"200"})") : QByteArray(R"({"code":200})"));
}
QJsonObject expected(int mode)
{
    if (mode < 2)
        return {{"name", name}, {"privacy", mode == 0 ? "0" : "10"}, {"type", "NORMAL"}};
    if (mode == 2)
        return {{"id", wide}, {"name", name}};
    return {{"id", wide}, {"desc", mode == 3 ? description : QString{}}};
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
    static std::unique_ptr<HttpTransport> transport(QList<Plan> plans, FakeManager*& http)
    {
        auto manager = std::make_unique<FakeManager>();
        http = manager.get();
        http->plans = plans;
        return std::make_unique<HttpTransport>(std::move(manager));
    }
    Harness(QList<Plan> plans, bool auth = true, bool cookie = true)
        : client(transport(plans, http), "synthetic-playlist-write")
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
    quint64 submit(CollectionMutation m, QString account = "101")
    {
        return gateway.submit(session.snapshot().token, account, m);
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
class PlaylistWritesTest : public QObject
{
    Q_OBJECT
  private slots:
    void inputs()
    {
        for (int i = 0; i < 5; ++i)
            QVERIFY(mutation(i).isValid());
        for (const QString bad :
             {QString{}, QString(" \t "), QString(129, 'a'), QString(1, QChar(0)),
              QString(1, QChar(0xd800)), QString("x\ny"), QString(1, QChar(0x7f))}) {
            QVERIFY(!PlaylistCreate{bad}.isValid());
            QVERIFY(!(PlaylistRename{{"1"}, bad}).isValid());
        }
        QVERIFY(PlaylistCreate{QString(128, 'a')}.isValid());
        QVERIFY(!(PlaylistCreate{name, PlaylistPrivacy(9)}).isValid());
        QVERIFY((PlaylistDescription{{"1"}, QString(4096, 'a')}).isValid());
        QVERIFY(!(PlaylistDescription{{"1"}, QString(4097, 'a')}).isValid());
        QVERIFY(!(PlaylistDescription{{"1"}, QString(1, QChar(0xdfff))}).isValid());
        QVERIFY(!(PlaylistDescription{{"1"}, QString(1, QChar(0))}).isValid());
        QVERIFY(!(PlaylistDescription{{"1"}, QString(1, QChar(1))}).isValid());
        QVERIFY((PlaylistDescription{{"1"}, "\r\n\t"}).isValid());
        QVERIFY(PlaylistCreate{QString::fromUtf8("合成🎵")}.isValid());
        for (const QString id : {"0", "01", "-1", " 1", "1.0", "9223372036854775808"}) {
            QVERIFY(!(PlaylistRename{{id}, name}).isValid());
            QVERIFY(!(PlaylistDescription{{id}, ""}).isValid());
        }
        auto m = mutation();
        m.eventId = {};
        QVERIFY(!m.isValid());
        QVERIFY(!collectionRequest(m));
    }
    void contracts_data()
    {
        QTest::addColumn<int>("mode");
        for (int i = 0; i < 5; ++i)
            QTest::newRow(names[i]) << i;
    }
    void contracts()
    {
        QFETCH(int, mode);
        const auto r = collectionRequest(mutation(mode));
        QVERIFY(r);
        QCOMPARE(r->operation, operations[mode]);
        QVERIFY(isCollectionOperation(r->operation));
        const auto c = operationContract(r->operation);
        QVERIFY(c);
        QCOMPARE(c->logicalPath, paths[mode]);
        QCOMPARE(c->protocol, mode < 2 ? Protocol::Weapi : Protocol::Eapi);
        QCOMPARE(c->destination, OfficialDestination::Default);
        QCOMPARE(c->responseEncoding, ResponseEncoding::Json);
        QCOMPARE(c->responseLimit, 64 * 1024);
        QVERIFY(requiresAccountCookie(r->operation));
        DecodedResponse decoded;
        decoded.businessCode = 200;
        QVERIFY(!acceptsResponseCookies(r->operation, decoded));
        QCOMPARE(operationPayload(r->operation, r->parameters),
                 std::optional<QJsonObject>(expected(mode)));
        for (const char* key : {"uid", "type", "crypto", "checkToken", "timestamp", "header", "e_r",
                                "url", "privacyOverride"}) {
            auto bad = r->parameters;
            bad.insert(key, "overwrite");
            QVERIFY(!operationPayload(r->operation, bad));
        }
        auto bad = r->parameters;
        const QString key = mode < 3 ? "name" : "desc";
        bad.remove(key);
        QVERIFY(!operationPayload(r->operation, bad));
        bad = r->parameters;
        bad[key] = QJsonValue::Null;
        QVERIFY(!operationPayload(r->operation, bad));
        bad = r->parameters;
        bad[key] = true;
        QVERIFY(!operationPayload(r->operation, bad));
        if (mode < 2) {
            bad = r->parameters;
            bad["privacy"] = 10;
            QVERIFY(!operationPayload(r->operation, bad));
            bad["privacy"] = "SHARED";
            QVERIFY(!operationPayload(r->operation, bad));
        } else {
            bad = r->parameters;
            bad["id"] = 1;
            QVERIFY(!operationPayload(r->operation, bad));
        }
    }
    void native_data() { contracts_data(); }
    void native()
    {
        QFETCH(int, mode);
        Plan p{ack(mode)};
        p.headers.append(std::pair<QByteArray, QByteArray>{
            "Set-Cookie", "MUSIC_U=synthetic-rotation; Domain=.music.163.com; Path=/"});
        Harness h({p, Plan{ack(mode)}});
        QSignalSpy done(&h.gateway, &ICollectionGateway::completed);
        const auto token = h.session.snapshot().token;
        for (int n = 0; n < 2; ++n) {
            const auto m = mutation(mode);
            QVERIFY(h.submit(m));
            QTRY_COMPARE(done.size(), n + 1);
            QCOMPARE(result(done).outcome, CollectionOutcome::Acknowledged);
            QCOMPARE(result(done).error, CollectionError::None);
            QCOMPARE(result(done).eventId, m.eventId);
            QCOMPARE(qvariant_cast<SessionToken>(done.last()[1]), token);
            QCOMPARE(result(done).createdPlaylist.has_value(), mode < 2);
            if (mode < 2)
                QCOMPARE(result(done).createdPlaylist->value, wide);
            const auto& r = h.http->requests.last();
            QCOMPARE(r.url().path(),
                     QString(mode < 2 ? "/weapi" : "/eapi") + QString(paths[mode]).mid(4));
            QCOMPARE(r.url().host(), mode < 2 ? "music.163.com" : "interfacepc.music.163.com");
            QVERIFY(r.rawHeader("Cookie").contains("MUSIC_U=synthetic-original"));
            QVERIFY(!r.rawHeader("Cookie").contains("synthetic-rotation"));
            if (mode >= 2) {
                const auto w = decodeEapiRequest(h.http->bodies.last());
                QVERIFY(w);
                QCOMPARE(w->logicalPath, paths[mode]);
                auto payload = w->payload;
                QVERIFY(payload.take("header").isObject());
                QCOMPARE(payload.take("e_r"), QJsonValue(false));
                QCOMPARE(payload, expected(mode));
            } else {
                QVERIFY(h.http->bodies.last().startsWith("params="));
                QVERIFY(h.http->bodies.last().contains("&encSecKey="));
            }
            QCOMPARE(h.http->requests.size(), n + 2);
        }
    }
    void responses_data()
    {
        QTest::addColumn<int>("mode");
        QTest::addColumn<QByteArray>("body");
        QTest::addColumn<int>("status");
        QTest::addColumn<CollectionOutcome>("outcome");
        QTest::addColumn<CollectionError>("error");
        for (int i = 0; i < 5; ++i) {
            const auto pre = QByteArray(names[i]) + "-";
            QTest::newRow(pre + "ack")
                << i << ack(i) << 200 << CollectionOutcome::Acknowledged << CollectionError::None;
            QTest::newRow(pre + "string-code")
                << i << ack(i, true) << 200 << CollectionOutcome::Acknowledged
                << CollectionError::None;
            for (int code : {201, 403, 512, -462})
                QTest::newRow(pre + QByteArray::number(code))
                    << i << QByteArray("{\"code\":" + QByteArray::number(code) + "}") << 200
                    << CollectionOutcome::Rejected << CollectionError::ProviderRejected;
            for (const QByteArray b :
                 {QByteArray("{}"), QByteArray("broken"), QByteArray("[]"),
                  QByteArray(R"({"code":true})"), QByteArray(R"({"code":"2e2"})")})
                QTest::newRow(pre + "schema-" + b) << i << b << 200 << CollectionOutcome::Unknown
                                                   << CollectionError::InvalidResponse;
            QTest::newRow(pre + "provider429")
                << i << QByteArray(R"({"code":429})") << 200 << CollectionOutcome::Rejected
                << CollectionError::RateLimited;
            QTest::newRow(pre + "http429")
                << i << QByteArray("{}") << 429 << CollectionOutcome::Unknown
                << CollectionError::RateLimited;
            QTest::newRow(pre + "http502")
                << i << ack(i) << 502 << CollectionOutcome::Unknown << CollectionError::Network;
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
        QVERIFY(h.submit(mutation(mode)));
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(result(done).outcome, outcome);
        QCOMPARE(result(done).error, error);
        QCOMPARE(result(done).createdPlaylist.has_value(),
                 mode < 2 && outcome == CollectionOutcome::Acknowledged);
        if (error == CollectionError::RateLimited)
            QCOMPARE(result(done).retryAfterMs, std::optional<qint64>(3000));
        QTest::qWait(1);
        QCOMPARE(h.http->requests.size(), 2);
        QCOMPARE(done.size(), 1);
    }
    void creationSchema_data()
    {
        QTest::addColumn<QByteArray>("body");
        const QList<QByteArray> bad = {R"({"code":200})",
                                       R"({"code":200,"id":"1"})",
                                       R"({"code":200,"data":{"id":"1"}})",
                                       R"({"code":200,"playlist":null})",
                                       R"({"code":200,"playlist":[]})",
                                       R"({"code":200,"playlist":"1"})",
                                       R"({"code":200,"playlist":{}})",
                                       R"({"code":200,"playlist":{"id":null}})",
                                       R"({"code":200,"playlist":{"id":0}})",
                                       R"({"code":200,"playlist":{"id":"0"}})",
                                       R"({"code":200,"playlist":{"id":true}})",
                                       R"({"code":200,"playlist":{"id":-1}})",
                                       R"({"code":200,"playlist":{"id":1.5}})",
                                       R"({"code":200,"playlist":{"id":"01"}})",
                                       R"({"code":200,"playlist":{"id":" 1"}})",
                                       R"({"code":200,"playlist":{"id":"1e3"}})",
                                       R"({"code":200,"playlist":{"id":9007199254740992}})",
                                       R"({"code":200,"playlist":{"id":"9223372036854775808"}})",
                                       R"({"code":200,"playlist":{"id":"1"},"id":"2"})",
                                       R"({"code":200,"playlist":{"id":"1"},"id":null})",
                                       R"({"code":200,"playlist":{"id":"1"},"id":true})",
                                       R"({"code":200,"playlist":{"id":"1"},"id":"01"})"};
        for (int i = 0; i < bad.size(); ++i)
            QTest::newRow(QByteArray::number(i)) << bad[i];
    }
    void creationSchema()
    {
        QFETCH(QByteArray, body);
        const auto doc = QJsonDocument::fromJson(body);
        QVERIFY(doc.isObject());
        Harness h({Plan{body}});
        QSignalSpy done(&h.gateway, &ICollectionGateway::completed);
        QVERIFY(h.submit(mutation()));
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(result(done).outcome, CollectionOutcome::Unknown);
        QCOMPARE(result(done).error, CollectionError::InvalidResponse);
        QCOMPARE(result(done).providerCode, std::optional<int>(200));
        QVERIFY(!result(done).createdPlaylist);
        QCOMPARE(h.http->requests.size(), 2);
    }
    void identityClassifier()
    {
        auto api = ApiResult{};
        api.submittedToTransport = true;
        for (
            const QByteArray body :
            {QByteArray(R"({"code":200,"playlist":{"id":1},"id":"1"})"),
             QByteArray(
                 R"({"code":200,"playlist":{"id":"9223372036854775807"},"id":"9223372036854775807","unknown":true})")}) {
            api.decoded = decodeResponse(body, ResponseEncoding::Json);
            const auto r = collectionResult(api, mutation());
            QCOMPARE(r.outcome, CollectionOutcome::Acknowledged);
            QVERIFY(r.createdPlaylist);
            QVERIFY(
                !collectionResult(api, mutation(2)).createdPlaylist); // not a creation capability
        }
        api.decoded = decodeResponse(ack(0), ResponseEncoding::Json);
        api.error = ApiError::Transport;
        api.transportError = TransportError::Timeout;
        auto r = collectionResult(api, mutation());
        QCOMPARE(r.outcome, CollectionOutcome::Unknown);
        QCOMPARE(r.error, CollectionError::Timeout);
        QVERIFY(!r.createdPlaylist);
        api.error = ApiError::None;
        api.transportError = TransportError::None;
        api.decoded =
            decodeResponse(R"({"code":403,"playlist":{"id":"1"}})", ResponseEncoding::Json);
        r = collectionResult(api, mutation());
        QCOMPARE(r.outcome, CollectionOutcome::Rejected);
        QVERIFY(!r.createdPlaylist);
    }
    void gates_data() { contracts_data(); }
    void gates()
    {
        QFETCH(int, mode);
        Harness guest({}, false);
        QSignalSpy g(&guest.gateway, &ICollectionGateway::completed);
        QVERIFY(guest.submit(mutation(mode)));
        QTRY_COMPARE(g.size(), 1);
        QCOMPARE(result(g).outcome, CollectionOutcome::NotSent);
        QVERIFY(guest.http->requests.isEmpty());
        Harness noCookie({}, true, false);
        noCookie.client.setAnonymousToken("synthetic");
        QSignalSpy a(&noCookie.gateway, &ICollectionGateway::completed);
        QVERIFY(noCookie.submit(mutation(mode)));
        QTRY_COMPARE(a.size(), 1);
        QCOMPARE(result(a).error, CollectionError::AuthenticationRequired);
        QVERIFY(noCookie.http->requests.isEmpty());
        Harness h({});
        QSignalSpy done(&h.gateway, &ICollectionGateway::completed);
        QVERIFY(h.submit(mutation(mode), "202"));
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(result(done).error, CollectionError::InvalidRequest);
        QCOMPARE(h.http->requests.size(), 1);
        QVERIFY(h.gateway.submit({QUuid::createUuid(), 1}, "101", mutation(mode)));
        QTRY_COMPARE(done.size(), 2);
        QCOMPARE(result(done).error, CollectionError::StaleContext);
    }
    void cancellation_data() { contracts_data(); }
    void cancellation()
    {
        QFETCH(int, mode);
        Harness h({Plan{"", 200, -1}, Plan{"", 200, -1}});
        QSignalSpy done(&h.gateway, &ICollectionGateway::completed);
        auto id = h.submit(mutation(mode));
        QVERIFY(id);
        h.gateway.cancel(id);
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(result(done).outcome, CollectionOutcome::NotSent);
        QVERIFY(!result(done).createdPlaylist);
        QCOMPARE(h.http->requests.size(), 1);
        id = h.submit(mutation(mode));
        QTRY_COMPARE(h.http->requests.size(), 2);
        h.gateway.cancel(id);
        QTRY_COMPARE(done.size(), 2);
        QCOMPARE(result(done).outcome, CollectionOutcome::Unknown);
        QVERIFY(!result(done).createdPlaylist);
        id = h.submit(mutation(mode));
        QTRY_COMPARE(h.http->requests.size(), 3);
        const auto original = h.session.snapshot().token;
        QVERIFY(h.session.beginAuthentication().isValid());
        QTRY_COMPARE(done.size(), 3);
        QCOMPARE(result(done).outcome, CollectionOutcome::Unknown);
        QCOMPARE(qvariant_cast<SessionToken>(done.last()[1]), original);
        QVERIFY(!result(done).createdPlaylist);
    }
    void invalidInputsNotSent()
    {
        Harness h({});
        QSignalSpy done(&h.gateway, &ICollectionGateway::completed);
        auto m = mutation();
        m.change = PlaylistCreate{QString(129, 'a')};
        QVERIFY(h.submit(m));
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(result(done).outcome, CollectionOutcome::NotSent);
        QCOMPARE(result(done).error, CollectionError::InvalidRequest);
        QVERIFY(!result(done).createdPlaylist);
        QSignalSpy api(&h.client, &SessionApiClient::completed);
        QVERIFY(h.client.submit(h.session.snapshot().token, ApiOperation::PlaylistCreate,
                                {{"name", "synthetic"}, {"privacy", "0"}, {"type", "SHARED"}}));
        QTRY_COMPARE(api.size(), 1);
        const auto rejected = qvariant_cast<ApiResult>(api.last()[2]);
        QCOMPARE(rejected.error, ApiError::InvalidRequest);
        QVERIFY(!rejected.submittedToTransport);
        QCOMPARE(h.http->requests.size(), 1);
    }
    void invalidCreationDelivery()
    {
        BackendFacade f;
        ready(f);
        QSignalSpy requested(&f, &BackendFacade::collectionRequested),
            done(&f, &BackendFacade::collectionCompleted);
        for (const QString id : {QString{}, QString("0"), QString("01"), QString("-1")}) {
            QVERIFY(f.mutateCollection(mutation()));
            const auto c = qvariant_cast<CollectionCommand>(requested.last()[0]);
            CollectionResult bad;
            bad.outcome = CollectionOutcome::Acknowledged;
            bad.providerCode = 200;
            if (!id.isEmpty())
                bad.createdPlaylist = PlaylistId{id};
            f.deliverCollection({c.id, c.generation, c.context, bad});
            QCOMPARE(result(done).outcome, CollectionOutcome::Unknown);
            QCOMPARE(result(done).error, CollectionError::InvalidResponse);
            QCOMPARE(result(done).providerCode, std::optional<int>(200));
            QVERIFY(!result(done).createdPlaylist);
        }
    }
    void sameTokenBarrier_data()
    {
        QTest::addColumn<int>("mode");
        QTest::newRow("guest") << 0;
        QTest::newRow("committing") << 1;
        QTest::newRow("foreign-account") << 2;
    }
    void sameTokenBarrier()
    {
        QFETCH(int, mode);
        BackendFacade f;
        ready(f);
        QSignalSpy requested(&f, &BackendFacade::collectionRequested),
            done(&f, &BackendFacade::collectionCompleted);
        QVERIFY(f.mutateCollection(mutation()));
        const auto c = qvariant_cast<CollectionCommand>(requested.last()[0]);
        BackendEvent changed;
        changed.kind = BackendEvent::Kind::Session;
        changed.session = f.snapshot();
        if (mode == 2)
            changed.session.accountId = "202";
        else
            changed.session.phase = mode == 0 ? SessionPhase::Guest : SessionPhase::Committing;
        f.deliver(changed);
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(result(done).outcome, CollectionOutcome::Unknown);
        QVERIFY(!result(done).createdPlaylist);
        QCOMPARE(qvariant_cast<SessionToken>(done.last()[1]), c.context);
        CollectionResult late;
        late.outcome = CollectionOutcome::Acknowledged;
        late.createdPlaylist = PlaylistId{wide};
        f.deliverCollection({c.id, c.generation, c.context, late});
        QCOMPARE(done.size(), 1);
    }
    void facadeBarrier()
    {
        BackendFacade f;
        ready(f);
        QSignalSpy requested(&f, &BackendFacade::collectionRequested),
            done(&f, &BackendFacade::collectionCompleted);
        QList<CollectionCommand> commands;
        for (int i = 0; i < 8; ++i) {
            QVERIFY(f.mutateCollection(mutation(i % 5)));
            commands.append(qvariant_cast<CollectionCommand>(requested.last()[0]));
        }
        QCOMPARE(f.mutateCollection(mutation()), quint64(0));
        QCOMPARE(f.mutateCollection(commands.first().mutation), quint64(0));
        const auto c = commands.first();
        f.cancelCollection(c.id);
        QTRY_COMPARE(done.size(), 1);
        CollectionResult success;
        success.outcome = CollectionOutcome::Acknowledged;
        success.createdPlaylist = PlaylistId{wide};
        f.deliverCollection({c.id, c.generation, c.context, success});
        QCOMPARE(done.size(), 1);
        QVERIFY(!result(done).createdPlaylist);
        const auto rename = commands[2];
        f.deliverCollection({rename.id, rename.generation, rename.context, success});
        QCOMPARE(done.size(), 2);
        QVERIFY(!result(done).createdPlaylist);
        const auto creation = commands[1];
        f.deliverCollection({creation.id, creation.generation, creation.context, success});
        QCOMPARE(done.size(), 3);
        QCOMPARE(result(done).createdPlaylist->value, wide);
        const auto stale = commands[5];
        f.deliverCollection({stale.id, stale.generation + 1, stale.context, success});
        QCOMPARE(done.size(), 4);
        QCOMPARE(result(done).outcome, CollectionOutcome::Unknown);
        QVERIFY(!result(done).createdPlaylist);
        QVERIFY(f.signOut());
        QTRY_COMPARE(done.size(), 8);
        for (const auto& row : done)
            QCOMPARE(qvariant_cast<SessionToken>(row[1]), c.context);
    }
};
QTEST_GUILESS_MAIN(PlaylistWritesTest)
#include "playlist_writes_test.moc"
