#pragma once
#include "domain/account/session_types.h"
#include "domain/music/cache_retention.h"
#include "domain/music/resource_cache_types.h"
#include "infrastructure/storage/database_worker.h"

namespace cirrus::cache
{
enum class CacheOperation { Read, Write, Remove, Clear, ClearAll, Usage, Cover };
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
class ResourceCacheStore final : public QObject
{
    Q_OBJECT
  public:
    ResourceCacheStore(storage::DatabaseWorker& database, QString root, QObject* parent = nullptr);
    void submit(CacheJob job);
    qsizetype pending() const { return m_pending; }
    bool writeFailed() const { return m_writeFailed; }
  signals:
    void completed(cirrus::domain::CacheResult result);
    void idle();

  private:
    storage::DatabaseWorker& m_database;
    QString m_root;
    qsizetype m_pending = 0;
    bool m_writeFailed = false;
};
} // namespace cirrus::cache
Q_DECLARE_METATYPE(cirrus::cache::CacheJob)
