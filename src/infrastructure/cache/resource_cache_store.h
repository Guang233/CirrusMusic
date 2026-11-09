#pragma once
#include "domain/account/session_types.h"
#include "domain/music/cache_retention.h"
#include "domain/music/resource_cache_types.h"
#include "infrastructure/storage/database_worker.h"
#include <QHash>
#include <QThread>

namespace cirrus::cache
{
enum class CacheOperation { Read, Write, Remove, Clear, ClearAll, Usage, Cover };
// Jobs in flight at once through the service, the worker and the store (one of them a
// clear). Covers are most of them: a page asks for a few dozen at a time.
inline constexpr int CacheJobsInFlight = 32;
// Rows kept at most, of all kinds (a cover is some 10–60 KB: the cover budget's worth).
inline constexpr qint64 CacheMaximumEntries = 100000;
class StoreConnection;
struct CacheJob {
    quint64 id = 0, revision = 0;
    domain::SessionSnapshot source; // private in-process binding, never persisted
    CacheOperation operation{};
    domain::CacheKind kind{};
    QString key;
    QByteArray payload;
    qint64 ttlMs = 0;
    std::optional<domain::CacheRetention> retention; // in-process only; never a DB column
    qint64 notAfterMs = 0; // admission-time expiry; SQL queueing must not extend retention
    QUrl coverUrl;         // transient only, not stored in the cover row
    int coverSize = 256;
    domain::CacheReadMode mode{};
};
// The SQLite file of the resource cache, on a thread of its own (not the state
// database's): see StoreConnection in the .cpp.
class ResourceCacheStore final : public QObject
{
    Q_OBJECT
  public:
    // `database` only to tell its path apart from the cache's.
    ResourceCacheStore(storage::DatabaseWorker& database, QString root, QObject* parent = nullptr);
    ~ResourceCacheStore() override;
    void submit(CacheJob job);
    qsizetype pending() const { return m_pending; }
    bool writeFailed() const { return m_writeFailed; }
  signals:
    void completed(cirrus::domain::CacheResult result);
    void idle();

  private:
    void delivered(domain::CacheResult result);
    QString m_root, m_mainPath;
    QThread m_thread;
    StoreConnection* m_connection = nullptr; // lives on m_thread
    QHash<quint64, bool> m_writes;           // pending job -> whether it writes
    qsizetype m_pending = 0;
    bool m_writeFailed = false;
};
} // namespace cirrus::cache
Q_DECLARE_METATYPE(cirrus::cache::CacheJob)
