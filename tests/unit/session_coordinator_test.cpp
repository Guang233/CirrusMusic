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
    void twoPhaseAuthentication();
    void supersededAttempt();
    void cancellation_data();
    void cancellation();
    void expirationAndLogout();
    void foreignCoordinator();
    void reentrantNotification();
    void reentrantPreparation();
    void reentrantCompletion();
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
    QVERIFY(!session.prepareAuthentication(snapshot.token, QStringLiteral("123")));
    QVERIFY(!session.completeAuthentication(snapshot.token));
    QVERIFY(!session.expire(snapshot.token));
}

void SessionCoordinatorTest::twoPhaseAuthentication()
{
    SessionCoordinator session;
    QSignalSpy spy(&session, &SessionCoordinator::changed);
    const auto guest = session.snapshot().token;
    const auto attempt = session.beginAuthentication();
    QVERIFY(!session.accepts(guest));
    QVERIFY(!session.prepareAuthentication(attempt, {}));
    QVERIFY(!session.prepareAuthentication(attempt, QStringLiteral("  ")));
    QVERIFY(!session.completeAuthentication(attempt));
    const auto prepared =
        session.prepareAuthentication(attempt, QStringLiteral("9007199254740993"));
    QVERIFY(prepared);
    QCOMPARE(session.snapshot().phase, SessionPhase::Committing);
    QCOMPARE(prepared->epoch, attempt.epoch + 1);
    QVERIFY(!session.accepts(attempt));
    QVERIFY(!session.expire(*prepared));
    QVERIFY(!session.prepareAuthentication(*prepared, QStringLiteral("other")));
    QVERIFY(session.completeAuthentication(*prepared));
    QCOMPARE(session.snapshot().phase, SessionPhase::Authenticated);
    QCOMPARE(session.snapshot().accountId, QStringLiteral("9007199254740993"));
    QCOMPARE(session.snapshot().token, *prepared);
    QVERIFY(!session.completeAuthentication(*prepared));
    QCOMPARE(spy.size(), 3);
}

void SessionCoordinatorTest::supersededAttempt()
{
    SessionCoordinator session;
    const auto first = session.beginAuthentication();
    const auto prepared = session.prepareAuthentication(first, QStringLiteral("A"));
    QVERIFY(prepared);
    const auto second = session.beginAuthentication();
    QVERIFY(!session.completeAuthentication(*prepared));
    QVERIFY(!session.prepareAuthentication(first, QStringLiteral("A")));
    QVERIFY(!session.cancelAuthentication(*prepared));
    QVERIFY(session.accepts(second));
}

void SessionCoordinatorTest::cancellation_data()
{
    QTest::addColumn<bool>("prepare");
    QTest::newRow("authenticating") << false;
    QTest::newRow("committing") << true;
}
void SessionCoordinatorTest::cancellation()
{
    QFETCH(bool, prepare);
    SessionCoordinator session;
    auto token = session.beginAuthentication();
    if (prepare) {
        const auto next = session.prepareAuthentication(token, QStringLiteral("A"));
        QVERIFY(next);
        token = *next;
    }
    QVERIFY(session.cancelAuthentication(token));
    QCOMPARE(session.snapshot().phase, SessionPhase::Guest);
    QVERIFY(session.snapshot().accountId.isEmpty());
    QVERIFY(!session.completeAuthentication(token));
    QVERIFY(!session.cancelAuthentication(token));
}

void SessionCoordinatorTest::expirationAndLogout()
{
    SessionCoordinator session;
    const auto a =
        session.prepareAuthentication(session.beginAuthentication(), QStringLiteral("A"));
    QVERIFY(a && session.completeAuthentication(*a));
    QVERIFY(session.expire(*a));
    QCOMPARE(session.snapshot().phase, SessionPhase::Expired);
    QVERIFY(session.snapshot().accountId.isEmpty());
    const auto b =
        session.prepareAuthentication(session.beginAuthentication(), QStringLiteral("B"));
    QVERIFY(b && session.completeAuthentication(*b));
    QVERIFY(!session.expire(*a));
    session.signOut();
    QVERIFY(!session.accepts(*b));
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
    QVERIFY(!second.prepareAuthentication(a, QStringLiteral("A")));
    QVERIFY(!first.completeAuthentication(b));
}

void SessionCoordinatorTest::reentrantNotification()
{
    SessionCoordinator session;
    connect(&session, &SessionCoordinator::changed, &session, [&session](SessionSnapshot snapshot) {
        if (snapshot.phase == SessionPhase::Authenticating) {
            session.signOut();
        }
    });
    const auto attempt = session.beginAuthentication();
    QVERIFY(!session.accepts(attempt));
    QVERIFY(!session.prepareAuthentication(attempt, QStringLiteral("late")));
}

void SessionCoordinatorTest::reentrantPreparation()
{
    SessionCoordinator session;
    const auto attempt = session.beginAuthentication();
    connect(&session, &SessionCoordinator::changed, &session, [&session](SessionSnapshot snapshot) {
        if (snapshot.phase == SessionPhase::Committing) {
            session.signOut();
        }
    });
    const auto prepared = session.prepareAuthentication(attempt, QStringLiteral("A"));
    QVERIFY(prepared);
    QCOMPARE(prepared->epoch, attempt.epoch + 1);
    QVERIFY(!session.accepts(*prepared));
    QVERIFY(!session.completeAuthentication(*prepared));
    QCOMPARE(session.snapshot().phase, SessionPhase::Guest);
}

void SessionCoordinatorTest::reentrantCompletion()
{
    SessionCoordinator session;
    const auto prepared =
        session.prepareAuthentication(session.beginAuthentication(), QStringLiteral("A"));
    QVERIFY(prepared);
    connect(&session, &SessionCoordinator::changed, &session, [&session](SessionSnapshot snapshot) {
        if (snapshot.phase == SessionPhase::Authenticated) {
            session.signOut();
        }
    });
    QVERIFY(!session.completeAuthentication(*prepared));
    QCOMPARE(session.snapshot().phase, SessionPhase::Guest);
}

QTEST_GUILESS_MAIN(SessionCoordinatorTest)
#include "session_coordinator_test.moc"
