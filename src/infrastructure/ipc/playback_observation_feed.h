#pragma once
#include "application/playback/playback_observation_publisher.h"
#include "playback_observation_connection.h"
#include <QPointer>
#include <atomic>

namespace cirrus::infrastructure::ipc
{
// One queued wake (+ possibly its executing callback), not one queued value/update.
// I/O acknowledges only when the wake callback STARTS, before pump/take; periodic
// pump/take must NOT acknowledge a notification that is still queued.
class ObservationWakeTicket final
{
  public:
    bool request() { return !m_pending.exchange(true, std::memory_order_acq_rel); }
    void acknowledge() { m_pending.store(false, std::memory_order_release); }
    [[nodiscard]] bool pending() const { return m_pending.load(std::memory_order_acquire); }

  private:
    std::atomic<bool> m_pending{false};
};
// Owner-thread producer composition only. No sockets, JSON, encoder, filesystem,
// timer, command/query or I/O consumer. Install wake receiver before enabling.
// Sole producer of its fresh mailbox; consumers must never publish/beginView.
class PlaybackObservationFeed final : public QObject
{
    Q_OBJECT
  public:
    explicit PlaybackObservationFeed(application::PlaybackObservationPublisher& publisher,
                                     QObject* parent = nullptr);
    ~PlaybackObservationFeed() override { close(); }
    bool setEnabled(bool enabled); // default off; false retires the common export view
    void close();                  // one-shot source retirement, no owner stop/signout
    [[nodiscard]] bool enabled() const { return m_enabled && !m_closed; }
    [[nodiscard]] std::shared_ptr<ObservationMailbox> mailbox() const { return m_mailbox; }
    [[nodiscard]] std::shared_ptr<ObservationWakeTicket> wakeTicket() const { return m_wake; }
  signals:
    void wakeRequested(); // queue this scalar wake, never a historical snapshot argument
  private:
    void refreshFromOwner();
    void retire(domain::ObservationBinding previous);
    void requestWake();
    QPointer<application::PlaybackObservationPublisher> m_publisher;
    std::shared_ptr<ObservationMailbox> m_mailbox = std::make_shared<ObservationMailbox>();
    std::shared_ptr<ObservationWakeTicket> m_wake = std::make_shared<ObservationWakeTicket>();
    std::optional<domain::ObservationBinding> m_view;
    std::shared_ptr<const domain::PlaybackLyricDocument> m_document;
    QMetaObject::Connection m_sample, m_barrier, m_destroyed;
    bool m_enabled = false, m_closed = false;
};
} // namespace cirrus::infrastructure::ipc
