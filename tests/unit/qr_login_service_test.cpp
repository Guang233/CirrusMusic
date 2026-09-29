#include "application/account/qr_login_service.h"

#include <QSignalSpy>
#include <QtTest>

using namespace cirrus::application;
using namespace cirrus::domain;

// This target links only application/Core/Test, never Network or infrastructure.
class FakeGateway : public IQrLoginGateway
{
  public:
    SessionToken current, source, target;
    quint64 sequence = 0, keyId = 0, pollId = 0, identityId = 0, promotionId = 0;
    int promotions = 0;
    bool syncAccepted = true;
    QList<quint64> canceled;
    bool synchronize(SessionToken context) override
    {
        if (syncAccepted)
            current = context;
        return syncAccepted;
    }
    quint64 requestKey(SessionToken) override { return keyId = ++sequence; }
    quint64 checkStatus(SessionToken, QString) override { return pollId = ++sequence; }
    quint64 verifyIdentity(SessionToken) override { return identityId = ++sequence; }
    quint64 promoteCredentials(SessionToken from, SessionToken to, QUuid) override
    {
        source = from;
        target = to;
        ++promotions;
        return promotionId = ++sequence;
    }
    void cancel(quint64 id) override { canceled.append(id); }
    void key(SessionToken token)
    {
        emit keyCompleted(keyId, token,
                          QrLoginChallenge{QStringLiteral("synthetic"),
                                           QUrl(QStringLiteral(
                                               "https://music.163.com/login?codekey=synthetic"))});
    }
    void authorized(SessionToken token)
    {
        emit statusCompleted(pollId, token, QrLoginStatus::Authorized);
    }
    void verified(SessionToken token)
    {
        emit identityCompleted(identityId, token,
                               VerifiedLogin{{QStringLiteral("101"), QStringLiteral("Synthetic")},
                                             QUuid::createUuid()});
    }
    void promoted()
    {
        current = target;
        emit promotionCompleted(
            promotionId, target,
            AccountIdentity{QStringLiteral("101"), QStringLiteral("Synthetic")});
    }
};

class QrLoginServiceTest : public QObject
{
    Q_OBJECT
  private slots:
    void readyBarrier();
    void cancelAndLateResult();
    void logoutDuringPreparation();
    void logoutDuringPublication();
    void rejectedPromotion();
    void invalidIdentity();
    void expired();
    void contextBarrierFailure();
    void destructionCancels();
};

void QrLoginServiceTest::readyBarrier()
{
    SessionCoordinator session;
    FakeGateway gateway;
    QrLoginService service(session, gateway);
    QSignalSpy ready(&service, &QrLoginService::signedIn);
    connect(&session, &SessionCoordinator::changed, this, [&](SessionSnapshot snapshot) {
        if (snapshot.phase == SessionPhase::Authenticated)
            QCOMPARE(gateway.current, snapshot.token);
    });
    const auto attempt = service.begin();
    gateway.key(attempt);
    QVERIFY(service.poll());
    QVERIFY(!service.poll());
    gateway.authorized(attempt);
    QCOMPARE(session.snapshot().phase, SessionPhase::Authenticating);
    QVERIFY(!service.poll());
    gateway.verified(attempt);
    QCOMPARE(session.snapshot().phase, SessionPhase::Committing);
    QCOMPARE(ready.size(), 0);
    QCOMPARE(gateway.source, attempt);
    QCOMPARE(gateway.target, session.snapshot().token);
    QCOMPARE(gateway.current, attempt); // normal synchronize must NOT erase staged credentials
    gateway.promoted();
    QCOMPARE(ready.size(), 1);
    QCOMPARE(session.snapshot().phase, SessionPhase::Authenticated);
    QCOMPARE(qvariant_cast<SessionToken>(ready.at(0).at(0)), gateway.target);
    QCOMPARE(session.snapshot().token, gateway.target);
    session.signOut();
    QCOMPARE(gateway.current, session.snapshot().token);
}

void QrLoginServiceTest::cancelAndLateResult()
{
    SessionCoordinator session;
    FakeGateway gateway;
    QrLoginService service(session, gateway);
    QSignalSpy ready(&service, &QrLoginService::signedIn);
    const auto attempt = service.begin();
    gateway.key(attempt);
    QVERIFY(service.poll());
    gateway.authorized(attempt);
    gateway.verified(attempt);
    const auto oldTarget = gateway.target;
    const auto oldId = gateway.promotionId;
    service.cancel();
    QCOMPARE(session.snapshot().phase, SessionPhase::Guest);
    QCOMPARE(gateway.current, session.snapshot().token);
    const auto next = service.begin();
    emit gateway.promotionCompleted(oldId, oldTarget, AccountIdentity{QStringLiteral("101"), {}});
    QCOMPARE(ready.size(), 0);
    QCOMPARE(session.snapshot().token, next);
    QVERIFY(gateway.canceled.contains(oldId));
}

void QrLoginServiceTest::logoutDuringPreparation()
{
    SessionCoordinator session;
    FakeGateway gateway;
    QrLoginService service(session, gateway);
    connect(&session, &SessionCoordinator::changed, this, [&](SessionSnapshot snapshot) {
        if (snapshot.phase == SessionPhase::Committing)
            session.signOut();
    });
    const auto attempt = service.begin();
    gateway.key(attempt);
    QVERIFY(service.poll());
    gateway.authorized(attempt);
    gateway.verified(attempt);
    QCOMPARE(gateway.promotions, 0);
    QCOMPARE(session.snapshot().phase, SessionPhase::Guest);
    QCOMPARE(gateway.current, session.snapshot().token);
}

void QrLoginServiceTest::logoutDuringPublication()
{
    SessionCoordinator session;
    FakeGateway gateway;
    QrLoginService service(session, gateway);
    QSignalSpy ready(&service, &QrLoginService::signedIn);
    connect(&session, &SessionCoordinator::changed, this, [&](SessionSnapshot snapshot) {
        if (snapshot.phase == SessionPhase::Authenticated)
            session.signOut();
    });
    const auto attempt = service.begin();
    gateway.key(attempt);
    QVERIFY(service.poll());
    gateway.authorized(attempt);
    gateway.verified(attempt);
    gateway.promoted();
    QCOMPARE(ready.size(), 0);
    QCOMPARE(session.snapshot().phase, SessionPhase::Guest);
    QCOMPARE(gateway.current, session.snapshot().token);
}

void QrLoginServiceTest::rejectedPromotion()
{
    SessionCoordinator session;
    FakeGateway gateway;
    QrLoginService service(session, gateway);
    QSignalSpy failed(&service, &QrLoginService::failed);
    const auto attempt = service.begin();
    gateway.key(attempt);
    QVERIFY(service.poll());
    gateway.authorized(attempt);
    gateway.verified(attempt);
    emit gateway.promotionCompleted(gateway.promotionId, gateway.target,
                                    AuthFailure{AuthError::CredentialRejected});
    QCOMPARE(session.snapshot().phase, SessionPhase::Guest);
    QCOMPARE(failed.size(), 1);
    QCOMPARE(qvariant_cast<SessionToken>(failed.at(0).at(0)), session.snapshot().token);
    QCOMPARE(gateway.current, session.snapshot().token);
}

void QrLoginServiceTest::invalidIdentity()
{
    SessionCoordinator session;
    FakeGateway gateway;
    QrLoginService service(session, gateway);
    const auto attempt = service.begin();
    gateway.key(attempt);
    QVERIFY(service.poll());
    gateway.authorized(attempt);
    emit gateway.identityCompleted(gateway.identityId, attempt,
                                   VerifiedLogin{{QStringLiteral("101"), {}}, {}});
    QCOMPARE(session.snapshot().phase, SessionPhase::Guest);
    QCOMPARE(gateway.promotions, 0);
}

void QrLoginServiceTest::expired()
{
    SessionCoordinator session;
    FakeGateway gateway;
    QrLoginService service(session, gateway);
    QSignalSpy statuses(&service, &QrLoginService::statusChanged);
    const auto attempt = service.begin();
    gateway.key(attempt);
    QVERIFY(service.poll());
    emit gateway.statusCompleted(gateway.pollId, attempt, QrLoginStatus::Expired);
    QCOMPARE(session.snapshot().phase, SessionPhase::Guest);
    QCOMPARE(statuses.size(), 1);
    QCOMPARE(qvariant_cast<SessionToken>(statuses.at(0).at(0)), session.snapshot().token);
}

void QrLoginServiceTest::contextBarrierFailure()
{
    SessionCoordinator session;
    FakeGateway gateway;
    gateway.syncAccepted = false;
    QrLoginService service(session, gateway);
    QSignalSpy failed(&service, &QrLoginService::failed);
    const auto attempt = service.begin();
    QVERIFY(!session.accepts(attempt));
    QCOMPARE(gateway.keyId, quint64(0));
    QCOMPARE(failed.size(), 1);
}

void QrLoginServiceTest::destructionCancels()
{
    SessionCoordinator session;
    FakeGateway gateway;
    auto service = std::make_unique<QrLoginService>(session, gateway);
    const auto attempt = service->begin();
    service.reset();
    QVERIFY(!session.accepts(attempt));
    QCOMPARE(session.snapshot().phase, SessionPhase::Guest);
    QCOMPARE(gateway.current, session.snapshot().token);
    QVERIFY(gateway.canceled.contains(gateway.keyId));
    gateway.key(attempt); // no receiver survives teardown
}

QTEST_GUILESS_MAIN(QrLoginServiceTest)
#include "qr_login_service_test.moc"
