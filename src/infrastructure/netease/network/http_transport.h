#pragma once

#include "domain/music/cache_retention.h"
#include "infrastructure/netease/protocol/official_route.h"

#include <QByteArray>
#include <QHash>
#include <QList>
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
    ResponseHeadersTooLarge,
    RedirectBlocked,
    Network,
    HttpStatus
};
CIRRUS_LOG_ENUM(TransportError, None, InvalidRequest, Canceled, Timeout, ResponseTooLarge,
                ResponseHeadersTooLarge, RedirectBlocked, Network, HttpStatus)

struct TransportRequest {
    Protocol protocol = Protocol::Eapi;
    QString logicalPath;
    QByteArray formBody;
    int timeoutMs = 15000;
    qsizetype responseLimit = 4 * 1024 * 1024;
    // Explicit, bounded profile fields; no arbitrary header/Host overrides.
    QByteArray cookieHeader;
    QByteArray userAgent;
    bool musicReferer = false; // fixed official web origin, never caller-supplied URL
    // Protocol::Xeapi only; names restricted to the fixed profile set, no duplicates.
    QList<std::pair<QByteArray, QByteArray>> xeapiHeaders;
    QByteArray commentV3Token; // only two closed XEAPI comment paths, not arbitrary headers
    OfficialDestination destination = OfficialDestination::Default;
    // ListeningLog only: the `?csrf_token=` the web player adds to its log URL. That
    // destination also gets the player's fixed Origin and `x-music-web-os` headers.
    QByteArray csrfQuery;
    // Reads only: a connection that closed before any response (HTTP/2 "connection
    // closed", remote host closed) is tried again over HTTP/1.1, at once and, closed
    // again, a second later. Never for writes.
    bool retryLostConnection = false;
};

struct TransportResult {
    TransportError error = TransportError::None;
    int httpStatus = 0;
    int networkError = 0;
    QByteArray body;
    QByteArray retryAfter;
    QList<QByteArray> setCookieHeaders; // infrastructure-only, never log/broadcast to UI
    // x-encr-ssid / x-encr-sskey: successful Protocol::Xeapi responses only, both
    // present and bounded, otherwise both empty. Secret; never log.
    QByteArray xeapiSessionId;
    QByteArray xeapiSessionKey;
    domain::CacheRetention cacheRetention{}; // no raw response headers leave infrastructure
};

// Thread-affine async POST transport. It does not sign, decrypt, parse
// provider codes or persist credentials. No automatic cookies/auth/redirects. The
// only retries are TransportRequest::retryLostConnection's, for reads.
class HttpTransport : public QObject
{
    Q_OBJECT
  public:
    explicit HttpTransport(QObject* parent = nullptr);
    // Owned, same-thread, parentless manager injection for offline tests.
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
