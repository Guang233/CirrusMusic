#include "infrastructure/netease/network/http_transport.h"

#include "../support/fake_http.h"

#include <QElapsedTimer>
#include <QSignalSpy>
#include <QtTest>

using namespace cirrus::netease;
using namespace cirrus::test;

class HttpTransportTest : public QObject
{
    Q_OBJECT
  private slots:
    void successAndPolicy();
    void outcomes_data();
    void outcomes();
    void cancelBeforeStart();
    void cancelInFlight();
    void invalidRequest();
    void outOfOrder();
    void destruction();
    void lostConnectionResentForReads();
    void explicitHeaders();
    void invalidHeaders_data();
    void invalidHeaders();
    void responseCookieLimits_data();
    void responseCookieLimits();
};

static TransportRequest request()
{
    return {Protocol::Eapi, QStringLiteral("/api/test"), "params=synthetic", 1000, 1024};
}

static TransportResult result(const QSignalSpy& spy, int index = 0)
{
    return qvariant_cast<TransportResult>(spy.at(index).at(1));
}

void HttpTransportTest::successAndPolicy()
{
    auto manager = std::make_unique<FakeManager>();
    auto* fake = manager.get();
    fake->plans.append({QByteArray("a\0b", 3)});
    HttpTransport transport(std::move(manager));
    QSignalSpy spy(&transport, &HttpTransport::completed);
    const auto id = transport.post(request());
    QCOMPARE(spy.size(), 0);
    QTRY_COMPARE(spy.size(), 1);
    QCOMPARE(spy.at(0).at(0).toULongLong(), id);
    QCOMPARE(result(spy).error, TransportError::None);
    QCOMPARE(result(spy).body, QByteArray("a\0b", 3));
    QCOMPARE(result(spy).httpStatus, 200);
    QCOMPARE(fake->bodies.first(), QByteArray("params=synthetic"));
    const auto wire = fake->requests.first();
    QCOMPARE(wire.url(), QUrl(QStringLiteral("https://interfacepc.music.163.com/eapi/test")));
    QCOMPARE(wire.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt(),
             int(QNetworkRequest::ManualRedirectPolicy));
    QCOMPARE(wire.attribute(QNetworkRequest::CookieLoadControlAttribute).toInt(),
             int(QNetworkRequest::Manual));
    QCOMPARE(wire.attribute(QNetworkRequest::CookieSaveControlAttribute).toInt(),
             int(QNetworkRequest::Manual));
    QCOMPARE(wire.attribute(QNetworkRequest::AuthenticationReuseAttribute).toInt(),
             int(QNetworkRequest::Manual));
}

void HttpTransportTest::outcomes_data()
{
    QTest::addColumn<int>("status");
    QTest::addColumn<int>("network");
    QTest::addColumn<int>("delay");
    QTest::addColumn<int>("limit");
    QTest::addColumn<int>("expected");
    QTest::newRow("http-error") << 429 << int(QNetworkReply::UnknownContentError) << 0 << 1024
                                << int(TransportError::HttpStatus);
    QTest::newRow("redirect") << 302 << 0 << 0 << 1024 << int(TransportError::RedirectBlocked);
    QTest::newRow("tls-error") << 0 << int(QNetworkReply::SslHandshakeFailedError) << 0 << 1024
                               << int(TransportError::Network);
    QTest::newRow("deadline") << 0 << 0 << -1 << 1024 << int(TransportError::Timeout);
    QTest::newRow("oversize") << 200 << 0 << 0 << 1 << int(TransportError::ResponseTooLarge);
}

void HttpTransportTest::outcomes()
{
    QFETCH(int, status);
    QFETCH(int, network);
    QFETCH(int, delay);
    QFETCH(int, limit);
    QFETCH(int, expected);
    auto manager = std::make_unique<FakeManager>();
    auto* fake = manager.get();
    fake->plans.append({"body", status, delay, static_cast<QNetworkReply::NetworkError>(network)});
    HttpTransport transport(std::move(manager));
    QSignalSpy spy(&transport, &HttpTransport::completed);
    auto req = request();
    req.timeoutMs = 30;
    req.responseLimit = limit;
    const auto id = transport.post(req);
    QTRY_COMPARE(spy.size(), 1);
    QCOMPARE(int(result(spy).error), expected);
    if (expected == int(TransportError::ResponseTooLarge)) {
        QVERIFY(result(spy).body.isEmpty());
    }
    if (status == 429) {
        QCOMPARE(result(spy).body, QByteArray("body"));
        QCOMPARE(result(spy).retryAfter, QByteArray("3"));
    }
    transport.cancel(id);
    QTest::qWait(40);
    QCOMPARE(spy.size(), 1);
    QCOMPARE(fake->requests.size(), 1); // no retry, redirect or TLS-ignore fallback
}

void HttpTransportTest::cancelBeforeStart()
{
    auto manager = std::make_unique<FakeManager>();
    auto* fake = manager.get();
    HttpTransport transport(std::move(manager));
    QSignalSpy spy(&transport, &HttpTransport::completed);
    const auto id = transport.post(request());
    transport.cancel(id);
    QTRY_COMPARE(spy.size(), 1);
    QCOMPARE(result(spy).error, TransportError::Canceled);
    QVERIFY(fake->requests.isEmpty());
}

void HttpTransportTest::cancelInFlight()
{
    auto manager = std::make_unique<FakeManager>();
    auto* fake = manager.get();
    fake->plans.append({{}, 0, -1});
    HttpTransport transport(std::move(manager));
    QSignalSpy spy(&transport, &HttpTransport::completed);
    const auto id = transport.post(request());
    QTRY_COMPARE(fake->requests.size(), 1);
    transport.cancel(id);
    QTRY_COMPARE(spy.size(), 1);
    QCOMPARE(result(spy).error, TransportError::Canceled);
}

void HttpTransportTest::invalidRequest()
{
    auto manager = std::make_unique<FakeManager>();
    auto* fake = manager.get();
    HttpTransport transport(std::move(manager));
    QSignalSpy spy(&transport, &HttpTransport::completed);
    auto req = request();
    req.logicalPath = QStringLiteral("https://evil.example/api/test");
    const auto id = transport.post(req);
    QVERIFY(id > 0);
    QTRY_COMPARE(spy.size(), 1);
    QCOMPARE(result(spy).error, TransportError::InvalidRequest);
    QVERIFY(fake->requests.isEmpty());
}

void HttpTransportTest::outOfOrder()
{
    auto manager = std::make_unique<FakeManager>();
    manager->plans = {{"slow", 200, 40}, {"fast", 200, 0}};
    HttpTransport transport(std::move(manager));
    QSignalSpy spy(&transport, &HttpTransport::completed);
    const auto slow = transport.post(request());
    const auto fast = transport.post(request());
    QTRY_COMPARE(spy.size(), 2);
    QCOMPARE(spy.at(0).at(0).toULongLong(), fast);
    QCOMPARE(spy.at(1).at(0).toULongLong(), slow);
    QCOMPARE(result(spy).body, QByteArray("fast"));
    QCOMPARE(result(spy, 1).body, QByteArray("slow"));
}

void HttpTransportTest::destruction()
{
    auto manager = std::make_unique<FakeManager>();
    auto* fake = manager.get();
    fake->plans.append({{}, 0, -1});
    auto transport = std::make_unique<HttpTransport>(std::move(manager));
    int completions = 0;
    connect(transport.get(), &HttpTransport::completed, this, [&completions] { ++completions; });
    const auto id = transport->post(request());
    QVERIFY(id > 0);
    QTRY_COMPARE(fake->requests.size(), 1);
    transport.reset();
    QCoreApplication::processEvents();
    QCOMPARE(completions, 0);
}

void HttpTransportTest::explicitHeaders()
{
    auto manager = std::make_unique<FakeManager>();
    auto* fake = manager.get();
    Plan plan;
    plan.headers = {{"Set-Cookie", "one=1; Expires=Wed, 21 Oct 2037 07:28:00 GMT; Path=/"},
                    {"set-cookie", "two=2; Path=/"}};
    fake->plans.append(plan);
    HttpTransport transport(std::move(manager));
    QSignalSpy spy(&transport, &HttpTransport::completed);
    auto req = request();
    req.cookieHeader = "MUSIC_U=synthetic";
    req.userAgent = "Cirrus-offline-test";
    const auto id = transport.post(req);
    QVERIFY(id > 0);
    QTRY_COMPARE(spy.size(), 1);
    QCOMPARE(fake->requests.first().rawHeader("Cookie"), req.cookieHeader);
    QCOMPARE(fake->requests.first().rawHeader("User-Agent"), req.userAgent);
    QCOMPARE(result(spy).setCookieHeaders,
             QList<QByteArray>(
                 {"one=1; Expires=Wed, 21 Oct 2037 07:28:00 GMT; Path=/", "two=2; Path=/"}));
}

void HttpTransportTest::invalidHeaders_data()
{
    QTest::addColumn<QByteArray>("cookie");
    QTest::addColumn<QByteArray>("ua");
    QTest::newRow("cookie-injection") << QByteArray("x=y\r\nHost: evil.example") << QByteArray{};
    QTest::newRow("cookie-limit") << QByteArray(16 * 1024 + 1, 'a') << QByteArray{};
    QTest::newRow("ua-injection") << QByteArray{} << QByteArray("test\nCookie: x=y");
    QTest::newRow("ua-limit") << QByteArray{} << QByteArray(1025, 'a');
    QTest::newRow("nul") << QByteArray("x=y\0z", 5) << QByteArray{};
}

void HttpTransportTest::invalidHeaders()
{
    QFETCH(QByteArray, cookie);
    QFETCH(QByteArray, ua);
    auto manager = std::make_unique<FakeManager>();
    auto* fake = manager.get();
    HttpTransport transport(std::move(manager));
    QSignalSpy spy(&transport, &HttpTransport::completed);
    auto req = request();
    req.cookieHeader = cookie;
    req.userAgent = ua;
    const auto id = transport.post(req);
    QVERIFY(id > 0);
    QTRY_COMPARE(spy.size(), 1);
    QCOMPARE(result(spy).error, TransportError::InvalidRequest);
    QVERIFY(fake->requests.isEmpty());
}

void HttpTransportTest::responseCookieLimits_data()
{
    QTest::addColumn<int>("count");
    QTest::addColumn<int>("size");
    QTest::newRow("single") << 1 << 4097;
    QTest::newRow("count") << 65 << 8;
    QTest::newRow("total") << 17 << 4096;
}

void HttpTransportTest::responseCookieLimits()
{
    QFETCH(int, count);
    QFETCH(int, size);
    auto manager = std::make_unique<FakeManager>();
    Plan plan;
    plan.headers.clear();
    for (int i = 0; i < count; ++i) {
        plan.headers.append({"Set-Cookie", QByteArray("x=") + QByteArray(size - 2, 'a')});
    }
    manager->plans.append(plan);
    HttpTransport transport(std::move(manager));
    QSignalSpy spy(&transport, &HttpTransport::completed);
    const auto id = transport.post(request());
    QVERIFY(id > 0);
    QTRY_COMPARE(spy.size(), 1);
    QCOMPARE(result(spy).error, TransportError::ResponseHeadersTooLarge);
    QVERIFY(result(spy).setCookieHeaders.isEmpty());
    QVERIFY(result(spy).body.isEmpty());
}

void HttpTransportTest::lostConnectionResentForReads()
{
    const auto lost = [] {
        Plan plan{"", 0};
        plan.error = QNetworkReply::ProtocolFailure;
        plan.headers = {};
        return plan;
    };
    // A marked read: once more over HTTP/1.1, then its answer.
    {
        auto manager = std::make_unique<FakeManager>();
        auto* fake = manager.get();
        fake->plans = {lost(), Plan{"fine"}};
        HttpTransport transport(std::move(manager));
        QSignalSpy spy(&transport, &HttpTransport::completed);
        auto read = request();
        read.retryLostConnection = true;
        const auto id = transport.post(read);
        QTRY_COMPARE(spy.size(), 1);
        QCOMPARE(spy.at(0).at(0).toULongLong(), id);
        QCOMPARE(result(spy).error, TransportError::None);
        QCOMPARE(result(spy).body, QByteArray("fine"));
        QCOMPARE(fake->requests.size(), 2);
        QVERIFY(
            fake->requests.at(0).attribute(QNetworkRequest::Http2AllowedAttribute, true).toBool());
        QVERIFY(
            !fake->requests.at(1).attribute(QNetworkRequest::Http2AllowedAttribute, true).toBool());
        QCOMPARE(fake->bodies.at(1), fake->bodies.at(0));
    }
    // Lost twice: a second later once more, over HTTP/1.1.
    {
        auto manager = std::make_unique<FakeManager>();
        auto* fake = manager.get();
        fake->plans = {lost(), lost(), Plan{"fine"}};
        HttpTransport transport(std::move(manager));
        QSignalSpy spy(&transport, &HttpTransport::completed);
        auto read = request();
        read.retryLostConnection = true;
        QElapsedTimer clock;
        clock.start();
        (void)transport.post(read);
        QTRY_COMPARE(fake->requests.size(), 2);
        QTest::qWait(500);
        QCOMPARE(fake->requests.size(), 2); // not yet
        QCOMPARE(spy.size(), 0);
        QTRY_COMPARE(spy.size(), 1);
        QVERIFY(clock.elapsed() >= 1000);
        QCOMPARE(result(spy).error, TransportError::None);
        QCOMPARE(result(spy).body, QByteArray("fine"));
        QCOMPARE(fake->requests.size(), 3);
        QVERIFY(
            !fake->requests.at(2).attribute(QNetworkRequest::Http2AllowedAttribute, true).toBool());
        QCOMPARE(fake->bodies.at(2), fake->bodies.at(0));
    }
    // Lost three times: the failure, no fourth try.
    {
        auto manager = std::make_unique<FakeManager>();
        auto* fake = manager.get();
        fake->plans = {lost(), lost(), lost(), Plan{"never"}};
        HttpTransport transport(std::move(manager));
        QSignalSpy spy(&transport, &HttpTransport::completed);
        auto read = request();
        read.retryLostConnection = true;
        (void)transport.post(read);
        QTRY_COMPARE(spy.size(), 1);
        QCOMPARE(result(spy).error, TransportError::Network);
        QTest::qWait(20);
        QCOMPARE(fake->requests.size(), 3);
    }
    // Canceled while waiting for the second resend: canceled at once, never sent.
    {
        auto manager = std::make_unique<FakeManager>();
        auto* fake = manager.get();
        fake->plans = {lost(), lost(), Plan{"never"}};
        HttpTransport transport(std::move(manager));
        QSignalSpy spy(&transport, &HttpTransport::completed);
        auto read = request();
        read.retryLostConnection = true;
        const auto id = transport.post(read);
        QTRY_COMPARE(fake->requests.size(), 2);
        QTest::qWait(20);
        transport.cancel(id);
        QTRY_COMPARE(spy.size(), 1);
        QCOMPARE(spy.at(0).at(0).toULongLong(), id);
        QCOMPARE(result(spy).error, TransportError::Canceled);
        QTest::qWait(1200);
        QCOMPARE(fake->requests.size(), 2);
        QCOMPARE(spy.size(), 1);
    }
    // Not marked (writes): never again.
    {
        auto manager = std::make_unique<FakeManager>();
        auto* fake = manager.get();
        fake->plans = {lost(), Plan{"never"}};
        HttpTransport transport(std::move(manager));
        QSignalSpy spy(&transport, &HttpTransport::completed);
        (void)transport.post(request());
        QTRY_COMPARE(spy.size(), 1);
        QCOMPARE(result(spy).error, TransportError::Network);
        QTest::qWait(20);
        QCOMPARE(fake->requests.size(), 1);
    }
}

QTEST_GUILESS_MAIN(HttpTransportTest)
#include "http_transport_test.moc"
