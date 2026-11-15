#pragma once

#include "domain/music/resource_cache_types.h"
#include "sources.h"

#include <QCache>
#include <QHash>
#include <QObject>

#include <memory>

namespace cirrus::runtime
{
class BackendRuntime;
}

namespace cirrus::ui
{

class CacheQueue;

// Covers from the backend's resource cache (docs/ui/integration.md §4 ImageSource):
// a cover key is the cover's URL, fetched once and kept on disk at one of the cache's
// sizes. Keys that aren't URLs (the sample covers, while pages still show fake data)
// go to `fallback`. The same cover asked for twice at once is fetched once; the bytes
// of the last ones shown stay in memory (decoding them again is cheap, the trip to the
// backend's disk cache is not).
class BackendImages : public QObject, public ImageSource
{
  public:
    BackendImages(runtime::BackendRuntime& runtime, std::unique_ptr<ImageSource> fallback,
                  QObject* parent = nullptr);
    ~BackendImages() override;

    Request image(const QString& key, int size, Reply<QByteArray> reply) override;

  private:
    struct Fetch;
    void finish(const QString& id, Outcome<QByteArray> outcome);
    void warnFailed(cirrus::domain::CacheError error);

    std::unique_ptr<ImageSource> m_fallback;
    CacheQueue* m_queue;
    // "url@size" -> the fetch and who waits for it.
    QHash<QString, std::shared_ptr<Fetch>> m_fetches;
    // "url@size" -> the image hosts' bytes; cost in KiB.
    QCache<QString, QByteArray> m_bytes{64 * 1024};
    quint64 m_nextWaiter = 0;
    quint32 m_warned = 0; // the CacheErrors cover.failed has warned about (bits)
};

// The resource cache's size and clearing (Settings › Storage).
class BackendCache : public QObject, public CacheSource
{
  public:
    explicit BackendCache(runtime::BackendRuntime& runtime, QObject* parent = nullptr);
    ~BackendCache() override;

    Request usage(Reply<CacheUsage> reply) override;
    Request clear(Reply<bool> reply) override;
    Request audioUsage(Reply<qint64> reply) override;
    Request clearAudio(Reply<qint64> reply) override;
    void setAudioCapacity(qint64 bytes) override;

  private:
    Request audio(bool clear, Reply<qint64> reply);

    runtime::BackendRuntime& m_runtime;
    CacheQueue* m_queue;
};

} // namespace cirrus::ui
