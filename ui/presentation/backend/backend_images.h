#pragma once

#include "sources.h"

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
// go to `fallback`. The same cover asked for twice at once is fetched once.
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

    std::unique_ptr<ImageSource> m_fallback;
    CacheQueue* m_queue;
    // "url@size" -> the fetch and who waits for it.
    QHash<QString, std::shared_ptr<Fetch>> m_fetches;
    quint64 m_nextWaiter = 0;
};

// The resource cache's size and clearing (Settings › Storage).
class BackendCache : public QObject, public CacheSource
{
  public:
    explicit BackendCache(runtime::BackendRuntime& runtime, QObject* parent = nullptr);
    ~BackendCache() override;

    Request usage(Reply<CacheUsage> reply) override;
    Request clear(Reply<bool> reply) override;

  private:
    CacheQueue* m_queue;
};

} // namespace cirrus::ui
