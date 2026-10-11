#pragma once
#include <QString>
#include <QUuid>
#include <memory>

namespace cirrus::infrastructure::ipc
{
// Linux-only resource/credential primitive, NOT a running observation service.
// No defaults for release identity/root. Tests use an isolated synthetic namespace.
// Single I/O-thread ownership: setup/cleanup/guards may perform filesystem I/O,
// never invoke them on the playback owner. Accepted descriptors belong to callers;
// close() does not close those clients or supply their scheduling/privacy policy.
struct LinuxObservationEndpointConfig {
    bool enabled = false;
    QString runtimeRoot;
    QString applicationNamespace;
    QUuid instanceId;
};
enum class LinuxEndpointError {
    None,
    Disabled,
    InvalidConfig,
    UnsafePath,
    Collision,
    PathLimit,
    Io
};
// Connected AF_UNIX/SOCK_STREAM only; kernel SO_PEERCRED must match current euid.
// Both client and server must invoke this, never infer identity from path/pid/UUID.
bool linuxSameUserPeer(int descriptor);
class LinuxObservationEndpoint final
{
  public:
    static std::unique_ptr<LinuxObservationEndpoint>
    create(const LinuxObservationEndpointConfig& config, LinuxEndpointError& error);
    ~LinuxObservationEndpoint();
    LinuxObservationEndpoint(const LinuxObservationEndpoint&) = delete;
    LinuxObservationEndpoint& operator=(const LinuxObservationEndpoint&) = delete;
    [[nodiscard]] QString manifestPath() const;
    [[nodiscard]] QString socketPath() const;
    // Borrowed nonblocking CLOEXEC listener; do not close or transfer ownership.
    [[nodiscard]] int listeningDescriptor() const;
    // Accept <=1 per call, verify before returning ownership of the descriptor.
    // -1 means no admissible peer; caller must close a successful descriptor.
    int acceptSameUser();
    [[nodiscard]] bool current() const; // I/O-thread path/permission guard, fail closed
    void close(); // idempotent, only inode-matched owned resources; no recursion

  private:
    LinuxObservationEndpoint();
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace cirrus::infrastructure::ipc
