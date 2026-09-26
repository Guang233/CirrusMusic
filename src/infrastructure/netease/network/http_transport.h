#pragma once

#include "infrastructure/netease/protocol/official_route.h"

#include <QByteArray>
#include <QHash>
#include <QNetworkAccessManager>
#include <QObject>

#include <memory>

namespace cirrus::netease
{

enum class TransportError {
    None,
    InvalidRequest,
    Canceled,
    Timeout,
    ResponseTooLarge,
    RedirectBlocked,
    Network,
    HttpStatus
};

struct TransportRequest {
    Protocol protocol = Protocol::Eapi;
    QString logicalPath;
    QByteArray formBody;
    int timeoutMs = 15000;
    qsizetype responseLimit = 4 * 1024 * 1024;
};

struct TransportResult {
    TransportError error = TransportError::None;
    int httpStatus = 0;
    int networkError = 0;
    QByteArray body;
    QByteArray retryAfter;
};

// Thread-affine async POST transport. It does not sign, retry, decrypt, parse
// provider codes or persist credentials. No automatic cookies/auth/redirects.
class HttpTransport : public QObject
{
    Q_OBJECT
  public:
    explicit HttpTransport(QObject* parent = nullptr);
    // Owned, same-thread, unparented manager injection for offline tests.
    explicit HttpTransport(std::unique_ptr<QNetworkAccessManager> manager,
                           QObject* parent = nullptr);
    ~HttpTransport() override;

    [[nodiscard]] quint64 post(TransportRequest request);
    void cancel(quint64 requestId);

  signals:
    void completed(quint64 requestId, const cirrus::netease::TransportResult& result);

  private:
    struct Pending;
    void start(quint64 requestId, const TransportRequest& request);
    void consume(quint64 requestId);
    void terminate(quint64 requestId, TransportError error);
    void finish(quint64 requestId);
    static bool drain(const std::shared_ptr<Pending>& pending);

    // QObject parent owns the manager after constructor injection transfers it.
    QNetworkAccessManager* m_manager = nullptr;
    QHash<quint64, std::shared_ptr<Pending>> m_pending;
    quint64 m_nextId = 0;
};

} // namespace cirrus::netease

Q_DECLARE_METATYPE(cirrus::netease::TransportResult)
