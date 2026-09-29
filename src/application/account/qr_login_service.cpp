#include "qr_login_service.h"

#include <QThread>
#include <utility>

namespace cirrus::application
{

QrLoginService::QrLoginService(SessionCoordinator& session, IQrLoginGateway& gateway,
                               QObject* parent)
    : QObject(parent), m_session(session), m_gateway(&gateway)
{
    Q_ASSERT(session.thread() == thread() && gateway.thread() == thread());
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

void QrLoginService::sessionChanged(domain::SessionSnapshot snapshot)
{
    if (!m_session.accepts(snapshot.token)) {
        return; // stale reentrant notification
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
    const auto context = m_session.beginAuthentication();
    if (!m_session.accepts(context)) {
        return context;
    }
    if (!m_gateway || m_synchronized != context) {
        fail(context, {AuthError::StaleContext});
    } else {
        m_keyRequest = m_gateway->requestKey(context);
    }
    return context;
}

bool QrLoginService::poll()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!m_gateway || !m_session.accepts(m_context) || m_key.isEmpty() ||
        m_session.snapshot().phase != domain::SessionPhase::Authenticating || m_pollRequest != 0 ||
        m_identityRequest != 0 || m_promotionRequest != 0) {
        return false;
    }
    m_pollRequest = m_gateway->checkStatus(m_context, m_key);
    return true;
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
    if (const auto failure = std::get_if<AuthFailure>(&result)) {
        fail(context, *failure);
        return;
    }
    const auto challenge = std::get<domain::QrLoginChallenge>(result);
    m_key = challenge.key;
    emit challengeReady(context, challenge);
}

void QrLoginService::statusCompleted(quint64 id, domain::SessionToken context,
                                     QrStatusResult result)
{
    if (id != m_pollRequest || !m_session.accepts(context) || context != m_context) {
        return;
    }
    m_pollRequest = 0;
    if (const auto failure = std::get_if<AuthFailure>(&result)) {
        fail(context, *failure);
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
        m_identityRequest = m_gateway->verifyIdentity(context);
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
    if (const auto failure = std::get_if<AuthFailure>(&result)) {
        fail(context, *failure);
        return;
    }
    const auto verified = std::get<VerifiedLogin>(result);
    if (verified.receipt.isNull() || verified.identity.accountId.trimmed().isEmpty()) {
        fail(context, {AuthError::InvalidResponse});
        return;
    }
    const auto prepared = m_session.prepareAuthentication(context, verified.identity.accountId);
    if (!prepared || !m_session.accepts(*prepared) ||
        m_session.snapshot().phase != domain::SessionPhase::Committing) {
        return; // synchronous logout/restart during prepare wins
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
