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
    m_historyTimer.setSingleShot(true);
    m_historyTimer.setInterval(5000);
    connect(&m_historyTimer, &QTimer::timeout, this, &ListeningStateService::flushHistory);
    connect(&listening, &application::ListeningSession::listeningRecordChanged, this,
            &ListeningStateService::recordChanged, Qt::DirectConnection);
    connect(&listening, &application::ListeningSession::historyCleared, this,
            &ListeningStateService::clearHistory, Qt::DirectConnection);
    connect(
        &listening, &application::ListeningSession::historyStorageOverflow, this,
        [this] {
            m_writeRejected = true;
            emit storageFailed(StorageError::Query);
        },
        Qt::DirectConnection);
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
    ++m_historyEpoch; // pending page data is never delivered into a different live account
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
    m_clearRequests.remove(result.id);
    const QPointer<ListeningStateService> guard(this);
    if (m_historyReads.contains(result.id)) {
        const auto read = m_historyReads.take(result.id);
        auto error = result.error;
        if (!live() || read.source != m_source || read.epoch != m_historyEpoch)
            error = StorageError::NotOpen;
        else if (error == StorageError::None && !result.history)
            error = StorageError::Corrupt;
        emit historyPageReady(result.id, error,
                              error == StorageError::None ? std::move(*result.history)
                                                          : domain::ListeningHistoryPage{});
        if (guard && !m_closed && !m_historyTimer.isActive())
            flushHistory();
        return;
    }
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
    if (guard && !m_closed && !m_historyTimer.isActive())
        flushHistory();
}
void ListeningStateService::recordChanged(domain::StoredListeningRecord record)
{
    if (m_closed)
        return;
    const auto existing = m_historyDirty.constFind(record.id);
    const bool changedIdentity =
        existing != m_historyDirty.cend() &&
        (existing->accountId != record.accountId || existing->song != record.song ||
         existing->startedAtMs != record.startedAtMs || existing->source != record.source ||
         existing->queueEntry != record.queueEntry);
    if (!domain::validStoredListeningRecord(record) || changedIdentity ||
        (!m_historyDirty.contains(record.id) && m_historyDirty.size() >= 256)) {
        m_writeRejected = true;
        emit storageFailed(StorageError::Query);
        return;
    }
    const bool finalized = record.end != domain::ListeningEnd::Active;
    m_historyDirty.insert(record.id, std::move(record));
    if (finalized)
        flushHistory();
    else if (!m_historyTimer.isActive())
        m_historyTimer.start();
}
void ListeningStateService::clearHistory(domain::SessionSnapshot source)
{
    if (m_closed || !eligible(source))
        return;
    ++m_historyEpoch; // cancels pre-clear reads as well as deferred writes
    for (auto it = m_historyDirty.begin(); it != m_historyDirty.end();)
        if (it->accountId == source.accountId)
            it = m_historyDirty.erase(it);
        else
            ++it;
    if (!m_historyClears.contains(source.accountId) && m_historyClears.size() >= 8) {
        m_writeRejected = true;
        emit storageFailed(StorageError::Query);
        return;
    }
    m_historyClears.insert(source.accountId);
    flushHistory(); // FIFO erase BEFORE any subsequently observed new load; no requeue on ack
}
QList<ListeningStateJob> ListeningStateService::historyJobs(qsizetype maximumJobs)
{
    QList<ListeningStateJob> jobs;
    while (jobs.size() < maximumJobs && (!m_historyClears.isEmpty() || !m_historyDirty.isEmpty())) {
        if (m_nextId == std::numeric_limits<quint64>::max()) {
            m_writeRejected = true;
            break;
        }
        ListeningStateJob job;
        job.id = ++m_nextId;
        if (!m_historyClears.isEmpty()) {
            job.operation = ListeningStateOperation::ClearHistory;
            job.accountId = *m_historyClears.begin();
            m_historyClears.remove(job.accountId);
            m_clearRequests.insert(job.id, job.accountId);
        } else {
            job.operation = ListeningStateOperation::SaveHistory;
            job.accountId = m_historyDirty.first().accountId;
            for (auto it = m_historyDirty.begin();
                 it != m_historyDirty.end() && job.records.size() < 64;)
                if (it->accountId == job.accountId) {
                    job.records.append(std::move(it.value()));
                    it = m_historyDirty.erase(it);
                } else
                    ++it;
        }
        m_pending.insert(job.id);
        jobs.append(std::move(job));
    }
    return jobs;
}
void ListeningStateService::flushHistory()
{
    if (m_closed)
        return;
    m_historyTimer.stop();
    const QPointer<ListeningStateService> guard(this);
    while (m_pending.size() <
           7) { // leave an admission slot; close handoff is independently drained
        auto jobs = historyJobs(1);
        if (jobs.isEmpty())
            break;
        emit requested(std::move(jobs.first()));
        if (!guard || m_closed)
            return;
    }
    if ((!m_historyDirty.isEmpty() || !m_historyClears.isEmpty()) && !m_historyTimer.isActive())
        m_historyTimer.start();
}
quint64 ListeningStateService::historyPage(int pageSize,
                                           std::optional<domain::ListeningHistoryCursor> cursor)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!live() || m_historyClears.contains(m_source.accountId) ||
        m_clearRequests.values().contains(m_source.accountId) || pageSize < 1 || pageSize > 100 ||
        m_pending.size() >= 6 || m_historyReads.size() >= 2 ||
        m_nextId == std::numeric_limits<quint64>::max() ||
        (cursor && (cursor->id.isNull() || cursor->startedAtMs <= 0 ||
                    cursor->startedAtMs > 253402300799999LL))) {
        emit storageFailed(StorageError::Query);
        return 0;
    }
    ListeningStateJob job;
    const auto id = job.id = ++m_nextId;
    job.operation = ListeningStateOperation::LoadHistory;
    job.accountId = m_source.accountId;
    job.cursor = cursor;
    job.pageSize = pageSize;
    m_pending.insert(id);
    m_historyReads.insert(id, {m_source, m_historyEpoch});
    emit requested(std::move(job));
    return id;
}
QList<ListeningStateJob> ListeningStateService::close()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_closed)
        return {};
    m_closed = true;
    m_loadId = 0;
    ++m_historyEpoch;
    m_timer.stop();
    m_historyTimer.stop();
    QList<ListeningStateJob> jobs;
    if (m_save && m_nextId != std::numeric_limits<quint64>::max()) {
        m_save->id = ++m_nextId;
        m_pending.insert(m_save->id);
        jobs.append(std::move(*m_save));
        m_save.reset();
    }
    jobs.append(historyJobs(264)); // <=256 facts/8 scope erasures, never a retention policy
    if (m_save || !m_historyDirty.isEmpty() || !m_historyClears.isEmpty())
        m_writeRejected = true;
    return jobs; // worker admits final jobs as earlier DB slots complete; no owner callback needed
}
} // namespace cirrus::storage
