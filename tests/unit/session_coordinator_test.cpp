#include "application/account/session_coordinator.h"

#include <QSignalSpy>
#include <QtTest>

using namespace cirrus::application;
using namespace cirrus::domain;

class SessionCoordinatorTest : public QObject
{
    Q_OBJECT
  private slots:
    void initialState();
    void authenticationRotatesEpoch();
    void supersededAttempt();
    void cancellation();
    void expirationAndLogout();
    void foreignCoordinator();
    void reentrantNotification();
    void reentrantAuthenticationResult();
};

void SessionCoordinatorTest::initialState()
{
    SessionCoordinator session;
    const auto snapshot = session.snapshot();
    QCOMPARE(snapshot.phase, SessionPhase::Guest);
    QVERIFY(snapshot.token.isValid());
    QVERIFY(snapshot.accountId.isEmpty());
    QVERIFY(session.accepts(snapshot.token));
    QVERIFY(!session.accepts({}));
    QVERIFY(!session.authenticate(snapshot.token, QStringLiteral("123")));
    QVERIFY(!session.expire(snapshot.token));
}

void SessionCoordinatorTest::authenticationRotatesEpoch()
{
    SessionCoordinator session;
    QSignalSpy spy(&session, &SessionCoordinator::changed);
    const auto guest = session.snapshot().token;
    const auto attempt = session.beginAuthentication();
    QVERIFY(!session.accepts(guest));
    QVERIFY(session.accepts(attempt));
    QVERIFY(!session.authenticate(attempt, {}));
    QVERIFY(!session.authenticate(attempt, QStringLiteral("  ")));
    QVERIFY(session.authenticate(attempt, QStringLiteral("9007199254740993")));
    const auto authenticated = session.snapshot();
    QCOMPARE(authenticated.phase, SessionPhase::Authenticated);
    QCOMPARE(authenticated.accountId, QStringLiteral("9007199254740993"));
    QVERIFY(authenticated.token.epoch > attempt.epoch);
    QVERIFY(!session.accepts(attempt));
    QVERIFY(!session.authenticate(attempt, QStringLiteral("different-account")));
    QCOMPARE(spy.size(), 2);
    const auto first = qvariant_cast<SessionSnapshot>(spy.at(0).at(0));
    QCOMPARE(first.phase, SessionPhase::Authenticating);
    QCOMPARE(first.token, attempt);
}

void SessionCoordinatorTest::supersededAttempt()
{
    SessionCoordinator session;
    const auto first = session.beginAuthentication();
    const auto second = session.beginAuthentication();
    QVERIFY(!session.authenticate(first, QStringLiteral("account-A")));
    QVERIFY(!session.cancelAuthentication(first));
    QVERIFY(session.authenticate(second, QStringLiteral("account-B")));
    QCOMPARE(session.snapshot().accountId, QStringLiteral("account-B"));
}

void SessionCoordinatorTest::cancellation()
{
    SessionCoordinator session;
    const auto attempt = session.beginAuthentication();
    QVERIFY(session.cancelAuthentication(attempt));
    QCOMPARE(session.snapshot().phase, SessionPhase::Guest);
    QVERIFY(!session.authenticate(attempt, QStringLiteral("late-login")));
    QVERIFY(!session.cancelAuthentication(attempt));
}

void SessionCoordinatorTest::expirationAndLogout()
{
    SessionCoordinator session;
    QVERIFY(session.authenticate(session.beginAuthentication(), QStringLiteral("A")));
    const auto accountA = session.snapshot().token;
    QVERIFY(session.expire(accountA));
    QCOMPARE(session.snapshot().phase, SessionPhase::Expired);
    QVERIFY(session.snapshot().accountId.isEmpty());
    QVERIFY(!session.expire(accountA));
    QVERIFY(session.authenticate(session.beginAuthentication(), QStringLiteral("B")));
    const auto accountB = session.snapshot().token;
    QVERIFY(!session.expire(accountA));
    QVERIFY(session.accepts(accountB));
    session.signOut();
    QVERIFY(!session.accepts(accountB));
    QVERIFY(session.snapshot().accountId.isEmpty());
    QCOMPARE(session.snapshot().phase, SessionPhase::Guest);
    const auto guest = session.snapshot().token;
    session.signOut();
    QVERIFY(!session.accepts(guest));
}

void SessionCoordinatorTest::foreignCoordinator()
{
    SessionCoordinator first;
    SessionCoordinator second;
    const auto a = first.beginAuthentication();
    const auto b = second.beginAuthentication();
    QCOMPARE(a.epoch, b.epoch);
    QVERIFY(a.coordinatorId != b.coordinatorId);
    QVERIFY(!second.authenticate(a, QStringLiteral("A")));
    QVERIFY(!first.accepts(b));
}

void SessionCoordinatorTest::reentrantNotification()
{
    SessionCoordinator session;
    connect(&session, &SessionCoordinator::changed, &session, [&session](SessionSnapshot snapshot) {
        if (snapshot.phase == SessionPhase::Authenticating) {
            session.signOut();
        }
    });
    const auto canceledDuringNotification = session.beginAuthentication();
    QVERIFY(!session.accepts(canceledDuringNotification));
    QCOMPARE(session.snapshot().phase, SessionPhase::Guest);
    QVERIFY(!session.authenticate(canceledDuringNotification, QStringLiteral("late")));
}

void SessionCoordinatorTest::reentrantAuthenticationResult()
{
    SessionCoordinator session;
    const auto attempt = session.beginAuthentication();
    connect(&session, &SessionCoordinator::changed, &session, [&session](SessionSnapshot snapshot) {
        if (snapshot.phase == SessionPhase::Authenticated) {
            session.signOut();
        }
    });
    const auto promoted = session.authenticate(attempt, QStringLiteral("A"));
    QVERIFY(promoted.has_value());
    QCOMPARE(promoted->epoch, attempt.epoch + 1);
    QVERIFY(!session.accepts(*promoted));
    QVERIFY(promoted->epoch < session.snapshot().token.epoch);
    QCOMPARE(session.snapshot().phase, SessionPhase::Guest);
}

QTEST_GUILESS_MAIN(SessionCoordinatorTest)
#include "session_coordinator_test.moc"
