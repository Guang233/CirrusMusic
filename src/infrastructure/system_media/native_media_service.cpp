#include "native_media_service.h"
#include "native_media_platform.h"
#include <QCoreApplication>
#include <QThread>
#include <QTimer>
#include <utility>

namespace cirrus::infrastructure::system_media
{
using namespace domain;
class NativeMediaService::Impl : public std::enable_shared_from_this<Impl>
{
  public:
    Impl(NativeMediaService& service, application::PlaybackObservationPublisher& p,
         application::SystemMediaCommands& c)
        : owner(&service), publisher(&p), commands(&c),
          inbox(std::make_shared<NativeMediaInbox>(p, c))
    {
    }
    void change(NativeMediaState value)
    {
        state = value;
        if (owner)
            emit owner->stateChanged(value);
    }
    void schedule()
    {
        if (queued || closed)
            return;
        queued = true;
        const auto self = shared_from_this();
        QTimer::singleShot(0, QCoreApplication::instance(), [self] {
            self->queued = false;
            if (!self->closed)
                self->refresh();
        });
    }
    void barrier()
    {
        inbox->retire(); // synchronous, including reentrant publication and first sample
        if (closed)
            return;
        clearWanted = true;
        if (!busy && platform) {
            busy = true;
            const bool ok = platform->clear();
            busy = false;
            clearWanted = false;
            if (!ok)
                stop(true);
        }
    }
    void refresh()
    {
        if (closed || busy || !platform)
            return;
        busy = true;
        inbox->retire();
        const auto live = publisher ? publisher->validatedExport() : std::nullopt;
        bool ok = true;
        if (!live || live->snapshot.status != ObservationStatus::Ready) {
            ok = platform->clear();
        } else {
            const auto projection = live->snapshot;
            const auto revision =
                commands ? commands->admissionRevision(projection.binding) : std::nullopt;
            const auto self = shared_from_this();
            const auto permit = [self, binding = projection.binding, sequence = projection.sequence,
                                 revision] {
                if (self->closed || self->clearWanted || !self->publisher || !self->commands ||
                    !self->platform || !self->platform->current() || !revision)
                    return false;
                const auto now = self->publisher->validatedExport();
                return now && now->snapshot.status == ObservationStatus::Ready &&
                       now->snapshot.binding == binding && now->snapshot.sequence == sequence &&
                       self->commands->admissionRevision(binding) == revision;
            };
            auto result = NativeProjectionResult::Failed;
            try {
                result = platform->publish(projection, config, permit);
            } catch (...) {
                // A platform/C++ exception must not leave busy set and postpone
                // cleanup forever. Fail closed without exporting native error text.
            }
            if (result == NativeProjectionResult::Failed)
                ok = false;
            else if (result == NativeProjectionResult::Retired || !permit())
                clearWanted = true;
            else if (config.allowControls && !inbox->arm(projection))
                clearWanted = true;
        }
        if (clearWanted && !closed) {
            ok = platform->clear() && ok;
            clearWanted = false;
        }
        busy = false;
        if (!ok)
            stop(true);
    }
    void stop(bool failure)
    {
        failed = failed || failure;
        if (closed)
            return;
        closed = true;
        inbox->close();
        shutdown->canceled.store(true, std::memory_order_release);
        if (active && publisher)
            publisher->retireExports(); // original tickets must not execute after policy/close
        if (!platform) {
            change(failed ? NativeMediaState::Failed : NativeMediaState::Stopped);
            return;
        }
        change(failed ? NativeMediaState::Failed : NativeMediaState::Stopping);
        const auto self = shared_from_this();
        QTimer::singleShot(0, QCoreApplication::instance(), [self] { cleanup(self); });
    }
    static void cleanup(const std::shared_ptr<Impl>& self)
    {
        // A native call can pump a nested main loop. Never revoke/free its port
        // mid-call; keep exactly one cleanup turn pending until it unwinds.
        if (self->busy) {
            QTimer::singleShot(25, QCoreApplication::instance(), [self] { cleanup(self); });
            return;
        }
        const auto port = std::exchange(self->platform, {});
        const bool ok = !port || port->close(); // revoke only our exact native registrations
        self->shutdown->cleanupSucceeded.store(ok, std::memory_order_release);
        self->shutdown->complete.store(true, std::memory_order_release);
        self->change(self->failed || !ok ? NativeMediaState::Failed : NativeMediaState::Stopped);
    }
    QPointer<NativeMediaService> owner;
    QPointer<application::PlaybackObservationPublisher> publisher;
    QPointer<application::SystemMediaCommands> commands;
    std::shared_ptr<NativeMediaInbox> inbox;
    std::shared_ptr<NativeMediaPlatform> platform;
    std::shared_ptr<NativeMediaShutdown> shutdown = std::make_shared<NativeMediaShutdown>();
    NativeMediaConfig config;
    NativeMediaState state{};
    bool started = false, active = false, closed = false, busy = false, queued = false,
         clearWanted = false, failed = false;
};
NativeMediaService::NativeMediaService(application::PlaybackObservationPublisher& publisher,
                                       application::SystemMediaCommands& commands, QObject* parent)
    : QObject(parent), m_impl(std::make_shared<Impl>(*this, publisher, commands))
{
    Q_ASSERT(publisher.thread() == thread() && commands.thread() == thread());
    const std::weak_ptr<Impl> weak = m_impl;
    connect(
        &publisher, &application::PlaybackObservationPublisher::contextRetired, this,
        [weak] {
            if (const auto p = weak.lock())
                p->barrier();
        },
        Qt::DirectConnection);
    connect(
        &publisher, &application::PlaybackObservationPublisher::changed, this,
        [weak] {
            if (const auto p = weak.lock())
                p->schedule();
        },
        Qt::DirectConnection);
    connect(
        &publisher, &QObject::destroyed, this,
        [weak] {
            if (const auto p = weak.lock())
                p->stop(false);
        },
        Qt::DirectConnection);
    connect(
        &commands, &QObject::destroyed, this,
        [weak] {
            if (const auto p = weak.lock())
                p->stop(false);
        },
        Qt::DirectConnection);
}
NativeMediaService::~NativeMediaService()
{
    m_impl->owner = nullptr; // no emission into a destructing observer
    close();
}
bool NativeMediaService::start(NativeMediaConfig config)
{
    if (!config.enabled || QThread::currentThread() != thread())
        return false;
    const auto p = m_impl;
    auto* app = QCoreApplication::instance();
    if (p->started || p->closed || !app || app->thread() != thread() || !p->publisher ||
        !p->commands || p->publisher->thread() != thread() || p->commands->thread() != thread())
        return false;
    p->started = true;
    p->config = config;
#if defined(CIRRUS_NATIVE_SMTC)
    p->platform = createWindowsSmtcPlatform();
#elif defined(CIRRUS_NATIVE_NOW_PLAYING)
    p->platform = createMacNowPlayingPlatform();
#else
    p->closed = true;
    p->inbox->close();
    p->change(NativeMediaState::Unsupported);
    return false; // build opt-in/toolchain required; no fallback
#endif
    if (!p->platform) {
        p->stop(true);
        return false;
    }
    p->shutdown->complete.store(false, std::memory_order_release);
    p->shutdown->cleanupSucceeded.store(false, std::memory_order_release);
    p->busy = true;
    const std::weak_ptr<Impl> weak = p;
    bool started = false;
    try {
        started = p->platform->start(config, p->inbox->callback(), [weak] {
            if (const auto state = weak.lock())
                state->stop(true); // cleanup queued, not reentrant inside native dispatch
        });
    } catch (...) {
        // Close partial registrations on the main loop after the native call unwinds.
    }
    p->busy = false;
    if (!started || p->closed) {
        p->stop(!started);
        return false;
    }
    p->active = true;
    p->change(NativeMediaState::Running);
    p->schedule();
    return !p->closed;
}
void NativeMediaService::close()
{
    Q_ASSERT(QThread::currentThread() == thread());
    const auto p = m_impl;
    p->stop(false);
}
NativeMediaState NativeMediaService::state() const { return m_impl->state; }
std::shared_ptr<NativeMediaShutdown> NativeMediaService::shutdownTicket() const
{
    return m_impl->shutdown;
}
} // namespace cirrus::infrastructure::system_media
