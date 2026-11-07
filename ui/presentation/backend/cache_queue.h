#pragma once

#include "request.h"

#include "domain/music/resource_cache_types.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QTimer>

#include <deque>
#include <functional>

namespace cirrus::application
{
class BackendFacade;
}
namespace cirrus::cache
{
class ResourceCacheService;
}

namespace cirrus::ui
{

// Jobs for the resource cache (ResourceCacheService), which takes only a few at a time
// and none before the backend is ready with a guest or signed-in session: jobs wait
// here until there is room and a session, and run again if the session changed under
// them (docs/ui/integration.md §2, §4). The backend may be gone at exit (QPointer).
class CacheQueue : public QObject
{
  public:
    // Submits the job to the cache; 0 when it didn't take it.
    using Submit = std::function<quint64(cache::ResourceCacheService&)>;
    using Done = std::function<void(const domain::CacheResult&)>;
    using Fail = std::function<void(failure::Kind)>;

    CacheQueue(application::BackendFacade& facade, cache::ResourceCacheService* cache,
               QObject* parent = nullptr);
    ~CacheQueue() override;

    // Runs `submit` when it can; then `done` with the result, or `fail` when the backend
    // won't take it any more. Neither runs synchronously from here. Returns a ticket.
    quint64 enqueue(Submit submit, Done done, Fail fail);
    // Forgets a job; neither callback runs afterwards.
    void cancel(quint64 ticket);

  private:
    struct Job {
        quint64 ticket = 0;
        Submit submit;
        Done done;
        Fail fail;
    };

    // Whether the cache takes jobs now (backend ready, a settled session).
    [[nodiscard]] bool open() const;
    // Whether it never will again (failed, closing).
    [[nodiscard]] bool closed() const;
    void pump();
    void schedulePump();
    void completed(const domain::CacheResult& result);

    QPointer<application::BackendFacade> m_facade;
    QPointer<cache::ResourceCacheService> m_cache;
    std::deque<Job> m_waiting;
    // Cache command id -> job.
    QHash<quint64, Job> m_running;
    quint64 m_nextTicket = 0;
    QTimer m_retry;
    bool m_pumpScheduled = false;
};

} // namespace cirrus::ui
