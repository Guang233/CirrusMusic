#include "cache_queue.h"

#include "application/backend/backend_facade.h"
#include "infrastructure/cache/resource_cache_service.h"

namespace cirrus::ui
{
namespace
{

// The cache takes 31 jobs at a time (cache::CacheJobsInFlight); some stay free for the
// backend's own (song details) and the other queue (Settings › Storage).
constexpr int kRunning = 24;
// When the cache was full anyway (the backend's jobs), try again after this.
constexpr int kRetryMs = 200;

} // namespace

CacheQueue::CacheQueue(application::BackendFacade& facade, cache::ResourceCacheService* cache,
                       QObject* parent)
    : QObject(parent), m_facade(&facade), m_cache(cache)
{
    m_retry.setSingleShot(true);
    m_retry.setInterval(kRetryMs);
    connect(&m_retry, &QTimer::timeout, this, &CacheQueue::pump);
    connect(&facade, &application::BackendFacade::stateChanged, this, &CacheQueue::schedulePump);
    connect(&facade, &application::BackendFacade::sessionChanged, this, &CacheQueue::schedulePump);
    if (cache)
        connect(cache, &cache::ResourceCacheService::completed, this, &CacheQueue::completed);
}

CacheQueue::~CacheQueue()
{
    if (m_cache) {
        for (auto it = m_running.cbegin(); it != m_running.cend(); ++it)
            m_cache->cancel(it.key());
    }
}

quint64 CacheQueue::enqueue(Submit submit, Done done, Fail fail)
{
    const quint64 ticket = ++m_nextTicket;
    m_waiting.push_back(Job{ticket, std::move(submit), std::move(done), std::move(fail)});
    schedulePump();
    return ticket;
}

void CacheQueue::cancel(quint64 ticket)
{
    for (auto it = m_waiting.begin(); it != m_waiting.end(); ++it) {
        if (it->ticket == ticket) {
            m_waiting.erase(it);
            return;
        }
    }
    for (auto it = m_running.begin(); it != m_running.end(); ++it) {
        if (it->ticket == ticket) {
            if (m_cache)
                m_cache->cancel(it.key());
            m_running.erase(it);
            schedulePump();
            return;
        }
    }
}

bool CacheQueue::open() const
{
    if (!m_facade || !m_cache || m_facade->state() != application::BackendState::Ready)
        return false;
    const auto session = m_facade->snapshot();
    return session.token.isValid() && (session.phase == domain::SessionPhase::Guest ||
                                       session.phase == domain::SessionPhase::Authenticated);
}

bool CacheQueue::closed() const
{
    if (!m_facade || !m_cache)
        return true;
    const auto state = m_facade->state();
    return state == application::BackendState::Failed ||
           state == application::BackendState::Stopping ||
           state == application::BackendState::Stopped;
}

void CacheQueue::schedulePump()
{
    if (m_pumpScheduled)
        return;
    m_pumpScheduled = true;
    QMetaObject::invokeMethod(this, &CacheQueue::pump, Qt::QueuedConnection);
}

void CacheQueue::pump()
{
    m_pumpScheduled = false;
    if (closed()) {
        // Nothing will run any more.
        auto waiting = std::exchange(m_waiting, {});
        for (Job& job : waiting)
            job.fail(failure::Unknown);
        return;
    }
    if (m_busy && m_retry.isActive())
        return; // turned away a moment ago: wait for the retry
    m_busy = false;
    // Newest first: what was asked for last is what is on screen now (a scrolled-away
    // cover is canceled by its page).
    while (!m_waiting.empty() && m_running.size() < kRunning && open()) {
        Job& job = m_waiting.back();
        const quint64 id = job.submit(*m_cache);
        if (id == 0) {
            // Full with the backend's own jobs; a moment later.
            m_retry.start();
            return;
        }
        m_running.insert(id, std::move(job));
        m_waiting.pop_back();
    }
}

void CacheQueue::completed(const domain::CacheResult& result)
{
    auto it = m_running.find(result.id);
    if (it == m_running.end())
        return;
    Job job = std::move(*it);
    m_running.erase(it);
    schedulePump();
    // The session changed (or the cache was cleared) under it: again, when it can.
    if (result.error == domain::CacheError::StaleContext ||
        result.error == domain::CacheError::Canceled) {
        m_waiting.push_back(std::move(job)); // next to run
        return;
    }
    // The cache was full: after the others.
    if (result.error == domain::CacheError::Busy) {
        m_waiting.push_front(std::move(job));
        m_busy = true;
        m_retry.start();
        return;
    }
    job.done(result);
}

} // namespace cirrus::ui
