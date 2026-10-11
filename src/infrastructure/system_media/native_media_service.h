#pragma once
#include "application/playback/system_media_commands.h"
#include <atomic>
#include <memory>

namespace cirrus::infrastructure::system_media
{
struct NativeMediaConfig {
    bool enabled = false;
    bool allowControls = false;
    bool exportMetadata = false;
    qintptr ownedTopLevelWindow = 0; // Windows: real owned GUI HWND; macOS: must be zero
};
enum class NativeMediaState { Disabled, Running, Stopping, Stopped, Failed, Unsupported };
struct NativeMediaShutdown {
    std::atomic<bool> canceled{false}, complete{true}, cleanupSucceeded{true};
};
// Windows SMTC / macOS Now Playing, opt-in and platform-build gated. Must live
// with publisher/commands on the application's GUI/main thread and apartment.
// No second player/clock, hidden window or queries. Native API registration and
// projection obey platform UI affinity; socket/pipe services remain on their I/O thread.
// One-shot start. close immediately retires private gates/exports and queues
// native cleanup on the main event loop, never waits. Drain shutdownTicket before
// exiting that loop; complete alone is not a claim that native cleanup succeeded.
class NativeMediaService final : public QObject
{
    Q_OBJECT
  public:
    NativeMediaService(application::PlaybackObservationPublisher& publisher,
                       application::SystemMediaCommands& commands, QObject* parent = nullptr);
    ~NativeMediaService() override;
    bool start(NativeMediaConfig config);
    void close();
    [[nodiscard]] NativeMediaState state() const;
    [[nodiscard]] std::shared_ptr<NativeMediaShutdown> shutdownTicket() const;
  signals:
    void stateChanged(cirrus::infrastructure::system_media::NativeMediaState state);

  private:
    class Impl;
    std::shared_ptr<Impl> m_impl;
};
} // namespace cirrus::infrastructure::system_media
