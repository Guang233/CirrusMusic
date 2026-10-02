#pragma once

#include "api_operation.h"
#include "infrastructure/netease/network/http_transport.h"
#include "infrastructure/netease/session/session_cookie_store.h"
#include "xeapi_key_ring.h"

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QPointer>

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
    Cookie,
    MissingProtocolKey // xeapi without a valid installation key; nothing was sent
};

// Infrastructure envelope only. Application ports must map this to typed values.
struct ApiResult {
    ApiError error{};
    TransportError transportError{};
    int httpStatus = 0;
    QByteArray retryAfter;
    DecodedResponse decoded;
    QUuid credentialReceipt; // candidate snapshot handle, never itself proof of identity
    // AnonymousRegister with code 200 only: validated MUSIC_A, NOT adopted automatically.
    QByteArray anonymousToken;
};

[[nodiscard]] bool validAnonymousToken(const QByteArray& token);
// Single consistent, unexpired MUSIC_A scoped to music.163.com or the origin host.
[[nodiscard]] QByteArray anonymousTokenFrom(const QUrl& origin,
                                            const QList<QByteArray>& setCookieHeaders);

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
    // Infrastructure-only: auth adapter must validate identity before handing
    // this one-use request-bound snapshot to the prepared session token.
    [[nodiscard]] bool promoteReceipt(domain::SessionToken from, domain::SessionToken to,
                                      const QUuid& receipt);
    void discardReceipt(const QUuid& receipt);
    // Secure-store adapter only. Import stages an untrusted saved record into the
    // current attempt; callers MUST verify identity before promotion/publication.
    [[nodiscard]] std::optional<QByteArray> saveCredentials(domain::SessionToken context,
                                                            QString accountId) const;
    [[nodiscard]] std::optional<QString> restoreCredentials(domain::SessionToken context,
                                                            const QByteArray& record);
    // Installation key for xeapi operations; same thread, tracked weakly.
    void setXeapiKeyRing(XeapiKeyRing* ring);
    // Installation-level guest credential; survives epoch changes and is sent only
    // when the session has no MUSIC_U. Invalid values clear it.
    void setAnonymousToken(QByteArray token);
    [[nodiscard]] bool hasAnonymousToken() const { return !m_anonymousToken.isEmpty(); }

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
        std::shared_ptr<SessionCookieStore> credentials;
    };
    void invalidatePrevious(domain::SessionToken context);
    void start(quint64 requestId);
    void receive(quint64 transportId, const TransportResult& result);
    void finish(quint64 requestId, ApiResult result = {});
    void startXeapi(quint64 requestId, Pending& pending);
    [[nodiscard]] QByteArray guestMusicA(const QByteArray& musicU, const QByteArray& musicA) const;

    HttpTransport* m_transport = nullptr; // QObject parent ownership
    QPointer<XeapiKeyRing> m_xeapiKeys;
    std::optional<XeapiSessionKey> m_xeapiSession; // current epoch only; secret
    QByteArray m_anonymousToken;                   // installation-level; secret
    const QString m_deviceId;
    SessionCookieStore m_cookies;
    domain::SessionToken m_context;
    QHash<quint64, Pending> m_pending;
    QHash<quint64, quint64> m_transportIds;
    quint64 m_nextId = 0;
    QUuid m_receiptId;
    domain::SessionToken m_receiptContext;
    std::shared_ptr<SessionCookieStore> m_receiptCookies;
    QElapsedTimer m_receiptAge;
};

} // namespace cirrus::netease

Q_DECLARE_METATYPE(cirrus::netease::ApiResult)
