#pragma once

#include "domain/account/credential_login_types.h"
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

// One-use correlation handle, not a Cookie or persistent login credential.
struct VerifiedLogin {
    domain::AccountIdentity identity;
    QUuid receipt;
};
using IdentityResult = std::variant<VerifiedLogin, AuthFailure>;
using PromotionResult = std::variant<domain::AccountIdentity, AuthFailure>;
using SavedSessionResult = std::variant<bool, AuthFailure>; // true saved, false forgotten
// Both the same type as SavedSessionResult (one metatype).
// true: the provider accepted the credentials (like QR 803); identity is still unverified.
using CredentialLoginResult = std::variant<bool, AuthFailure>;
using SmsCodeResult = std::variant<bool, AuthFailure>; // true: the provider sent a code

// Narrow, thread-affine port; invoke in the object's owning event-loop thread.
// All results (including validation failures) are deferred, by value, and carry
// the original token. Composition must synchronize the provider context first.
// Synchronization is a local, nonblocking context barrier. Network operations
// and credential promotion complete asynchronously. No HTTP/JSON/Cookie leaks.
class IQrLoginGateway : public QObject
{
    Q_OBJECT
  public:
    using QObject::QObject;
    ~IQrLoginGateway() override = default;
    [[nodiscard]] virtual quint64 requestKey(domain::SessionToken context) = 0;
    [[nodiscard]] virtual quint64 checkStatus(domain::SessionToken context, QString key) = 0;
    [[nodiscard]] virtual bool synchronize(domain::SessionToken context) = 0;
    [[nodiscard]] virtual quint64 verifyIdentity(domain::SessionToken context) = 0;
    [[nodiscard]] virtual quint64 promoteCredentials(domain::SessionToken attempt,
                                                     domain::SessionToken target,
                                                     QUuid receipt) = 0;
    [[nodiscard]] virtual quint64 restoreIdentity(domain::SessionToken context) = 0;
    virtual void storeSession(domain::SessionToken context, domain::AccountIdentity identity,
                              bool remember) = 0;
    virtual void forgetSession(domain::SessionToken context) = 0;
    virtual void cancel(quint64 requestId) = 0;
    // Credential sign-in (docs/api/credential-login.md). 0: not supported by this
    // gateway (the caller fails at once); otherwise completes like the QR operations.
    [[nodiscard]] virtual quint64 requestCredentialLogin(domain::SessionToken context,
                                                         domain::CredentialLogin login)
    {
        Q_UNUSED(context);
        Q_UNUSED(login);
        return 0;
    }
    // Sends an SMS code for a later PhoneCode login; no session change.
    [[nodiscard]] virtual quint64 sendSmsCode(domain::SessionToken context,
                                              domain::SmsCodeRequest request)
    {
        Q_UNUSED(context);
        Q_UNUSED(request);
        return 0;
    }

  signals:
    void keyCompleted(quint64 requestId, cirrus::domain::SessionToken context,
                      cirrus::application::QrKeyResult result);
    void statusCompleted(quint64 requestId, cirrus::domain::SessionToken context,
                         cirrus::application::QrStatusResult result);
    void identityCompleted(quint64 requestId, cirrus::domain::SessionToken context,
                           cirrus::application::IdentityResult result);
    void promotionCompleted(quint64 requestId, cirrus::domain::SessionToken target,
                            cirrus::application::PromotionResult result);
    void savedSessionCompleted(cirrus::domain::SessionToken context,
                               cirrus::application::SavedSessionResult result);
    void credentialLoginCompleted(quint64 requestId, cirrus::domain::SessionToken context,
                                  cirrus::application::CredentialLoginResult result);
    void smsCodeCompleted(quint64 requestId, cirrus::domain::SessionToken context,
                          cirrus::application::SmsCodeResult result);
};

} // namespace cirrus::application

Q_DECLARE_METATYPE(cirrus::application::AuthFailure)
Q_DECLARE_METATYPE(cirrus::application::QrKeyResult)
Q_DECLARE_METATYPE(cirrus::application::QrStatusResult)
Q_DECLARE_METATYPE(cirrus::application::IdentityResult)
Q_DECLARE_METATYPE(cirrus::application::PromotionResult)
Q_DECLARE_METATYPE(cirrus::application::SavedSessionResult)
