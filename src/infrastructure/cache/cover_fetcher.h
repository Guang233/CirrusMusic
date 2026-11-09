#pragma once
#include "resource_cache_store.h"
#include <QHash>
#include <QNetworkAccessManager>
#include <memory>

namespace cirrus::cache
{
class CoverFetcher final : public QObject
{
    Q_OBJECT
  public:
    explicit CoverFetcher(QObject* parent = nullptr);
    ~CoverFetcher() override;
    void request(CacheJob job); // explicit, never implicit metadata prefetch
    void cancel(quint64 id);
    void close();
    // Why the image hosts' bytes are not taken as a cover (nullptr when they are): a
    // single-frame JPEG, PNG or WebP of at most 2048×2048 px (4 Mpx) that decodes in
    // full. Decoding is the slow part: called on a pool thread.
    static const char* rejection(const QByteArray& bytes);
    // The same checks on what the cache kept, from the header only (fast); the store
    // already compared the bytes with their digest.
    static bool plausible(const QByteArray& bytes);
  signals:
    // `bytes` are the image hosts' own (scaled by them to the asked size), not re-encoded.
    void completed(quint64 id, cirrus::domain::CacheError error, QByteArray bytes, qint64 ttlMs);

  private:
    struct Pending;
    void consume(quint64 id);
    void finish(quint64 id, domain::CacheError error);
    void checked(quint64 id, const char* rejected);
    void complete(const std::shared_ptr<Pending>& pending, domain::CacheError error,
                  QByteArray bytes, qint64 ttlMs);
    QNetworkAccessManager m_network;
    QHash<quint64, std::shared_ptr<Pending>> m_pending;
    // Downloaded, being checked on the pool.
    QHash<quint64, std::shared_ptr<Pending>> m_checking;
    bool m_closed = false;
};
} // namespace cirrus::cache
