#include "qr_login_service.h"

#include <QThread>
#include <utility>

namespace cirrus::application
{

QrLoginService::QrLoginService(SessionCoordinator& session, IQrLoginGateway& gateway,
                               QObject* parent)
    : QrLoginService(session, gateway, QrLoginOptions{}, nullptr, parent)
{
}

QrLoginService::QrLoginService(SessionCoordinator& session, IQrLoginGateway& gateway,
                               QrLoginOptions options, ILoginScheduler* scheduler, QObject* parent)
    : QObject(parent), m_session(session), m_gateway(&gateway), m_scheduler(scheduler),
      m_options(options)
{
    Q_ASSERT(session.thread() == thread() && gateway.thread() == thread());
    if (!m_scheduler) {
        m_ownedScheduler = std::make_unique<QtLoginScheduler>();
        m_scheduler = m_ownedScheduler.get();
    }
    connect(&session, &SessionCoordinator::changed, this, &QrLoginService::sessionChanged);
    connect(&gateway, &IQrLoginGateway::keyCompleted, this, &QrLoginService::keyCompleted);
    connect(&gateway, &IQrLoginGateway::statusCompleted, this, &QrLoginService::statusCompleted);
    connect(&gateway, &IQrLoginGateway::identityCompleted, this,
            &QrLoginService::identityCompleted);
    connect(&gateway, &IQrLoginGateway::promotionCompleted, this,
            &QrLoginService::promotionCompleted);
    connect(&gateway, &QObject::destroyed, this, [this] { fail(m_context, {AuthError::Network}); });
    sessionChanged(session.snapshot());
}

QrLoginService::~QrLoginService()
{
    cancel();
    cancelRequests();
    endSchedule();
}

void QrLoginService::cancelRequests()
{
    const quint64 ids[]{std::exchange(m_keyRequest, 0), std::exchange(m_pollRequest, 0),
                        std::exchange(m_identityRequest, 0), std::exchange(m_promotionRequest, 0)};
    for (const auto id : ids) {
        if (id != 0 && m_gateway) {
            m_gateway->cancel(id);
        }
    }
}

void QrLoginService::endSchedule()
{
    // Invalidate first: a callback already dequeued by the scheduler sees a new generation.
    const auto old = std::exchange(m_schedule, Schedule{});
    if (old.deadlineTimer != 0) {
        m_scheduler->stop(old.deadlineTimer);
    }
    if (old.pollTimer != 0) {
        m_scheduler->stop(old.pollTimer);
    }
}

void QrLoginService::sessionChanged(domain::SessionSnapshot snapshot)
{
    if (!m_session.accepts(snapshot.token)) {
        return; // stale reentrant notification
    }
    // Only our own attempt -> prepared target keeps the attempt's schedule and deadline.
    // begin/cancel/logout/Authenticated or any other transition ends it.
    const bool continuation = std::exchange(m_preparing, false) && m_schedule.generation != 0 &&
                              snapshot.phase == domain::SessionPhase::Committing;
    if (!continuation) {
        endSchedule();
    }
    cancelRequests();
    if (!m_session.accepts(snapshot.token)) {
        return;
    }
    m_context = snapshot.token;
    m_key.clear();
    m_expectedAccount.clear();
    m_synchronized = {};
    // The credential receipt, not a normal clear-on-advance, owns this transition.
    if (snapshot.phase != domain::SessionPhase::Committing && m_gateway &&
        m_gateway->synchronize(snapshot.token) && m_session.accepts(snapshot.token)) {
        m_synchronized = snapshot.token;
    }
}

domain::SessionToken QrLoginService::begin()
{
    Q_ASSERT(QThread::currentThread() == thread());
    const auto started = m_scheduler->now();
    const auto options = m_options; // fixed for this attempt
    const auto context = m_session.beginAuthentication();
    if (!m_session.accepts(context)) {
        return context;
    }
    if (!options.isValid()) {
        fail(context, {AuthError::InvalidRequest});
        return context;
    }
    if (!m_gateway || m_synchronized != context) {
        fail(context, {AuthError::StaleContext});
        return context;
    }
    m_schedule.generation = ++m_generations;
    m_schedule.autoPoll = options.autoPoll;
    m_schedule.interval = options.pollInterval;
    m_schedule.deadline = started + options.totalTimeout;
    armDeadline();
    m_keyRequest = m_gateway->requestKey(context);
    return context;
}

void QrLoginService::armDeadline()
{
    const auto remaining = std::max(m_schedule.deadline - m_scheduler->now(), Milliseconds(0));
    const auto generation = m_schedule.generation;
    m_schedule.deadlineTimer =
        m_scheduler->start(remaining, [this, generation] { deadlineReached(generation); });
}

bool QrLoginService::pastDeadline() const
{
    return m_schedule.generation != 0 && m_scheduler->now() >= m_schedule.deadline;
}

bool QrLoginService::failIfTimedOut()
{
    if (!pastDeadline()) {
        return false;
    }
    // Never published as server-side QR expiry; cleanup happens in sessionChanged.
    fail(m_context, {AuthError::Timeout});
    return true;
}

void QrLoginService::deadlineReached(quint64 generation)
{
    if (generation == 0 || generation != m_schedule.generation) {
        return;
    }
    m_schedule.deadlineTimer = 0;
    if (!pastDeadline()) {
        armDeadline(); // early wakeup: wait for the remaining monotonic time
        return;
    }
    const auto phase = m_session.snapshot().phase;
    if (m_session.accepts(m_context) && (phase == domain::SessionPhase::Authenticating ||
                                         phase == domain::SessionPhase::Committing)) {
        fail(m_context, {AuthError::Timeout});
    } else {
        endSchedule();
    }
}

void QrLoginService::stopPollTimer()
{
    if (const auto id = std::exchange(m_schedule.pollTimer, 0)) {
        m_scheduler->stop(id);
    }
}

void QrLoginService::schedulePoll()
{
    if (m_schedule.generation == 0 || !m_schedule.autoPoll) {
        return;
    }
    stopPollTimer();
    // Paced from the previous completion, so a slow request never accumulates timers.
    m_schedule.nextPollAt = m_scheduler->now() + m_schedule.interval;
    const auto generation = m_schedule.generation;
    m_schedule.pollTimer =
        m_scheduler->start(m_schedule.interval, [this, generation] { pollDue(generation); });
}

void QrLoginService::pollDue(quint64 generation)
{
    if (generation == 0 || generation != m_schedule.generation) {
        return;
    }
    m_schedule.pollTimer = 0;
    if (failIfTimedOut()) {
        return;
    }
    const auto remaining = m_schedule.nextPollAt - m_scheduler->now();
    if (remaining > Milliseconds(0)) {
        m_schedule.pollTimer =
            m_scheduler->start(remaining, [this, generation] { pollDue(generation); });
        return;
    }
    const bool sent = poll();
    Q_UNUSED(sent); // a concurrent manual poll already owns the single slot
}

bool QrLoginService::poll()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!m_gateway || !m_session.accepts(m_context) || m_key.isEmpty() ||
        m_session.snapshot().phase != domain::SessionPhase::Authenticating || m_pollRequest != 0 ||
        m_identityRequest != 0 || m_promotionRequest != 0 || m_schedule.generation == 0) {
        return false;
    }
    // Past deadline: the deadline callback reports Timeout asynchronously, exactly once.
    if (pastDeadline()) {
        return false;
    }
    if (m_schedule.autoPoll && m_scheduler->now() < m_schedule.nextPollAt) {
        return false; // automatic pacing cannot be bypassed
    }
    sendPoll();
    return true;
}

void QrLoginService::sendPoll()
{
    stopPollTimer();
    m_pollRequest = m_gateway->checkStatus(m_context, m_key);
}

void QrLoginService::cancel()
{
    Q_ASSERT(QThread::currentThread() == thread());
    // Only our current authentication transaction, never an authenticated session.
    if (m_session.accepts(m_context)) {
        const auto ignored = m_session.cancelAuthentication(m_context);
        Q_UNUSED(ignored);
    }
}

void QrLoginService::fail(domain::SessionToken context, AuthFailure failure)
{
    if (!m_session.accepts(context)) {
        return;
    }
    if (!m_session.cancelAuthentication(context)) {
        return;
    }
    const auto after = m_session.snapshot();
    if (after.phase == domain::SessionPhase::Guest &&
        after.token.coordinatorId == context.coordinatorId &&
        after.token.epoch - context.epoch == 1) {
        emit failed(after.token, failure);
    }
}

void QrLoginService::keyCompleted(quint64 id, domain::SessionToken context, QrKeyResult result)
{
    if (id != m_keyRequest || !m_session.accepts(context) || context != m_context) {
        return;
    }
    m_keyRequest = 0;
    if (failIfTimedOut()) {
        return;
    }
    if (const auto failure = std::get_if<AuthFailure>(&result)) {
        fail(context, *failure);
        return;
    }
    const auto challenge = std::get<domain::QrLoginChallenge>(result);
    m_key = challenge.key;
    schedulePoll(); // first automatic check waits one full interval
    emit challengeReady(context, challenge);
}

void QrLoginService::statusCompleted(quint64 id, domain::SessionToken context,
                                     QrStatusResult result)
{
    if (id != m_pollRequest || !m_session.accepts(context) || context != m_context) {
        return;
    }
    m_pollRequest = 0;
    if (failIfTimedOut()) {
        return;
    }
    if (const auto failure = std::get_if<AuthFailure>(&result)) {
        fail(context, *failure); // includes 429/network/request timeout: no auto retry
        return;
    }
    const auto state = std::get<domain::QrLoginStatus>(result);
    if (state == domain::QrLoginStatus::Expired) {
        const bool canceled = m_session.cancelAuthentication(context);
        const auto after = m_session.snapshot();
        if (canceled && after.token.epoch - context.epoch == 1 &&
            after.phase == domain::SessionPhase::Guest) {
            emit statusChanged(after.token, state);
        }
        return;
    }
    if (state == domain::QrLoginStatus::Authorized) {
        m_key.clear();
        stopPollTimer();
        m_identityRequest = m_gateway->verifyIdentity(context);
    } else {
        schedulePoll();
    }
    emit statusChanged(context, state);
}

void QrLoginService::identityCompleted(quint64 id, domain::SessionToken context,
                                       IdentityResult result)
{
    if (id != m_identityRequest || !m_session.accepts(context) || context != m_context) {
        return;
    }
    m_identityRequest = 0;
    if (failIfTimedOut()) {
        return;
    }
    if (const auto failure = std::get_if<AuthFailure>(&result)) {
        fail(context, *failure);
        return;
    }
    const auto verified = std::get<VerifiedLogin>(result);
    if (verified.receipt.isNull() || verified.identity.accountId.trimmed().isEmpty()) {
        fail(context, {AuthError::InvalidResponse});
        return;
    }
    m_preparing = true;
    const auto prepared = m_session.prepareAuthentication(context, verified.identity.accountId);
    m_preparing = false;
    if (!prepared || !m_session.accepts(*prepared) ||
        m_session.snapshot().phase != domain::SessionPhase::Committing) {
        return; // synchronous logout/restart during prepare wins
    }
    if (failIfTimedOut()) {
        return; // slow reentrant listeners must not stretch the deadline
    }
    if (!m_gateway) {
        fail(*prepared, {AuthError::Network});
        return;
    }
    m_expectedAccount = verified.identity.accountId;
    m_promotionRequest = m_gateway->promoteCredentials(context, *prepared, verified.receipt);
}

void QrLoginService::promotionCompleted(quint64 id, domain::SessionToken context,
                                        PromotionResult result)
{
    if (id != m_promotionRequest || !m_session.accepts(context) || context != m_context) {
        return;
    }
    m_promotionRequest = 0;
    // A late success must not outrun the deadline; Guest advance clears bound credentials.
    if (failIfTimedOut()) {
        return;
    }
    if (const auto failure = std::get_if<AuthFailure>(&result)) {
        fail(context, *failure);
        return;
    }
    const auto identity = std::get<domain::AccountIdentity>(result);
    if (identity.accountId != m_expectedAccount) {
        fail(context, {AuthError::InvalidResponse});
        return;
    }
    if (m_session.completeAuthentication(context)) {
        emit signedIn(context, identity);
    }
}

} // namespace cirrus::application
