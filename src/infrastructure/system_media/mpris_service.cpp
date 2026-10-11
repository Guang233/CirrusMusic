#include "mpris_service.h"
#include "mpris_worker.h"
#include <QSet>
#include <QThread>

namespace cirrus::infrastructure::system_media
{
class MprisService::Impl
{
  public:
    Impl(MprisService& o, application::PlaybackObservationPublisher& p,
         application::SystemMediaCommands& c)
        : owner(o), publisher(&p), commands(&c),
          feed(std::make_unique<ipc::PlaybackObservationFeed>(p))
    {
    }
    void change(MprisServiceState value)
    {
        state = value;
        emit owner.stateChanged(value);
    }
    MprisService& owner;
    QPointer<application::PlaybackObservationPublisher> publisher;
    QPointer<application::SystemMediaCommands> commands;
    std::unique_ptr<ipc::PlaybackObservationFeed> feed;
    std::shared_ptr<MprisShutdown> shutdown = std::make_shared<MprisShutdown>();
    QSet<quint64> pending;
    MprisServiceState state{};
    bool started = false, closed = false;
};
MprisService::MprisService(application::PlaybackObservationPublisher& publisher,
                           application::SystemMediaCommands& commands, QObject* parent)
    : QObject(parent), m_impl(std::make_unique<Impl>(*this, publisher, commands))
{
    Q_ASSERT(publisher.thread() == thread() && commands.thread() == thread());
    connect(&publisher, &QObject::destroyed, this, &MprisService::close, Qt::DirectConnection);
    connect(&commands, &QObject::destroyed, this, &MprisService::close, Qt::DirectConnection);
    connect(&commands, &application::SystemMediaCommands::finished, this, [this](quint64 id, bool) {
        auto& p = *m_impl;
        if (p.pending.remove(id))
            p.shutdown->pendingCommands.fetch_sub(1, std::memory_order_acq_rel);
    });
}
MprisService::~MprisService() { close(); }
bool MprisService::start(const MprisServiceConfig& config)
{
    if (QThread::currentThread() != thread())
        return false;
    auto& p = *m_impl;
    if (!config.enabled || p.started || p.closed || !p.publisher || !p.commands ||
        p.publisher->thread() != thread() || p.commands->thread() != thread())
        return false;
    p.started = true;
    auto* thread = new QThread; // independent lifetime; never wait in owner destructor
    auto* io = new MprisWorker(p.feed->mailbox(), p.feed->wakeTicket(), p.shutdown);
    // Receiver installed before enabling any source publication.
    connect(p.feed.get(), &ipc::PlaybackObservationFeed::wakeRequested, io, &MprisWorker::wake,
            Qt::QueuedConnection);
    connect(
        io, &MprisWorker::commandRequested, this,
        [this](domain::ObservationBinding binding, quint64 fence,
               application::SystemMediaCommand intent) {
            auto& p = *m_impl;
            quint64 id = 0;
            if (!p.closed && !p.shutdown->canceled.load(std::memory_order_acquire) && p.publisher &&
                p.commands && p.feed->mailbox()->fence() == fence) {
                const auto live = p.publisher->validatedExport();
                // Historical D-Bus state/binding is NOT private provenance. Only a
                // current coherent owner factory can authorize the original bridge.
                if (live && live->snapshot.status == domain::ObservationStatus::Ready &&
                    live->snapshot.binding == binding)
                    id = p.commands->submit(binding, intent);
            }
            if (id)
                p.pending.insert(id);
            else
                p.shutdown->pendingCommands.fetch_sub(1, std::memory_order_acq_rel);
        },
        Qt::QueuedConnection);
    connect(
        io, &MprisWorker::initialized, this,
        [this](bool success) {
            auto& p = *m_impl;
            if (p.closed)
                return;
            if (!success || p.shutdown->canceled.load(std::memory_order_acquire)) {
                p.closed = true;
                p.feed->close();
                p.change(MprisServiceState::Failed);
                return;
            }
            if (!p.publisher || !p.feed->setEnabled(true)) {
                p.closed = true;
                p.feed->close();
                p.shutdown->canceled.store(true, std::memory_order_release);
                p.change(MprisServiceState::Failed);
                return;
            }
            p.change(MprisServiceState::Running);
        },
        Qt::QueuedConnection);
    connect(io, &MprisWorker::stopped, thread, &QThread::quit, Qt::DirectConnection);
    connect(
        io, &MprisWorker::stopped, this,
        [this](bool failed) {
            auto& p = *m_impl;
            if (p.feed->enabled())
                p.feed->setEnabled(false);
            p.feed->close();
            p.closed = true;
            p.shutdown->canceled.store(true, std::memory_order_release);
            if (failed || p.state == MprisServiceState::Failed)
                p.change(MprisServiceState::Failed);
            else
                p.change(MprisServiceState::Stopped);
        },
        Qt::QueuedConnection);
    const auto shutdown = p.shutdown;
    shutdown->complete.store(false, std::memory_order_release);
    io->moveToThread(thread);
    connect(thread, &QThread::started, io, [io, config] { io->start(config); });
    connect(thread, &QThread::finished, io, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    connect(
        thread, &QThread::finished, thread,
        [shutdown] { shutdown->complete.store(true, std::memory_order_release); },
        Qt::DirectConnection);
    p.change(MprisServiceState::Starting);
    thread->start();
    return true;
}
void MprisService::close()
{
    Q_ASSERT(QThread::currentThread() == thread());
    auto& p = *m_impl;
    if (p.closed)
        return;
    p.closed = true;
    if (p.feed->enabled())
        p.feed->setEnabled(false);
    p.feed->close();
    p.shutdown->canceled.store(true, std::memory_order_release);
    p.change(p.shutdown->complete.load(std::memory_order_acquire) ? MprisServiceState::Stopped
                                                                  : MprisServiceState::Stopping);
}
MprisServiceState MprisService::state() const { return m_impl->state; }
std::shared_ptr<MprisShutdown> MprisService::shutdownTicket() const { return m_impl->shutdown; }
} // namespace cirrus::infrastructure::system_media
