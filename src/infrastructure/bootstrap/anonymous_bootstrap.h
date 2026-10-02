#pragma once

#include "infrastructure/netease/api/session_api_client.h"
#include "infrastructure/netease/api/xeapi_key_registrar.h"
#include "xeapi_key_vault.h"

#include <functional>

namespace cirrus::bootstrap
{

enum class BootstrapError {
    None,
    Busy,
    Canceled,       // reset completed; not a successful bootstrap
    KeyUnavailable, // no stored key and registration failed
    RegisterFailed, // transport/decode/crypto/stale context of anonymous registration
    Rejected        // business code != 200 or no valid MUSIC_A in the response
};

struct BootstrapResult {
    BootstrapError error = BootstrapError::None;
    bool restored = false; // token came from the keyring, no registration was sent
    netease::XeapiKeyError keyError{};
    netease::ApiError apiError{};
    std::optional<int> businessCode;
};

// Guest startup sequence, never retried automatically:
//   restore xeapi key -> (register key) -> restore anonymous token -> (register
//   anonymous) -> adopt into the client -> persist to the keyring.
// The stored token is bound to the device ID it was registered for; a different
// device ID makes it unusable. Persist failures are reported but the token stays
// usable in memory. All collaborators live in this thread and outlive this object.
// One bootstrap owns token sequencing for a device/client/store; callers must not
// concurrently restore the vault or write/remove the same token entry elsewhere.
// Destruction cancels this object's API request, not already-dispatched keyring jobs.
// Keep this object alive until reset completion when persistent erasure is required.
class AnonymousBootstrap final : public QObject
{
    Q_OBJECT
  public:
    static constexpr char TokenEntryKey[] = "anonymous-token";
    using ContextProvider = std::function<domain::SessionToken()>;

    AnonymousBootstrap(netease::SessionApiClient& client, netease::XeapiKeyRing& ring,
                       netease::XeapiKeyRegistrar& registrar, XeapiKeyVault& vault,
                       secrets::SecretStore& store, QString deviceId, ContextProvider context,
                       QObject* parent = nullptr);
    ~AnonymousBootstrap() override;

    [[nodiscard]] bool start(); // false while running or draining a reset
    // Clear memory immediately, drain pending work, then delete the keyring record.
    // finished(Canceled) acknowledges the reset; persistFailed reports deletion failure.
    // A failed deletion is not proof of erasure across process restart.
    void reset();

  signals:
    void finished(cirrus::bootstrap::BootstrapResult result);
    void persistFailed(cirrus::secrets::SecretError error);

  private:
    void keyRestored();
    void keyRegistered(const netease::XeapiKeyResult& result);
    void restoreToken();
    void registerAnonymous();
    void registered(quint64 id, domain::SessionToken context, const netease::ApiResult& result);
    void complete(BootstrapResult result);
    void eraseToken();
    [[nodiscard]] bool currentContext();

    netease::SessionApiClient& m_client;
    netease::XeapiKeyRing& m_ring;
    netease::XeapiKeyRegistrar& m_registrar;
    XeapiKeyVault& m_vault;
    secrets::SecretStore& m_store;
    const QString m_deviceId;
    ContextProvider m_context;
    enum class Stage {
        Idle,
        Key,
        Registering,
        Token,
        Anonymous,
        Persisting,
        Erasing
    } m_stage = Stage::Idle;
    domain::SessionToken m_attempt;
    bool m_resetting = false;
    bool m_ignoreStoredToken = false;
    quint64 m_request = 0;
};

} // namespace cirrus::bootstrap

Q_DECLARE_METATYPE(cirrus::bootstrap::BootstrapResult)
