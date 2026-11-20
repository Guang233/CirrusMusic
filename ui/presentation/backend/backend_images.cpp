#include "backend_images.h"

#include "cache_queue.h"

#include "infrastructure/cache/resource_cache_service.h"
#include "infrastructure/runtime/backend_runtime.h"
#include "logging/log.h"

#include <QElapsedTimer>
#include <QPointer>
#include <QTimer>
#include <QUrl>

namespace cirrus::ui
{
namespace
{

// The sizes the cache keeps covers at.
constexpr int kSizes[] = {128, 256, 512, 1024};

// The smallest kept size at least `size` wide; 512 when unknown.
int bucket(int size)
{
    if (size <= 0)
        return 512;
    for (int kept : kSizes) {
        if (kept >= size)
            return kept;
    }
    return kSizes[std::size(kSizes) - 1];
}

// The cover URL the cache accepts (https on the service's image hosts, no query), or
// an invalid one. The service gives some covers as http.
QUrl coverUrl(const QString& key)
{
    QUrl url(key);
    if (url.scheme() == QStringLiteral("http"))
        url.setScheme(QStringLiteral("https"));
    url.setQuery(QString());
    url.setFragment(QString());
    static const QStringList hosts = {
        QStringLiteral("p1.music.126.net"), QStringLiteral("p2.music.126.net"),
        QStringLiteral("p3.music.126.net"), QStringLiteral("p4.music.126.net")};
    if (!url.isValid() || url.scheme() != QStringLiteral("https") || !hosts.contains(url.host()) ||
        !url.userInfo().isEmpty() || url.path().isEmpty())
        return {};
    return url;
}

failure::Kind failureOf(domain::CacheError error)
{
    switch (error) {
    case domain::CacheError::Network:
        return failure::Network;
    case domain::CacheError::Timeout:
        return failure::Timeout;
    case domain::CacheError::Busy:
        return failure::Busy;
    case domain::CacheError::InvalidRequest:
    case domain::CacheError::Unsupported:
        return failure::Rejected;
    default:
        return failure::Unknown;
    }
}

// Replies with a failure after the call returned.
template <class T> Request failLater(QObject* context, Reply<T> reply, failure::Kind kind)
{
    auto alive = std::make_shared<bool>(true);
    QTimer::singleShot(0, context, [alive, reply = std::move(reply), kind]() {
        if (*alive)
            reply(Outcome<T>{std::nullopt, false, Failure{kind, std::nullopt}});
    });
    return Request([alive]() { *alive = false; });
}

} // namespace

struct BackendImages::Fetch {
    quint64 ticket = 0;
    QElapsedTimer elapsed;
    QHash<quint64, Reply<QByteArray>> waiters;
};

BackendImages::BackendImages(runtime::BackendRuntime& runtime,
                             std::unique_ptr<ImageSource> fallback, QObject* parent)
    : QObject(parent), m_fallback(std::move(fallback)),
      m_queue(new CacheQueue(runtime.facade(), runtime.resourceCache(), this))
{
}

BackendImages::~BackendImages() = default;

Request BackendImages::image(const QString& key, int size, Reply<QByteArray> reply)
{
    if (!key.startsWith(QStringLiteral("http://")) && !key.startsWith(QStringLiteral("https://"))) {
        if (m_fallback)
            return m_fallback->image(key, size, std::move(reply));
        return failLater(this, std::move(reply), failure::Invalid);
    }
    const QUrl url = coverUrl(key);
    if (!url.isValid())
        return failLater(this, std::move(reply), failure::Rejected);
    const int kept = bucket(size);
    const QString id = url.toString() + u'@' + QString::number(kept);
    if (const QByteArray* bytes = m_bytes.object(id)) {
        auto alive = std::make_shared<bool>(true);
        QTimer::singleShot(0, this, [alive, reply = std::move(reply), bytes = *bytes]() {
            if (*alive)
                reply(Outcome<QByteArray>{bytes});
        });
        return Request([alive]() { *alive = false; });
    }

    std::shared_ptr<Fetch>& fetch = m_fetches[id];
    if (!fetch) {
        fetch = std::make_shared<Fetch>();
        fetch->elapsed.start();
        fetch->ticket = m_queue->enqueue(
            [url, kept](cache::ResourceCacheService& cache) {
                return cache.cover(url, kept, domain::CacheReadMode::PreferCache);
            },
            [this, id](const domain::CacheResult& result) {
                Outcome<QByteArray> outcome;
                // A cover fetched but not saved (the cache's disk) still shows.
                const auto* bytes = std::get_if<QByteArray>(&result.value);
                if (bytes && !bytes->isEmpty()) {
                    outcome.value = *bytes;
                    m_bytes.insert(id, new QByteArray(*bytes),
                                   int(std::max<qsizetype>(1, bytes->size() / 1024)));
                    if (const auto it = m_fetches.constFind(id); it != m_fetches.cend())
                        log::debug(lcUiBackend, "cover.served",
                                   {{"from", result.status == domain::CacheStatus::Hit ? "disk"
                                                                                       : "network"},
                                    {"bytes", bytes->size()},
                                    {"ms", (*it)->elapsed.elapsed()}});
                } else {
                    outcome.failure = Failure{failureOf(result.error), std::nullopt};
                    warnFailed(result.error);
                }
                finish(id, std::move(outcome));
            },
            [this, id](failure::Kind kind) {
                finish(id, Outcome<QByteArray>{std::nullopt, false, Failure{kind, std::nullopt}});
            });
    }
    const quint64 waiter = ++m_nextWaiter;
    fetch->waiters.insert(waiter, std::move(reply));

    return Request([self = QPointer<BackendImages>(this), id, waiter]() {
        if (!self)
            return;
        auto it = self->m_fetches.find(id);
        if (it == self->m_fetches.end())
            return;
        (*it)->waiters.remove(waiter);
        if ((*it)->waiters.isEmpty()) {
            self->m_queue->cancel((*it)->ticket);
            self->m_fetches.erase(it);
        }
    });
}

// The cover fetcher already warns about network failures (cover.failed). A cover the
// cache itself refuses (its folder, its database) fails the same way for every cover, so
// the first one of each kind is a warning and the rest only reach the flight recorder.
void BackendImages::warnFailed(domain::CacheError error)
{
    using domain::CacheError;
    if (error == CacheError::Network || error == CacheError::Timeout ||
        error == CacheError::Canceled || error == CacheError::StaleContext)
        return;
    const quint32 bit = 1u << (unsigned(error) % 32);
    const bool first = !(m_warned & bit);
    m_warned |= bit;
    if (first)
        log::warn(lcUiBackend, "cover.failed", {{"err", error}, {"first", true}});
    else
        log::debug(lcUiBackend, "cover.failed", {"err", error});
}

void BackendImages::finish(const QString& id, Outcome<QByteArray> outcome)
{
    const std::shared_ptr<Fetch> fetch = m_fetches.take(id);
    if (!fetch)
        return;
    for (const Reply<QByteArray>& reply : std::as_const(fetch->waiters))
        reply(outcome);
}

BackendCache::BackendCache(runtime::BackendRuntime& runtime, QObject* parent)
    : QObject(parent), m_runtime(runtime),
      m_queue(new CacheQueue(runtime.facade(), runtime.resourceCache(), this))
{
}

BackendCache::~BackendCache() = default;

Request BackendCache::usage(Reply<CacheUsage> reply)
{
    auto shared = std::make_shared<Reply<CacheUsage>>(std::move(reply));
    const quint64 ticket = m_queue->enqueue(
        [](cache::ResourceCacheService& cache) { return cache.usage(); },
        [shared](const domain::CacheResult& result) {
            Outcome<CacheUsage> outcome;
            if (result.error == domain::CacheError::None)
                outcome.value = CacheUsage{result.usage.metadataBytes + result.usage.coverBytes,
                                           domain::MetadataCacheBudget + domain::CoverCacheBudget};
            else
                outcome.failure = Failure{failureOf(result.error), std::nullopt};
            (*shared)(std::move(outcome));
        },
        [shared](failure::Kind kind) {
            (*shared)(Outcome<CacheUsage>{std::nullopt, false, Failure{kind, std::nullopt}});
        });
    return Request([queue = QPointer<CacheQueue>(m_queue), ticket]() {
        if (queue)
            queue->cancel(ticket);
    });
}

Request BackendCache::clear(Reply<bool> reply)
{
    auto shared = std::make_shared<Reply<bool>>(std::move(reply));
    const quint64 ticket = m_queue->enqueue(
        // Every account's: on this computer the cache is one thing to clear.
        [](cache::ResourceCacheService& cache) { return cache.clear(true); },
        [shared](const domain::CacheResult& result) {
            Outcome<bool> outcome;
            if (result.error == domain::CacheError::None)
                outcome.value = true;
            else
                outcome.failure = Failure{failureOf(result.error), std::nullopt};
            (*shared)(std::move(outcome));
        },
        [shared](failure::Kind kind) {
            (*shared)(Outcome<bool>{std::nullopt, false, Failure{kind, std::nullopt}});
        });
    return Request([queue = QPointer<CacheQueue>(m_queue), ticket]() {
        if (queue)
            queue->cancel(ticket);
    });
}

Request BackendCache::audioUsage(Reply<qint64> reply) { return audio(false, std::move(reply)); }

Request BackendCache::clearAudio(Reply<qint64> reply) { return audio(true, std::move(reply)); }

Request BackendCache::audio(bool clear, Reply<qint64> reply)
{
    // The runtime always answers (-1 when the backend isn't running); canceling or
    // destroying us only drops the answer.
    auto live = std::make_shared<bool>(true);
    auto answer = [live, self = QPointer<BackendCache>(this),
                   reply = std::move(reply)](qint64 bytes) {
        if (!*live || !self)
            return;
        Outcome<qint64> outcome;
        if (bytes >= 0)
            outcome.value = bytes;
        else
            outcome.failure = Failure{failure::Unknown, std::nullopt};
        reply(std::move(outcome));
    };
    if (clear)
        m_runtime.clearAudioCache(std::move(answer));
    else
        m_runtime.audioCacheUsage(std::move(answer));
    return Request([live] { *live = false; });
}

void BackendCache::setAudioCapacity(qint64 bytes) { m_runtime.setAudioCacheCapacity(bytes); }

} // namespace cirrus::ui
