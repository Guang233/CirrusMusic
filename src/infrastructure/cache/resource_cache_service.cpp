#include "resource_cache_service.h"
#include "resource_cache_codec.h"
#include <QDateTime>
#include <QPointer>
#include <QThread>
#include <algorithm>
#include <limits>
#include <utility>

namespace cirrus::cache
{
using namespace domain;
ResourceCacheService::ResourceCacheService(application::BackendFacade& facade, QObject* parent)
    : QObject(parent), m_facade(facade), m_source(facade.snapshot())
{
    Q_ASSERT(facade.thread() == thread());
    connect(
        &facade, &application::BackendFacade::sessionChanged, this, [this] { contextChanged(); },
        Qt::DirectConnection);
    connect(
        &facade, &application::BackendFacade::stateChanged, this, [this] { contextChanged(); },
        Qt::DirectConnection);
}
bool ResourceCacheService::live() const
{
    return QThread::currentThread() == thread() && !m_closed && !m_retiring &&
           m_facade.state() == application::BackendState::Ready &&
           m_facade.snapshot() == m_source && m_source.token.isValid() &&
           ((m_source.phase == SessionPhase::Guest && m_source.accountId.isEmpty()) ||
            (m_source.phase == SessionPhase::Authenticated && validMusicId(m_source.accountId)));
}
std::optional<CacheWriteContext> ResourceCacheService::writeContext() const
{
    if (!live() || m_clearing)
        return {};
    return CacheWriteContext(m_instance, m_source, m_revision);
}
bool ResourceCacheService::validWrite(const CacheWriteContext& context) const
{
    return live() && context.m_instance == m_instance && context.m_source == m_source &&
           context.m_revision == m_revision;
}
void ResourceCacheService::contextChanged()
{
    if (m_closed)
        return;
    if (m_facade.snapshot() != m_source || m_facade.state() != application::BackendState::Ready) {
        if (retire(CacheError::StaleContext))
            m_source = m_facade.snapshot();
    }
}
quint64 ResourceCacheService::submit(CacheJob job)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const bool clearing = job.operation == CacheOperation::Clear ||
                          job.operation == CacheOperation::ClearAll ||
                          job.operation == CacheOperation::Remove;
    if (!live() || (m_clearing && !clearing) || m_pending.size() >= (clearing ? 8 : 7) ||
        m_nextId == std::numeric_limits<quint64>::max())
        return 0;
    job.id = ++m_nextId;
    job.revision = m_revision;
    job.source = m_source;
    if (job.operation == CacheOperation::Write) {
        if (job.retention)
            job.ttlMs = std::min(job.ttlMs, job.retention->remainingMs());
        if (job.ttlMs <= 0)
            return 0;
        const auto now = QDateTime::currentMSecsSinceEpoch();
        if (now <= 0 || now > 253402300799999LL - 7 * CacheDayMs)
            return 0;
        job.notAfterMs = now + job.ttlMs;
    }
    const auto id = job.id;
    m_pending.insert(id, {m_source, m_revision, clearing});
    emit requested(std::move(job));
    return id;
}
quint64 ResourceCacheService::song(SongId song)
{
    if (!validMusicId(song.value))
        return 0;
    CacheJob job;
    job.operation = CacheOperation::Read;
    job.kind = CacheKind::Song;
    job.key = song.value;
    return submit(std::move(job));
}
quint64 ResourceCacheService::lyrics(SongId song)
{
    if (!validMusicId(song.value))
        return 0;
    CacheJob job;
    job.operation = CacheOperation::Read;
    job.kind = CacheKind::Lyrics;
    job.key = song.value;
    return submit(std::move(job));
}
quint64 ResourceCacheService::putSong(const CacheWriteContext& context, Song song,
                                      qint64 allowedTtlMs, std::optional<CacheRetention> retention)
{
    if (!validWrite(context) || m_clearing || m_pending.size() >= 7 || allowedTtlMs <= 0 ||
        allowedTtlMs > maximumCacheTtl(CacheKind::Song))
        return 0;
    CacheJob job;
    job.operation = CacheOperation::Write;
    job.kind = CacheKind::Song;
    job.key = song.id.value;
    job.ttlMs = allowedTtlMs;
    job.retention = retention;
    job.payload = encodeCacheValue(CacheValue(std::move(song)), job.kind);
    if (job.payload.isEmpty())
        return 0;
    return submit(std::move(job));
}
quint64 ResourceCacheService::putLyrics(const CacheWriteContext& context, SongLyrics lyrics,
                                        qint64 allowedTtlMs,
                                        std::optional<CacheRetention> retention)
{
    if (!validWrite(context) || m_clearing || m_pending.size() >= 7 || allowedTtlMs <= 0 ||
        allowedTtlMs > maximumCacheTtl(CacheKind::Lyrics))
        return 0;
    CacheJob job;
    job.operation = CacheOperation::Write;
    job.kind = CacheKind::Lyrics;
    job.key = lyrics.song.value;
    job.ttlMs = allowedTtlMs;
    job.retention = retention;
    job.payload = encodeCacheValue(CacheValue(std::move(lyrics)), job.kind);
    if (job.payload.isEmpty())
        return 0;
    return submit(std::move(job));
}
quint64 ResourceCacheService::cover(QUrl url, int size, CacheReadMode mode)
{
    if (!validCoverUrl(url) || !validCoverSize(size) || mode < CacheReadMode::CacheOnly ||
        mode > CacheReadMode::Refresh)
        return 0;
    CacheJob job;
    job.operation = CacheOperation::Cover;
    job.kind = CacheKind::Cover;
    job.key = coverCacheKey(url, size);
    job.coverUrl = std::move(url);
    job.coverSize = size;
    job.mode = mode;
    return submit(std::move(job));
}
quint64 ResourceCacheService::remove(CacheKind kind, SongId song)
{
    if ((kind != CacheKind::Song && kind != CacheKind::Lyrics) || !validMusicId(song.value))
        return 0;
    CacheJob job;
    job.operation = CacheOperation::Remove;
    job.kind = kind;
    job.key = song.value;
    return invalidate(std::move(job));
}
quint64 ResourceCacheService::removeCover(QUrl url, int size)
{
    CacheJob job;
    job.key = coverCacheKey(url, size);
    if (job.key.isEmpty())
        return 0;
    job.operation = CacheOperation::Remove;
    job.kind = CacheKind::Cover;
    return invalidate(std::move(job));
}
quint64 ResourceCacheService::clear(bool allAccounts)
{
    CacheJob job;
    job.operation = allAccounts ? CacheOperation::ClearAll : CacheOperation::Clear;
    return invalidate(std::move(job));
}
quint64 ResourceCacheService::invalidate(CacheJob job)
{
    if (!live() || m_clearing)
        return 0;
    const auto source = m_source;
    m_clearing = true;
    if (!retire(CacheError::Canceled))
        return 0;
    if (!live() || m_source != source) {
        m_clearing = false;
        return 0;
    }
    m_clearing = true;
    const auto id = submit(std::move(job));
    if (!id)
        m_clearing = false;
    return id;
}
quint64 ResourceCacheService::usage()
{
    CacheJob job;
    job.operation = CacheOperation::Usage;
    return submit(std::move(job));
}
void ResourceCacheService::cancel(quint64 id)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (QThread::currentThread() != thread())
        return;
    const auto it = m_pending.find(id);
    if (it == m_pending.end())
        return;
    // Keep clear admission closed until its backend receipt; cancel is not SQL rollback.
    if (it->clear || it->forced)
        return;
    it->forced = CacheError::Canceled;
    emit cancelRequested(
        id); // hold admission ticket until backend receipt, even after cancellation
}
bool ResourceCacheService::retire(CacheError error)
{
    if (m_retiring)
        return true;
    m_retiring = true;
    if (m_revision == std::numeric_limits<quint64>::max())
        m_closed = true;
    else
        ++m_revision;
    const auto ids = m_pending.keys();
    const QPointer<ResourceCacheService> guard(this);
    emit invalidated();
    if (!guard)
        return false;
    for (const auto id : ids) {
        const auto it = m_pending.find(id);
        if (it == m_pending.end() || it->forced)
            continue;
        it->forced = error;
        emit cancelRequested(id);
        if (!guard)
            return false;
    }
    m_retiring = false;
    return true;
}
void ResourceCacheService::deliver(CacheResult result)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (QThread::currentThread() != thread())
        return;
    const auto it = m_pending.find(result.id);
    if (it == m_pending.end())
        return;
    const auto original = *it;
    m_pending.erase(it);
    if (original.clear)
        m_clearing = false;
    if (original.forced || !live() || original.source != m_source ||
        original.revision != m_revision) {
        result.value = {};
        result.usage = {};
        result.persisted = false;
        result.status = CacheStatus::Miss;
        result.error = original.forced.value_or(CacheError::StaleContext);
    }
    emit completed(std::move(result));
}
void ResourceCacheService::close()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (QThread::currentThread() != thread())
        return;
    if (m_closed)
        return;
    m_closed = true;
    retire(CacheError::Canceled);
}
} // namespace cirrus::cache
