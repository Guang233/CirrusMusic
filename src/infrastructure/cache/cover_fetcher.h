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
    static QByteArray normalize(QByteArray bytes, int size);
  signals:
    void completed(quint64 id, cirrus::domain::CacheError error, QByteArray png, qint64 ttlMs);

  private:
    struct Pending;
    void consume(quint64 id);
    void finish(quint64 id, domain::CacheError error);
    QNetworkAccessManager m_network;
    QHash<quint64, std::shared_ptr<Pending>> m_pending;
    bool m_closed = false;
};
} // namespace cirrus::cache
