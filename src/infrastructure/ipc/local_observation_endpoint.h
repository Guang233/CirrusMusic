#pragma once
#include <QString>
#include <QUuid>
#include <memory>

namespace cirrus::infrastructure::ipc
{
struct LocalObservationEndpointConfig {
    bool enabled = false;
    QString runtimeRoot; // Explicit private OS root; no HOME/temp/identity fallback.
    QString applicationNamespace;
    QUuid instanceId;
};
enum class LocalEndpointError {
    None,
    Disabled,
    InvalidConfig,
    UnsafePath,
    Collision,
    PathLimit,
    Io,
    Unsupported
};
enum class LocalObservationHandleKind { Invalid, Socket, Pipe, Event };
struct LocalObservationHandle {
    using Kind = LocalObservationHandleKind;
    qintptr value = -1;
    Kind kind{};
    [[nodiscard]] bool valid() const;
};
inline bool LocalObservationHandle::valid() const { return value != -1 && kind != Kind::Invalid; }
struct LocalObservationAdmission {
    LocalObservationHandle peer;
    quint8 firstByte = 0;
    bool hasFirstByte = false;
    // Windows must READ one byte before impersonation. Caller must feed this
    // byte to its framer FIRST and account for it in its bounded input storage.
    [[nodiscard]] bool valid() const { return peer.valid(); }
};
enum class LocalObservationPeerRole { Client, Server };
// Native identity, never a discovery PID/UUID/name inference. On Windows the
// server requires a completed client-byte READ, not Peek, before impersonation.
bool sameUserLocalObservationPeer(LocalObservationHandle peer, LocalObservationPeerRole role);
// Caller owns accepted peer. Windows server disconnects first; client only closes.
void closeLocalObservationPeer(LocalObservationHandle peer, LocalObservationPeerRole role);
// Resource/credential boundary ONLY, not a composed platform observation service.
// All operations after construction, including guards/destruction, belong to I/O.
// Never call on playback owner. No native I/O loop, privacy policy or decoder here.
class LocalObservationEndpoint
{
  public:
    static std::unique_ptr<LocalObservationEndpoint>
    create(const LocalObservationEndpointConfig& config, LocalEndpointError& error);
    virtual ~LocalObservationEndpoint() = default;
    [[nodiscard]] virtual QString manifestPath() const = 0;
    [[nodiscard]] virtual QString address() const = 0;
    // Borrowed socket-read/event handle. Windows resets connect readiness then
    // arms an overlapped one-byte credential probe; its completion signals it.
    // Caller must also schedule the fixed initial-byte deadline, never spin.
    // Handle may change on accept: disarm notifier before accept/close and rebind
    // the new borrowed handle afterwards. Endpoint owns this event/socket.
    [[nodiscard]] virtual LocalObservationHandle readiness() const = 0;
    [[nodiscard]] virtual bool current() const = 0;
    // <=1 admission/call; invalid means no admissible peer. Caller owns success.
    virtual LocalObservationAdmission acceptSameUser() = 0;
    virtual void close() = 0;
};
} // namespace cirrus::infrastructure::ipc
