#pragma once
#include "resource_cache_service.h"
#include <QPointer>
#include <memory>

namespace cirrus::cache
{
using CachedMusicValue =
    std::variant<domain::SongDetails, domain::SongLyrics, application::MusicFailure>;
struct CachedMusicResult {
    quint64 id = 0;
    domain::SessionToken context;
    domain::CacheStatus status = domain::CacheStatus::Miss;
    domain::CacheError cacheError = domain::CacheError::None;
    std::optional<CachedMusicValue> value; // no synthetic empty lyrics on a cache miss
    QList<domain::SongId> cacheMisses;     // local absence, NEVER provider omission or entitlement
    int storageCandidates = 0, storedEntries = 0; // partial/absent persistence stays explicit
};

// Explicit owner-thread composition; existing facade queries retain network-only behavior.
class CachedMusicQueryService final : public QObject
{
    Q_OBJECT
  public:
    CachedMusicQueryService(application::BackendFacade& facade, ResourceCacheService& cache,
                            QObject* parent = nullptr);
    ~CachedMusicQueryService() override;
    quint64 details(application::DetailQuery query, domain::CacheReadMode mode);
    quint64 lyrics(domain::SongId song, domain::CacheReadMode mode);
    void cancel(quint64 id);
    void close();
  signals:
    void completed(cirrus::cache::CachedMusicResult result);

  private:
    enum class Stage { Read, Network, Write, Done };
    struct Pending {
        domain::SessionToken context;
        std::optional<CacheWriteContext> permit;
        QList<domain::SongId> keys, networkKeys;
        bool lyrics = false, wake = false;
        domain::CacheReadMode mode{};
        Stage stage = Stage::Read;
        qsizetype index = 0;
        quint64 cacheId = 0, musicId = 0;
        std::optional<application::MusicError> forced;
        QHash<QString, domain::Song> songs;
        QList<domain::Song> toWrite;
        domain::CacheRetention retention{};
        CachedMusicResult result;
    };
    quint64 request(QList<domain::SongId> keys, bool lyrics, domain::CacheReadMode mode);
    bool live(const Pending& pending) const;
    void schedule(quint64 id);
    void advance(quint64 id);
    void startCache(quint64 id, const std::shared_ptr<Pending>& pending, bool write);
    void startNetwork(quint64 id, const std::shared_ptr<Pending>& pending);
    void cacheCompleted(domain::CacheResult result);
    void musicCompleted(quint64 id, domain::SessionToken context, application::MusicResult result,
                        domain::CacheRetention retention);
    void retire(quint64 id, application::MusicError error);
    void retireAll(application::MusicError error);
    void finish(quint64 id);
    QPointer<application::BackendFacade> m_facade;
    QPointer<ResourceCacheService> m_cache;
    QHash<quint64, std::shared_ptr<Pending>> m_pending;
    QHash<quint64, quint64> m_cacheIds, m_musicIds;
    quint64 m_nextId = 0;
    bool m_closed = false;
};
} // namespace cirrus::cache
Q_DECLARE_METATYPE(cirrus::cache::CachedMusicResult)
