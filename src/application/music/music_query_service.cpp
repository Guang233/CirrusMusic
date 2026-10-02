#include "music_query_service.h"

#include <QThread>
#include <QTimer>

namespace cirrus::application
{
MusicQueryService::MusicQueryService(SessionCoordinator& session, IMusicGateway& gateway,
                                     QObject* parent)
    : QObject(parent), m_session(session), m_gateway(&gateway)
{
    Q_ASSERT(session.thread() == thread() && gateway.thread() == thread());
    connect(&gateway, &IMusicGateway::completed, this,
            [this](quint64 gatewayId, domain::SessionToken token, MusicResult result) {
                const auto id = m_ids.value(gatewayId);
                if (!m_pending.contains(id))
                    return;
                if (m_pending.value(id).context != token)
                    result = MusicFailure{MusicError::StaleContext};
                finish(id, std::move(result));
            });
    connect(&gateway, &QObject::destroyed, this, [this] {
        for (auto id : m_pending.keys())
            rejectLater(id, MusicError::Network);
    });
    connect(&session, &SessionCoordinator::changed, this, [this](domain::SessionSnapshot snapshot) {
        for (auto id : m_pending.keys()) {
            if (m_pending.value(id).context == snapshot.token)
                continue;
            rejectLater(id, MusicError::StaleContext);
            if (m_gateway)
                m_gateway->cancel(m_pending.value(id).gatewayId);
        }
    });
}

MusicQueryService::~MusicQueryService()
{
    if (m_gateway) {
        disconnect(m_gateway, nullptr, this, nullptr);
        for (const auto& pending : m_pending)
            m_gateway->cancel(pending.gatewayId);
    }
}

quint64 MusicQueryService::request(domain::SessionToken context, MusicQuery query)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_pending.size() >= 16)
        return 0;
    const auto id = ++m_nextId;
    m_pending.insert(id, {context});
    const auto snapshot = m_session.snapshot();
    if (!m_session.accepts(context))
        rejectLater(id, MusicError::StaleContext);
    else if (snapshot.phase != domain::SessionPhase::Guest &&
             snapshot.phase != domain::SessionPhase::Authenticated)
        rejectLater(id, MusicError::Busy);
    else if (requiresAuthenticatedMusicQuery(query) &&
             snapshot.phase != domain::SessionPhase::Authenticated)
        rejectLater(id, MusicError::AuthenticationRequired);
    else if (!validMusicQuery(query))
        rejectLater(id, MusicError::InvalidRequest);
    else if (const auto account = libraryQueryAccount(query);
             account && *account != snapshot.accountId)
        rejectLater(id, MusicError::InvalidRequest);
    else if (!m_gateway)
        rejectLater(id, MusicError::Network);
    else {
        const auto gatewayId = m_gateway->request(context, std::move(query));
        m_pending[id].gatewayId = gatewayId;
        m_ids.insert(gatewayId, id);
    }
    return id;
}

void MusicQueryService::rejectLater(quint64 id, MusicError error)
{
    if (!m_pending.contains(id))
        return;
    m_pending[id].forced = error;
    QTimer::singleShot(0, this, [this, id, error] { finish(id, MusicFailure{error}); });
}

void MusicQueryService::cancel(quint64 id)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!m_pending.contains(id))
        return;
    rejectLater(id, MusicError::Canceled);
    if (m_gateway)
        m_gateway->cancel(m_pending.value(id).gatewayId);
}

void MusicQueryService::finish(quint64 id, MusicResult result)
{
    if (!m_pending.contains(id))
        return;
    const auto pending = m_pending.take(id);
    m_ids.remove(pending.gatewayId);
    if (!m_session.accepts(pending.context))
        result = MusicFailure{MusicError::StaleContext};
    else if (pending.forced)
        result = MusicFailure{*pending.forced};
    emit completed(id, pending.context, std::move(result));
}
} // namespace cirrus::application
