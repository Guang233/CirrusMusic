#include "linux_observation_service.h"
#include <QThread>

namespace cirrus::infrastructure::ipc
{
LinuxObservationServiceConfig LinuxObservationServiceConfig::fromXdg(bool enabled,
                                                                     QString applicationNamespace)
{
    return {enabled, enabled ? qEnvironmentVariable("XDG_RUNTIME_DIR") : QString{},
            applicationNamespace};
}
class LinuxObservationService::Impl
{
  public:
    Impl(LinuxObservationService& o, application::PlaybackObservationPublisher& p)
        : owner(o), publisher(&p), feed(std::make_unique<PlaybackObservationFeed>(p))
    {
    }
    void change(LinuxObservationServiceState next,
                LinuxEndpointError error = LinuxEndpointError::None)
    {
        state = next;
        emit owner.stateChanged(next, error);
    }
    LinuxObservationService& owner;
    QPointer<application::PlaybackObservationPublisher> publisher;
    std::unique_ptr<PlaybackObservationFeed> feed;
    std::shared_ptr<LinuxObservationShutdown> shutdown =
        std::make_shared<LinuxObservationShutdown>();
    LinuxObservationServiceState state = LinuxObservationServiceState::Disabled;
    LinuxObservationIoStats stats;
    QString manifest;
    bool started = false, closed = false;
};
LinuxObservationService::LinuxObservationService(
    application::PlaybackObservationPublisher& publisher, QObject* parent)
    : QObject(parent), m_impl(std::make_unique<Impl>(*this, publisher))
{
    connect(&publisher, &QObject::destroyed, this, &LinuxObservationService::close,
            Qt::DirectConnection);
}
LinuxObservationService::~LinuxObservationService() { close(); }
bool LinuxObservationService::start(const LinuxObservationServiceConfig& config)
{
    if (QThread::currentThread() != thread())
        return false;
    auto& p = *m_impl;
    if (!config.enabled || p.started || p.closed || !p.publisher ||
        p.publisher->thread() != thread())
        return false;
    p.started = true;
    auto* thread = new QThread; // no owner parent: lifetime extends nonblocking owner destruction
    auto* io = new LinuxObservationIo(p.feed->mailbox(), p.feed->wakeTicket());
    connect(p.feed.get(), &PlaybackObservationFeed::wakeRequested, io, &LinuxObservationIo::wake,
            Qt::QueuedConnection);
    // Install the receiver BEFORE the new instance barrier can emit its first wake.
    const auto binding = p.publisher->restartExportInstance();
    if (binding.instanceId.isNull() || p.closed) {
        delete io;
        delete thread;
        p.closed = true;
        p.change(LinuxObservationServiceState::Failed, LinuxEndpointError::InvalidConfig);
        return false;
    }
    io->moveToThread(thread);
    const auto run = p.shutdown;
    run->complete.store(false, std::memory_order_release);
    const LinuxObservationEndpointConfig endpoint{true, config.runtimeRoot,
                                                  config.applicationNamespace, binding.instanceId};
    const QPointer<LinuxObservationService> owner = this;
    io->cancelWhen([run] { return run->canceled.load(std::memory_order_acquire); });
    io->onStopped([thread] { thread->quit(); });
    connect(thread, &QThread::finished, io, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    connect(
        thread, &QThread::finished, thread,
        [run] { run->complete.store(true, std::memory_order_release); }, Qt::DirectConnection);
    // Native setup on I/O only; exactly one status event, no state stream back to owner.
    connect(thread, &QThread::started, io, [io, owner, run, thread, endpoint] {
        if (run->canceled.load(std::memory_order_acquire)) {
            thread->quit();
            return;
        }
        const auto e = io->start(endpoint);
        const auto manifest = io->manifestPath();
        QMetaObject::invokeMethod(
            owner,
            [owner, e, manifest] {
                if (!owner)
                    return;
                auto& p = *owner->m_impl;
                if (p.closed)
                    return;
                if (e != LinuxEndpointError::None) {
                    p.feed->close();
                    p.closed = true;
                    p.change(LinuxObservationServiceState::Failed, e);
                } else {
                    p.manifest = manifest;
                    p.feed->setEnabled(true);
                    p.change(LinuxObservationServiceState::Running);
                }
            },
            Qt::QueuedConnection);
        if (e != LinuxEndpointError::None || run->canceled.load(std::memory_order_acquire)) {
            io->stop();
            thread->quit();
        }
    });
    connect(
        thread, &QThread::finished, io,
        [io, owner] {
            const auto stats = io->statistics();
            QMetaObject::invokeMethod(
                owner,
                [owner, stats] {
                    if (!owner)
                        return;
                    auto& p = *owner->m_impl;
                    p.stats = stats;
                    p.manifest.clear();
                    if (p.feed->enabled())
                        p.feed->setEnabled(false);
                    p.feed->close();
                    p.closed = true;
                    if (p.state != LinuxObservationServiceState::Failed)
                        p.change(LinuxObservationServiceState::Stopped);
                },
                Qt::QueuedConnection);
        },
        Qt::DirectConnection);
    p.change(LinuxObservationServiceState::Starting);
    thread->start();
    return true;
}
void LinuxObservationService::close()
{
    Q_ASSERT(QThread::currentThread() == thread());
    auto& p = *m_impl;
    if (p.closed)
        return;
    p.closed = true;
    // Fence before cancellation: the old I/O thread cannot admit old bytes while
    // its stop callback is pending. No physical playback/owner command is sent.
    if (p.feed->enabled())
        p.feed->setEnabled(false);
    p.feed->close();
    p.shutdown->canceled.store(true, std::memory_order_release);
    if (!p.shutdown->complete.load(std::memory_order_acquire))
        p.change(LinuxObservationServiceState::Stopping);
    else if (p.state != LinuxObservationServiceState::Failed)
        p.change(LinuxObservationServiceState::Stopped);
}
LinuxObservationServiceState LinuxObservationService::state() const { return m_impl->state; }
QString LinuxObservationService::manifestPath() const { return m_impl->manifest; }
std::shared_ptr<LinuxObservationShutdown> LinuxObservationService::shutdownTicket() const
{
    return m_impl->shutdown;
}
LinuxObservationIoStats LinuxObservationService::finalStatistics() const { return m_impl->stats; }
} // namespace cirrus::infrastructure::ipc
