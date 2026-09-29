#pragma once

#include "api_operation.h"
#include "infrastructure/netease/network/http_transport.h"
#include "infrastructure/netease/session/session_cookie_store.h"

#include <QHash>
#include <QObject>

namespace cirrus::netease
{

enum class ApiError {
    None,
    InvalidRequest,
    StaleContext,
    Canceled,
    Busy,
    Transport,
    Crypto,
    Decode,
    Cookie
};

// Infrastructure envelope only. Application ports must map this to typed values.
struct ApiResult {
    ApiError error{};
    TransportError transportError{};
    int httpStatus = 0;
    QByteArray retryAfter;
    DecodedResponse decoded;
};

// Owns transport + current-epoch credentials in ONE event-loop thread. Construct
// and destroy in that thread (the value-owned jar must not be moved afterwards).
// All completions carry the original request context; cancel is only an optimization.
class SessionApiClient final : public QObject
{
    Q_OBJECT
  public:
    explicit SessionApiClient(QString deviceId, QObject* parent = nullptr);
    explicit SessionApiClient(std::unique_ptr<HttpTransport> transport, QString deviceId,
                              QObject* parent = nullptr);
    [[nodiscard]] bool advanceTo(const domain::SessionToken& context);
    [[nodiscard]] bool accepts(const domain::SessionToken& context) const;
    [[nodiscard]] quint64 submit(domain::SessionToken context, ApiOperation operation,
                                 QJsonObject parameters = {});
    void cancel(quint64 requestId);

  signals:
    void contextAdvanced(cirrus::domain::SessionToken context);
    void completed(quint64 requestId, cirrus::domain::SessionToken context,
                   cirrus::netease::ApiResult result);

  private:
    struct Pending {
        domain::SessionToken context;
        ApiOperation operation{};
        ApiOperationContract contract;
        QUrl origin;
        QJsonObject payload;
        quint64 transportId = 0;
        ApiError forcedError{};
    };
    void start(quint64 requestId);
    void receive(quint64 transportId, const TransportResult& result);
    void finish(quint64 requestId, ApiResult result = {});

    HttpTransport* m_transport = nullptr; // QObject parent ownership
    const QString m_deviceId;
    SessionCookieStore m_cookies;
    domain::SessionToken m_context;
    QHash<quint64, Pending> m_pending;
    QHash<quint64, quint64> m_transportIds;
    quint64 m_nextId = 0;
};

} // namespace cirrus::netease

Q_DECLARE_METATYPE(cirrus::netease::ApiResult)
