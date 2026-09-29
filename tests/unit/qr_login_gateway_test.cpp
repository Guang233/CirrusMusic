#include "application/account/session_coordinator.h"
#include "infrastructure/netease/auth/qr_login_gateway.h"

#include "../support/fake_http.h"

#include <QSignalSpy>
#include <QUrlQuery>
#include <QtTest>

using namespace cirrus::application;
using namespace cirrus::domain;
using namespace cirrus::netease;
using namespace cirrus::test;

struct Harness {
    SessionCoordinator session;
    FakeManager* fake = nullptr;
    std::unique_ptr<SessionApiClient> client;
    std::unique_ptr<QrLoginGateway> gateway;
    SessionToken context;

    explicit Harness(QList<Plan> plans)
    {
        auto manager = std::make_unique<FakeManager>();
        fake = manager.get();
        fake->plans = std::move(plans);
        client =
            std::make_unique<SessionApiClient>(std::make_unique<HttpTransport>(std::move(manager)),
                                               QStringLiteral("synthetic-device"));
        gateway = std::make_unique<QrLoginGateway>(*client);
        context = session.beginAuthentication();
        const bool accepted = client->advanceTo(context);
        Q_ASSERT(accepted);
    }
};

static Plan keyPlan() { return {R"({"code":200,"unikey":"synthetic-key"})"}; }

template <typename Result> static std::optional<Result> resultFor(const QSignalSpy& spy, quint64 id)
{
    for (const auto& row : spy) {
        if (row.at(0).toULongLong() == id) {
            return qvariant_cast<Result>(row.at(2));
        }
    }
    return std::nullopt;
}

class QrLoginGatewayTest : public QObject
{
    Q_OBJECT
  private slots:
    void keyAndLocalUrl();
    void keyFailures_data();
    void keyFailures();
    void pollStates_data();
    void pollStates();
    void pollErrors_data();
    void pollErrors();
    void singleFlightAndCancel();
    void contextSwitchBeforeDispatch();
    void keyCannotCrossAttempts();
    void queuedResultsKeepOriginalContext();
    void clientDestruction();
    void gatewayDestruction();
};

void QrLoginGatewayTest::keyAndLocalUrl()
{
    Harness h({{R"({"code":200,"unikey":"key&next=evil+%#?","url":"https://evil.example/"})"}});
    QSignalSpy spy(h.gateway.get(), &IQrLoginGateway::keyCompleted);
    const auto id = h.gateway->requestKey(h.context);
    QCOMPARE(spy.size(), 0);
    QTRY_COMPARE(spy.size(), 1);
    const auto result = resultFor<QrKeyResult>(spy, id);
    QVERIFY(result && std::holds_alternative<QrLoginChallenge>(*result));
    const auto challenge = std::get<QrLoginChallenge>(*result);
    QCOMPARE(challenge.key, QStringLiteral("key&next=evil+%#?"));
    QCOMPARE(challenge.loginUrl.scheme(), QStringLiteral("https"));
    QCOMPARE(challenge.loginUrl.host(), QStringLiteral("music.163.com"));
    QCOMPARE(challenge.loginUrl.path(), QStringLiteral("/login"));
    QVERIFY(!challenge.loginUrl.hasFragment());
    const QUrlQuery query(challenge.loginUrl);
    QCOMPARE(query.queryItems().size(), 1);
    QCOMPARE(query.queryItemValue(QStringLiteral("codekey"), QUrl::FullyDecoded), challenge.key);
    QCOMPARE(h.fake->requests.size(), 1); // QR URL creation is local, not /login/qr/create HTTP
    QCOMPARE(qvariant_cast<SessionToken>(spy.at(0).at(1)), h.context);
    const auto repeat = h.gateway->requestKey(h.context);
    QTRY_COMPARE(spy.size(), 2);
    const auto repeated = resultFor<QrKeyResult>(spy, repeat);
    QVERIFY(repeated && std::holds_alternative<AuthFailure>(*repeated));
    QCOMPARE(std::get<AuthFailure>(*repeated).error, AuthError::InvalidRequest);
    QCOMPARE(h.fake->requests.size(), 1);
}

void QrLoginGatewayTest::keyFailures_data()
{
    QTest::addColumn<QByteArray>("body");
    QTest::addColumn<int>("http");
    QTest::addColumn<int>("error");
    QTest::newRow("wrapper-envelope")
        << QByteArray(R"({"code":200,"data":{"unikey":"wrong-layer"}})") << 200
        << int(AuthError::InvalidResponse);
    QTest::newRow("missing-code") << QByteArray(R"({"unikey":"synthetic"})") << 200
                                  << int(AuthError::InvalidResponse);
    QTest::newRow("empty-key") << QByteArray(R"({"code":200,"unikey":""})") << 200
                               << int(AuthError::InvalidResponse);
    QTest::newRow("numeric-key") << QByteArray(R"({"code":200,"unikey":123})") << 200
                                 << int(AuthError::InvalidResponse);
    QTest::newRow("control-key") << QByteArray(R"({"code":200,"unikey":"bad\nkey"})") << 200
                                 << int(AuthError::InvalidResponse);
    QTest::newRow("html") << QByteArray("<html>challenge</html>") << 200
                          << int(AuthError::InvalidResponse);
    QTest::newRow("business-rate-limit")
        << QByteArray(R"({"code":429})") << 200 << int(AuthError::RateLimited);
    QTest::newRow("http-rate-limit") << QByteArray("busy") << 429 << int(AuthError::RateLimited);
    QTest::newRow("business-rejection")
        << QByteArray(R"({"code":502,"message":"synthetic private text"})") << 200
        << int(AuthError::ProviderRejected);
    QTest::newRow("http-forbidden")
        << QByteArray("forbidden") << 403 << int(AuthError::ProviderRejected);
    QTest::newRow("redirect") << QByteArray("redirect") << 302 << int(AuthError::InvalidResponse);
}

void QrLoginGatewayTest::keyFailures()
{
    QFETCH(QByteArray, body);
    QFETCH(int, http);
    QFETCH(int, error);
    Harness h({{body, http}});
    QSignalSpy spy(h.gateway.get(), &IQrLoginGateway::keyCompleted);
    const auto id = h.gateway->requestKey(h.context);
    QTRY_COMPARE(spy.size(), 1);
    const auto result = resultFor<QrKeyResult>(spy, id);
    QVERIFY(result && std::holds_alternative<AuthFailure>(*result));
    const auto failure = std::get<AuthFailure>(*result);
    QCOMPARE(int(failure.error), error);
    if (failure.error == AuthError::RateLimited) {
        QCOMPARE(failure.retryAfterMs, std::optional<qint64>(3000));
    }
    if (body.contains("502")) {
        QCOMPARE(failure.providerCode, std::optional<int>(502));
    }
    QCOMPARE(h.session.snapshot().phase, SessionPhase::Authenticating);
    QCOMPARE(h.fake->requests.size(), 1); // no hidden noCookie/crypto/endpoint retry
}

void QrLoginGatewayTest::pollStates_data()
{
    QTest::addColumn<int>("code");
    QTest::addColumn<int>("state");
    QTest::newRow("expired") << 800 << int(QrLoginStatus::Expired);
    QTest::newRow("waiting") << 801 << int(QrLoginStatus::WaitingForScan);
    QTest::newRow("scanned") << 802 << int(QrLoginStatus::WaitingForConfirmation);
    QTest::newRow("authorized-not-authenticated") << 803 << int(QrLoginStatus::Authorized);
}

void QrLoginGatewayTest::pollStates()
{
    QFETCH(int, code);
    QFETCH(int, state);
    Plan poll{"{\"code\":" + QByteArray::number(code) + "}"};
    poll.headers = {
        {"Set-Cookie", "MUSIC_U=synthetic-user; Domain=.music.163.com; Path=/; Secure; HttpOnly"}};
    Harness h({keyPlan(), poll});
    QSignalSpy keys(h.gateway.get(), &IQrLoginGateway::keyCompleted);
    QSignalSpy statuses(h.gateway.get(), &IQrLoginGateway::statusCompleted);
    QVERIFY(h.gateway->requestKey(h.context) > 0);
    QTRY_COMPARE(keys.size(), 1);
    const auto id = h.gateway->checkStatus(h.context, QStringLiteral("synthetic-key"));
    QCOMPARE(statuses.size(), 0);
    QTRY_COMPARE(statuses.size(), 1);
    const auto result = resultFor<QrStatusResult>(statuses, id);
    QVERIFY(result && std::holds_alternative<QrLoginStatus>(*result));
    QCOMPARE(int(std::get<QrLoginStatus>(*result)), state);
    // The port cannot turn a QR status into verified identity or a promoted epoch.
    QCOMPARE(h.session.snapshot().phase, SessionPhase::Authenticating);
    QCOMPARE(h.session.snapshot().token, h.context);
    QVERIFY(h.session.snapshot().accountId.isEmpty());
    if (code == 800 || code == 803) {
        const auto terminal = h.gateway->checkStatus(h.context, QStringLiteral("synthetic-key"));
        QTRY_COMPARE(statuses.size(), 2);
        const auto again = resultFor<QrStatusResult>(statuses, terminal);
        QVERIFY(again && std::holds_alternative<AuthFailure>(*again));
        QCOMPARE(std::get<AuthFailure>(*again).error, AuthError::InvalidRequest);
        QCOMPARE(h.fake->requests.size(), 2);
    }
}

void QrLoginGatewayTest::pollErrors_data()
{
    QTest::addColumn<QByteArray>("body");
    QTest::addColumn<QByteArray>("cookie");
    QTest::addColumn<int>("error");
    QTest::newRow("unknown-code") << QByteArray(R"({"code":502})") << QByteArray{}
                                  << int(AuthError::ProviderRejected);
    QTest::newRow("missing-code") << QByteArray("{}") << QByteArray{}
                                  << int(AuthError::InvalidResponse);
    QTest::newRow("invalid-code") << QByteArray(R"({"code":803.5})") << QByteArray{}
                                  << int(AuthError::InvalidResponse);
    QTest::newRow("cookie-rejected")
        << QByteArray(R"({"code":803})") << QByteArray("MUSIC_U=synthetic; Domain=.163.com; Path=/")
        << int(AuthError::CredentialRejected);
}

void QrLoginGatewayTest::pollErrors()
{
    QFETCH(QByteArray, body);
    QFETCH(QByteArray, cookie);
    QFETCH(int, error);
    Plan poll{body};
    if (!cookie.isEmpty()) {
        poll.headers.append({"Set-Cookie", cookie});
    }
    Harness h({keyPlan(), poll});
    QSignalSpy keys(h.gateway.get(), &IQrLoginGateway::keyCompleted);
    QSignalSpy statuses(h.gateway.get(), &IQrLoginGateway::statusCompleted);
    QVERIFY(h.gateway->requestKey(h.context) > 0);
    QTRY_COMPARE(keys.size(), 1);
    const auto id = h.gateway->checkStatus(h.context, QStringLiteral("synthetic-key"));
    QTRY_COMPARE(statuses.size(), 1);
    const auto result = resultFor<QrStatusResult>(statuses, id);
    QVERIFY(result && std::holds_alternative<AuthFailure>(*result));
    QCOMPARE(int(std::get<AuthFailure>(*result).error), error);
    QCOMPARE(h.fake->requests.size(), 2);
}

void QrLoginGatewayTest::singleFlightAndCancel()
{
    Harness h({{"", 0, -1}});
    QSignalSpy spy(h.gateway.get(), &IQrLoginGateway::keyCompleted);
    const auto first = h.gateway->requestKey(h.context);
    const auto busy = h.gateway->requestKey(h.context);
    QTRY_COMPARE(spy.size(), 1);
    const auto rejection = resultFor<QrKeyResult>(spy, busy);
    QVERIFY(rejection && std::holds_alternative<AuthFailure>(*rejection));
    QCOMPARE(std::get<AuthFailure>(*rejection).error, AuthError::Busy);
    QTRY_COMPARE(h.fake->requests.size(), 1);
    h.gateway->cancel(first);
    QTRY_COMPARE(spy.size(), 2);
    const auto canceled = resultFor<QrKeyResult>(spy, first);
    QVERIFY(canceled && std::holds_alternative<AuthFailure>(*canceled));
    QCOMPARE(std::get<AuthFailure>(*canceled).error, AuthError::Canceled);
    h.gateway->cancel(first);
    QCoreApplication::processEvents();
    QCOMPARE(spy.size(), 2);
}

void QrLoginGatewayTest::contextSwitchBeforeDispatch()
{
    Harness h({keyPlan()});
    QSignalSpy spy(h.gateway.get(), &IQrLoginGateway::keyCompleted);
    const auto old = h.gateway->requestKey(h.context);
    const auto next = h.session.beginAuthentication();
    QVERIFY(h.client->advanceTo(next));
    const auto current = h.gateway->requestKey(next); // must not get Busy from old queued work
    QTRY_COMPARE(spy.size(), 2);
    const auto stale = resultFor<QrKeyResult>(spy, old);
    const auto ready = resultFor<QrKeyResult>(spy, current);
    QVERIFY(stale && std::holds_alternative<AuthFailure>(*stale));
    QCOMPARE(std::get<AuthFailure>(*stale).error, AuthError::StaleContext);
    QVERIFY(ready && std::holds_alternative<QrLoginChallenge>(*ready));
    QCOMPARE(h.fake->requests.size(), 1);
}

void QrLoginGatewayTest::keyCannotCrossAttempts()
{
    Harness h({keyPlan(), keyPlan()});
    QSignalSpy keys(h.gateway.get(), &IQrLoginGateway::keyCompleted);
    QSignalSpy statuses(h.gateway.get(), &IQrLoginGateway::statusCompleted);
    QVERIFY(h.gateway->requestKey(h.context) > 0);
    QTRY_COMPARE(keys.size(), 1);
    const auto next = h.session.beginAuthentication();
    const auto unbound = h.gateway->checkStatus(next, QStringLiteral("synthetic-key"));
    QTRY_COMPARE(statuses.size(), 1);
    const auto stale = resultFor<QrStatusResult>(statuses, unbound);
    QVERIFY(stale && std::holds_alternative<AuthFailure>(*stale));
    QCOMPARE(std::get<AuthFailure>(*stale).error, AuthError::StaleContext);
    QVERIFY(h.client->advanceTo(next));
    const auto oldKey = h.gateway->checkStatus(next, QStringLiteral("synthetic-key"));
    QTRY_COMPARE(statuses.size(), 2);
    const auto invalid = resultFor<QrStatusResult>(statuses, oldKey);
    QVERIFY(invalid && std::holds_alternative<AuthFailure>(*invalid));
    QCOMPARE(std::get<AuthFailure>(*invalid).error, AuthError::InvalidRequest);
    QVERIFY(h.gateway->requestKey(next) > 0);
    QTRY_COMPARE(keys.size(), 2);
    QCOMPARE(h.fake->requests.size(), 2);
}

void QrLoginGatewayTest::queuedResultsKeepOriginalContext()
{
    Harness h({keyPlan()});
    bool queuedReceived = false;
    connect(
        h.gateway.get(), &IQrLoginGateway::keyCompleted, this,
        [&](quint64, SessionToken context, QrKeyResult result) {
            queuedReceived = true;
            QCOMPARE(context, h.context);
            QVERIFY(!h.session.accepts(context));
            QVERIFY(std::holds_alternative<QrLoginChallenge>(result));
            QCOMPARE(std::get<QrLoginChallenge>(result).key, QStringLiteral("synthetic-key"));
        },
        Qt::QueuedConnection);
    connect(h.gateway.get(), &IQrLoginGateway::keyCompleted, this,
            [&](quint64, SessionToken context, QrKeyResult) {
                QVERIFY(h.session.cancelAuthentication(context));
                QVERIFY(h.client->advanceTo(h.session.snapshot().token));
            });
    QVERIFY(h.gateway->requestKey(h.context) > 0);
    QTRY_VERIFY(queuedReceived);
}

void QrLoginGatewayTest::clientDestruction()
{
    Harness h({{"", 0, -1}});
    QSignalSpy spy(h.gateway.get(), &IQrLoginGateway::keyCompleted);
    const auto id = h.gateway->requestKey(h.context);
    QTRY_COMPARE(h.fake->requests.size(), 1);
    h.client.reset();
    QTRY_COMPARE(spy.size(), 1);
    const auto result = resultFor<QrKeyResult>(spy, id);
    QVERIFY(result && std::holds_alternative<AuthFailure>(*result));
    QCOMPARE(std::get<AuthFailure>(*result).error, AuthError::Network);
    h.gateway->cancel(id);
}

void QrLoginGatewayTest::gatewayDestruction()
{
    Harness h({keyPlan()});
    int completed = 0;
    connect(h.gateway.get(), &IQrLoginGateway::keyCompleted, this, [&] { ++completed; });
    QVERIFY(h.gateway->requestKey(h.context) > 0);
    h.gateway.reset();
    QCoreApplication::processEvents();
    QCOMPARE(completed, 0);
    QVERIFY(h.fake->requests.isEmpty());
}

QTEST_GUILESS_MAIN(QrLoginGatewayTest)
#include "qr_login_gateway_test.moc"
