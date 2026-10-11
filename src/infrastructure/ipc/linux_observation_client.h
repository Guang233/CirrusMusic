#pragma once
#include <QString>
#include <QUuid>
#include <memory>
namespace cirrus::infrastructure::ipc
{
struct LinuxObservationClientConfig {
    QString runtimeRoot, applicationNamespace;
    QUuid instanceId; // explicit instance selection; no scan/fallback/auto-start
};
enum class LinuxObservationClientState { Connecting, Hello, Verified, Failed, Closed };
enum class LinuxObservationClientError {
    None,
    InvalidConfig,
    UnsafePath,
    Unavailable,
    Protocol,
    InstanceMismatch,
    Timeout
};
// I/O-thread-only private discovery/hello consumer. Filesystem guards may block;
// socket operations are nonblocking, <=1 send/recv per advance, fixed 5s deadline.
// Both kernel UID and canonical manifest/hello instance association are mandatory.
// No business request, media command, auto-reconnect, log or file cleanup authority.
class LinuxObservationClient final
{
  public:
    static std::unique_ptr<LinuxObservationClient>
    connect(const LinuxObservationClientConfig& config, LinuxObservationClientError& error);
    ~LinuxObservationClient();
    LinuxObservationClientState advance();
    [[nodiscard]] LinuxObservationClientState state() const;
    [[nodiscard]] LinuxObservationClientError error() const;
    int takeVerifiedDescriptor(); // transfer exactly once; caller owns consumer lifecycle/budgets
    void close();

  private:
    LinuxObservationClient();
    class Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace cirrus::infrastructure::ipc
