#pragma once
#include "linux_observation_endpoint.h"
#include "playback_observation_feed.h"
#include <QObject>
#include <functional>
#include <memory>

namespace cirrus::infrastructure::ipc
{
struct LinuxObservationIoStats {
    quint64 turns = 0, wakes = 0, receivedBytes = 0, admittedBytes = 0;
    qsizetype maximumInput = 0, maximumOutput = 0, maximumTotalOutput = 0;
    int maximumClients = 0, maximumAcceptsPerTurn = 0, currentClients = 0;
};
// Single I/O-thread worker, not a product entry. Construction has no filesystem
// effect; start/stop/destruction after start must run on its I/O thread. All receive
// storage is 1KiB/client + portable partial frame + 1KiB reserved/pending typed id.
// Native send debt conservatively charges ALL accepted bytes until kernel OUTQ=0;
// partial kernel drain gives progress, never frees an unproven byte reservation.
class LinuxObservationIo final : public QObject
{
  public:
    LinuxObservationIo(std::shared_ptr<ObservationMailbox> source,
                       std::shared_ptr<ObservationWakeTicket> wake, QObject* parent = nullptr);
    ~LinuxObservationIo() override;
    LinuxEndpointError start(const LinuxObservationEndpointConfig& config);
    void wake(); // ONLY queued wake callback: ack at entry, then bounded scheduling
    void stop();
    void onStopped(std::function<void()> callback);   // one terminal I/O-thread callback
    void cancelWhen(std::function<bool()> predicate); // thread-safe state, never owner getters
    [[nodiscard]] bool running() const;
    [[nodiscard]] QString manifestPath() const;
    [[nodiscard]] LinuxObservationIoStats statistics() const;

  private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace cirrus::infrastructure::ipc
