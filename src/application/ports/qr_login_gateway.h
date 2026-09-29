#pragma once

#include "domain/account/qr_login_types.h"
#include "domain/account/session_types.h"

#include <QObject>

#include <optional>
#include <variant>

namespace cirrus::application
{

enum class AuthError {
    InvalidRequest,
    Canceled,
    StaleContext,
    Busy,
    Network,
    Timeout,
    RateLimited,
    ProviderRejected,
    InvalidResponse,
    CredentialRejected,
    ProtocolFailure
};

struct AuthFailure {
    AuthError error{};
    std::optional<int> providerCode;
    std::optional<qint64> retryAfterMs;
};

using QrKeyResult = std::variant<domain::QrLoginChallenge, AuthFailure>;
using QrStatusResult = std::variant<domain::QrLoginStatus, AuthFailure>;

// Narrow, thread-affine port; invoke in the object's owning event-loop thread.
// All results (including validation failures) are deferred, by value, and carry
// the original token. Composition must synchronize the provider context first.
// No polling loop, QR rasterizer, HTTP, JSON, Cookie or identity promotion here.
class IQrLoginGateway : public QObject
{
    Q_OBJECT
  public:
    using QObject::QObject;
    ~IQrLoginGateway() override = default;
    [[nodiscard]] virtual quint64 requestKey(domain::SessionToken context) = 0;
    [[nodiscard]] virtual quint64 checkStatus(domain::SessionToken context, QString key) = 0;
    virtual void cancel(quint64 requestId) = 0;

  signals:
    void keyCompleted(quint64 requestId, cirrus::domain::SessionToken context,
                      cirrus::application::QrKeyResult result);
    void statusCompleted(quint64 requestId, cirrus::domain::SessionToken context,
                         cirrus::application::QrStatusResult result);
};

} // namespace cirrus::application

Q_DECLARE_METATYPE(cirrus::application::QrKeyResult)
Q_DECLARE_METATYPE(cirrus::application::QrStatusResult)
