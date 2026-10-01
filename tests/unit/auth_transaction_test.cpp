#include "../support/fake_http.h"
#include "../support/fake_login_scheduler.h"
#include "application/account/qr_login_service.h"
#include "infrastructure/netease/auth/account_identity_mapper.h"
#include "infrastructure/netease/auth/qr_login_gateway.h"

#include <QJsonDocument>
#include <QSignalSpy>
#include <QUrlQuery>
#include <QtTest>

using namespace cirrus::application;
using namespace cirrus::domain;
using namespace cirrus::netease;
using namespace cirrus::test;

static Plan keyPlan() { return {R"({"code":200,"unikey":"synthetic-key"})"}; }
static Plan
authorizedPlan(QByteArray cookie = "MUSIC_U=synthetic-A; Domain=.music.163.com; Path=/; Secure")
{
    Plan p{R"({"code":803})"};
    if (!cookie.isEmpty())
        p.headers.append({"Set-Cookie", cookie});
    return p;
}
static Plan identityPlan()
{
    return {
        R"({"code":200,"account":{"id":"9007199254740993"},"profile":{"userId":"9007199254740993","nickname":"Synthetic"}})"};
}
struct Fixture {
    SessionCoordinator session;
    FakeManager* fake = nullptr;
    std::unique_ptr<SessionApiClient> client;
    std::unique_ptr<QrLoginGateway> gateway;
    std::unique_ptr<QrLoginService> service;
    explicit Fixture(QList<Plan> plans, bool withService = true,
                     ILoginScheduler* scheduler = nullptr, QrLoginOptions options = {})
    {
        auto manager = std::make_unique<FakeManager>();
        fake = manager.get();
        fake->plans = std::move(plans);
        client =
            std::make_unique<SessionApiClient>(std::make_unique<HttpTransport>(std::move(manager)),
                                               QStringLiteral("synthetic-device"));
        gateway = std::make_unique<QrLoginGateway>(*client);
        if (withService)
            service = std::make_unique<QrLoginService>(session, *gateway, options, scheduler);
    }
};

class AuthTransactionTest : public QObject
{
    Q_OBJECT
  private slots:
    void identitySchema_data();
    void identitySchema();
    void endToEndOffline_data();
    void endToEndOffline();
    void failuresRollback_data();
    void failuresRollback();
    void logoutDuringBinding();
    void verificationSnapshotCannotMixAccounts();
    void cancelVerification();
    void mixedCredentialScopesRejected();
    void wrongPromotionTarget_data();
    void wrongPromotionTarget();
    void deadlineSpansCommitting_data();
    void deadlineSpansCommitting();
};

void AuthTransactionTest::identitySchema_data()
{
    QTest::addColumn<QByteArray>("body");
    QTest::addColumn<QString>("expected");
    QTest::newRow("integer")
        << QByteArray(R"({"account":{"id":101},"profile":{"userId":101,"nickname":"N"}})")
        << QStringLiteral("101");
    QTest::newRow("wide-string") << identityPlan().body << QStringLiteral("9007199254740993");
    QTest::newRow("safe-numeric-boundary")
        << QByteArray(
               R"({"account":{"id":9007199254740991},"profile":{"userId":9007199254740991,"nickname":"N"}})")
        << QStringLiteral("9007199254740991");
    QTest::newRow("unsafe-integer")
        << QByteArray(
               R"({"account":{"id":9007199254740993},"profile":{"userId":9007199254740993,"nickname":"N"}})")
        << QString{};
    QTest::newRow("max-string")
        << QByteArray(
               R"({"account":{"id":"9223372036854775807"},"profile":{"userId":"9223372036854775807","nickname":"N"}})")
        << QStringLiteral("9223372036854775807");
    QTest::newRow("different-users")
        << QByteArray(R"({"account":{"id":101},"profile":{"userId":102,"nickname":"N"}})")
        << QString{};
    QTest::newRow("profile-null") << QByteArray(R"({"account":{"id":101},"profile":null})")
                                  << QString{};
    QTest::newRow("wrapper")
        << QByteArray(R"({"data":{"account":{"id":101},"profile":{"userId":101,"nickname":"N"}}})")
        << QString{};
    QTest::newRow("zero") << QByteArray(
                                 R"({"account":{"id":0},"profile":{"userId":0,"nickname":"N"}})")
                          << QString{};
    QTest::newRow("negative")
        << QByteArray(R"({"account":{"id":-1},"profile":{"userId":-1,"nickname":"N"}})")
        << QString{};
    QTest::newRow("fraction")
        << QByteArray(R"({"account":{"id":101.5},"profile":{"userId":101.5,"nickname":"N"}})")
        << QString{};
    QTest::newRow("wide-floating-point")
        << QByteArray(
               R"({"account":{"id":9007199254740993.0},"profile":{"userId":9007199254740993.0,"nickname":"N"}})")
        << QString{};
    QTest::newRow("overflow")
        << QByteArray(
               R"({"account":{"id":"9223372036854775808"},"profile":{"userId":"9223372036854775808","nickname":"N"}})")
        << QString{};
    QTest::newRow("leading-zero")
        << QByteArray(R"({"account":{"id":"0101"},"profile":{"userId":"0101","nickname":"N"}})")
        << QString{};
    QTest::newRow("nickname-type")
        << QByteArray(R"({"account":{"id":101},"profile":{"userId":101,"nickname":[]}})")
        << QString{};
}
void AuthTransactionTest::identitySchema()
{
    QFETCH(QByteArray, body);
    QFETCH(QString, expected);
    const auto identity = mapAccountIdentity(QJsonDocument::fromJson(body).object());
    QCOMPARE(identity.has_value(), !expected.isEmpty());
    if (identity)
        QCOMPARE(identity->accountId, expected);
}

void AuthTransactionTest::endToEndOffline_data()
{
    QTest::addColumn<bool>("expire");
    QTest::newRow("logout") << false;
    QTest::newRow("expiration") << true;
}

void AuthTransactionTest::endToEndOffline()
{
    QFETCH(bool, expire);
    auto profile = identityPlan();
    profile.headers.append(std::pair<QByteArray, QByteArray>{
        "Set-Cookie", "MUSIC_U=synthetic-rotated-A; Domain=.music.163.com; Path=/; Secure"});
    Fixture h({keyPlan(), authorizedPlan(), profile, keyPlan(), keyPlan()});
    QSignalSpy key(h.service.get(), &QrLoginService::challengeReady);
    QSignalSpy ready(h.service.get(), &QrLoginService::signedIn);
    bool boundBeforePublished = false;
    connect(&h.session, &SessionCoordinator::changed, this, [&](SessionSnapshot snapshot) {
        if (snapshot.phase == SessionPhase::Authenticated)
            boundBeforePublished = h.client->accepts(snapshot.token);
    });
    const auto attempt = h.service->begin();
    QTRY_COMPARE(key.size(), 1);
    QVERIFY(h.service->poll());
    QTRY_COMPARE(ready.size(), 1);
    QVERIFY(boundBeforePublished);
    QCOMPARE(h.session.snapshot().phase, SessionPhase::Authenticated);
    QCOMPARE(h.session.snapshot().token.epoch, attempt.epoch + 1);
    QCOMPARE(h.session.snapshot().accountId, QStringLiteral("9007199254740993"));
    QCOMPARE(h.fake->requests.at(2).url(),
             QUrl(QStringLiteral("https://music.163.com/weapi/w/nuser/account/get")));
    QCOMPARE(h.fake->requests.at(2).rawHeader("Referer"), QByteArray("https://music.163.com"));
    QVERIFY(h.fake->requests.at(2).rawHeader("Cookie").contains("MUSIC_U=synthetic-A"));
    const QUrlQuery form(QString::fromLatin1(h.fake->bodies.at(2)));
    QVERIFY(form.hasQueryItem(QStringLiteral("params")));
    QVERIFY(form.hasQueryItem(QStringLiteral("encSecKey")));
    QVERIFY(h.client->submit(h.session.snapshot().token, ApiOperation::QrKey) > 0);
    QTRY_COMPARE(h.fake->requests.size(), 4);
    QVERIFY(h.fake->requests.last().rawHeader("Cookie").contains("synthetic-rotated-A"));
    if (expire) {
        QVERIFY(h.session.expire(h.session.snapshot().token));
    } else {
        h.session.signOut();
    }
    QVERIFY(h.client->submit(h.session.snapshot().token, ApiOperation::QrKey) > 0);
    QTRY_COMPARE(h.fake->requests.size(), 5);
    QVERIFY(!h.fake->requests.last().rawHeader("Cookie").contains("MUSIC_U="));
}

void AuthTransactionTest::failuresRollback_data()
{
    QTest::addColumn<QByteArray>("loginCookie");
    QTest::addColumn<QByteArray>("identityBody");
    QTest::addColumn<QByteArray>("identityCookie");
    QTest::addColumn<int>("http");
    const QByteArray good = "MUSIC_U=synthetic-A; Domain=.music.163.com; Path=/; Secure";
    QTest::newRow("missing-credential")
        << QByteArray{} << identityPlan().body << QByteArray{} << 200;
    QTest::newRow("host-only-cannot-bridge") << QByteArray("MUSIC_U=synthetic-A; Path=/; Secure")
                                             << identityPlan().body << QByteArray{} << 200;
    QTest::newRow("identity-mismatch")
        << good
        << QByteArray(R"({"code":200,"account":{"id":1},"profile":{"userId":2,"nickname":"N"}})")
        << QByteArray{} << 200;
    QTest::newRow("bad-json") << good << QByteArray("broken") << QByteArray{} << 200;
    QTest::newRow("no-code") << good << QByteArray("{}") << QByteArray{} << 200;
    QTest::newRow("http-rate-limit") << good << identityPlan().body << QByteArray{} << 429;
    QTest::newRow("business-rejected")
        << good << QByteArray(R"({"code":502})") << QByteArray{} << 200;
    QTest::newRow("credential-deleted")
        << good << identityPlan().body
        << QByteArray("MUSIC_U=deleted; Domain=.music.163.com; Path=/; Max-Age=0") << 200;
    QTest::newRow("invalid-domain") << good << identityPlan().body
                                    << QByteArray("MUSIC_U=wrong; Domain=.163.com; Path=/") << 200;
}
void AuthTransactionTest::failuresRollback()
{
    QFETCH(QByteArray, loginCookie);
    QFETCH(QByteArray, identityBody);
    QFETCH(QByteArray, identityCookie);
    QFETCH(int, http);
    Plan profile{identityBody, http};
    if (!identityCookie.isEmpty())
        profile.headers.append({"Set-Cookie", identityCookie});
    Fixture h({keyPlan(), authorizedPlan(loginCookie), profile});
    QSignalSpy key(h.service.get(), &QrLoginService::challengeReady);
    QSignalSpy ready(h.service.get(), &QrLoginService::signedIn);
    QSignalSpy failed(h.service.get(), &QrLoginService::failed);
    const auto attempt = h.service->begin();
    QTRY_COMPARE(key.size(), 1);
    QVERIFY(h.service->poll());
    QTRY_COMPARE(failed.size(), 1);
    QCOMPARE(ready.size(), 0);
    QCOMPARE(h.session.snapshot().phase, SessionPhase::Guest);
    QVERIFY(h.client->accepts(h.session.snapshot().token));
    QVERIFY(!h.session.accepts(attempt));
    QVERIFY(h.session.snapshot().accountId.isEmpty());
}

void AuthTransactionTest::logoutDuringBinding()
{
    Fixture h({keyPlan(), authorizedPlan(), identityPlan(), keyPlan()});
    QSignalSpy key(h.service.get(), &QrLoginService::challengeReady);
    QSignalSpy ready(h.service.get(), &QrLoginService::signedIn);
    connect(h.client.get(), &SessionApiClient::contextAdvanced, this, [&](SessionToken token) {
        if (h.session.accepts(token) && h.session.snapshot().phase == SessionPhase::Committing)
            h.session.signOut();
    });
    const auto attempt = h.service->begin();
    QTRY_COMPARE(key.size(), 1);
    QVERIFY(h.service->poll());
    QTRY_VERIFY(h.session.snapshot().phase == SessionPhase::Guest);
    QCOMPARE(ready.size(), 0);
    QVERIFY(!h.session.accepts(attempt));
    QVERIFY(h.client->submit(h.session.snapshot().token, ApiOperation::QrKey) > 0);
    QTRY_COMPARE(h.fake->requests.size(), 4);
    QVERIFY(!h.fake->requests.last().rawHeader("Cookie").contains("MUSIC_U="));
}

void AuthTransactionTest::verificationSnapshotCannotMixAccounts()
{
    auto profile = identityPlan();
    profile.delayMs = 300;
    Fixture h({keyPlan(), authorizedPlan(), profile,
               authorizedPlan("MUSIC_U=synthetic-B; Domain=.music.163.com; Path=/; Secure"),
               keyPlan()},
              false);
    const auto attempt = h.session.beginAuthentication();
    QVERIFY(h.gateway->synchronize(attempt));
    QSignalSpy keys(h.gateway.get(), &IQrLoginGateway::keyCompleted);
    QSignalSpy states(h.gateway.get(), &IQrLoginGateway::statusCompleted);
    QSignalSpy identities(h.gateway.get(), &IQrLoginGateway::identityCompleted);
    QSignalSpy promotions(h.gateway.get(), &IQrLoginGateway::promotionCompleted);
    QVERIFY(h.gateway->requestKey(attempt) > 0);
    QTRY_COMPARE(keys.size(), 1);
    QVERIFY(h.gateway->checkStatus(attempt, QStringLiteral("synthetic-key")) > 0);
    QTRY_COMPARE(states.size(), 1);
    QSignalSpy deliveryOrder(h.client.get(), &SessionApiClient::completed);
    QVERIFY(h.gateway->verifyIdentity(attempt) > 0);
    QTRY_COMPARE(h.fake->requests.size(), 3); // A is captured in the verification request
    QVERIFY(h.client->submit(attempt, ApiOperation::QrCheck,
                             {{QStringLiteral("key"), QStringLiteral("parallel")}}) > 0);
    QTRY_COMPARE(h.fake->requests.size(), 4);
    QTRY_COMPARE(identities.size(), 1);
    QCOMPARE(deliveryOrder.size(), 2);
    QCOMPARE(qvariant_cast<ApiResult>(deliveryOrder.first().at(2)).decoded.businessCode,
             std::optional<int>(803));
    const auto response = qvariant_cast<IdentityResult>(identities.first().at(2));
    QVERIFY(std::holds_alternative<VerifiedLogin>(response));
    const auto verified = std::get<VerifiedLogin>(response);
    const auto target = h.session.prepareAuthentication(attempt, verified.identity.accountId);
    QVERIFY(target);
    QVERIFY(h.gateway->promoteCredentials(attempt, *target, verified.receipt) > 0);
    QTRY_COMPARE(promotions.size(), 1);
    QVERIFY(std::holds_alternative<AccountIdentity>(
        qvariant_cast<PromotionResult>(promotions.first().at(2))));
    QVERIFY(h.session.completeAuthentication(*target));
    QVERIFY(!h.client->promoteReceipt(attempt, *target, verified.receipt)); // consumed once
    QVERIFY(h.client->submit(*target, ApiOperation::QrKey) > 0);
    QTRY_COMPARE(h.fake->requests.size(), 5);
    const auto cookie = h.fake->requests.last().rawHeader("Cookie");
    QVERIFY(cookie.contains("MUSIC_U=synthetic-A"));
    QVERIFY(!cookie.contains("synthetic-B"));
}

void AuthTransactionTest::cancelVerification()
{
    auto profile = identityPlan();
    profile.delayMs = -1;
    Fixture h({keyPlan(), authorizedPlan(), profile, keyPlan()});
    QSignalSpy key(h.service.get(), &QrLoginService::challengeReady);
    QSignalSpy ready(h.service.get(), &QrLoginService::signedIn);
    const auto attempt = h.service->begin();
    QTRY_COMPARE(key.size(), 1);
    QVERIFY(h.service->poll());
    QTRY_COMPARE(h.fake->requests.size(), 3);
    h.service->cancel();
    QCOMPARE(h.session.snapshot().phase, SessionPhase::Guest);
    const auto next = h.service->begin();
    QVERIFY(next != attempt);
    QTRY_COMPARE(key.size(), 2);
    QCOMPARE(ready.size(), 0);
    QVERIFY(!h.fake->requests.last().rawHeader("Cookie").contains("MUSIC_U="));
}

void AuthTransactionTest::mixedCredentialScopesRejected()
{
    auto authorized = authorizedPlan();
    authorized.headers.append(
        std::pair<QByteArray, QByteArray>{"Set-Cookie", "MUSIC_U=synthetic-B; Path=/eapi; Secure"});
    Fixture h({keyPlan(), authorized, identityPlan()});
    QSignalSpy key(h.service.get(), &QrLoginService::challengeReady);
    QSignalSpy ready(h.service.get(), &QrLoginService::signedIn);
    QSignalSpy failed(h.service.get(), &QrLoginService::failed);
    const auto attempt = h.service->begin();
    QTRY_COMPARE(key.size(), 1);
    QVERIFY(h.service->poll());
    QTRY_COMPARE(failed.size(), 1);
    QCOMPARE(h.fake->requests.size(), 3);
    const auto cookie = h.fake->requests.last().rawHeader("Cookie");
    QVERIFY(cookie.contains("synthetic-A"));
    QVERIFY(!cookie.contains("synthetic-B"));
    QCOMPARE(qvariant_cast<AuthFailure>(failed.first().at(1)).error, AuthError::CredentialRejected);
    QCOMPARE(ready.size(), 0);
    QVERIFY(!h.session.accepts(attempt));
}

void AuthTransactionTest::wrongPromotionTarget_data()
{
    QTest::addColumn<int>("kind");
    QTest::newRow("same-epoch") << 0;
    QTest::newRow("skipped-epoch") << 1;
    QTest::newRow("foreign-coordinator") << 2;
    QTest::newRow("wrong-receipt") << 3;
}
void AuthTransactionTest::wrongPromotionTarget()
{
    QFETCH(int, kind);
    Fixture h({keyPlan(), authorizedPlan(), identityPlan()}, false);
    const auto attempt = h.session.beginAuthentication();
    QVERIFY(h.gateway->synchronize(attempt));
    QSignalSpy keys(h.gateway.get(), &IQrLoginGateway::keyCompleted);
    QSignalSpy statuses(h.gateway.get(), &IQrLoginGateway::statusCompleted);
    QSignalSpy identities(h.gateway.get(), &IQrLoginGateway::identityCompleted);
    QSignalSpy promotions(h.gateway.get(), &IQrLoginGateway::promotionCompleted);
    QVERIFY(h.gateway->requestKey(attempt) > 0);
    QTRY_COMPARE(keys.size(), 1);
    QVERIFY(h.gateway->checkStatus(attempt, QStringLiteral("synthetic-key")) > 0);
    QTRY_COMPARE(statuses.size(), 1);
    QVERIFY(h.gateway->verifyIdentity(attempt) > 0);
    QTRY_COMPARE(identities.size(), 1);
    const auto result = qvariant_cast<IdentityResult>(identities.first().at(2));
    QVERIFY(std::holds_alternative<VerifiedLogin>(result));
    auto verified = std::get<VerifiedLogin>(result);
    SessionToken target{attempt.coordinatorId, attempt.epoch + 1};
    if (kind == 0)
        target = attempt;
    if (kind == 1)
        ++target.epoch;
    if (kind == 2)
        target.coordinatorId = QUuid::createUuid();
    if (kind == 3)
        verified.receipt = QUuid::createUuid();
    QVERIFY(h.gateway->promoteCredentials(attempt, target, verified.receipt) > 0);
    QTRY_COMPARE(promotions.size(), 1);
    QVERIFY(std::holds_alternative<AuthFailure>(
        qvariant_cast<PromotionResult>(promotions.first().at(2))));
    QVERIFY(h.client->accepts(attempt));
    QVERIFY(h.session.snapshot().phase != SessionPhase::Authenticated);
}

void AuthTransactionTest::deadlineSpansCommitting_data()
{
    QTest::addColumn<bool>("afterBinding");
    // Deadline timer delivered while Committing (before the receipt is promoted).
    QTest::newRow("timer-during-committing") << false;
    // Credentials already bound to the target, acknowledgement arrives after the deadline.
    QTest::newRow("late-binding-ack") << true;
}
void AuthTransactionTest::deadlineSpansCommitting()
{
    QFETCH(bool, afterBinding);
    FakeLoginScheduler clock;
    QrLoginOptions options;
    options.autoPoll = true;
    options.pollInterval = std::chrono::milliseconds(1000);
    options.totalTimeout = std::chrono::milliseconds(10000);
    Fixture h({keyPlan(), authorizedPlan(), identityPlan(), keyPlan()}, true, &clock, options);
    QSignalSpy key(h.service.get(), &QrLoginService::challengeReady);
    QSignalSpy ready(h.service.get(), &QrLoginService::signedIn);
    QSignalSpy failed(h.service.get(), &QrLoginService::failed);
    bool bound = false;
    if (afterBinding) {
        connect(h.client.get(), &SessionApiClient::contextAdvanced, this, [&](SessionToken t) {
            if (h.session.snapshot().phase == SessionPhase::Committing && h.client->accepts(t)) {
                bound = true;
                clock.jump(std::chrono::milliseconds(20000)); // no timer delivery
            }
        });
    } else {
        connect(&h.session, &SessionCoordinator::changed, this, [&](SessionSnapshot snapshot) {
            if (snapshot.phase == SessionPhase::Committing) {
                QCOMPARE(clock.now(), std::chrono::milliseconds(1000));
                clock.advance(std::chrono::milliseconds(8999));
                QCOMPARE(failed.size(), 0); // epoch rotation did not restart the clock
                clock.advance(std::chrono::milliseconds(1));
            }
        });
    }
    const auto attempt = h.service->begin();
    QTRY_COMPARE(key.size(), 1);
    clock.advance(std::chrono::milliseconds(1000)); // automatic check -> 803 -> verify
    QTRY_COMPARE(failed.size(), 1);
    QCOMPARE(bound, afterBinding);
    QCOMPARE(qvariant_cast<AuthFailure>(failed.first().at(1)).error, AuthError::Timeout);
    QCOMPARE(h.fake->requests.size(), 3);
    QTest::qWait(20); // any queued promotion acknowledgement is drained and ignored
    QCOMPARE(ready.size(), 0);
    QCOMPARE(failed.size(), 1);
    QCOMPARE(h.session.snapshot().phase, SessionPhase::Guest);
    QVERIFY(!h.session.accepts(attempt));
    QVERIFY(h.client->accepts(h.session.snapshot().token));
    QVERIFY(h.client->submit(h.session.snapshot().token, ApiOperation::QrKey) > 0);
    QTRY_COMPARE(h.fake->requests.size(), 4);
    QVERIFY(!h.fake->requests.last().rawHeader("Cookie").contains("MUSIC_U="));
    QCOMPARE(clock.pending(), 0);
}

QTEST_GUILESS_MAIN(AuthTransactionTest)
#include "auth_transaction_test.moc"
