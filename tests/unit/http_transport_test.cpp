#include "infrastructure/netease/network/http_transport.h"

#include <QNetworkReply>
#include <QSignalSpy>
#include <QTimer>
#include <QtTest>

#include <cstring>

using namespace cirrus::netease;

struct Plan {
    QByteArray body = "ok";
    int status = 200;
    int delayMs = 0; // negative means never completes without abort
    QNetworkReply::NetworkError error = QNetworkReply::NoError;
};

class FakeReply : public QNetworkReply
{
  public:
    FakeReply(const QNetworkRequest& request, Plan plan, QObject* parent) : QNetworkReply(parent)
    {
        setRequest(request);
        setUrl(request.url());
        open(QIODevice::ReadOnly);
        if (plan.delayMs >= 0) {
            QTimer::singleShot(plan.delayMs, this, [this, plan] {
                if (isFinished()) {
                    return;
                }
                m_body = plan.body;
                setAttribute(QNetworkRequest::HttpStatusCodeAttribute, plan.status);
                setRawHeader("Retry-After", "3");
                if (plan.error != QNetworkReply::NoError) {
                    setError(plan.error, QStringLiteral("synthetic failure"));
                }
                emit readyRead();
                if (!isFinished()) {
                    setFinished(true);
                    emit finished();
                }
            });
        }
    }
    void abort() override
    {
        if (!isFinished()) {
            setError(OperationCanceledError, QStringLiteral("canceled"));
            setFinished(true);
            emit finished(); // deliberately synchronous to exercise reentrancy
        }
    }
    qint64 bytesAvailable() const override
    {
        return m_body.size() - m_offset + QIODevice::bytesAvailable();
    }

  protected:
    qint64 readData(char* buffer, qint64 maximum) override
    {
        const qint64 count = qMin(maximum, static_cast<qint64>(m_body.size() - m_offset));
        if (count <= 0) {
            return 0;
        }
        std::memcpy(buffer, m_body.constData() + m_offset, static_cast<size_t>(count));
        m_offset += count;
        return count;
    }

  private:
    QByteArray m_body;
    qint64 m_offset = 0;
};

class FakeManager : public QNetworkAccessManager
{
  public:
    QList<Plan> plans;
    QList<QNetworkRequest> requests;
    QList<QByteArray> bodies;

  protected:
    QNetworkReply* createRequest(Operation operation, const QNetworkRequest& request,
                                 QIODevice* outgoing) override
    {
        Q_ASSERT(operation == PostOperation);
        requests.append(request);
        bodies.append(outgoing ? outgoing->readAll() : QByteArray{});
        return new FakeReply(request, plans.isEmpty() ? Plan{} : plans.takeFirst(), this);
    }
};

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

QTEST_GUILESS_MAIN(HttpTransportTest)
#include "http_transport_test.moc"
