#include "resource_cache_worker.h"
#include "resource_cache_codec.h"
#include <QDateTime>
#include <QPointer>

namespace cirrus::cache
{
using namespace domain;
namespace
{
CacheResult failure(quint64 id, CacheError error)
{
    CacheResult result;
    result.id = id;
    result.error = error;
    return result;
}
} // namespace
ResourceCacheWorker::ResourceCacheWorker(storage::DatabaseWorker& db, QString root, Context context,
                                         QObject* parent)
    : QObject(parent), m_store(db, std::move(root)), m_context(std::move(context))
{
    connect(&m_store, &ResourceCacheStore::completed, this, &ResourceCacheWorker::stored);
    connect(&m_store, &ResourceCacheStore::idle, this, &ResourceCacheWorker::drain);
    connect(&m_fetcher, &CoverFetcher::completed, this, &ResourceCacheWorker::fetched);
}
bool ResourceCacheWorker::live(const CacheJob& job) const
{
    const auto context = m_context();
    return !m_closed && context && *context == job.source && job.source.token.isValid() &&
           job.revision == m_revision;
}
void ResourceCacheWorker::submit(CacheJob job)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!job.id || m_jobs.contains(job.id) || m_jobs.size() >= CacheJobsInFlight || m_closed ||
        job.revision < m_revision) {
        emit completed(failure(job.id, CacheError::Busy));
        return;
    }
    m_revision = job.revision;
    if (!live(job)) {
        emit completed(failure(job.id, CacheError::StaleContext));
        return;
    }
    if (job.operation == CacheOperation::Write) {
        const auto value = decodeCacheValue(job.payload, job.kind);
        if (job.kind == CacheKind::Cover || !value ||
            (job.kind == CacheKind::Song && std::get<Song>(*value).id.value != job.key) ||
            (job.kind == CacheKind::Lyrics && std::get<SongLyrics>(*value).song.value != job.key)) {
            emit completed(failure(job.id, CacheError::InvalidRequest));
            return;
        }
    }
    if (job.operation == CacheOperation::Cover &&
        (coverCacheKey(job.coverUrl, job.coverSize) != job.key || job.key.isEmpty())) {
        emit completed(failure(job.id, CacheError::InvalidRequest));
        return;
    }
    const auto id = job.id;
    m_jobs.insert(id, job);
    if (job.operation == CacheOperation::Clear || job.operation == CacheOperation::ClearAll ||
        job.operation == CacheOperation::Remove) {
        const auto ids = m_jobs.keys();
        for (const auto previous : ids)
            if (previous != id)
                cancel(previous);
        m_clear = std::move(job);
        drain();
        return;
    }
    if (job.operation == CacheOperation::Cover && job.mode == CacheReadMode::Refresh)
        fetch(job);
    else
        m_store.submit(std::move(job));
}
void ResourceCacheWorker::stored(CacheResult result)
{
    const auto it = m_jobs.constFind(result.id);
    if (it == m_jobs.cend()) {
        drain();
        return;
    }
    const auto job = *it;
    if (!live(job)) {
        finish(failure(job.id, CacheError::StaleContext));
        return;
    }
    if (m_networkValues.contains(job.id)) {
        result.value = m_networkValues.take(job.id);
        result.status = CacheStatus::Network; // error still reports a failed cache write
        finish(std::move(result));
        return;
    }
    if (job.operation == CacheOperation::Cover && result.error == CacheError::None) {
        if (result.status == CacheStatus::Hit) {
            // Checked in full when fetched; the digest matched: the header is enough.
            if (!CoverFetcher::plausible(std::get<QByteArray>(result.value)))
                result = failure(job.id, CacheError::Corrupt);
        } else if (job.mode == CacheReadMode::PreferCache &&
                   (result.status == CacheStatus::Miss || result.status == CacheStatus::Expired)) {
            fetch(job);
            return;
        }
    }
    finish(std::move(result));
}
void ResourceCacheWorker::fetch(const CacheJob& job)
{
    if (m_fetcher.full())
        m_fetchQueue.push_back(job.id);
    else
        m_fetcher.request(job);
}
void ResourceCacheWorker::nextFetch()
{
    while (!m_fetchQueue.empty() && !m_fetcher.full()) {
        const auto id = m_fetchQueue.front();
        m_fetchQueue.pop_front();
        const auto it = m_jobs.constFind(id);
        if (it == m_jobs.cend())
            continue; // canceled meanwhile
        if (!live(*it)) {
            finish(failure(id, CacheError::StaleContext));
            continue;
        }
        m_fetcher.request(*it);
    }
}
void ResourceCacheWorker::fetched(quint64 id, CacheError error, QByteArray bytes, qint64 ttlMs)
{
    // A download slot is free: the next waiting one goes first.
    QMetaObject::invokeMethod(this, &ResourceCacheWorker::nextFetch, Qt::QueuedConnection);
    const auto it = m_jobs.constFind(id);
    if (it == m_jobs.cend())
        return;
    auto job = *it;
    if (!live(job)) {
        finish(failure(id, CacheError::StaleContext));
        return;
    }
    if (error != CacheError::None) {
        finish(failure(id, error));
        return;
    }
    const auto now = QDateTime::currentMSecsSinceEpoch();
    if (ttlMs <= 0 || ttlMs > maximumCacheTtl(CacheKind::Cover) || now <= 0 ||
        now > 253402300799999LL - 7 * CacheDayMs) {
        CacheResult result;
        result.id = id;
        result.value = std::move(bytes);
        result.status = CacheStatus::Network;
        finish(std::move(result));
        return;
    }
    m_networkValues.insert(id, bytes);
    job.operation = CacheOperation::Write;
    job.payload = std::move(bytes);
    job.ttlMs = ttlMs;
    job.notAfterMs = now + ttlMs;
    m_store.submit(std::move(job));
}
void ResourceCacheWorker::finish(CacheResult result)
{
    m_jobs.remove(result.id);
    m_networkValues.remove(result.id);
    const QPointer<ResourceCacheWorker> guard(this);
    emit completed(std::move(result));
    if (guard)
        drain();
}
void ResourceCacheWorker::cancel(quint64 id)
{
    if (!m_jobs.remove(id))
        return;
    if (m_clear && m_clear->id == id)
        m_clear.reset();
    m_networkValues.remove(id);
    m_fetcher.cancel(id);
    emit completed(failure(id, CacheError::Canceled));
}
void ResourceCacheWorker::contextChanged()
{
    const auto ids = m_jobs.keys();
    for (const auto id : ids) {
        const auto it = m_jobs.constFind(id);
        if (it != m_jobs.cend() && !live(*it))
            cancel(id);
    }
    drain();
}
void ResourceCacheWorker::close()
{
    if (m_closed)
        return;
    m_closed = true;
    m_clear.reset();
    m_fetchQueue.clear();
    const auto ids = m_jobs.keys();
    for (const auto id : ids)
        cancel(id);
    m_fetcher.close();
    drain();
}
bool ResourceCacheWorker::idle() const
{
    return m_jobs.isEmpty() && m_store.pending() == 0 && !m_clear;
}
void ResourceCacheWorker::drain()
{
    if (m_clear && m_store.pending() == 0 && !m_closed) {
        const auto job = *m_clear;
        m_clear.reset();
        m_store.submit(job);
        return;
    }
    if (idle())
        emit drained();
}
} // namespace cirrus::cache
