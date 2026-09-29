#include "session_coordinator.h"

#include <QThread>

#include <limits>
#include <utility>

namespace cirrus::application
{

SessionCoordinator::SessionCoordinator(QObject* parent) : QObject(parent)
{
    m_snapshot.token = {QUuid::createUuid(), 1};
}

domain::SessionSnapshot SessionCoordinator::snapshot() const
{
    Q_ASSERT(QThread::currentThread() == thread());
    return m_snapshot;
}

bool SessionCoordinator::accepts(const domain::SessionToken& token) const
{
    Q_ASSERT(QThread::currentThread() == thread());
    return token.isValid() && token == m_snapshot.token;
}

domain::SessionToken SessionCoordinator::transition(domain::SessionPhase phase, QString accountId)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_snapshot.token.epoch == std::numeric_limits<quint64>::max()) {
        qFatal("Session epoch exhausted");
    }
    ++m_snapshot.token.epoch;
    m_snapshot.phase = phase;
    m_snapshot.accountId = std::move(accountId);
    const auto notification = m_snapshot;
    emit changed(notification);
    return notification.token; // never accidentally return a newer reentrant token
}

domain::SessionToken SessionCoordinator::beginAuthentication()
{
    return transition(domain::SessionPhase::Authenticating);
}

std::optional<domain::SessionToken>
SessionCoordinator::prepareAuthentication(const domain::SessionToken& attempt,
                                          QString verifiedAccountId)
{
    if (!accepts(attempt) || m_snapshot.phase != domain::SessionPhase::Authenticating ||
        verifiedAccountId.trimmed().isEmpty()) {
        return std::nullopt;
    }
    return transition(domain::SessionPhase::Committing, std::move(verifiedAccountId));
}

bool SessionCoordinator::completeAuthentication(const domain::SessionToken& prepared)
{
    if (!accepts(prepared) || m_snapshot.phase != domain::SessionPhase::Committing) {
        return false;
    }
    // Same token: credentials were already bound to this exact epoch.
    m_snapshot.phase = domain::SessionPhase::Authenticated;
    emit changed(m_snapshot);
    return accepts(prepared) && m_snapshot.phase == domain::SessionPhase::Authenticated;
}

bool SessionCoordinator::cancelAuthentication(const domain::SessionToken& attempt)
{
    if (!accepts(attempt) || (m_snapshot.phase != domain::SessionPhase::Authenticating &&
                              m_snapshot.phase != domain::SessionPhase::Committing)) {
        return false;
    }
    transition(domain::SessionPhase::Guest);
    return true;
}

bool SessionCoordinator::expire(const domain::SessionToken& context)
{
    if (!accepts(context) || m_snapshot.phase != domain::SessionPhase::Authenticated) {
        return false;
    }
    transition(domain::SessionPhase::Expired);
    return true;
}

void SessionCoordinator::signOut() { transition(domain::SessionPhase::Guest); }

} // namespace cirrus::application
