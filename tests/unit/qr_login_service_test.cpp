#include "../support/fake_login_scheduler.h"
#include "application/account/qr_login_service.h"

#include <QSignalSpy>
#include <QtTest>

using namespace cirrus::application;
using namespace cirrus::domain;
using cirrus::test::FakeLoginScheduler;
using Ms = std::chrono::milliseconds;

// This target links only application/Core/Test, never Network or infrastructure.
class FakeGateway : public IQrLoginGateway
{
  public:
    SessionToken current, source, target;
    quint64 sequence = 0, keyId = 0, pollId = 0, identityId = 0, promotionId = 0;
    int promotions = 0, keys = 0, checks = 0, identities = 0;
    SessionToken lastCheck;
    bool syncAccepted = true;
    QList<quint64> canceled;
    bool synchronize(SessionToken context) override
    {
        if (syncAccepted)
            current = context;
        return syncAccepted;
    }
    quint64 requestKey(SessionToken) override
    {
        ++keys;
        return keyId = ++sequence;
    }
    quint64 checkStatus(SessionToken context, QString) override
    {
        ++checks;
        lastCheck = context;
        return pollId = ++sequence;
    }
    quint64 verifyIdentity(SessionToken) override
    {
        ++identities;
        return identityId = ++sequence;
    }
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
    void status(SessionToken token, QrLoginStatus state)
    {
        emit statusCompleted(pollId, token, state);
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

    // B-010: automatic polling and total authentication deadline (virtual time).
    void optionBounds_data();
    void optionBounds();
    void invalidOptionsNoNetwork_data();
    void invalidOptionsNoNetwork();
    void manualModeNeverPollsItself();
    void autoPollPacing();
    void slowRequestNoBacklog();
    void manualPollRespectsAutoPacing();
    void expiredStopsPolling();
    void authorizedVerifiesOnce();
    void failuresNotRetried_data();
    void failuresNotRetried();
    void deadlineCoversEveryStage_data();
    void deadlineCoversEveryStage();
    void lateSuccessLosesToDeadline_data();
    void lateSuccessLosesToDeadline();
    void timeoutAndResultSameTurn();
    void slowCommittingListenerCannotStretchDeadline();
    void newBeginReplacesSchedule();
    void lateOldTimerIgnored();
    void externalSignOutStopsSchedule();
    void reentrantCancelFromStatus();
    void reentrantBeginFromChallenge();
    void reentrantRestartDuringPrepare();
    void authenticatedSurvivesOldDeadline();
    void destructionStopsTimers();
    void qtSchedulerSmoke();
    void qtTimersDriveService();
};

namespace
{
QrLoginOptions autoOptions(int interval = 3000, int timeout = 180000)
{
    QrLoginOptions o;
    o.autoPoll = true;
    o.pollInterval = Ms(interval);
    o.totalTimeout = Ms(timeout);
    return o;
}

struct Rig {
    SessionCoordinator session;
    FakeGateway gateway;
    FakeLoginScheduler clock;
    std::unique_ptr<QrLoginService> service;
    QSignalSpy failed;
    QSignalSpy ready;
    QSignalSpy statuses;
    explicit Rig(QrLoginOptions options = autoOptions())
        : service(std::make_unique<QrLoginService>(session, gateway, options, &clock)),
          failed(service.get(), &QrLoginService::failed),
          ready(service.get(), &QrLoginService::signedIn),
          statuses(service.get(), &QrLoginService::statusChanged)
    {
    }
    [[nodiscard]] AuthFailure failure(int i = 0) const
    {
        return qvariant_cast<AuthFailure>(failed.at(i).at(1));
    }
    // Drive one attempt to Committing; returns the attempt token.
    SessionToken toCommitting()
    {
        const auto attempt = service->begin();
        gateway.key(attempt);
        clock.advance(service->options().pollInterval);
        gateway.authorized(attempt);
        gateway.verified(attempt);
        return attempt;
    }
};
} // namespace

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

void QrLoginServiceTest::optionBounds_data()
{
    QTest::addColumn<int>("interval");
    QTest::addColumn<int>("timeout");
    QTest::addColumn<bool>("valid");
    QTest::newRow("defaults") << 3000 << 180000 << true;
    QTest::newRow("min-interval-equals-timeout") << 1000 << 1000 << true;
    QTest::newRow("max-bounds") << 60000 << 600000 << true;
    QTest::newRow("zero-interval") << 0 << 180000 << false;
    QTest::newRow("negative-interval") << -1 << 180000 << false;
    QTest::newRow("interval-too-small") << 999 << 180000 << false;
    QTest::newRow("interval-too-large") << 60001 << 600000 << false;
    QTest::newRow("timeout-below-interval") << 3000 << 2999 << false;
    QTest::newRow("timeout-too-large") << 3000 << 600001 << false;
}
void QrLoginServiceTest::optionBounds()
{
    QFETCH(int, interval);
    QFETCH(int, timeout);
    QFETCH(bool, valid);
    QrLoginOptions defaults;
    QVERIFY(!defaults.autoPoll);
    QCOMPARE(defaults.pollInterval, Ms(3000));
    QCOMPARE(defaults.totalTimeout, Ms(180000));
    for (const bool automatic : {false, true}) {
        auto o = autoOptions(interval, timeout);
        o.autoPoll = automatic;
        QCOMPARE(o.isValid(), valid);
    }
}

void QrLoginServiceTest::invalidOptionsNoNetwork_data()
{
    QTest::addColumn<int>("interval");
    QTest::addColumn<int>("timeout");
    QTest::newRow("zero-interval") << 0 << 180000;
    QTest::newRow("interval-too-small") << 999 << 180000;
    QTest::newRow("interval-too-large") << 60001 << 600000;
    QTest::newRow("timeout-below-interval") << 3000 << 2999;
    QTest::newRow("timeout-too-large") << 3000 << 600001;
}
void QrLoginServiceTest::invalidOptionsNoNetwork()
{
    QFETCH(int, interval);
    QFETCH(int, timeout);
    for (const bool automatic : {false, true}) {
        auto o = autoOptions(interval, timeout);
        o.autoPoll = automatic;
        Rig r(o);
        const auto attempt = r.service->begin();
        QVERIFY(!r.session.accepts(attempt));
        QCOMPARE(r.session.snapshot().phase, SessionPhase::Guest);
        QCOMPARE(r.gateway.keys, 0);
        QCOMPARE(r.failed.size(), 1);
        QCOMPARE(r.failure().error, AuthError::InvalidRequest);
        QCOMPARE(qvariant_cast<SessionToken>(r.failed.at(0).at(0)), r.session.snapshot().token);
        QCOMPARE(r.clock.pending(), 0);
        QVERIFY(!r.service->poll());
        r.clock.advance(Ms(700000));
        QCOMPARE(r.failed.size(), 1);
        QCOMPARE(r.gateway.keys + r.gateway.checks, 0);
    }
}

void QrLoginServiceTest::manualModeNeverPollsItself()
{
    Rig r(QrLoginOptions{});
    const auto attempt = r.service->begin();
    r.gateway.key(attempt);
    r.clock.advance(Ms(179999));
    QCOMPARE(r.gateway.checks, 0);
    QVERIFY(r.service->poll()); // caller pacing, no earliest time in manual mode
    r.gateway.status(attempt, QrLoginStatus::WaitingForScan);
    QVERIFY(r.service->poll());
    QCOMPARE(r.gateway.checks, 2);
    r.gateway.status(attempt, QrLoginStatus::WaitingForScan);
    QCOMPARE(r.clock.pending(), 1); // deadline only, never a poll timer
    r.clock.advance(Ms(1));
    QCOMPARE(r.gateway.checks, 2);
    QCOMPARE(r.failed.size(), 1);
    QCOMPARE(r.failure().error, AuthError::Timeout); // manual mode is bounded too
}

void QrLoginServiceTest::autoPollPacing()
{
    Rig r;
    const auto attempt = r.service->begin();
    r.clock.advance(Ms(5000)); // key still pending: never check
    QCOMPARE(r.gateway.checks, 0);
    r.gateway.key(attempt);
    r.clock.advance(Ms(2999));
    QCOMPARE(r.gateway.checks, 0);
    r.clock.advance(Ms(1));
    QCOMPARE(r.gateway.checks, 1);
    QCOMPARE(r.gateway.lastCheck, attempt);
    r.clock.advance(Ms(1000));
    r.gateway.status(attempt, QrLoginStatus::WaitingForScan); // completes at 9000
    r.clock.advance(Ms(2999));
    QCOMPARE(r.gateway.checks, 1);
    r.clock.advance(Ms(1));
    QCOMPARE(r.gateway.checks, 2);
    r.gateway.status(attempt, QrLoginStatus::WaitingForConfirmation);
    r.clock.advance(Ms(3000));
    QCOMPARE(r.gateway.checks, 3);
    QCOMPARE(r.statuses.size(), 2);
    QCOMPARE(r.failed.size(), 0);
}

void QrLoginServiceTest::slowRequestNoBacklog()
{
    Rig r;
    const auto attempt = r.service->begin();
    r.gateway.key(attempt);
    r.clock.advance(Ms(3000));
    QCOMPARE(r.gateway.checks, 1);
    r.clock.advance(Ms(60000)); // one slow request spanning many intervals
    QCOMPARE(r.gateway.checks, 1);
    QCOMPARE(r.clock.pending(), 1); // deadline only
    r.gateway.status(attempt, QrLoginStatus::WaitingForScan);
    r.clock.advance(Ms(2999));
    QCOMPARE(r.gateway.checks, 1); // no catch-up burst
    r.clock.advance(Ms(1));
    QCOMPARE(r.gateway.checks, 2);
}

void QrLoginServiceTest::manualPollRespectsAutoPacing()
{
    Rig r;
    const auto attempt = r.service->begin();
    QVERIFY(!r.service->poll()); // no key yet
    r.gateway.key(attempt);
    QVERIFY(!r.service->poll());
    r.clock.jump(Ms(2999));
    QVERIFY(!r.service->poll());
    QCOMPARE(r.failed.size(), 0);
    QCOMPARE(r.gateway.checks, 0);
    r.clock.jump(Ms(1)); // due, but timer not yet delivered (busy loop)
    QVERIFY(r.service->poll());
    QVERIFY(!r.service->poll()); // single flight
    r.clock.deliverDue();        // stale-timing timer must not double-send
    r.clock.advance(Ms(10000));
    QCOMPARE(r.gateway.checks, 1);
    r.gateway.status(attempt, QrLoginStatus::WaitingForScan);
    QVERIFY(!r.service->poll());
    r.clock.advance(Ms(3000));
    QCOMPARE(r.gateway.checks, 2);
    QVERIFY(!r.service->poll());
    QCOMPARE(r.gateway.checks, 2);
    QCOMPARE(r.failed.size(), 0);
}

void QrLoginServiceTest::expiredStopsPolling()
{
    Rig r;
    const auto attempt = r.service->begin();
    r.gateway.key(attempt);
    r.clock.advance(Ms(3000));
    r.gateway.status(attempt, QrLoginStatus::Expired);
    QCOMPARE(r.session.snapshot().phase, SessionPhase::Guest);
    QCOMPARE(r.statuses.size(), 1);
    QCOMPARE(r.clock.pending(), 0);
    r.clock.advance(Ms(600000));
    QCOMPARE(r.gateway.checks, 1);
    QCOMPARE(r.gateway.keys, 1); // no automatic key refresh
    QCOMPARE(r.failed.size(), 0);
}

void QrLoginServiceTest::authorizedVerifiesOnce()
{
    Rig r;
    const auto attempt = r.service->begin();
    r.gateway.key(attempt);
    r.clock.advance(Ms(3000));
    r.gateway.authorized(attempt);
    QCOMPARE(r.gateway.identities, 1);
    QCOMPARE(r.clock.pending(), 1); // deadline only
    r.clock.advance(Ms(30000));
    QVERIFY(!r.service->poll());
    QCOMPARE(r.gateway.checks, 1);
    QCOMPARE(r.gateway.identities, 1);
    r.gateway.verified(attempt);
    r.gateway.promoted();
    QCOMPARE(r.ready.size(), 1);
    QCOMPARE(r.gateway.checks, 1);
}

void QrLoginServiceTest::failuresNotRetried_data()
{
    QTest::addColumn<int>("error");
    QTest::addColumn<int>("retryAfter"); // -1: absent
    QTest::newRow("rate-limited") << int(AuthError::RateLimited) << 5000;
    QTest::newRow("rate-limited-no-hint") << int(AuthError::RateLimited) << -1;
    QTest::newRow("network") << int(AuthError::Network) << -1;
    QTest::newRow("request-timeout") << int(AuthError::Timeout) << -1;
    QTest::newRow("protocol") << int(AuthError::ProtocolFailure) << -1;
}
void QrLoginServiceTest::failuresNotRetried()
{
    QFETCH(int, error);
    QFETCH(int, retryAfter);
    Rig r;
    const auto attempt = r.service->begin();
    r.gateway.key(attempt);
    r.clock.advance(Ms(3000));
    AuthFailure failure{AuthError(error)};
    if (retryAfter >= 0)
        failure.retryAfterMs = retryAfter;
    emit r.gateway.statusCompleted(r.gateway.pollId, attempt, failure);
    QCOMPARE(r.session.snapshot().phase, SessionPhase::Guest);
    QCOMPARE(r.failed.size(), 1);
    QCOMPARE(r.failure().error, AuthError(error));
    QCOMPARE(r.failure().retryAfterMs.has_value(), retryAfter >= 0);
    if (retryAfter >= 0)
        QCOMPARE(*r.failure().retryAfterMs, qint64(retryAfter));
    QCOMPARE(r.clock.pending(), 0);
    r.clock.advance(Ms(600000));
    QCOMPARE(r.gateway.checks, 1);
    QCOMPARE(r.gateway.keys, 1);
    QCOMPARE(r.failed.size(), 1);
}

void QrLoginServiceTest::deadlineCoversEveryStage_data()
{
    QTest::addColumn<int>(
        "stage"); // 0 key, 1 between polls, 2 poll in flight, 3 identity, 4 commit
    QTest::newRow("key") << 0;
    QTest::newRow("waiting-for-next-poll") << 1;
    QTest::newRow("poll-in-flight") << 2;
    QTest::newRow("identity") << 3;
    QTest::newRow("committing") << 4;
}
void QrLoginServiceTest::deadlineCoversEveryStage()
{
    QFETCH(int, stage);
    Rig r(autoOptions(1000, 10000));
    const auto attempt = r.service->begin();
    if (stage >= 1) {
        r.clock.advance(Ms(500));
        r.gateway.key(attempt);
    }
    if (stage >= 1)
        r.clock.advance(Ms(1000)); // first check sent at 1500
    if (stage == 1)
        r.gateway.status(attempt, QrLoginStatus::WaitingForScan);
    if (stage >= 3)
        r.gateway.authorized(attempt);
    if (stage >= 4) {
        r.gateway.verified(attempt);
        QCOMPARE(r.session.snapshot().phase, SessionPhase::Committing); // epoch rotated
    }
    const auto context = r.session.snapshot().token;
    const auto rest = Ms(10000) - r.clock.now();
    r.clock.advance(rest - Ms(1));
    QCOMPARE(r.failed.size(), 0);
    QVERIFY(r.session.accepts(context)); // the epoch change did not restart the clock
    r.clock.advance(Ms(1));
    QCOMPARE(r.failed.size(), 1);
    QCOMPARE(r.failure().error, AuthError::Timeout);
    QCOMPARE(r.session.snapshot().phase, SessionPhase::Guest);
    QCOMPARE(r.statuses.size(), stage == 1 || stage >= 3 ? 1 : 0); // never fake 800
    QCOMPARE(r.gateway.current, r.session.snapshot().token);       // staged credentials cleared
    const quint64 inFlight[]{r.gateway.keyId, 0, r.gateway.pollId, r.gateway.identityId,
                             r.gateway.promotionId};
    if (inFlight[stage] != 0)
        QVERIFY(r.gateway.canceled.contains(inFlight[stage]));
    QCOMPARE(r.clock.pending(), 0);
    const auto checksAtTimeout = r.gateway.checks;
    r.clock.advance(Ms(60000));
    QCOMPARE(r.gateway.checks, checksAtTimeout);
    QCOMPARE(r.failed.size(), 1);
    if (stage == 4)
        r.gateway.promoted(); // late acknowledgement for a dead target
    QCOMPARE(r.ready.size(), 0);
}

void QrLoginServiceTest::lateSuccessLosesToDeadline_data()
{
    QTest::addColumn<int>("stage"); // 0 key, 1 803, 2 identity, 3 promotion
    QTest::addColumn<int>("overshoot");
    for (const int over : {0, 5000}) {
        const QByteArray suffix = over == 0 ? "-exact" : "-overdue";
        QTest::newRow(("key" + suffix).constData()) << 0 << over;
        QTest::newRow(("authorized" + suffix).constData()) << 1 << over;
        QTest::newRow(("identity" + suffix).constData()) << 2 << over;
        QTest::newRow(("promotion" + suffix).constData()) << 3 << over;
    }
}
void QrLoginServiceTest::lateSuccessLosesToDeadline()
{
    QFETCH(int, stage);
    QFETCH(int, overshoot);
    Rig r(autoOptions(1000, 10000));
    const auto attempt = r.service->begin();
    if (stage >= 1) {
        r.gateway.key(attempt);
        r.clock.advance(Ms(1000));
    }
    if (stage >= 2)
        r.gateway.authorized(attempt);
    if (stage >= 3)
        r.gateway.verified(attempt);
    const auto identities = r.gateway.identities;
    const auto promotions = r.gateway.promotions;
    // Event loop was busy: deadline passed but its timer has not been delivered yet.
    r.clock.jump(Ms(10000) + Ms(overshoot) - r.clock.now());
    if (stage == 0)
        r.gateway.key(attempt);
    if (stage == 1)
        r.gateway.authorized(attempt);
    if (stage == 2)
        r.gateway.verified(attempt);
    if (stage == 3)
        r.gateway.promoted();
    QCOMPARE(r.failed.size(), 1);
    QCOMPARE(r.failure().error, AuthError::Timeout);
    QCOMPARE(r.ready.size(), 0);
    QCOMPARE(r.gateway.identities, identities); // no verify after expiry
    QCOMPARE(r.gateway.promotions, promotions); // no prepare/promote after expiry
    QCOMPARE(r.session.snapshot().phase, SessionPhase::Guest);
    QCOMPARE(r.gateway.current, r.session.snapshot().token);
    r.clock.deliverDue(); // the late deadline event must not fail twice
    r.clock.advance(Ms(60000));
    QCOMPARE(r.failed.size(), 1);
    QCOMPARE(r.gateway.checks, stage >= 1 ? 1 : 0);
}

void QrLoginServiceTest::timeoutAndResultSameTurn()
{
    Rig r(autoOptions(1000, 10000));
    const auto attempt = r.service->begin();
    r.gateway.key(attempt);
    r.clock.advance(Ms(1000));
    r.clock.advance(Ms(9000)); // deadline delivered first
    QCOMPARE(r.failed.size(), 1);
    r.gateway.authorized(attempt); // result queued in the same turn, now stale
    QCOMPARE(r.gateway.identities, 0);
    QCOMPARE(r.failed.size(), 1);
    QCOMPARE(r.statuses.size(), 0);
}

void QrLoginServiceTest::slowCommittingListenerCannotStretchDeadline()
{
    Rig r(autoOptions(1000, 10000));
    connect(&r.session, &SessionCoordinator::changed, this, [&](SessionSnapshot snapshot) {
        if (snapshot.phase == SessionPhase::Committing)
            r.clock.jump(Ms(20000)); // listener blocked past the deadline
    });
    r.toCommitting();
    QCOMPARE(r.gateway.promotions, 0);
    QCOMPARE(r.failed.size(), 1);
    QCOMPARE(r.failure().error, AuthError::Timeout);
    QCOMPARE(r.session.snapshot().phase, SessionPhase::Guest);
    r.clock.deliverDue();
    QCOMPARE(r.failed.size(), 1);
}

void QrLoginServiceTest::newBeginReplacesSchedule()
{
    Rig r(autoOptions(1000, 10000));
    const auto first = r.service->begin();
    r.gateway.key(first);
    r.clock.advance(Ms(500));
    const auto second = r.service->begin(); // invalidates first; deadline restarts at 500
    QVERIFY(second != first);
    QCOMPARE(r.clock.pending(), 1);
    r.clock.advance(Ms(5000));
    QCOMPARE(r.gateway.checks, 0); // first's poll timer is gone; second has no key yet
    r.gateway.key(second);
    r.clock.advance(Ms(1000));
    QCOMPARE(r.gateway.checks, 1);
    QCOMPARE(r.gateway.lastCheck, second);
    r.gateway.status(second, QrLoginStatus::WaitingForScan);
    r.clock.advance(Ms(3499)); // t = 9999 < 10500
    QCOMPARE(r.failed.size(), 0);
    r.clock.advance(Ms(501));
    QCOMPARE(r.failed.size(), 1);
    QVERIFY(!r.session.accepts(second));
}

void QrLoginServiceTest::lateOldTimerIgnored()
{
    Rig r(autoOptions(1000, 10000));
    r.clock.ignoreStop = true; // old callbacks still run after stop()
    const auto first = r.service->begin();
    r.gateway.key(first);
    r.clock.advance(Ms(500));
    const auto second = r.service->begin();
    r.clock.advance(Ms(9999)); // first's poll (1500) and deadline (10000) fire late
    QCOMPARE(r.failed.size(), 0);
    QCOMPARE(r.gateway.checks, 0);
    QVERIFY(r.session.accepts(second));
    r.clock.advance(Ms(1));
    QCOMPARE(r.failed.size(), 1);
    QCOMPARE(r.failure().error, AuthError::Timeout);
}

void QrLoginServiceTest::externalSignOutStopsSchedule()
{
    Rig r;
    const auto attempt = r.service->begin();
    r.gateway.key(attempt);
    r.session.signOut();
    QCOMPARE(r.clock.pending(), 0);
    r.clock.advance(Ms(600000));
    QCOMPARE(r.gateway.checks, 0);
    QCOMPARE(r.failed.size(), 0);
    QCOMPARE(r.session.snapshot().phase, SessionPhase::Guest);
}

void QrLoginServiceTest::reentrantCancelFromStatus()
{
    Rig r;
    connect(r.service.get(), &QrLoginService::statusChanged, this,
            [&](SessionToken, QrLoginStatus) { r.service->cancel(); });
    const auto attempt = r.service->begin();
    r.gateway.key(attempt);
    r.clock.advance(Ms(3000));
    r.gateway.status(attempt, QrLoginStatus::WaitingForScan);
    QCOMPARE(r.session.snapshot().phase, SessionPhase::Guest);
    QCOMPARE(r.clock.pending(), 0);
    r.clock.advance(Ms(600000));
    QCOMPARE(r.gateway.checks, 1);
    QCOMPARE(r.failed.size(), 0);
}

void QrLoginServiceTest::reentrantBeginFromChallenge()
{
    Rig r(autoOptions(1000, 10000));
    SessionToken restarted;
    bool once = true;
    connect(r.service.get(), &QrLoginService::challengeReady, this, [&](SessionToken) {
        if (std::exchange(once, false)) {
            r.clock.jump(Ms(2000));
            restarted = r.service->begin();
        }
    });
    const auto first = r.service->begin();
    r.gateway.key(first);
    QVERIFY(r.session.accepts(restarted));
    QCOMPARE(r.clock.pending(), 1); // only the new attempt's deadline
    r.clock.advance(Ms(5000));
    QCOMPARE(r.gateway.checks, 0);
    r.gateway.key(restarted);
    r.clock.advance(Ms(1000));
    QCOMPARE(r.gateway.lastCheck, restarted);
    r.clock.advance(Ms(10000) - r.clock.now() + Ms(1999)); // first deadline is irrelevant
    QCOMPARE(r.failed.size(), 0);
    r.clock.advance(Ms(1));
    QCOMPARE(r.failed.size(), 1);
}

void QrLoginServiceTest::reentrantRestartDuringPrepare()
{
    Rig r(autoOptions(1000, 10000));
    SessionToken restarted;
    connect(&r.session, &SessionCoordinator::changed, this, [&](SessionSnapshot snapshot) {
        if (snapshot.phase == SessionPhase::Committing && !restarted.isValid()) {
            r.clock.jump(Ms(3000));
            restarted = r.service->begin();
        }
    });
    r.toCommitting(); // prepare at t=1000, restart at t=4000
    QCOMPARE(r.gateway.promotions, 0);
    QVERIFY(r.session.accepts(restarted));
    QCOMPARE(r.session.snapshot().phase, SessionPhase::Authenticating);
    QCOMPARE(r.clock.pending(), 1);
    r.clock.advance(Ms(9999));
    QCOMPARE(r.failed.size(), 0);
    r.clock.advance(Ms(1));
    QCOMPARE(r.failed.size(), 1);
}

void QrLoginServiceTest::authenticatedSurvivesOldDeadline()
{
    for (const bool lateCallbacks : {false, true}) {
        Rig r(autoOptions(1000, 10000));
        r.clock.ignoreStop = lateCallbacks;
        const auto attempt = r.toCommitting();
        Q_UNUSED(attempt);
        r.clock.advance(Ms(8998));
        r.gateway.promoted(); // t = 9999
        QCOMPARE(r.ready.size(), 1);
        const auto signedIn = r.session.snapshot();
        QCOMPARE(signedIn.phase, SessionPhase::Authenticated);
        QCOMPARE(r.clock.pending(), lateCallbacks ? 1 : 0);
        r.clock.advance(Ms(600000));
        QCOMPARE(r.session.snapshot(), signedIn);
        QCOMPARE(r.failed.size(), 0);
        QVERIFY(!r.service->poll());
    }
}

void QrLoginServiceTest::destructionStopsTimers()
{
    Rig r;
    const auto attempt = r.service->begin();
    r.gateway.key(attempt);
    r.service.reset();
    QCOMPARE(r.clock.pending(), 0);
    r.clock.advance(Ms(600000));
    QCOMPARE(r.gateway.checks, 0);
    QCOMPARE(r.session.snapshot().phase, SessionPhase::Guest);
}

void QrLoginServiceTest::qtSchedulerSmoke()
{
    QtLoginScheduler scheduler;
    const auto before = scheduler.now();
    int fired = 0, stopped = 0, nested = 0;
    const auto victim = scheduler.start(Ms(10), [&] { ++stopped; });
    [[maybe_unused]] const auto first = scheduler.start(Ms(0), [&] {
        ++fired;
        scheduler.stop(victim);
        [[maybe_unused]] const auto id =
            scheduler.start(Ms(5), [&] { ++nested; }); // reentrant start inside callback
    });
    QCOMPARE(fired, 0); // never synchronous
    QTRY_COMPARE_WITH_TIMEOUT(nested, 1, 2000);
    QTest::qWait(30);
    QCOMPARE(fired, 1); // one-shot
    QCOMPARE(stopped, 0);
    QVERIFY(scheduler.now() >= before + Ms(5));
    scheduler.stop(victim); // stopping twice/after-run is harmless
}

void QrLoginServiceTest::qtTimersDriveService()
{
    // Bounded real event loop: proves the Qt timer wiring, not the policy boundaries.
    SessionCoordinator session;
    FakeGateway gateway;
    QrLoginService service(session, gateway, autoOptions(1000, 1500));
    QSignalSpy failed(&service, &QrLoginService::failed);
    const auto attempt = service.begin();
    gateway.key(attempt);
    QCOMPARE(gateway.checks, 0);
    QTRY_COMPARE_WITH_TIMEOUT(gateway.checks, 1, 3000);
    QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 3000);
    QCOMPARE(qvariant_cast<AuthFailure>(failed.at(0).at(1)).error, AuthError::Timeout);
    QVERIFY(gateway.canceled.contains(gateway.pollId));
    QCOMPARE(session.snapshot().phase, SessionPhase::Guest);
}

QTEST_GUILESS_MAIN(QrLoginServiceTest)
#include "qr_login_service_test.moc"
