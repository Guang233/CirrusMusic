#include "native_media_inbox.h"
#include <QThread>
#include <chrono>
#include <limits>
#include <mutex>

namespace cirrus::infrastructure::system_media
{
using namespace domain;
struct NativeMediaInbox::State {
    std::mutex mutex;
    NativeMediaInbox* receiver = nullptr; // lifetime protected by mutex, never dereferenced by OS
    bool closed = false, armed = false;
    quint64 generation = 0;
    ObservationBinding binding;
    application::SystemMediaCommands::AdmissionRevision revision{}; // PRIVATE; never exported
    PlaybackObservationCapabilities capabilities;
    bool togglePause = false;
    std::optional<qint64> duration;
    unsigned outstanding = 0, requests = 0;
    std::chrono::steady_clock::time_point window{};
};
NativeMediaInbox::NativeMediaInbox(application::PlaybackObservationPublisher& publisher,
                                   application::SystemMediaCommands& commands)
    : m_publisher(&publisher), m_commands(&commands), m_state(std::make_shared<State>())
{
    Q_ASSERT(publisher.thread() == thread() && commands.thread() == thread());
    m_state->receiver = this;
    connect(
        &publisher, &application::PlaybackObservationPublisher::contextRetired, this,
        [this] { retire(); }, Qt::DirectConnection);
    connect(&publisher, &QObject::destroyed, this, [this] { close(); }, Qt::DirectConnection);
    connect(&commands, &QObject::destroyed, this, [this] { close(); }, Qt::DirectConnection);
    connect(&commands, &application::SystemMediaCommands::finished, this, [this](quint64 id, bool) {
        if (m_pending.remove(id))
            release();
    });
}
NativeMediaInbox::~NativeMediaInbox() { close(); }
NativeMediaInbox::Callback NativeMediaInbox::callback() const
{
    const auto state = m_state;
    return [state](NativeMediaRequest request) {
        std::lock_guard lock(state->mutex);
        if (state->closed || !state->armed || !state->receiver ||
            state->outstanding >= unsigned(application::SystemMediaCommands::MaximumPending))
            return false;
        if (request.action == NativeMediaAction::Toggle)
            request.action =
                state->togglePause ? NativeMediaAction::Pause : NativeMediaAction::Play;
        const auto& c = state->capabilities;
        bool permitted = false;
        switch (request.action) {
        case NativeMediaAction::Play:
            permitted = c.play;
            break;
        case NativeMediaAction::Pause:
            permitted = c.pause;
            break;
        case NativeMediaAction::Stop:
            permitted = c.stop;
            break;
        case NativeMediaAction::Next:
            permitted = c.next;
            break;
        case NativeMediaAction::Previous:
            permitted = c.previous;
            break;
        case NativeMediaAction::Seek:
            permitted = c.seek && state->duration && request.positionMs >= 0 &&
                        request.positionMs < *state->duration;
            break;
        case NativeMediaAction::Toggle:
            break;
        }
        if (!permitted)
            return false;
        const auto now = std::chrono::steady_clock::now();
        if (now - state->window >= std::chrono::seconds(1)) {
            state->window = now;
            state->requests = 0;
        }
        if (state->requests >= 32)
            return false;
        ++state->requests;
        ++state->outstanding;
        auto* receiver = state->receiver;
        const auto binding = state->binding;
        const auto revision = state->revision;
        const auto generation = state->generation;
        const bool queued = QMetaObject::invokeMethod(
            receiver,
            [receiver, binding, revision, generation, request] {
                receiver->consume(binding, revision, generation, request);
            },
            Qt::QueuedConnection);
        if (!queued)
            --state->outstanding;
        return queued; // queue admission only; not acceptance, seek completion or playback effect
    };
}
bool NativeMediaInbox::arm(const PlaybackObservation& projection)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!m_publisher || !m_commands || m_publisher->thread() != thread() ||
        m_commands->thread() != thread())
        return false;
    const auto live = m_publisher->validatedExport();
    const auto revision = m_commands->admissionRevision(projection.binding);
    if (!live || !revision || projection.status != ObservationStatus::Ready ||
        live->snapshot.status != ObservationStatus::Ready ||
        live->snapshot.binding != projection.binding ||
        live->snapshot.sequence != projection.sequence)
        return false;
    std::lock_guard lock(m_state->mutex);
    auto& s = *m_state;
    if (s.closed || s.generation == std::numeric_limits<quint64>::max())
        return false;
    if (!s.armed || s.binding != projection.binding || s.revision != *revision)
        ++s.generation;
    s.binding = projection.binding;
    s.revision = *revision;
    s.capabilities = projection.capabilities;
    s.duration = projection.durationMs;
    s.togglePause =
        projection.state == PlaybackState::Playing || projection.intent == TransportIntent::Play;
    s.armed = true;
    return true;
}
void NativeMediaInbox::consume(ObservationBinding binding,
                               application::SystemMediaCommands::AdmissionRevision revision,
                               quint64 generation, NativeMediaRequest request)
{
    Q_ASSERT(QThread::currentThread() == thread());
    bool allowed = false;
    {
        std::lock_guard lock(m_state->mutex);
        allowed = !m_state->closed && m_state->armed && m_state->generation == generation &&
                  m_state->binding == binding;
    }
    quint64 id = 0;
    if (allowed && m_commands && m_publisher) {
        const auto live = m_publisher->validatedExport();
        if (live && live->snapshot.status == ObservationStatus::Ready &&
            live->snapshot.binding == binding) {
            application::SystemMediaCommand command;
            using A = application::SystemMediaAction;
            bool known = true;
            switch (request.action) {
            case NativeMediaAction::Play:
                command.action = A::Play;
                break;
            case NativeMediaAction::Pause:
                command.action = A::Pause;
                break;
            case NativeMediaAction::Stop:
                command.action = A::Stop;
                break;
            case NativeMediaAction::Next:
                command.action = A::Next;
                break;
            case NativeMediaAction::Previous:
                command.action = A::Previous;
                break;
            case NativeMediaAction::Seek:
                command.action = A::Seek;
                command.positionMs = request.positionMs;
                break;
            case NativeMediaAction::Toggle:
                known = false;
                break;
            }
            if (known)
                id = m_commands->submitGuarded(binding, command, revision);
        }
    }
    if (id)
        m_pending.insert(id);
    else
        release();
}
void NativeMediaInbox::release()
{
    std::lock_guard lock(m_state->mutex);
    if (m_state->outstanding)
        --m_state->outstanding;
}
void NativeMediaInbox::retire()
{
    Q_ASSERT(QThread::currentThread() == thread());
    std::lock_guard lock(m_state->mutex);
    m_state->armed = false;
    m_state->binding = {};
    m_state->revision = {};
    m_state->capabilities = {};
    m_state->duration.reset();
    if (m_state->generation != std::numeric_limits<quint64>::max())
        ++m_state->generation;
}
void NativeMediaInbox::close()
{
    Q_ASSERT(QThread::currentThread() == thread());
    std::lock_guard lock(m_state->mutex);
    m_state->closed = true;
    m_state->armed = false;
    m_state->receiver = nullptr;
    m_state->binding = {};
    m_state->revision = {};
    m_state->capabilities = {};
    m_state->duration.reset();
}
} // namespace cirrus::infrastructure::system_media
