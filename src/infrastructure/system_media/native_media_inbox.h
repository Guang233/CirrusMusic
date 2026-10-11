#pragma once
#include "application/playback/system_media_commands.h"
#include <QSet>
#include <functional>
#include <memory>

namespace cirrus::infrastructure::system_media
{
enum class NativeMediaAction { Play, Pause, Toggle, Stop, Next, Previous, Seek };
struct NativeMediaRequest {
    NativeMediaAction action{};
    qint64 positionMs = 0;
};
// OS callbacks capture only callback(), NOT a QObject/raw adapter pointer. At
// most eight scalar callbacks + admitted original bridge tickets are outstanding.
// The mutex pins the invokeMethod receiver until enqueue finishes; destruction
// closes this gate before QObject removes its queued calls. No native I/O here.
class NativeMediaInbox final : public QObject
{
  public:
    using Callback = std::function<bool(NativeMediaRequest)>;
    NativeMediaInbox(application::PlaybackObservationPublisher& publisher,
                     application::SystemMediaCommands& commands);
    ~NativeMediaInbox() override;
    [[nodiscard]] Callback callback() const;
    bool arm(const domain::PlaybackObservation& projection);
    void retire();
    void close();

  private:
    struct State;
    void consume(domain::ObservationBinding binding,
                 application::SystemMediaCommands::AdmissionRevision revision, quint64 generation,
                 NativeMediaRequest request);
    void release();
    QPointer<application::PlaybackObservationPublisher> m_publisher;
    QPointer<application::SystemMediaCommands> m_commands;
    std::shared_ptr<State> m_state;
    QSet<quint64> m_pending;
};
} // namespace cirrus::infrastructure::system_media
