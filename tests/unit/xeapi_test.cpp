#include "infrastructure/netease/api/session_api_client.h"
#include "infrastructure/netease/api/xeapi_key_registrar.h"
#include "infrastructure/netease/protocol/xeapi_request_profile.h"

#include "../support/fake_http.h"

#include <QSignalSpy>
#include <QtTest>

using namespace cirrus::domain;
using namespace cirrus::netease;
using namespace cirrus::test;

Q_DECLARE_METATYPE(cirrus::test::Plan)

// Synthetic offline data only. Key material matches netease_crypto_test vectors.
class XeapiTest : public QObject
{
    Q_OBJECT
  private slots:
    void anonymousUsernameVector();
    void signatureVerification();
    void transportProfileHeaders();
    void transportRejectsForeignHeaders();
    void transportSessionCapture_data();
    void transportSessionCapture();
    void registrarSuccess();
    void registrarRejects_data();
    void registrarRejects();
    void registrarLocalFailure();
    void clientMissingKey();
    void clientRequestAndSessionKey();
    void clientStaleResponseKeepsSession();
};

namespace
{
const QByteArray EncryptedKey =
    "4+hVmeCQwLTgprLInve9fHSNDGu2fQk0eXSJWn0AQwej4hwIPigJPBD8Dppmf+ffzhWn0FTrxRq0zmGbVkri+jEFM3"
    "GCVoc4xtnf2hjsUYggW4Y4rS5k5i1lpQhXxgpJ";
// AES-128-ECB(eapi key) of {"code":200,"ok":true}
const QByteArray EncryptedOk =
    QByteArray::fromHex("e879bf9ce6ae320a55d1da6ac1d8b823a871f700a5a8fa0ea80d444039c2aa09");
// R for version v7 without / with session id sid-1 (also checked with OpenSSL CLI).
const QByteArray RoutingNoSession = "6wpHvI2HYma7VDIHXm3prw==";
const QByteArray RoutingSession = "oL5euWuLAVMn5tLIXjBTHg==";
constexpr char Nonce[] = "1111111111111111"; // from 32 bytes of 0x01
constexpr qint64 Now = 1700000000000;

XeapiPublicKey testKey()
{
    return {QStringLiteral("v7"),
            QByteArray::fromBase64("eaYx7t4b+cmPEgMs3q3Q56B5OY/HhriMyEbsia+FpRo="), "sk-test-123"};
}

SessionToken token() { return {QUuid::createUuid(), 1}; }

QByteArray formValue(const QByteArray& body, const QByteArray& name)
{
    for (const auto& part : body.split('&')) {
        if (part.startsWith(name + '=')) {
            auto value = part.mid(name.size() + 1);
            return QByteArray::fromPercentEncoding(value.replace('+', ' '));
        }
    }
    return {};
}

Plan keyResponse(const QByteArray& signature, const QByteArray& timestamp = "\"1700000000123\"")
{
    return {R"({"code":200,"data":{"encryptedData":")" + EncryptedKey + R"(","signature":")" +
            signature + R"(","timestamp":)" + timestamp + "}}"};
}

struct Registrar {
    FakeManager* fake = nullptr;
    XeapiKeyRing ring;
    std::unique_ptr<XeapiKeyRegistrar> registrar;
    explicit Registrar(
        QList<Plan> plans, RandomBytes random = [](qsizetype size) {
            return std::optional<QByteArray>(QByteArray(size, '\x01'));
        })
    {
        auto manager = std::make_unique<FakeManager>();
        fake = manager.get();
        fake->plans = std::move(plans);
        registrar = std::make_unique<XeapiKeyRegistrar>(
            ring, QStringLiteral("synthetic-device"),
            std::make_unique<HttpTransport>(std::move(manager)), [] { return Now; },
            std::move(random));
    }
};

struct Client {
    FakeManager* fake = nullptr;
    XeapiKeyRing ring;
    std::unique_ptr<SessionApiClient> client;
    explicit Client(QList<Plan> plans)
    {
        auto manager = std::make_unique<FakeManager>();
        fake = manager.get();
        fake->plans = std::move(plans);
        client =
            std::make_unique<SessionApiClient>(std::make_unique<HttpTransport>(std::move(manager)),
                                               QStringLiteral("synthetic-device"));
    }
};

Plan sessionPlan(QByteArray id = "sid-1", QByteArray key = "0123456789abcdef")
{
    Plan plan{EncryptedOk};
    plan.headers = {{"x-encr-ssid", id},
                    {"x-encr-sskey", key},
                    {"Set-Cookie", "MUSIC_A=synthetic-anon; Domain=.music.163.com; Path=/"}};
    return plan;
}
} // namespace

void XeapiTest::anonymousUsernameVector()
{
    // From the pinned upstream register_anonimous encoding (dev-only Node run).
    QCOMPARE(anonymousRegistrationUsername(u"0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123"),
             QStringLiteral("MDEyMzQ1Njc4OUFCQ0RFRjAxMjM0NTY3ODlBQkNERUYwMTIzNDU2Nzg5QUJDREVGMDEyM"
                            "yBYa2pIc2o5dnlXcTVRNDdCdXYyVWNnPT0="));
    QVERIFY(anonymousRegistrationUsername(u"").isEmpty());
    QVERIFY(anonymousRegistrationUsername(u"has space").isEmpty());
    QVERIFY(anonymousRegistrationUsername(u"设备").isEmpty());
}

void XeapiTest::signatureVerification()
{
    const auto signature = xeapiSignature("1", Nonce);
    QVERIFY(verifyXeapiSignature("1", Nonce, signature));
    QVERIFY(!verifyXeapiSignature("1", "2222222222222222", signature));
    QVERIFY(!verifyXeapiSignature("1", Nonce, signature.chopped(1)));
    QVERIFY(!verifyXeapiSignature("1", Nonce, {}));
}

void XeapiTest::transportProfileHeaders()
{
    auto manager = std::make_unique<FakeManager>();
    auto* fake = manager.get();
    HttpTransport transport(std::move(manager));
    QSignalSpy spy(&transport, &HttpTransport::completed);
    TransportRequest request{Protocol::Xeapi, QStringLiteral("/api/register/anonimous"), "B=1"};
    request.xeapiHeaders = {{"X-Client-Enc-State", "ENCRYPTED"}, {"x-deviceid", "device"}};
    QVERIFY(transport.post(request) > 0);
    QTRY_COMPARE(spy.size(), 1);
    const auto wire = fake->requests.first();
    QCOMPARE(wire.url(),
             QUrl(QStringLiteral("https://interface3.music.163.com/xeapi/register/anonimous")));
    QCOMPARE(wire.rawHeader("X-Client-Enc-State"), QByteArray("ENCRYPTED"));
    QCOMPARE(wire.rawHeader("x-deviceid"), QByteArray("device"));
}

void XeapiTest::transportRejectsForeignHeaders()
{
    const QList<std::pair<Protocol, QList<std::pair<QByteArray, QByteArray>>>> invalid{
        {Protocol::Eapi, {{"x-os", "android"}}},               // wrong protocol
        {Protocol::Xeapi, {{"Host", "evil.example"}}},         // not in profile
        {Protocol::Xeapi, {{"x-os", "a"}, {"x-os", "b"}}},     // duplicate
        {Protocol::Xeapi, {{"x-os", "bad\r\nInjected: yes"}}}, // control characters
        {Protocol::Xeapi, {{"x-os", ""}}}};
    auto manager = std::make_unique<FakeManager>();
    auto* fake = manager.get();
    HttpTransport transport(std::move(manager));
    QSignalSpy spy(&transport, &HttpTransport::completed);
    for (const auto& [protocol, headers] : invalid) {
        TransportRequest request{protocol, QStringLiteral("/api/test"), "B=1"};
        request.xeapiHeaders = headers;
        QVERIFY(transport.post(request) > 0);
    }
    QTRY_COMPARE(spy.size(), invalid.size());
    for (int i = 0; i < spy.size(); ++i) {
        QCOMPARE(qvariant_cast<TransportResult>(spy.at(i).at(1)).error,
                 TransportError::InvalidRequest);
    }
    QVERIFY(fake->requests.isEmpty());
}

void XeapiTest::transportSessionCapture_data()
{
    QTest::addColumn<int>("protocol");
    QTest::addColumn<int>("status");
    QTest::addColumn<QByteArray>("sessionId");
    QTest::addColumn<QByteArray>("sessionKey");
    QTest::addColumn<bool>("captured");
    QTest::newRow("xeapi-success")
        << int(Protocol::Xeapi) << 200 << QByteArray("sid") << QByteArray("key") << true;
    QTest::newRow("http-error") << int(Protocol::Xeapi) << 500 << QByteArray("sid")
                                << QByteArray("key") << false;
    QTest::newRow("other-protocol")
        << int(Protocol::Eapi) << 200 << QByteArray("sid") << QByteArray("key") << false;
    QTest::newRow("half-pair") << int(Protocol::Xeapi) << 200 << QByteArray("sid") << QByteArray()
                               << false;
    QTest::newRow("oversized") << int(Protocol::Xeapi) << 200 << QByteArray(257, 's')
                               << QByteArray("key") << false;
}

void XeapiTest::transportSessionCapture()
{
    QFETCH(int, protocol);
    QFETCH(int, status);
    QFETCH(QByteArray, sessionId);
    QFETCH(QByteArray, sessionKey);
    QFETCH(bool, captured);
    auto manager = std::make_unique<FakeManager>();
    Plan plan{"{}", status};
    plan.headers = {{"x-encr-ssid", sessionId}};
    if (!sessionKey.isEmpty()) {
        plan.headers.append({"x-encr-sskey", sessionKey});
    }
    manager->plans = {plan};
    HttpTransport transport(std::move(manager));
    QSignalSpy spy(&transport, &HttpTransport::completed);
    QVERIFY(transport.post({static_cast<Protocol>(protocol), QStringLiteral("/api/test"), "B=1"}) >
            0);
    QTRY_COMPARE(spy.size(), 1);
    const auto result = qvariant_cast<TransportResult>(spy.at(0).at(1));
    QCOMPARE(result.xeapiSessionId, captured ? sessionId : QByteArray());
    QCOMPARE(result.xeapiSessionKey, captured ? sessionKey : QByteArray());
}

void XeapiTest::registrarSuccess()
{
    const auto signature = xeapiSignature("1700000000123", Nonce);
    Registrar r({keyResponse(signature), keyResponse(signature, "1700000000123")});
    QSignalSpy finished(r.registrar.get(), &XeapiKeyRegistrar::finished);
    QSignalSpy changed(&r.ring, &XeapiKeyRing::changed);
    QVERIFY(r.registrar->refresh());
    QVERIFY(!r.registrar->refresh()); // single flight
    QTRY_COMPARE(finished.size(), 1);
    QCOMPARE(qvariant_cast<XeapiKeyResult>(finished.first().first()).error, XeapiKeyError::None);
    QCOMPARE(changed.size(), 1);
    QCOMPARE(r.ring.current()->version, testKey().version);
    QCOMPARE(r.ring.current()->publicKey, testKey().publicKey);
    QCOMPARE(r.ring.current()->sk, testKey().sk);

    const auto wire = r.fake->requests.first();
    QCOMPARE(wire.url(),
             QUrl(QStringLiteral(
                 "https://interface.music.163.com/api/gorilla/anti/crawler/security/key/get")));
    QCOMPARE(wire.rawHeader("Cookie"), QByteArray("deviceId=synthetic-device"));
    const auto body = r.fake->bodies.first();
    QCOMPARE(formValue(body, "nonce"), QByteArray(Nonce));
    QCOMPARE(formValue(body, "timestamp"), QByteArray::number(Now));
    QCOMPARE(formValue(body, "signature"), xeapiSignature(QByteArray::number(Now), Nonce));
    QCOMPARE(formValue(body, "deviceId"), QByteArray("synthetic-device"));
    QCOMPARE(formValue(body, "currentKeyVersion"), QByteArray());

    // A later refresh reports the held version; a numeric timestamp is accepted.
    QVERIFY(r.registrar->refresh());
    QTRY_COMPARE(finished.size(), 2);
    QCOMPARE(qvariant_cast<XeapiKeyResult>(finished.last().first()).error, XeapiKeyError::None);
    QCOMPARE(formValue(r.fake->bodies.last(), "currentKeyVersion"), QByteArray("v7"));
}

void XeapiTest::registrarRejects_data()
{
    QTest::addColumn<Plan>("plan");
    QTest::addColumn<int>("error");
    const auto good = xeapiSignature("1700000000123", Nonce);
    QTest::newRow("business-error")
        << Plan{R"({"code":500,"data":{}})"} << int(XeapiKeyError::Rejected);
    QTest::newRow("missing-data") << Plan{R"({"code":200})"} << int(XeapiKeyError::Rejected);
    QTest::newRow("replayed-nonce")
        << keyResponse(xeapiSignature("1700000000123", "2222222222222222"))
        << int(XeapiKeyError::Signature);
    QTest::newRow("wrong-timestamp")
        << keyResponse(good, "\"1700000000124\"") << int(XeapiKeyError::Signature);
    QTest::newRow("bad-ciphertext")
        << Plan{R"({"code":200,"data":{"encryptedData":"AAAA","signature":")" + good +
                R"(","timestamp":"1700000000123"}})"}
        << int(XeapiKeyError::InvalidKey);
    QTest::newRow("broken-json") << Plan{"{"} << int(XeapiKeyError::Decode);
    QTest::newRow("http-error") << Plan{"{}", 429} << int(XeapiKeyError::Transport);
}

void XeapiTest::registrarRejects()
{
    QFETCH(Plan, plan);
    QFETCH(int, error);
    Registrar r({plan});
    auto held = testKey();
    held.version = QStringLiteral("v6");
    QVERIFY(r.ring.replace(held));
    QSignalSpy finished(r.registrar.get(), &XeapiKeyRegistrar::finished);
    QSignalSpy changed(&r.ring, &XeapiKeyRing::changed);
    QVERIFY(r.registrar->refresh());
    QTRY_COMPARE(finished.size(), 1);
    QCOMPARE(int(qvariant_cast<XeapiKeyResult>(finished.first().first()).error), error);
    QCOMPARE(changed.size(), 0);
    QCOMPARE(r.ring.current()->version, QStringLiteral("v6")); // old state kept intact
    QVERIFY(!r.registrar->isRefreshing());
}

void XeapiTest::registrarLocalFailure()
{
    Registrar r({}, [](qsizetype) { return std::optional<QByteArray>(); });
    QSignalSpy finished(r.registrar.get(), &XeapiKeyRegistrar::finished);
    QVERIFY(r.registrar->refresh());
    QCOMPARE(finished.size(), 0); // still asynchronous
    QTRY_COMPARE(finished.size(), 1);
    QCOMPARE(qvariant_cast<XeapiKeyResult>(finished.first().first()).error,
             XeapiKeyError::InvalidRequest);
    QVERIFY(r.fake->requests.isEmpty());
}

void XeapiTest::clientMissingKey()
{
    Client c({sessionPlan()});
    const auto context = token();
    QVERIFY(c.client->advanceTo(context));
    QSignalSpy spy(c.client.get(), &SessionApiClient::completed);
    QVERIFY(c.client->submit(context, ApiOperation::AnonymousRegister) > 0); // no ring
    c.client->setXeapiKeyRing(&c.ring);                                      // empty ring
    QVERIFY(c.client->submit(context, ApiOperation::AnonymousRegister) > 0);
    QTRY_COMPARE(spy.size(), 2);
    for (int i = 0; i < 2; ++i) {
        QCOMPARE(qvariant_cast<ApiResult>(spy.at(i).at(2)).error, ApiError::MissingProtocolKey);
    }
    QVERIFY(c.fake->requests.isEmpty());
}

void XeapiTest::clientRequestAndSessionKey()
{
    Client c({sessionPlan(), {EncryptedOk}, {EncryptedOk}});
    QVERIFY(c.ring.replace(testKey()));
    c.client->setXeapiKeyRing(&c.ring);
    const auto context = token();
    QVERIFY(c.client->advanceTo(context));
    QSignalSpy spy(c.client.get(), &SessionApiClient::completed);
    QVERIFY(c.client->submit(context, ApiOperation::AnonymousRegister) > 0);
    QTRY_COMPARE(spy.size(), 1);
    const auto first = qvariant_cast<ApiResult>(spy.at(0).at(2));
    QCOMPARE(first.error, ApiError::None);
    QCOMPARE(first.decoded.businessCode, std::optional<int>(200));

    const auto wire = c.fake->requests.first();
    QCOMPARE(wire.url(),
             QUrl(QStringLiteral("https://interface3.music.163.com/xeapi/register/anonimous")));
    QCOMPARE(wire.rawHeader("X-Client-Enc-State"), QByteArray("ENCRYPTED"));
    QCOMPARE(wire.rawHeader("x-deviceid"), QByteArray("synthetic-device"));
    QCOMPARE(wire.rawHeader("x-sdeviceid"), QByteArray("synthetic-device"));
    QCOMPARE(wire.rawHeader("x-os"), QByteArray("android"));
    QVERIFY(wire.rawHeader("x-music-u").isEmpty());
    QVERIFY(wire.rawHeader("Cookie").contains("deviceId=synthetic-device"));
    QCOMPARE(formValue(c.fake->bodies.at(0), "R"), RoutingNoSession);
    QVERIFY(!formValue(c.fake->bodies.at(0), "B").isEmpty());
    QVERIFY(!formValue(c.fake->bodies.at(0), "S").isEmpty());

    // The session key from the response is used next, in the same epoch only.
    QVERIFY(c.client->submit(context, ApiOperation::AnonymousRegister) > 0);
    QTRY_COMPARE(spy.size(), 2);
    QCOMPARE(formValue(c.fake->bodies.at(1), "R"), RoutingSession);
    // The anonymous MUSIC_A cookie was not staged into the account jar.
    QVERIFY(!c.fake->requests.at(1).rawHeader("Cookie").contains("MUSIC_A"));

    const SessionToken next{context.coordinatorId, context.epoch + 1};
    QVERIFY(c.client->advanceTo(next));
    QVERIFY(c.client->submit(next, ApiOperation::AnonymousRegister) > 0);
    QTRY_COMPARE(spy.size(), 3);
    QCOMPARE(formValue(c.fake->bodies.at(2), "R"), RoutingNoSession);
}

void XeapiTest::clientStaleResponseKeepsSession()
{
    auto slow = sessionPlan("sid-old", "fedcba9876543210");
    slow.delayMs = 500; // well beyond QTRY's 50 ms polling step
    Client c({slow, {EncryptedOk}});
    QVERIFY(c.ring.replace(testKey()));
    c.client->setXeapiKeyRing(&c.ring);
    const auto context = token();
    QVERIFY(c.client->advanceTo(context));
    QSignalSpy spy(c.client.get(), &SessionApiClient::completed);
    QVERIFY(c.client->submit(context, ApiOperation::AnonymousRegister) > 0);
    QTRY_COMPARE(c.fake->requests.size(), 1);
    const SessionToken next{context.coordinatorId, context.epoch + 1};
    QVERIFY(c.client->advanceTo(next)); // the in-flight response is now stale
    QTRY_COMPARE(spy.size(), 1);
    QCOMPARE(qvariant_cast<ApiResult>(spy.at(0).at(2)).error, ApiError::StaleContext);
    QVERIFY(c.client->submit(next, ApiOperation::AnonymousRegister) > 0);
    QTRY_COMPARE(spy.size(), 2);
    QCOMPARE(formValue(c.fake->bodies.last(), "R"), RoutingNoSession);
}

QTEST_GUILESS_MAIN(XeapiTest)
#include "xeapi_test.moc"
