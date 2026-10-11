#pragma once
#include "linux_observation_io.h"
#include <QPointer>
#include <atomic>

namespace cirrus::infrastructure::ipc
{
struct LinuxObservationServiceConfig {
    bool enabled = false;
    QString runtimeRoot, applicationNamespace; // no fallback/release identity
    // Explicit opt-in XDG selection. Missing/unsafe XDG is a start failure, never
    // HOME/temp fallback. Permission/canonical/pinned checks remain on I/O thread.
    static LinuxObservationServiceConfig fromXdg(bool enabled, QString applicationNamespace);
};
enum class LinuxObservationServiceState { Disabled, Starting, Running, Stopping, Stopped, Failed };
struct LinuxObservationShutdown {
    std::atomic<bool> canceled{false}, complete{true};
};
// Owner-thread one-shot composition. Explicit start only, never product entry wiring.
// No filesystem/socket work or wait()/blocking queued invocation on owner. close()
// synchronously retires the producer and asynchronously cancels the I/O thread.
// Destruction is nonblocking; caller MUST drain shutdownTicket()->complete before
// application/event-loop exit (the detached QThread owns its own cleanup lifetime).
class LinuxObservationService final : public QObject
{
    Q_OBJECT
  public:
    explicit LinuxObservationService(application::PlaybackObservationPublisher& publisher,
                                     QObject* parent = nullptr);
    ~LinuxObservationService() override;
    bool start(const LinuxObservationServiceConfig& config);
    void close();
    [[nodiscard]] LinuxObservationServiceState state() const;
    [[nodiscard]] QString manifestPath() const; // infrastructure-only private discovery hint
    [[nodiscard]] std::shared_ptr<LinuxObservationShutdown> shutdownTicket() const;
    [[nodiscard]] LinuxObservationIoStats finalStatistics() const;
  signals:
    void stateChanged(cirrus::infrastructure::ipc::LinuxObservationServiceState state,
                      cirrus::infrastructure::ipc::LinuxEndpointError error);

  private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace cirrus::infrastructure::ipc
Q_DECLARE_METATYPE(cirrus::infrastructure::ipc::LinuxObservationServiceState)
Q_DECLARE_METATYPE(cirrus::infrastructure::ipc::LinuxEndpointError)
