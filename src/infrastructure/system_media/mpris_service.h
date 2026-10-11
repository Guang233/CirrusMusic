#pragma once
#include "application/playback/system_media_commands.h"
#include <QObject>
#include <atomic>
#include <memory>

namespace cirrus::infrastructure::system_media
{
struct MprisServiceConfig {
    bool enabled = false;
    bool allowControls = false;
    bool exportMetadata = false;
    QString runtimeRoot; // Explicit private OS root containing the user's bus socket.
    QString serviceName; // Explicit org.mpris.MediaPlayer2.* identity; never guessed.
    QString identity;    // Explicit public application display name, <=128 UTF16.
};
enum class MprisServiceState { Disabled, Starting, Running, Stopping, Stopped, Failed };
struct MprisShutdown {
    std::atomic<bool> canceled{false}, complete{true};
    std::atomic<unsigned> pendingCommands{0};
};
// Owner-thread composition. D-Bus registration/serialization/cleanup is on an
// independent I/O thread. No second player, queries or authoritative media clock.
// Explicit one-shot start; close synchronously fences exports, never waits.
// Application MUST drain shutdownTicket()->complete before exiting its loop.
class MprisService final : public QObject
{
    Q_OBJECT
  public:
    MprisService(application::PlaybackObservationPublisher& publisher,
                 application::SystemMediaCommands& commands, QObject* parent = nullptr);
    ~MprisService() override;
    bool start(const MprisServiceConfig& config);
    void close();
    [[nodiscard]] MprisServiceState state() const;
    [[nodiscard]] std::shared_ptr<MprisShutdown> shutdownTicket() const;
  signals:
    void stateChanged(cirrus::infrastructure::system_media::MprisServiceState state);

  private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace cirrus::infrastructure::system_media
