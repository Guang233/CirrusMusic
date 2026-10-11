#include "system_media_commands.h"
#include <QThread>
#include <cmath>
#include <limits>
#include <utility>

namespace cirrus::application
{
using namespace domain;
SystemMediaCommands::SystemMediaCommands(ListeningSession& owner,
                                         PlaybackObservationPublisher& publisher, QObject* parent)
    : QObject(parent), m_owner(&owner), m_publisher(&publisher)
{
    Q_ASSERT(owner.thread() == thread() && publisher.thread() == thread());
    connect(&owner, &QObject::destroyed, this, &SystemMediaCommands::close);
    connect(&publisher, &QObject::destroyed, this, &SystemMediaCommands::close);
}
std::optional<SystemMediaCommands::AdmissionRevision>
SystemMediaCommands::admissionRevision(ObservationBinding binding) const
{
    if (QThread::currentThread() != thread() || m_closed || !m_owner || !m_publisher)
        return {};
    const auto live = m_publisher->validatedExport();
    if (!live || live->snapshot.status != ObservationStatus::Ready ||
        live->snapshot.binding != binding)
        return {};
    return m_owner->intentRevision();
}
quint64 SystemMediaCommands::submitGuarded(ObservationBinding binding, SystemMediaCommand command,
                                           AdmissionRevision expected)
{
    const auto live = admissionRevision(binding);
    if (!live || *live != expected)
        return 0;
    return submit(binding, command); // no event-loop yield before original ticket admission
}
quint64 SystemMediaCommands::submit(ObservationBinding binding, SystemMediaCommand command)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_closed || !m_owner || !m_publisher || m_pending.size() >= MaximumPending ||
        m_next == std::numeric_limits<quint64>::max() ||
        m_owner->intentRevision().second == std::numeric_limits<quint64>::max())
        return 0;
    const auto s = m_publisher->snapshot();
    if (s.status != ObservationStatus::Ready || binding != s.binding)
        return 0;
    using A = SystemMediaAction;
    bool valid = false;
    switch (command.action) {
    case A::Play:
        valid = s.capabilities.play;
        break;
    case A::Pause:
        valid = s.capabilities.pause;
        break;
    case A::Stop:
        valid = s.capabilities.stop;
        break;
    case A::Next:
        valid = s.capabilities.next;
        break;
    case A::Previous:
        valid = s.capabilities.previous;
        break;
    case A::Seek:
        valid = s.capabilities.seek && command.positionMs >= 0 && s.durationMs &&
                command.positionMs < *s.durationMs;
        break;
    case A::Volume:
        valid = s.capabilities.volume && std::isfinite(command.volume) && command.volume >= 0 &&
                command.volume <= 1;
        break;
    case A::Modes:
        valid = s.capabilities.modes &&
                (command.order == QueueOrder::Sequential || command.order == QueueOrder::Shuffle) &&
                (command.repeat == QueueRepeat::Off || command.repeat == QueueRepeat::All ||
                 command.repeat == QueueRepeat::One);
        break;
    }
    if (!valid)
        return 0;
    const auto id = ++m_next;
    const auto revision = m_owner->intentRevision();
    m_pending.insert(id, revision);
    QTimer::singleShot(0, this, [this, id, binding, command] {
        if (!m_pending.contains(id))
            return;
        const auto revision = m_pending.take(id);
        bool accepted = false;
        if (!m_closed && m_owner && m_publisher && m_owner->intentRevision() == revision &&
            m_publisher->snapshot().status == ObservationStatus::Ready &&
            m_publisher->snapshot().binding == binding) {
            const auto current = m_publisher->snapshot();
            const auto& c = current.capabilities;
            switch (command.action) {
            case A::Play:
                accepted = c.play && m_owner->resume();
                break;
            case A::Pause:
                accepted = c.pause && m_owner->pause();
                break;
            case A::Stop:
                accepted = c.stop && m_owner->stop();
                break;
            case A::Next:
                accepted = c.next && m_owner->next();
                break;
            case A::Previous:
                accepted = c.previous && m_owner->previous();
                break;
            case A::Seek:
                accepted = c.seek && current.durationMs &&
                           command.positionMs < *current.durationMs &&
                           m_owner->seek(command.positionMs);
                break;
            case A::Volume:
                accepted = c.volume && m_owner->setVolume(command.volume);
                break;
            case A::Modes:
                accepted = c.modes && m_owner->setModes(command.order, command.repeat);
                break;
            }
        }
        if (accepted && m_owner) {
            const auto after = m_owner->intentRevision();
            if (after.first == revision.first && after.second == revision.second + 1) {
                // Our own scalar commands remain FIFO; intervening/reentrant external
                // intents don't retarget outstanding tickets to a new private revision.
                for (auto& pending : m_pending)
                    if (pending == revision)
                        pending = after;
            }
        }
        emit finished(id, accepted);
    });
    return id;
}
void SystemMediaCommands::close()
{
    if (m_closed)
        return;
    m_closed = true;
    const auto pending = std::exchange(m_pending, {});
    for (auto id : pending.keys())
        emit finished(id, false); // no effect rollback claim
}
} // namespace cirrus::application
