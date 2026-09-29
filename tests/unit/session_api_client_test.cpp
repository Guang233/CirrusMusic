#include "infrastructure/netease/api/session_api_client.h"
#include "infrastructure/netease/protocol/eapi_request_profile.h"

#include "../support/decode_eapi_request.h"
#include "../support/fake_http.h"

#include <QSignalSpy>
#include <QtTest>

using namespace cirrus::domain;
using namespace cirrus::netease;
using namespace cirrus::test;

class SessionApiClientTest : public QObject
{
    Q_OBJECT
  private slots:
    void profileFieldsAndEncoding();
    void invalidProfiles();
    void cookieChainAndClearing();
    void authorizedCookiesDoNotAutoPromote();
    void failedResponsesDoNotStageCookies_data();
    void failedResponsesDoNotStageCookies();
    void staleBeforeDispatch();
    void lateSuccessCannotRetag();
    void reentrantAdvance();
    void cancelBeforeDispatch();
    void cancelAndSwitchInFlight();
    void boundedConcurrency();
    void closedRegistry();
};

static SessionToken token() { return {QUuid::createUuid(), 1}; }
static Plan keyPlan() { return {R"({"code":200,"unikey":"synthetic-key"})"}; }
static ApiResult result(const QSignalSpy& spy, int index = 0)
{
    return qvariant_cast<ApiResult>(spy.at(index).at(2));
}

void SessionApiClientTest::profileFieldsAndEncoding()
{
    const EapiRequestMetadata metadata{QStringLiteral("synthetic-device"),
                                       1712345678000,
                                       7,
                                       "a+b /!*'()%",
                                       "synthetic-user",
                                       "synthetic-guest",
                                       "synthetic-nmtid"};
    const auto prepared =
        prepareEapiRequest(u"/api/login/qrcode/unikey",
                           {{QStringLiteral("type"), 3},
                            {QStringLiteral("e_r"), true},
                            {QStringLiteral("header"),
                             QJsonObject{{QStringLiteral("MUSIC_U"), QStringLiteral("wrong")}}}},
                           metadata);
    QVERIFY(prepared.ok());
    const auto wire = decodeEapiRequest(prepared.formBody);
    QVERIFY(wire);
    QCOMPARE(wire->logicalPath, QStringLiteral("/api/login/qrcode/unikey"));
    QCOMPARE(wire->payload.value(QStringLiteral("e_r")), QJsonValue(false));
    QCOMPARE(wire->payload.value(QStringLiteral("type")), QJsonValue(3));
    const auto header = wire->payload.value(QStringLiteral("header")).toObject();
    QCOMPARE(header.value(QStringLiteral("__csrf")).toString(), QStringLiteral("a+b /!*'()%"));
    QCOMPARE(header.value(QStringLiteral("MUSIC_U")).toString(), QStringLiteral("synthetic-user"));
    QCOMPARE(header.value(QStringLiteral("MUSIC_A")).toString(), QStringLiteral("synthetic-guest"));
    QCOMPARE(header.value(QStringLiteral("NMTID")).toString(), QStringLiteral("synthetic-nmtid"));
    QCOMPARE(header.value(QStringLiteral("requestId")).toString(),
             QStringLiteral("1712345678000_0007"));
    QCOMPARE(header.value(QStringLiteral("buildver")).toString(), QStringLiteral("1712345678"));
    QCOMPARE(header.value(QStringLiteral("deviceId")).toString(), metadata.deviceId);
    QCOMPARE(header.value(QStringLiteral("os")).toString(), QStringLiteral("pc"));
    QCOMPARE(header.value(QStringLiteral("appver")).toString(), QStringLiteral("3.1.17.204416"));
    QVERIFY(prepared.cookieHeader.contains("__csrf=a%2Bb%20%2F!*'()%25"));
    QVERIFY(!prepared.cookieHeader.contains("synthetic-guest+"));
    QCOMPARE(prepared.userAgent, QByteArray("NeteaseMusic 9.0.90/5038 (iPhone; iOS 16.2; zh_CN)"));
}

void SessionApiClientTest::invalidProfiles()
{
    const EapiRequestMetadata valid{QStringLiteral("synthetic"), 1712345678000, 0};
    QList<EapiRequestMetadata> invalid;
    auto input = valid;
    input.deviceId.clear();
    invalid.append(input);
    input = valid;
    input.deviceId = QStringLiteral("bad\r\nvalue");
    invalid.append(input);
    input = valid;
    input.timestampMs = 0;
    invalid.append(input);
    input = valid;
    input.requestSuffix = 1000;
    invalid.append(input);
    input = valid;
    input.csrf = QByteArray::fromHex("ff");
    invalid.append(input);
    input = valid;
    input.musicU = "bad\nvalue";
    invalid.append(input);
    input = valid;
    input.musicU = QByteArray(4097, 'x');
    invalid.append(input);
    input = valid;
    input.musicU = QByteArray(4096, '%');
    input.musicA = QByteArray(4096, '%');
    invalid.append(input);
    for (const auto& metadata : invalid) {
        const auto prepared = prepareEapiRequest(u"/api/login/qrcode/unikey", {}, metadata);
        QCOMPARE(prepared.error, CryptoError::InvalidInput);
        QVERIFY(prepared.formBody.isEmpty());
        QVERIFY(prepared.cookieHeader.isEmpty());
        QVERIFY(prepared.userAgent.isEmpty());
    }
}

void SessionApiClientTest::cookieChainAndClearing()
{
    auto manager = std::make_unique<FakeManager>();
    auto* fake = manager.get();
    auto first = keyPlan();
    first.headers = {{"Set-Cookie", "__csrf=synthetic-csrf; Domain=.music.163.com; Path=/; Secure"},
                     {"Set-Cookie", "NMTID=synthetic-nmtid; Path=/eapi/login/qrcode; Secure"}};
    fake->plans = {first, {R"({"code":801})"}, keyPlan()};
    SessionApiClient client(std::make_unique<HttpTransport>(std::move(manager)),
                            QStringLiteral("synthetic-device"));
    const auto context = token();
    QVERIFY(client.advanceTo(context));
    QSignalSpy spy(&client, &SessionApiClient::completed);
    const auto firstId = client.submit(context, ApiOperation::QrKey);
    QCOMPARE(spy.size(), 0);
    QTRY_COMPARE(spy.size(), 1);
    QCOMPARE(spy.at(0).at(0).toULongLong(), firstId);
    QCOMPARE(result(spy).error, ApiError::None);
    const auto checkId = client.submit(context, ApiOperation::QrCheck,
                                       {{QStringLiteral("key"), QStringLiteral("synthetic-key")}});
    QTRY_COMPARE(spy.size(), 2);
    QVERIFY(checkId != firstId);
    QCOMPARE(
        fake->requests.at(1).url(),
        QUrl(QStringLiteral("https://interfacepc.music.163.com/eapi/login/qrcode/client/login")));
    QVERIFY(fake->requests.at(1).rawHeader("Cookie").contains("__csrf=synthetic-csrf"));
    QVERIFY(fake->requests.at(1).rawHeader("Cookie").contains("NMTID=synthetic-nmtid"));
    const auto wire = decodeEapiRequest(fake->bodies.at(1));
    QVERIFY(wire);
    QCOMPARE(wire->payload.value(QStringLiteral("key")).toString(),
             QStringLiteral("synthetic-key"));
    QCOMPARE(wire->payload.value(QStringLiteral("header"))
                 .toObject()
                 .value(QStringLiteral("__csrf"))
                 .toString(),
             QStringLiteral("synthetic-csrf"));
    const SessionToken next{context.coordinatorId, context.epoch + 1};
    QVERIFY(client.advanceTo(next));
    QVERIFY(!client.advanceTo(context));
    QVERIFY(!client.advanceTo(token()));
    QVERIFY(client.submit(next, ApiOperation::QrKey) > checkId);
    QTRY_COMPARE(spy.size(), 3);
    QVERIFY(!fake->requests.last().rawHeader("Cookie").contains("synthetic-csrf"));
    QVERIFY(!fake->requests.last().rawHeader("Cookie").contains("synthetic-nmtid"));
}

void SessionApiClientTest::failedResponsesDoNotStageCookies_data()
{
    QTest::addColumn<QByteArray>("body");
    QTest::addColumn<int>("http");
    QTest::addColumn<int>("network");
    QTest::addColumn<QByteArray>("cookie");
    QTest::addColumn<int>("error");
    const QByteArray valid = R"({"code":200,"unikey":"synthetic-key"})";
    const QByteArray poison = "MUSIC_U=synthetic-poison; Domain=.music.163.com; Path=/; Secure";
    QTest::newRow("http-error") << valid << 429 << int(QNetworkReply::NoError) << poison
                                << int(ApiError::Transport);
    QTest::newRow("redirect") << valid << 302 << 0 << poison << int(ApiError::Transport);
    QTest::newRow("tls-error") << valid << 0 << int(QNetworkReply::SslHandshakeFailedError)
                               << poison << int(ApiError::Transport);
    QTest::newRow("business-error")
        << QByteArray(R"({"code":429})") << 200 << 0 << poison << int(ApiError::None);
    QTest::newRow("missing-code") << QByteArray(R"({"unikey":"synthetic-key"})") << 200 << 0
                                  << poison << int(ApiError::None);
    QTest::newRow("wrapper-not-official")
        << QByteArray(R"({"code":200,"data":{"unikey":"synthetic-key"}})") << 200 << 0 << poison
        << int(ApiError::None);
    QTest::newRow("broken-json") << QByteArray("{broken") << 200 << 0 << poison
                                 << int(ApiError::Decode);
    QTest::newRow("array-root") << QByteArray("[]") << 200 << 0 << poison << int(ApiError::Decode);
    QTest::newRow("invalid-utf8") << QByteArray::fromHex("ff") << 200 << 0 << poison
                                  << int(ApiError::Decode);
    QTest::newRow("response-limit")
        << QByteArray(64 * 1024 + 1, 'x') << 200 << 0 << poison << int(ApiError::Transport);
    QTest::newRow("wide-cookie-domain")
        << valid << 200 << 0 << QByteArray("MUSIC_U=synthetic-poison; Domain=.163.com; Path=/")
        << int(ApiError::Cookie);
}

void SessionApiClientTest::failedResponsesDoNotStageCookies()
{
    QFETCH(QByteArray, body);
    QFETCH(int, http);
    QFETCH(int, network);
    QFETCH(QByteArray, cookie);
    QFETCH(int, error);
    auto manager = std::make_unique<FakeManager>();
    auto* fake = manager.get();
    Plan first{
        body, http, 0, static_cast<QNetworkReply::NetworkError>(network), {{"Set-Cookie", cookie}}};
    fake->plans = {first, keyPlan()};
    SessionApiClient client(std::make_unique<HttpTransport>(std::move(manager)),
                            QStringLiteral("synthetic-device"));
    const auto context = token();
    QVERIFY(client.advanceTo(context));
    QSignalSpy spy(&client, &SessionApiClient::completed);
    QVERIFY(client.submit(context, ApiOperation::QrKey) > 0);
    QTRY_COMPARE(spy.size(), 1);
    QCOMPARE(int(result(spy).error), error);
    QVERIFY(client.submit(context, ApiOperation::QrKey) > 0);
    QTRY_COMPARE(spy.size(), 2);
    QVERIFY(!fake->requests.last().rawHeader("Cookie").contains("synthetic-poison"));
}

void SessionApiClientTest::staleBeforeDispatch()
{
    auto manager = std::make_unique<FakeManager>();
    auto* fake = manager.get();
    fake->plans = {keyPlan()};
    SessionApiClient client(std::make_unique<HttpTransport>(std::move(manager)),
                            QStringLiteral("synthetic-device"));
    const auto old = token();
    const SessionToken current{old.coordinatorId, old.epoch + 1};
    QVERIFY(client.advanceTo(old));
    QSignalSpy spy(&client, &SessionApiClient::completed);
    const auto staleId = client.submit(old, ApiOperation::QrKey);
    QVERIFY(client.advanceTo(current));
    const auto currentId = client.submit(current, ApiOperation::QrKey);
    QTRY_COMPARE(spy.size(), 2);
    QCOMPARE(spy.at(0).at(0).toULongLong(), staleId);
    QCOMPARE(qvariant_cast<SessionToken>(spy.at(0).at(1)), old);
    QCOMPARE(result(spy).error, ApiError::StaleContext);
    QVERIFY(result(spy).decoded.object.isEmpty());
    QCOMPARE(spy.at(1).at(0).toULongLong(), currentId);
    QCOMPARE(result(spy, 1).error, ApiError::None);
    QCOMPARE(fake->requests.size(), 1);
}

void SessionApiClientTest::lateSuccessCannotRetag()
{
    auto manager = std::make_unique<FakeManager>();
    auto* fake = manager.get();
    auto first = keyPlan();
    first.headers = {
        {"Set-Cookie", "MUSIC_U=late-old-account; Domain=.music.163.com; Path=/; Secure"}};
    fake->plans = {first, keyPlan()};
    auto transport = std::make_unique<HttpTransport>(std::move(manager));
    const auto old = token();
    const SessionToken current{old.coordinatorId, old.epoch + 1};
    SessionApiClient* observerClient = nullptr;
    bool switchBeforeDelivery = true;
    // Registered BEFORE the client: transport has already completed successfully
    // and removed its pending entry, so cancel can no longer change this result.
    connect(transport.get(), &HttpTransport::completed, this,
            [&](quint64, const TransportResult& response) {
                if (switchBeforeDelivery) {
                    switchBeforeDelivery = false;
                    QCOMPARE(response.error, TransportError::None);
                    QVERIFY(observerClient->advanceTo(current));
                }
            });
    SessionApiClient client(std::move(transport), QStringLiteral("synthetic-device"));
    observerClient = &client;
    QVERIFY(client.advanceTo(old));
    QSignalSpy spy(&client, &SessionApiClient::completed);
    const auto oldId = client.submit(old, ApiOperation::QrKey);
    QTRY_COMPARE(spy.size(), 1);
    QCOMPARE(spy.at(0).at(0).toULongLong(), oldId);
    QCOMPARE(qvariant_cast<SessionToken>(spy.at(0).at(1)), old);
    QCOMPARE(result(spy).error, ApiError::StaleContext);
    QVERIFY(client.submit(current, ApiOperation::QrKey) > oldId);
    QTRY_COMPARE(spy.size(), 2);
    QVERIFY(!fake->requests.last().rawHeader("Cookie").contains("late-old-account"));
}

void SessionApiClientTest::reentrantAdvance()
{
    SessionApiClient client(QStringLiteral("synthetic-device"));
    const auto old = token();
    const SessionToken middle{old.coordinatorId, 2};
    const SessionToken newest{old.coordinatorId, 3};
    QVERIFY(client.advanceTo(old));
    connect(&client, &SessionApiClient::contextAdvanced, this, [&](SessionToken context) {
        if (context == middle) {
            QVERIFY(client.advanceTo(newest));
        }
    });
    QVERIFY(!client.advanceTo(middle));
    QVERIFY(client.accepts(newest));
    QVERIFY(!client.accepts(middle));
}

void SessionApiClientTest::cancelBeforeDispatch()
{
    auto manager = std::make_unique<FakeManager>();
    auto* fake = manager.get();
    SessionApiClient client(std::make_unique<HttpTransport>(std::move(manager)),
                            QStringLiteral("synthetic-device"));
    const auto context = token();
    QVERIFY(client.advanceTo(context));
    QSignalSpy spy(&client, &SessionApiClient::completed);
    const auto id = client.submit(context, ApiOperation::QrKey);
    client.cancel(id);
    QTRY_COMPARE(spy.size(), 1);
    QCOMPARE(result(spy).error, ApiError::Canceled);
    QVERIFY(fake->requests.isEmpty());
    client.cancel(id);
    QCoreApplication::processEvents();
    QCOMPARE(spy.size(), 1);
}

void SessionApiClientTest::cancelAndSwitchInFlight()
{
    auto manager = std::make_unique<FakeManager>();
    auto* fake = manager.get();
    fake->plans = {{"", 0, -1}, keyPlan()};
    SessionApiClient client(std::make_unique<HttpTransport>(std::move(manager)),
                            QStringLiteral("synthetic-device"));
    const auto old = token();
    const SessionToken current{old.coordinatorId, 2};
    QVERIFY(client.advanceTo(old));
    QSignalSpy spy(&client, &SessionApiClient::completed);
    QVERIFY(client.submit(old, ApiOperation::QrKey) > 0);
    QTRY_COMPARE(fake->requests.size(), 1);
    QVERIFY(client.advanceTo(current));
    QTRY_COMPARE(spy.size(), 1);
    QCOMPARE(result(spy).error, ApiError::StaleContext);
    QVERIFY(client.submit(current, ApiOperation::QrKey) > 0);
    QTRY_COMPARE(spy.size(), 2);
    QCOMPARE(result(spy, 1).error, ApiError::None);
}

void SessionApiClientTest::boundedConcurrency()
{
    auto manager = std::make_unique<FakeManager>();
    auto* fake = manager.get();
    fake->plans = QList<Plan>(32, Plan{{}, 0, -1});
    SessionApiClient client(std::make_unique<HttpTransport>(std::move(manager)),
                            QStringLiteral("synthetic-device"));
    const auto context = token();
    QVERIFY(client.advanceTo(context));
    QSignalSpy spy(&client, &SessionApiClient::completed);
    QList<quint64> ids;
    for (int i = 0; i < 33; ++i) {
        ids.append(client.submit(context, ApiOperation::QrKey));
    }
    QTRY_COMPARE(fake->requests.size(), 32);
    QTRY_COMPARE(spy.size(), 1);
    QCOMPARE(spy.at(0).at(0).toULongLong(), ids.last());
    QCOMPARE(result(spy).error, ApiError::Busy);
    for (const auto id : ids) {
        client.cancel(id);
    }
    QTRY_COMPARE(spy.size(), 33);
}

void SessionApiClientTest::closedRegistry()
{
    auto manager = std::make_unique<FakeManager>();
    auto* fake = manager.get();
    SessionApiClient client(std::make_unique<HttpTransport>(std::move(manager)),
                            QStringLiteral("synthetic-device"));
    const auto context = token();
    QVERIFY(client.advanceTo(context));
    QSignalSpy spy(&client, &SessionApiClient::completed);
    QVERIFY(client.submit(context, static_cast<ApiOperation>(999)) > 0);
    QVERIFY(client.submit(context, ApiOperation::QrKey,
                          {{QStringLiteral("domain"), QStringLiteral("https://evil.example")}}) >
            0);
    QVERIFY(client.submit(context, ApiOperation::QrCheck,
                          {{QStringLiteral("key"), QString(513, u'x')}}) > 0);
    QVERIFY(client.submit(context, ApiOperation::QrCheck, {{QStringLiteral("key"), 1}}) > 0);
    QTRY_COMPARE(spy.size(), 4);
    for (int i = 0; i < 4; ++i) {
        QCOMPARE(result(spy, i).error, ApiError::InvalidRequest);
    }
    QVERIFY(fake->requests.isEmpty());
}

void SessionApiClientTest::authorizedCookiesDoNotAutoPromote()
{
    auto manager = std::make_unique<FakeManager>();
    auto* fake = manager.get();
    Plan authorized{R"({"code":803})"};
    authorized.headers = {
        {"Set-Cookie",
         "MUSIC_U=synthetic-staged-user; Domain=.music.163.com; Path=/; Secure; HttpOnly"}};
    fake->plans = {authorized, keyPlan(), keyPlan()};
    SessionApiClient client(std::make_unique<HttpTransport>(std::move(manager)),
                            QStringLiteral("synthetic-device"));
    const auto attempt = token();
    QVERIFY(client.advanceTo(attempt));
    QSignalSpy spy(&client, &SessionApiClient::completed);
    QVERIFY(client.submit(attempt, ApiOperation::QrCheck,
                          {{QStringLiteral("key"), QStringLiteral("synthetic-key")}}) > 0);
    QTRY_COMPARE(spy.size(), 1);
    QCOMPARE(result(spy).error, ApiError::None);
    QCOMPARE(result(spy).decoded.businessCode, std::optional<int>(803));
    QVERIFY(client.submit(attempt, ApiOperation::QrKey) > 0);
    QTRY_COMPARE(spy.size(), 2);
    QVERIFY(fake->requests.last().rawHeader("Cookie").contains("MUSIC_U=synthetic-staged-user"));
    // A plain context transition is NOT an authorized credential handoff.
    const SessionToken next{attempt.coordinatorId, attempt.epoch + 1};
    QVERIFY(client.advanceTo(next));
    QVERIFY(client.submit(next, ApiOperation::QrKey) > 0);
    QTRY_COMPARE(spy.size(), 3);
    QVERIFY(!fake->requests.last().rawHeader("Cookie").contains("synthetic-staged-user"));
}

QTEST_GUILESS_MAIN(SessionApiClientTest)
#include "session_api_client_test.moc"
