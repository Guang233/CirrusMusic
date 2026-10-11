#include "listening_state_service.h"

#include <QPointer>
#include <QThread>
#include <limits>

namespace cirrus::storage
{
namespace
{
bool sameQueue(const domain::QueueSnapshot& a, const domain::QueueSnapshot& b)
{
    if (a.active != b.active || a.order != b.order || a.repeat != b.repeat ||
        a.entries.size() != b.entries.size())
        return false;
    for (qsizetype i = 0; i < a.entries.size(); ++i)
        if (a.entries[i].id != b.entries[i].id || a.entries[i].song != b.entries[i].song)
            return false;
    return true;
}
bool eligible(const domain::SessionSnapshot& snapshot)
{
    return snapshot.token.isValid() && domain::validListeningAccount(snapshot.accountId) &&
           ((snapshot.phase == domain::SessionPhase::Guest && snapshot.accountId.isEmpty()) ||
            (snapshot.phase == domain::SessionPhase::Authenticated &&
             !snapshot.accountId.isEmpty()));
}
} // namespace
ListeningStateService::ListeningStateService(application::ListeningSession& listening,
                                             application::BackendFacade& facade, QObject* parent)
    : QObject(parent), m_listening(listening), m_facade(facade)
{
    Q_ASSERT(listening.thread() == thread() && facade.thread() == thread());
    m_timer.setSingleShot(true);
    m_timer.setInterval(500);
    connect(&m_timer, &QTimer::timeout, this, &ListeningStateService::flush);
    connect(
        &facade, &application::BackendFacade::sessionChanged, this, [this] { contextChanged(); },
        Qt::DirectConnection);
    connect(
        &facade, &application::BackendFacade::stateChanged, this, [this] { contextChanged(); },
        Qt::DirectConnection);
    connect(&listening, &application::ListeningSession::queueChanged, this,
            &ListeningStateService::queueChanged, Qt::DirectConnection);
    connect(
        &listening, &application::ListeningSession::queueCleared, this,
        [this](domain::SessionSnapshot source) {
            if (live() && source == m_source) {
                m_loadId = 0; // even an already-empty explicit clear defeats a late restore
                m_save = ListeningStateJob{0, ListeningStateOperation::SaveQueue, source.accountId,
                                           m_listening.queue()};
                flush();
            }
        },
        Qt::DirectConnection);
}
bool ListeningStateService::live() const
{
    return !m_closed && m_facade.state() == application::BackendState::Ready &&
           eligible(m_source) && m_facade.snapshot() == m_source;
}
void ListeningStateService::contextChanged()
{
    if (m_closed)
        return;
    const auto source = m_facade.snapshot();
    if (m_facade.state() != application::BackendState::Ready || !eligible(source)) {
        m_loadId = 0;
        return;
    }
    if (source == m_source)
        return;
    const QPointer<ListeningStateService> guard(this);
    flush(); // old account's captured edit, not the reset of the new account
    if (!guard || m_closed || m_facade.state() != application::BackendState::Ready ||
        m_facade.snapshot() != source)
        return;
    m_source = source;
    m_loadId = 0;
    if (!m_listening.queue().entries.isEmpty() ||
        m_listening.mode() != domain::ListeningMode::Queue)
        return;
    if (m_pending.size() >= 7 || m_nextId == std::numeric_limits<quint64>::max()) {
        emit storageFailed(StorageError::Query);
        return;
    }
    m_restoreIntent = m_listening.intentRevision();
    m_loadId = ++m_nextId;
    m_pending.insert(m_loadId);
    emit requested({m_loadId, ListeningStateOperation::LoadQueue, source.accountId, {}});
}
void ListeningStateService::queueChanged(const domain::QueueSnapshot& queue)
{
    if (!live() || m_listening.mode() != domain::ListeningMode::Queue ||
        m_listening.queueContext() != m_source.token || !domain::validSavedQueue(queue) ||
        !sameQueue(queue, m_listening.queue()))
        return;
    if (m_restoring)
        return;
    m_loadId = 0; // append/remove/modes must win even if they don't alter playback revision
    m_save = ListeningStateJob{0, ListeningStateOperation::SaveQueue, m_source.accountId, queue};
    if (!m_timer.isActive())
        m_timer.start();
}
void ListeningStateService::flush()
{
    m_timer.stop();
    if (!m_save)
        return;
    if (m_pending.size() >= 8 || m_nextId == std::numeric_limits<quint64>::max()) {
        m_writeRejected = true;
        emit storageFailed(StorageError::Query);
        return; // retain dirty state; never claim a rejected write was saved
    }
    auto job = std::move(*m_save);
    m_save.reset();
    job.id = ++m_nextId;
    m_pending.insert(job.id);
    emit requested(std::move(job));
}
void ListeningStateService::deliver(ListeningStateResult result)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!m_pending.remove(result.id))
        return;
    const QPointer<ListeningStateService> guard(this);
    if (result.error != StorageError::None)
        emit storageFailed(result.error);
    if (!guard)
        return;
    if (!m_closed && result.id == m_loadId) {
        m_loadId = 0;
        if (result.error == StorageError::None && result.queue && live()) {
            const auto source = m_source;
            m_restoring = true;
            const bool applied = m_listening.restoreQueue(*result.queue, source, m_restoreIntent);
            if (!guard)
                return;
            m_restoring = false;
            // Native/direct signal observers may synchronously edit during restoration.
            if (live() && m_listening.queueContext() == source.token &&
                !sameQueue(m_listening.queue(), *result.queue))
                queueChanged(m_listening.queue());
            if (!guard)
                return;
            emit queueRestored(applied);
        }
    }
    if (guard && !m_closed && m_save)
        flush();
}
void ListeningStateService::close()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_closed)
        return;
    m_closed = true;
    m_loadId = 0;
    flush(); // queue BEFORE the worker close; its DB pending count owns the shutdown barrier
}
} // namespace cirrus::storage
