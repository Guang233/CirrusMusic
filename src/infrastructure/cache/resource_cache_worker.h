#pragma once
#include "cover_fetcher.h"
#include <deque>
#include <functional>

namespace cirrus::cache
{
class ResourceCacheWorker final : public QObject
{
    Q_OBJECT
  public:
    using Context = std::function<std::optional<domain::SessionSnapshot>()>;
    ResourceCacheWorker(storage::DatabaseWorker& db, QString root, Context context,
                        QObject* parent = nullptr);
    void submit(CacheJob job);
    void cancel(quint64 id);
    void contextChanged();
    void close();
    bool idle() const;
    bool writeFailed() const { return m_store.writeFailed(); }
  signals:
    void completed(cirrus::domain::CacheResult result);
    void drained();

  private:
    bool live(const CacheJob& job) const;
    void stored(domain::CacheResult result);
    void fetched(quint64 id, domain::CacheError error, QByteArray bytes, qint64 ttlMs);
    void finish(domain::CacheResult result);
    void drain();
    // Downloads the cover, or queues it while the fetcher is full.
    void fetch(const CacheJob& job);
    void nextFetch();
    ResourceCacheStore m_store;
    CoverFetcher m_fetcher;
    Context m_context;
    QHash<quint64, CacheJob> m_jobs;
    QHash<quint64, QByteArray> m_networkValues;
    std::deque<quint64> m_fetchQueue; // jobs (in m_jobs) waiting for the fetcher
    std::optional<CacheJob> m_clear;
    quint64 m_revision = 0;
    bool m_closed = false;
};
} // namespace cirrus::cache
