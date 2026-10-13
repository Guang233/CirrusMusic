#pragma once
#include "application/backend/backend_facade.h"
#include "resource_cache_store.h"
#include <QHash>

namespace cirrus::cache
{
// Capture BEFORE the originating query. Clear/account/close invalidates this private permit.
class CacheWriteContext
{
    friend class ResourceCacheService;
    CacheWriteContext(QUuid instance, domain::SessionSnapshot source, quint64 revision)
        : m_instance(instance), m_source(std::move(source)), m_revision(revision)
    {
    }
    QUuid m_instance;
    domain::SessionSnapshot m_source;
    quint64 m_revision;
};

// Explicit owner-thread cache API. Does not intercept or initiate MusicQuery requests.
class ResourceCacheService final : public QObject
{
    Q_OBJECT
  public:
    explicit ResourceCacheService(application::BackendFacade& facade, QObject* parent = nullptr);
    quint64 song(domain::SongId song); // cache only; Miss/Expired never trigger HTTP
    quint64 lyrics(domain::SongId song);
    // Positive TTL is explicit caller attestation that the source permits this retention.
    // Unknown policy / no-store: do not call put. Source tokens/credentials are not serialized.
    std::optional<CacheWriteContext> writeContext() const;
    quint64 putSong(const CacheWriteContext& context, domain::Song song, qint64 allowedTtlMs);
    quint64 putLyrics(const CacheWriteContext& context, domain::SongLyrics lyrics,
                      qint64 allowedTtlMs);
    quint64 cover(QUrl url, int size,
                  domain::CacheReadMode mode = domain::CacheReadMode::CacheOnly);
    quint64 remove(domain::CacheKind kind, domain::SongId song);
    quint64 removeCover(QUrl url, int size);
    quint64 clear(bool allAccounts = false); // erases only the resource cache, never user records
    quint64 usage();
    void cancel(quint64 id);
    void close();
    void deliver(domain::CacheResult result);
  signals:
    void completed(cirrus::domain::CacheResult result);
    void requested(cirrus::cache::CacheJob job);
    void cancelRequested(quint64 id);

  private:
    quint64 submit(CacheJob job);
    quint64 invalidate(CacheJob job);
    bool live() const;
    bool validWrite(const CacheWriteContext& context) const;
    void contextChanged();
    bool retire(domain::CacheError error);
    application::BackendFacade& m_facade;
    domain::SessionSnapshot m_source;
    struct Pending {
        domain::SessionSnapshot source;
        quint64 revision = 0;
        bool clear = false;
        std::optional<domain::CacheError> forced = std::nullopt;
    };
    QHash<quint64, Pending> m_pending;
    quint64 m_nextId = 0, m_revision = 1;
    const QUuid m_instance = QUuid::createUuid();
    bool m_closed = false, m_clearing = false, m_retiring = false;
};
} // namespace cirrus::cache
