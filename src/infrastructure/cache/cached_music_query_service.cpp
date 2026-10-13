#include "cached_music_query_service.h"
#include <QSet>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <limits>

namespace cirrus::cache
{
using namespace domain;
using namespace application;
CachedMusicQueryService::CachedMusicQueryService(BackendFacade& facade, ResourceCacheService& cache,
                                                 QObject* parent)
    : QObject(parent), m_facade(&facade), m_cache(&cache)
{
    Q_ASSERT(facade.thread() == thread() && cache.thread() == thread());
    connect(&cache, &ResourceCacheService::completed, this,
            &CachedMusicQueryService::cacheCompleted);
    connect(&facade, &BackendFacade::musicResourceCompleted, this,
            &CachedMusicQueryService::musicCompleted);
    connect(
        &cache, &ResourceCacheService::invalidated, this,
        [this] { retireAll(MusicError::StaleContext); }, Qt::DirectConnection);
    connect(&cache, &QObject::destroyed, this, [this] {
        m_cache.clear();
        m_cacheIds.clear();
        for (const auto& p : m_pending)
            p->cacheId = 0;
        retireAll(MusicError::StaleContext);
    });
    connect(&facade, &QObject::destroyed, this, [this] {
        m_facade.clear();
        m_musicIds.clear();
        for (const auto& p : m_pending)
            p->musicId = 0;
        retireAll(MusicError::StaleContext);
    });
}
CachedMusicQueryService::~CachedMusicQueryService()
{
    if (m_cache)
        disconnect(m_cache, nullptr, this, nullptr);
    if (m_facade)
        disconnect(m_facade, nullptr, this, nullptr);
    const auto cacheIds = m_cacheIds.keys(), musicIds = m_musicIds.keys();
    for (auto id : cacheIds)
        if (m_cache)
            m_cache->cancel(id);
    for (auto id : musicIds)
        if (m_facade)
            m_facade->cancelMusic(id);
}
quint64 CachedMusicQueryService::details(DetailQuery query, CacheReadMode mode)
{
    if (!query.isValid())
        return 0;
    return request(std::move(query.songs), false, mode);
}
quint64 CachedMusicQueryService::lyrics(SongId song, CacheReadMode mode)
{
    if (!validMusicId(song.value))
        return 0;
    return request({std::move(song)}, true, mode);
}
quint64 CachedMusicQueryService::request(QList<SongId> keys, bool lyrics, CacheReadMode mode)
{
    if (QThread::currentThread() != thread() || m_closed || !m_facade || !m_cache ||
        m_pending.size() >= 4 || m_nextId == std::numeric_limits<quint64>::max() ||
        mode < CacheReadMode::CacheOnly || mode > CacheReadMode::Refresh)
        return 0;
    const auto permit = m_cache->writeContext();
    if (!permit)
        return 0;
    const auto id = ++m_nextId;
    auto p = std::make_shared<Pending>();
    p->context = m_facade->snapshot().token;
    p->permit = permit;
    p->keys = std::move(keys);
    p->lyrics = lyrics;
    p->mode = mode;
    p->result.id = id;
    p->result.context = p->context;
    if (mode == CacheReadMode::Refresh) {
        p->stage = Stage::Network;
        p->networkKeys = p->keys;
    }
    m_pending.insert(id, p);
    schedule(id);
    return id;
}
bool CachedMusicQueryService::live(const Pending& p) const
{
    return QThread::currentThread() == thread() && !m_closed && m_facade && m_cache && p.permit &&
           m_facade->state() == BackendState::Ready && m_facade->snapshot().token == p.context &&
           m_cache->accepts(*p.permit);
}
void CachedMusicQueryService::schedule(quint64 id)
{
    const auto p = m_pending.value(id);
    if (!p || p->wake)
        return;
    p->wake = true;
    QTimer::singleShot(0, this, [this, id] {
        const auto p = m_pending.value(id);
        if (!p)
            return;
        p->wake = false;
        advance(id);
    });
}
void CachedMusicQueryService::advance(quint64 id)
{
    const auto p = m_pending.value(id);
    if (!p || p->cacheId || p->musicId)
        return; // cancellation retains admission until receipt
    if (p->forced || !live(*p)) {
        finish(id);
        return;
    }
    if (p->stage == Stage::Read) {
        if (p->index < p->keys.size()) {
            startCache(id, p, false);
            return;
        }
        if (!p->result.cacheMisses.isEmpty() && p->mode == CacheReadMode::PreferCache) {
            p->networkKeys = p->result.cacheMisses;
            p->stage = Stage::Network;
        } else {
            p->result.status =
                p->result.cacheMisses.isEmpty() ? CacheStatus::Hit : CacheStatus::Miss;
            if (!p->lyrics) {
                SongDetails details;
                for (const auto& key : p->keys)
                    if (p->songs.contains(key.value))
                        details.songs.append(p->songs.value(key.value));
                p->result.value =
                    std::move(details); // missing stays provider-only; see cacheMisses
            }
            p->stage = Stage::Done;
        }
    }
    if (p->stage == Stage::Network) {
        startNetwork(id, p);
        return;
    }
    if (p->stage == Stage::Write) {
        if (p->index < p->result.storageCandidates && p->retention.remainingMs() > 0) {
            startCache(id, p, true);
            return;
        }
        p->stage = Stage::Done;
    }
    if (p->stage == Stage::Done)
        finish(id);
}
void CachedMusicQueryService::startCache(quint64 id, const std::shared_ptr<Pending>& p, bool write)
{
    const QPointer<CachedMusicQueryService> guard(this);
    const auto cache = m_cache;
    quint64 lower = 0;
    if (write) {
        const auto ttl = std::min(p->retention.remainingMs(),
                                  maximumCacheTtl(p->lyrics ? CacheKind::Lyrics : CacheKind::Song));
        if (ttl <= 0) {
            p->stage = Stage::Done;
            schedule(id);
            return;
        }
        lower = p->lyrics ? cache->putLyrics(*p->permit, std::get<SongLyrics>(*p->result.value),
                                             ttl, p->retention)
                          : cache->putSong(*p->permit, p->toWrite.at(p->index), ttl, p->retention);
    } else {
        lower = p->lyrics ? cache->lyrics(p->keys.at(p->index)) : cache->song(p->keys.at(p->index));
    }
    if (!guard) {
        if (cache && lower)
            cache->cancel(lower);
        return;
    }
    if (!cache) {
        p->forced = MusicError::StaleContext;
        schedule(id);
        return; // dependency destruction is the terminal receipt; never wait on a dead object
    }
    p->cacheId = lower;
    if (!lower) {
        p->result.cacheError =
            write ? (p->retention.remainingMs() > 0 ? CacheError::InvalidRequest : CacheError::None)
                  : CacheError::Busy;
        p->stage = Stage::Done;
        schedule(id);
        return;
    }
    m_cacheIds.insert(lower, id);
    if (p->forced || !live(*p)) {
        if (cache)
            cache->cancel(lower);
    }
}
void CachedMusicQueryService::startNetwork(quint64 id, const std::shared_ptr<Pending>& p)
{
    const QPointer<CachedMusicQueryService> guard(this);
    const auto facade = m_facade;
    p->result.status = CacheStatus::Network;
    const auto lower =
        p->lyrics ? facade->lyrics(p->keys.front()) : facade->songDetails(p->networkKeys);
    if (!guard) {
        if (facade && lower)
            facade->cancelMusic(lower);
        return;
    }
    if (!facade) {
        p->forced = MusicError::StaleContext;
        schedule(id);
        return;
    }
    p->musicId = lower;
    if (!lower) {
        p->result.value = MusicFailure{MusicError::Busy};
        p->stage = Stage::Done;
        schedule(id);
        return;
    }
    m_musicIds.insert(lower, id);
    if (p->forced || !live(*p)) {
        if (facade)
            facade->cancelMusic(lower);
    }
}
void CachedMusicQueryService::cacheCompleted(CacheResult result)
{
    const auto id = m_cacheIds.take(result.id);
    const auto p = m_pending.value(id);
    if (!p || p->cacheId != result.id)
        return;
    p->cacheId = 0;
    if (p->forced || !live(*p)) {
        schedule(id);
        return;
    }
    if (result.error != CacheError::None) {
        p->result.cacheError = result.error;
        p->stage = Stage::Done;
    } else if (p->stage == Stage::Write) {
        if (result.status == CacheStatus::Stored && result.persisted) {
            ++p->result.storedEntries;
            ++p->index;
        } else
            p->stage = Stage::Done; // deadline may expire while waiting for SQL
    } else if (p->stage == Stage::Read) {
        const auto key = p->keys.at(p->index);
        if (result.status == CacheStatus::Miss || result.status == CacheStatus::Expired) {
            p->result.cacheMisses.append(key);
            ++p->index;
        } else if (result.status == CacheStatus::Hit) {
            if (p->lyrics) {
                const auto* lyrics = std::get_if<SongLyrics>(&result.value);
                if (lyrics && lyrics->song == key)
                    p->result.value = *lyrics;
                else
                    p->result.cacheError = CacheError::Corrupt;
            } else {
                const auto* song = std::get_if<Song>(&result.value);
                if (song && song->id == key)
                    p->songs.insert(key.value, *song);
                else
                    p->result.cacheError = CacheError::Corrupt;
            }
            ++p->index;
            if (p->result.cacheError != CacheError::None)
                p->stage = Stage::Done;
        } else {
            p->result.cacheError = CacheError::Corrupt;
            p->stage = Stage::Done;
        }
    }
    schedule(id);
}
void CachedMusicQueryService::musicCompleted(quint64 lower, SessionToken context,
                                             MusicResult result, CacheRetention retention)
{
    const auto id = m_musicIds.take(lower);
    const auto p = m_pending.value(id);
    if (!p || p->musicId != lower)
        return;
    p->musicId = 0;
    if (context != p->context)
        p->forced = MusicError::StaleContext;
    if (p->forced || !live(*p)) {
        schedule(id);
        return;
    }
    p->stage = Stage::Done;
    if (const auto* error = std::get_if<MusicFailure>(&result))
        p->result.value = *error;
    else if (p->lyrics) {
        const auto* lyrics = std::get_if<SongLyrics>(&result);
        if (!lyrics || lyrics->song != p->keys.front())
            p->result.value = MusicFailure{MusicError::InvalidResponse};
        else {
            p->result.value = *lyrics;
            p->result.storageCandidates = 1;
            p->stage = Stage::Write;
        }
    } else {
        const auto* details = std::get_if<SongDetails>(&result);
        bool valid =
            details && details->songs.size() + details->missing.size() == p->networkKeys.size();
        QSet<QString> seen;
        if (valid) {
            for (const auto& song : details->songs) {
                if (!p->networkKeys.contains(song.id) || seen.contains(song.id.value)) {
                    valid = false;
                    break;
                }
                seen.insert(song.id.value);
            }
            for (const auto& key : details->missing) {
                if (!p->networkKeys.contains(key) || seen.contains(key.value)) {
                    valid = false;
                    break;
                }
                seen.insert(key.value);
            }
        }
        if (!valid)
            p->result.value = MusicFailure{MusicError::InvalidResponse};
        else {
            p->toWrite = details->songs;
            for (const auto& song : details->songs)
                p->songs.insert(song.id.value, song);
            SongDetails combined;
            for (const auto& key : p->keys) {
                if (p->songs.contains(key.value))
                    combined.songs.append(p->songs.value(key.value));
                else
                    combined.missing.append(key);
            }
            p->result.value = std::move(combined);
            p->result.storageCandidates = p->toWrite.size();
            p->stage = Stage::Write;
        }
    }
    p->retention = retention;
    p->index = 0;
    schedule(id);
}
void CachedMusicQueryService::retire(quint64 id, MusicError error)
{
    const auto p = m_pending.value(id);
    if (!p)
        return;
    if (!p->forced)
        p->forced = error;
    const QPointer<CachedMusicQueryService> guard(this);
    if (p->cacheId && m_cache)
        m_cache->cancel(p->cacheId);
    if (!guard)
        return;
    if (p->musicId && m_facade)
        m_facade->cancelMusic(p->musicId);
    if (guard)
        schedule(id);
}
void CachedMusicQueryService::retireAll(MusicError error)
{
    const auto ids = m_pending.keys();
    const QPointer<CachedMusicQueryService> guard(this);
    for (auto id : ids) {
        retire(id, error);
        if (!guard)
            return;
    }
}
void CachedMusicQueryService::cancel(quint64 id)
{
    if (QThread::currentThread() == thread())
        retire(id, MusicError::Canceled);
}
void CachedMusicQueryService::close()
{
    if (QThread::currentThread() != thread() || m_closed)
        return;
    m_closed = true;
    retireAll(MusicError::Canceled);
}
void CachedMusicQueryService::finish(quint64 id)
{
    const auto p = m_pending.value(id);
    if (!p || p->cacheId || p->musicId)
        return;
    if (p->forced || !live(*p)) {
        const auto error = p->forced.value_or(MusicError::StaleContext);
        p->result = {};
        p->result.id = id;
        p->result.context = p->context;
        p->result.value = MusicFailure{error};
        p->result.cacheError =
            error == MusicError::Canceled ? CacheError::Canceled : CacheError::StaleContext;
    }
    m_pending.remove(id); // release before user callbacks, with no post-signal owner access
    emit completed(std::move(p->result));
}
} // namespace cirrus::cache
