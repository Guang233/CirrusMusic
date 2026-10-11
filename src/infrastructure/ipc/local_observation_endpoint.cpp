#include "local_observation_endpoint.h"
#if defined(Q_OS_LINUX)
#include "linux_observation_endpoint.h"
#endif
#if defined(Q_OS_LINUX) || defined(Q_OS_MACOS)
#include <climits>
#include <unistd.h>
#endif

namespace cirrus::infrastructure::ipc
{
#if defined(Q_OS_MACOS)
std::unique_ptr<LocalObservationEndpoint>
createMacObservationEndpoint(const LocalObservationEndpointConfig&, LocalEndpointError&);
bool macObservationSameUserPeer(qintptr);
#elif defined(Q_OS_WIN)
std::unique_ptr<LocalObservationEndpoint>
createWindowsObservationEndpoint(const LocalObservationEndpointConfig&, LocalEndpointError&);
bool windowsObservationSameUserPeer(qintptr, LocalObservationPeerRole);
void closeWindowsObservationPeer(qintptr, LocalObservationPeerRole);
#endif
namespace
{
#if defined(Q_OS_LINUX)
class LinuxEndpointAdapter final : public LocalObservationEndpoint
{
  public:
    explicit LinuxEndpointAdapter(std::unique_ptr<LinuxObservationEndpoint> endpoint)
        : m_endpoint(std::move(endpoint))
    {
    }
    QString manifestPath() const override { return m_endpoint->manifestPath(); }
    QString address() const override { return m_endpoint->socketPath(); }
    LocalObservationHandle readiness() const override
    {
        return {m_endpoint->listeningDescriptor(), LocalObservationHandleKind::Socket};
    }
    bool current() const override { return m_endpoint->current(); }
    LocalObservationAdmission acceptSameUser() override
    {
        const int fd = m_endpoint->acceptSameUser();
        if (fd < 0)
            return {};
        return {{fd, LocalObservationHandleKind::Socket}, 0, false};
    }
    void close() override { m_endpoint->close(); }

  private:
    std::unique_ptr<LinuxObservationEndpoint> m_endpoint;
};
LocalEndpointError mapped(LinuxEndpointError error)
{
    switch (error) {
    case LinuxEndpointError::None:
        return LocalEndpointError::None;
    case LinuxEndpointError::Disabled:
        return LocalEndpointError::Disabled;
    case LinuxEndpointError::InvalidConfig:
        return LocalEndpointError::InvalidConfig;
    case LinuxEndpointError::UnsafePath:
        return LocalEndpointError::UnsafePath;
    case LinuxEndpointError::Collision:
        return LocalEndpointError::Collision;
    case LinuxEndpointError::PathLimit:
        return LocalEndpointError::PathLimit;
    case LinuxEndpointError::Io:
        return LocalEndpointError::Io;
    }
    return LocalEndpointError::Unsupported;
}
#endif
} // namespace
std::unique_ptr<LocalObservationEndpoint>
LocalObservationEndpoint::create(const LocalObservationEndpointConfig& config,
                                 LocalEndpointError& error)
{
    error = LocalEndpointError::Disabled;
    if (!config.enabled)
        return {};
#if defined(Q_OS_LINUX)
    LinuxEndpointError nativeError;
    auto endpoint = LinuxObservationEndpoint::create(
        {config.enabled, config.runtimeRoot, config.applicationNamespace, config.instanceId},
        nativeError);
    error = mapped(nativeError);
    if (!endpoint)
        return {};
    return std::make_unique<LinuxEndpointAdapter>(std::move(endpoint));
#elif defined(Q_OS_MACOS)
    return createMacObservationEndpoint(config, error);
#elif defined(Q_OS_WIN)
    return createWindowsObservationEndpoint(config, error);
#else
    error = LocalEndpointError::Unsupported;
    return {};
#endif
}
bool sameUserLocalObservationPeer(LocalObservationHandle peer, LocalObservationPeerRole role)
{
    if (!peer.valid())
        return false;
#if defined(Q_OS_LINUX)
    Q_UNUSED(role);
    return peer.kind == LocalObservationHandleKind::Socket && peer.value >= 0 &&
           peer.value <= INT_MAX && linuxSameUserPeer(int(peer.value));
#elif defined(Q_OS_MACOS)
    Q_UNUSED(role);
    return peer.kind == LocalObservationHandleKind::Socket &&
           macObservationSameUserPeer(peer.value);
#elif defined(Q_OS_WIN)
    return peer.kind == LocalObservationHandleKind::Pipe &&
           windowsObservationSameUserPeer(peer.value, role);
#else
    Q_UNUSED(role);
    return false;
#endif
}
void closeLocalObservationPeer(LocalObservationHandle peer, LocalObservationPeerRole role)
{
    if (!peer.valid())
        return;
#if defined(Q_OS_WIN)
    if (peer.kind == LocalObservationHandleKind::Pipe)
        closeWindowsObservationPeer(peer.value, role);
#elif defined(Q_OS_LINUX) || defined(Q_OS_MACOS)
    Q_UNUSED(role);
    if (peer.kind == LocalObservationHandleKind::Socket && peer.value >= 0 && peer.value <= INT_MAX)
        ::close(int(peer.value));
#else
    Q_UNUSED(role);
#endif
}
} // namespace cirrus::infrastructure::ipc
