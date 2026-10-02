#include "playback_coordinator.h"
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <utility>

namespace cirrus::application
{
using namespace domain;
PlaybackCoordinator::PlaybackCoordinator(SessionCoordinator& session, IStreamResolver& resolver,
                                         IPlaybackBackend& backend, QObject* parent)
    : QObject(parent), m_session(session), m_resolver(resolver), m_backend(backend)
{
    Q_ASSERT(session.thread() == thread() && resolver.thread() == thread() &&
             backend.thread() == thread());
    m_deadline.setSingleShot(true);
    connect(&m_deadline, &QTimer::timeout, this, [this] {
        if (current(m_snapshot.token) && !m_ready)
            fail({PlaybackError::Timeout});
    });
    connect(&session, &SessionCoordinator::changed, this, [this](SessionSnapshot value) {
        if (m_snapshot.token.session != value.token)
            stop();
    });
    connect(&resolver, &IStreamResolver::completed, this,
            [this](quint64 id, SessionToken context, StreamResult result) {
                if (id != m_request || !m_request)
                    return;
                const auto token = m_snapshot.token;
                m_request = 0;
                if (!current(token) || context != token.session) {
                    fail({PlaybackError::StaleContext});
                    return;
                }
                if (const auto* failure = std::get_if<PlaybackFailure>(&result)) {
                    fail(*failure);
                    return;
                }
                auto source = std::get<StreamSource>(std::move(result));
                if (source.song != m_snapshot.song) {
                    fail({PlaybackError::InvalidResponse});
                    return;
                }
                if (source.validUntil.isForever() || source.validUntil.hasExpired()) {
                    fail({PlaybackError::Expired});
                    return;
                }
                m_snapshot.actualQuality = source.actualQuality;
                m_snapshot.preview = source.preview;
                m_snapshot.state = PlaybackState::Loading;
                emit changed(m_snapshot);
                if (current(token))
                    m_backend.load(std::move(source), token);
            });
    connect(&backend, &IPlaybackBackend::event, this, &PlaybackCoordinator::receive);
}
PlaybackCoordinator::~PlaybackCoordinator()
{
    disconnect(&m_backend, nullptr, this, nullptr);
    disconnect(&m_resolver, nullptr, this, nullptr);
    retire();
}
bool PlaybackCoordinator::current(LoadToken token) const
{
    return token == m_snapshot.token && token.generation && m_session.accepts(token.session) &&
           m_snapshot.state != PlaybackState::Empty && m_snapshot.state != PlaybackState::Failed &&
           m_snapshot.state != PlaybackState::Ended;
}
void PlaybackCoordinator::retire()
{
    m_ready = false;
    m_deadline.stop();
    m_seek.reset();
    const auto id = std::exchange(m_request, 0);
    if (id)
        m_resolver.cancel(id);
    m_backend.stop();
}
void PlaybackCoordinator::stop(LoadToken token)
{
    m_snapshot = {};
    m_snapshot.token = token;
    m_lastGeneration = qMax(m_lastGeneration, token.generation);
    retire();
    emit changed(m_snapshot);
}
bool PlaybackCoordinator::start(LoadToken token, StreamQuery query, TransportIntent intent)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const auto session = m_session.snapshot();
    if ((intent != TransportIntent::Play && intent != TransportIntent::Pause) ||
        !m_session.accepts(token.session) || token.generation <= m_lastGeneration ||
        !validMusicId(query.song.value) || qualityName(query.quality).isEmpty() ||
        (session.phase != SessionPhase::Guest && session.phase != SessionPhase::Authenticated))
        return false;
    m_lastGeneration = token.generation;
    // Invalidate prior state BEFORE cancel/stop (adapters may synchronously emit on abort).
    m_snapshot = {};
    retire();
    m_snapshot.token = token;
    m_snapshot.song = query.song;
    m_snapshot.state = PlaybackState::Resolving;
    m_snapshot.intent = intent;
    emit changed(m_snapshot);
    if (!current(token))
        return true;
    m_deadline.start(75000);
    m_request = m_resolver.resolve(token.session, std::move(query));
    if (!m_request)
        fail({PlaybackError::Unavailable});
    return true;
}
void PlaybackCoordinator::fail(PlaybackFailure failure)
{
    m_snapshot.state = PlaybackState::Failed;
    m_snapshot.failure = failure;
    m_snapshot.seekable = false;
    retire();
    emit changed(m_snapshot);
}
bool PlaybackCoordinator::pause()
{
    if (!current(m_snapshot.token))
        return false;
    m_snapshot.intent = TransportIntent::Pause;
    if (m_ready)
        m_snapshot.state = PlaybackState::Paused;
    const auto token = m_snapshot.token;
    emit changed(m_snapshot);
    if (current(token))
        m_backend.pause();
    return true;
}
bool PlaybackCoordinator::resume()
{
    if (!current(m_snapshot.token))
        return false;
    m_snapshot.intent = TransportIntent::Play;
    const auto token = m_snapshot.token;
    emit changed(m_snapshot);
    if (current(token))
        applyIntent();
    return true;
}
bool PlaybackCoordinator::seek(qint64 positionMs)
{
    if (!current(m_snapshot.token) || positionMs < 0 || positionMs > 7LL * 24 * 60 * 60 * 1000)
        return false;
    m_seek = positionMs; // coalesce while resolving/loading/not seekable
    applyIntent();
    return true;
}
void PlaybackCoordinator::applyIntent()
{
    if (!m_ready)
        return;
    const auto token = m_snapshot.token;
    if (m_seek && m_snapshot.seekable && m_snapshot.durationMs) {
        const auto position =
            std::clamp<qint64>(*m_seek, 0, qMax<qint64>(0, *m_snapshot.durationMs - 1));
        m_seek.reset();
        m_backend.seek(position);
        if (!current(token))
            return;
    }
    if (m_snapshot.intent == TransportIntent::Play)
        m_backend.play();
    else
        m_backend.pause();
}
void PlaybackCoordinator::setVolume(float linear) { m_backend.setVolume(linear); }
void PlaybackCoordinator::receive(PlaybackEvent event)
{
    if (!current(event.token))
        return;
    using Kind = PlaybackEvent::Kind;
    switch (event.kind) {
    case Kind::Failure:
        fail(event.failure);
        return;
    case Kind::Ready:
        if (m_ready)
            return;
        m_ready = true;
        m_deadline.stop();
        m_snapshot.state = m_snapshot.intent == TransportIntent::Play ? PlaybackState::Ready
                                                                      : PlaybackState::Paused;
        break;
    case Kind::State:
        if (!m_ready)
            return;
        if (event.state == PlaybackState::Playing && m_snapshot.intent == TransportIntent::Play)
            m_snapshot.state = event.state;
        else if (event.state == PlaybackState::Paused &&
                 m_snapshot.intent == TransportIntent::Pause)
            m_snapshot.state = event.state;
        else
            return;
        break;
    case Kind::Position:
        m_snapshot.positionMs = qMax<qint64>(0, event.value);
        if (m_snapshot.durationMs)
            m_snapshot.positionMs = qMin(m_snapshot.positionMs, *m_snapshot.durationMs);
        break;
    case Kind::Duration:
        if (event.value <= 0 || event.value > 7LL * 24 * 60 * 60 * 1000)
            return;
        m_snapshot.durationMs = event.value;
        if (m_snapshot.preview)
            m_snapshot.durationMs =
                qMin(event.value, m_snapshot.preview->endMs - m_snapshot.preview->startMs);
        break;
    case Kind::Seekable:
        m_snapshot.seekable = event.value != 0;
        break;
    case Kind::Ended:
        m_snapshot.state = PlaybackState::Ended;
        m_snapshot.seekable = false;
        retire(); // releases temporary media, repeated Ended is ignored
        break;
    }
    emit changed(m_snapshot);
    if (current(event.token) &&
        (event.kind == Kind::Ready || event.kind == Kind::Seekable || event.kind == Kind::Duration))
        applyIntent();
}
} // namespace cirrus::application
