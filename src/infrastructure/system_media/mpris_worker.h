#pragma once
#include "infrastructure/ipc/playback_observation_feed.h"
#include "mpris_service.h"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusVirtualObject>
#include <QElapsedTimer>
#include <QTimer>

namespace cirrus::infrastructure::system_media
{
// Private single I/O consumer; no owner getters or commands on this thread.
class MprisWorker final : public QDBusVirtualObject
{
    Q_OBJECT
  public:
    MprisWorker(std::shared_ptr<ipc::ObservationMailbox> source,
                std::shared_ptr<ipc::ObservationWakeTicket> wake,
                std::shared_ptr<MprisShutdown> shutdown);
    ~MprisWorker() override;
    bool start(MprisServiceConfig config);
    void stop();
    void wake();
    QString introspect(const QString& path) const override;
    bool handleMessage(const QDBusMessage&, const QDBusConnection&) override;
  signals:
    void initialized(bool success);
    void stopped(bool failed);
    void commandRequested(cirrus::domain::ObservationBinding binding, quint64 fence,
                          cirrus::application::SystemMediaCommand command);

  private:
    class SessionBusGuard;
    std::unique_ptr<SessionBusGuard> m_guard;
    std::optional<ipc::ObservationMailbox::Value> current() const;
    QVariantMap properties(const QString& interface) const;
    bool enqueue(application::SystemMediaCommand command);
    bool setProperty(const QDBusMessage&, const QDBusConnection&, const QString&, const QVariant&);
    void refresh();
    void announce();
    void tick();
    bool reply(const QDBusMessage&, const QDBusConnection&, const QVariantList& args = {});
    bool error(const QDBusMessage&, const QDBusConnection&, const QString& name);
    bool budget();
    std::shared_ptr<ipc::ObservationMailbox> m_source;
    std::shared_ptr<ipc::ObservationWakeTicket> m_wake;
    std::shared_ptr<MprisShutdown> m_shutdown;
    std::optional<ipc::ObservationMailbox::Value> m_latest;
    std::unique_ptr<QDBusConnection> m_bus;
    QPointer<QDBusPendingCallWatcher> m_ack;
    MprisServiceConfig m_config;
    QVariantMap m_sent;
    domain::ObservationBinding m_sentBinding;
    quint64 m_sentSeekRevision = 0;
    QString m_connectionName;
    QElapsedTimer m_clock;
    QTimer* m_timer = nullptr;
    qint64 m_window = 0, m_ackStarted = 0;
    unsigned m_requests = 0;
    bool m_registered = false, m_object = false, m_closed = false, m_dirty = true, m_failed = false;
};
} // namespace cirrus::infrastructure::system_media
Q_DECLARE_METATYPE(cirrus::domain::ObservationBinding)
Q_DECLARE_METATYPE(cirrus::application::SystemMediaCommand)
