#pragma once
#include "playback_observation_publisher.h"
#include <QHash>
#include <optional>
#include <utility>

namespace cirrus::application
{
enum class SystemMediaAction { Play, Pause, Stop, Next, Previous, Seek, Volume, Modes };
struct SystemMediaCommand {
    SystemMediaAction action{};
    qint64 positionMs = 0;
    float volume = 0;
    domain::QueueOrder order{};
    domain::QueueRepeat repeat{};
};
// Core admission only. Native callbacks must marshal here; IPC v1 never exposes this port.
class SystemMediaCommands final : public QObject
{
    Q_OBJECT
  public:
    SystemMediaCommands(ListeningSession& owner, PlaybackObservationPublisher& publisher,
                        QObject* parent = nullptr);
    ~SystemMediaCommands() override { close(); }
    quint64 submit(domain::ObservationBinding binding, SystemMediaCommand command);
    // Private in-process admission stamp. Never place these revisions on native
    // metadata, IPC or logs. Marshaled native callbacks cannot adopt a newer intent.
    using AdmissionRevision = std::pair<quint64, quint64>;
    [[nodiscard]] std::optional<AdmissionRevision>
    admissionRevision(domain::ObservationBinding binding) const;
    quint64 submitGuarded(domain::ObservationBinding binding, SystemMediaCommand command,
                          AdmissionRevision expected);
    void close();
    static constexpr int MaximumPending = 8;
  signals:
    void finished(quint64 id, bool accepted); // admission/execution acceptance, NOT effect proof
  private:
    QPointer<ListeningSession> m_owner;
    QPointer<PlaybackObservationPublisher> m_publisher;
    QHash<quint64, std::pair<quint64, quint64>> m_pending;
    quint64 m_next = 0;
    bool m_closed = false;
};
} // namespace cirrus::application
