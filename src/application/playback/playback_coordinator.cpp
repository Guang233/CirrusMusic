#include "playback_coordinator.h"
#include "logging/log.h"
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <variant>

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
    connect(&resolver, &IStreamResolver::completed, this, &PlaybackCoordinator::resolved);
    connect(&backend, &IPlaybackBackend::event, this, &PlaybackCoordinator::receive);
}
PlaybackCoordinator::~PlaybackCoordinator()
{
    disconnect(&m_backend, nullptr, this, nullptr);
    disconnect(&m_resolver, nullptr, this, nullptr);
    retire();
}
namespace
{
// The source when `result` is a usable address for `song`, else why not.
std::variant<StreamSource, PlaybackFailure> usable(StreamResult result, const SongId& song)
{
    if (const auto* failure = std::get_if<PlaybackFailure>(&result))
        return *failure;
    auto source = std::get<StreamSource>(std::move(result));
    if (source.song != song)
        return PlaybackFailure{PlaybackError::InvalidResponse};
    if (source.validUntil.isForever() || source.validUntil.hasExpired())
        return PlaybackFailure{PlaybackError::Expired};
    return source;
}
} // namespace

void PlaybackCoordinator::resolved(quint64 id, SessionToken context, StreamResult result)
{
    if (!id)
        return;
    if (m_next && id == m_next->request) {
        m_next->request = 0;
        if (context != m_next->token.session || !current(m_snapshot.token))
            dropNext();
        else
            preloaded(std::move(result));
        return;
    }
    for (qsizetype i = 0; i < m_refreshes.size(); ++i) {
        if (m_refreshes[i].request != id)
            continue;
        const auto refresh = m_refreshes.takeAt(i);
        if (context != refresh.token.session || !current(m_snapshot.token))
            return;
        const bool forCurrent = refresh.token == m_snapshot.token;
        const bool forNext = m_next && m_next->loaded && refresh.token == m_next->token;
        if (!forCurrent && !forNext)
            return;
        auto source = usable(std::move(result), refresh.query.song);
        if (const auto* failure = std::get_if<PlaybackFailure>(&source))
            qCWarning(lcPlayback) << log::event("load.refresh_failed")
                                  << log::field("token", refresh.token.generation)
                                  << log::field("song", refresh.query.song.value)
                                  << log::field("err", failure->error)
                                  << log::field("code", failure->providerCode.value_or(0));
        else
            qCInfo(lcPlayback) << log::event("load.refreshed")
                               << log::field("token", refresh.token.generation);
        if (auto* fresh = std::get_if<StreamSource>(&source)) {
            MediaSource physical = std::move(*fresh);
            m_backend.refresh(refresh.token, std::move(physical));
        }
        return; // no address: the backend fails the load as Expired in its own time
    }
    if (id != m_request)
        return;
    const auto token = m_snapshot.token;
    m_request = 0;
    if (!current(token) || context != token.session) {
        fail({PlaybackError::StaleContext});
        return;
    }
    auto checked = usable(std::move(result), m_snapshot.song);
    if (const auto* failure = std::get_if<PlaybackFailure>(&checked)) {
        fail(*failure);
        return;
    }
    auto& source = std::get<StreamSource>(checked);
    qCDebug(lcPlayback) << log::event("load.resolved") << log::field("token", token.generation)
                        << log::field("song", m_snapshot.song.value)
                        << log::field("quality", source.actualQuality)
                        << log::field("preview", source.preview.has_value())
                        << log::field("bytes", source.size)
                        << log::field("ms", m_loadClock.elapsed());
    m_snapshot.actualQuality = source.actualQuality;
    m_snapshot.preview = source.preview;
    m_snapshot.state = PlaybackState::Loading;
    emit changed(m_snapshot);
    if (current(token)) {
        MediaSource physical = std::move(source); // retire the song envelope here
        m_backend.load(std::move(physical), token);
    }
}

bool PlaybackCoordinator::preload(LoadToken currentToken, LoadToken next,
                                  std::optional<StreamQuery> query)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (currentToken != m_snapshot.token || !current(currentToken))
        return false;
    if (!query) {
        dropNext();
        return true;
    }
    if (next.session != currentToken.session || next.generation <= m_lastGeneration ||
        !validMusicId(query->song.value) || qualityName(query->quality).isEmpty())
        return false;
    dropNext();
    m_lastGeneration = next.generation;
    qCDebug(lcPlayback) << log::event("preload.start") << log::field("token", next.generation)
                        << log::field("song", query->song.value)
                        << log::field("quality", query->quality);
    m_next = Next{next, *query};
    const auto request = m_resolver.resolve(next.session, *query);
    if (!request) {
        m_next.reset(); // the song is loaded the usual way when this one ends
        return true;
    }
    if (m_next && m_next->token == next)
        m_next->request = request;
    return true;
}

void PlaybackCoordinator::preloaded(StreamResult result)
{
    auto checked = usable(std::move(result), m_next->query.song);
    if (const auto* failure = std::get_if<PlaybackFailure>(&checked)) {
        qCDebug(lcPlayback) << log::event("preload.unresolved")
                            << log::field("token", m_next->token.generation)
                            << log::field("err", failure->error)
                            << log::field("code", failure->providerCode.value_or(0));
        m_next.reset(); // loaded the usual way later, where a failure shows
        return;
    }
    auto& source = std::get<StreamSource>(checked);
    m_next->actualQuality = source.actualQuality;
    m_next->preview = source.preview;
    m_next->loaded = true;
    qCDebug(lcPlayback) << log::event("preload.resolved")
                        << log::field("token", m_next->token.generation)
                        << log::field("quality", source.actualQuality)
                        << log::field("preview", source.preview.has_value());
    MediaSource physical = std::move(source);
    m_backend.preload(std::move(physical), m_next->token);
}

void PlaybackCoordinator::dropNext()
{
    if (!m_next)
        return;
    const auto next = *std::exchange(m_next, std::nullopt);
    if (next.request)
        m_resolver.cancel(next.request);
    if (next.loaded)
        m_backend.preload({}, {});
}

void PlaybackCoordinator::advance()
{
    // The backend went on into the preloaded load: it is the current one now.
    const auto next = *std::exchange(m_next, std::nullopt);
    for (const auto& refresh : std::exchange(m_refreshes, {}))
        if (refresh.token == next.token)
            m_refreshes.append(refresh);
        else
            m_resolver.cancel(refresh.request);
    PlaybackSnapshot snapshot;
    snapshot.token = next.token;
    snapshot.song = next.query.song;
    snapshot.intent = m_snapshot.intent;
    snapshot.state =
        m_snapshot.intent == TransportIntent::Play ? PlaybackState::Playing : PlaybackState::Paused;
    snapshot.actualQuality = next.actualQuality;
    snapshot.preview = next.preview;
    snapshot.volumeLinear = m_snapshot.volumeLinear;
    m_snapshot = std::move(snapshot);
    m_query = next.query;
    m_ready = true;
    m_loadClock.start();
    qCInfo(lcPlayback) << log::event("load.advanced") << log::field("token", next.token.generation)
                       << log::field("song", next.query.song.value);
    m_seek.reset();
    m_deadline.stop();
}

void PlaybackCoordinator::expired(LoadToken token)
{
    StreamQuery query;
    if (token == m_snapshot.token)
        query = m_query;
    else if (m_next && m_next->loaded && token == m_next->token)
        query = m_next->query;
    else
        return;
    for (const auto& refresh : m_refreshes)
        if (refresh.token == token)
            return; // already asking
    qCInfo(lcPlayback) << log::event("load.refresh") << log::field("token", token.generation)
                       << log::field("song", query.song.value);
    const auto request = m_resolver.resolve(token.session, query);
    if (request)
        m_refreshes.append({token, std::move(query), request});
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
    if (m_next && m_next->request)
        m_resolver.cancel(m_next->request);
    m_next.reset();
    for (const auto& refresh : std::exchange(m_refreshes, {}))
        m_resolver.cancel(refresh.request);
    m_backend.stop(); // drops the preloaded load too
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
        (session.phase != SessionPhase::Guest && session.phase != SessionPhase::Authenticated)) {
        qCWarning(lcPlayback) << log::event("load.refused") << log::field("token", token.generation)
                              << log::field("song", query.song.value)
                              << log::field("intent", intent)
                              << log::field("stale_token", token.generation <= m_lastGeneration)
                              << log::field("session", m_session.accepts(token.session));
        return false;
    }
    m_lastGeneration = token.generation;
    m_loadClock.start();
    qCInfo(lcPlayback) << log::event("load.start") << log::field("token", token.generation)
                       << log::field("song", query.song.value)
                       << log::field("quality", query.quality) << log::field("intent", intent);
    // Invalidate prior state BEFORE cancel/stop (adapters may synchronously emit on abort).
    m_snapshot = {};
    retire();
    m_snapshot.token = token;
    m_snapshot.song = query.song;
    m_snapshot.state = PlaybackState::Resolving;
    m_snapshot.intent = intent;
    m_query = query;
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
    const bool expected =
        failure.error == PlaybackError::Canceled || failure.error == PlaybackError::StaleContext;
    if (expected)
        qCDebug(lcPlayback) << log::event("load.ended")
                            << log::field("token", m_snapshot.token.generation)
                            << log::field("err", failure.error);
    else
        qCWarning(lcPlayback) << log::event("load.failed")
                              << log::field("token", m_snapshot.token.generation)
                              << log::field("song", m_snapshot.song.value)
                              << log::field("err", failure.error)
                              << log::field("code", failure.providerCode.value_or(0))
                              << log::field("state", m_snapshot.state)
                              << log::field("ms", m_loadClock.elapsed());
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
    // Keep actual state until media feedback; accepting Pause is only intent.
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
void PlaybackCoordinator::setNormalization(bool on) { m_backend.setNormalization(on); }
void PlaybackCoordinator::receive(PlaybackEvent event)
{
    using Kind = PlaybackEvent::Kind;
    if (event.kind == Kind::Advanced) {
        if (!m_next || !m_next->loaded || event.token != m_next->token ||
            !current(m_snapshot.token) || !m_session.accepts(event.token.session))
            return;
        advance();
        emit changed(m_snapshot);
        return;
    }
    if (event.kind == Kind::SourceExpired) {
        if (current(m_snapshot.token) && m_session.accepts(event.token.session))
            expired(event.token);
        return;
    }
    if (!current(event.token))
        return;
    switch (event.kind) {
    case Kind::Failure:
        fail(event.failure);
        return;
    case Kind::Ready:
        if (m_ready)
            return;
        m_ready = true;
        m_deadline.stop();
        // load.resolved and the engine's records are debug: this is the song's one line.
        qCInfo(lcPlayback) << log::event("load.ready")
                           << log::field("token", event.token.generation)
                           << log::field("song", m_snapshot.song.value)
                           << log::field("quality", m_snapshot.actualQuality)
                           << log::field("preview", m_snapshot.preview.has_value())
                           << log::field("ms", m_loadClock.elapsed());
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
    case Kind::Observation:
        if (!m_ready || !event.observed || event.observed->playedMs < 0 ||
            event.observed->playedMs > ObservedPlayback::MaximumMs ||
            (event.observed->capped && event.observed->playedMs != ObservedPlayback::MaximumMs) ||
            (m_snapshot.observed && (event.observed->playedMs < m_snapshot.observed->playedMs ||
                                     (m_snapshot.observed->capped && !event.observed->capped))) ||
            m_snapshot.observed == event.observed)
            return;
        m_snapshot.observed = event.observed;
        break;
    case Kind::SeekConfirmed:
    case Kind::Position:
        if (event.value < 0 || event.value > ObservedPlayback::MaximumMs)
            return;
        if (m_snapshot.positionRevision == std::numeric_limits<quint64>::max() ||
            (event.kind == Kind::SeekConfirmed &&
             m_snapshot.seekRevision == std::numeric_limits<quint64>::max())) {
            fail({PlaybackError::InvalidResponse});
            return;
        }
        ++m_snapshot.positionRevision;
        if (event.kind == Kind::SeekConfirmed)
            ++m_snapshot.seekRevision;
        m_snapshot.positionMs = event.value;
        if (m_snapshot.durationMs)
            m_snapshot.positionMs = qMin(m_snapshot.positionMs, *m_snapshot.durationMs);
        break;
    case Kind::Volume:
        if (!event.volumeLinear || !std::isfinite(*event.volumeLinear) || *event.volumeLinear < 0 ||
            *event.volumeLinear > 1)
            return;
        m_snapshot.volumeLinear = event.volumeLinear;
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
    case Kind::Advanced:
    case Kind::SourceExpired:
        return; // handled above
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
