#include "backend_facade.h"

#include <QThread>
#include <QTimer>
#include <cmath>
#include <utility>

namespace cirrus::application
{

BackendFacade::BackendFacade(QObject* parent) : QObject(parent) {}

void BackendFacade::setState(BackendState state)
{
    m_state = state;
    emit stateChanged(state);
}

void BackendFacade::start()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_state != BackendState::Dormant)
        return;
    setState(BackendState::Starting);
    if (m_state == BackendState::Starting)
        emit startRequested();
}

void BackendFacade::close()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_state == BackendState::Stopping || m_state == BackendState::Stopped)
        return;
    ++m_generation;
    m_snapshot = {};
    invalidateMusic(MusicError::Canceled);
    invalidatePlayback();
    const auto pending = std::exchange(m_pending, {});
    setState(BackendState::Stopping);
    emit closeRequested();
    for (auto id : pending)
        emit acknowledged(id, false);
    QTimer::singleShot(5000, this, [this] {
        if (m_state == BackendState::Stopping)
            emit closeDelayed();
    });
}

quint64 BackendFacade::submit(AccountAction action, QrLoginOptions options)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_state != BackendState::Ready || m_pending.size() >= 32 || !options.isValid())
        return 0;
    const auto id = ++m_nextId;
    m_pending.insert(id);
    if (action != AccountAction::Poll) {
        ++m_generation;
        m_snapshot = {}; // immediate intent barrier, before backend processes the command
        invalidateMusic(MusicError::StaleContext);
        invalidatePlayback();
    }
    emit commandRequested({id, m_generation, action, options});
    return id;
}

quint64 BackendFacade::beginLogin(QrLoginOptions options)
{
    return submit(AccountAction::BeginLogin, options);
}
quint64 BackendFacade::restore() { return submit(AccountAction::Restore); }
quint64 BackendFacade::poll() { return submit(AccountAction::Poll); }
quint64 BackendFacade::cancel() { return submit(AccountAction::Cancel); }
quint64 BackendFacade::signOut() { return submit(AccountAction::SignOut); }

void BackendFacade::deliver(BackendEvent event)
{
    Q_ASSERT(QThread::currentThread() == thread());
    using Kind = BackendEvent::Kind;
    if (event.kind == Kind::Acknowledged) {
        if (m_pending.remove(event.commandId))
            emit acknowledged(event.commandId, event.accepted);
        return;
    }
    if (event.kind == Kind::StorageWarning) {
        if (m_state != BackendState::Stopped)
            emit persistenceWarning(event.persistenceArea);
        return;
    }
    if (event.kind == Kind::Saved) {
        if (m_state != BackendState::Stopped)
            emit savedSessionCompleted(event.session.token, event.saved);
        return; // storage outcomes retain their original token, never change session state
    }
    if (m_state == BackendState::Stopping || m_state == BackendState::Stopped ||
        event.generation != m_generation)
        return;
    if (event.kind == Kind::Started && m_state == BackendState::Starting) {
        const bool fatal = event.startupError == StartupError::Configuration ||
                           event.startupError == StartupError::Storage;
        setState(fatal ? BackendState::Failed : BackendState::Ready);
        if (m_state == BackendState::Ready || m_state == BackendState::Failed)
            emit started(event.startupError);
        return;
    }
    if (m_state != BackendState::Ready)
        return;
    if (event.kind == Kind::Session) {
        if (m_snapshot.token != event.session.token) {
            invalidateMusic(MusicError::StaleContext);
            invalidatePlayback();
        }
        m_snapshot = event.session;
        emit sessionChanged(m_snapshot);
        return;
    }
    if (event.session.token != m_snapshot.token)
        return;
    switch (event.kind) {
    case Kind::Challenge:
        emit challengeReady(event.session.token, event.challenge);
        break;
    case Kind::Status:
        emit statusChanged(event.session.token, event.status);
        break;
    case Kind::SignedIn:
        emit signedIn(event.session.token, event.identity);
        break;
    case Kind::AuthFailed:
        emit failed(event.session.token, event.failure);
        break;
    default:
        break;
    }
}

quint64 BackendFacade::searchSongs(domain::SongSearch query)
{
    return submitMusic(std::move(query));
}
quint64 BackendFacade::songDetails(QList<domain::SongId> songs)
{
    return submitMusic(DetailQuery{std::move(songs)});
}
quint64 BackendFacade::lyrics(domain::SongId song)
{
    return submitMusic(LyricQuery{std::move(song)});
}

quint64 BackendFacade::albumDetails(domain::AlbumId album)
{
    return submitMusic(domain::AlbumQuery{std::move(album)});
}
quint64 BackendFacade::artistDetails(domain::ArtistId artist)
{
    return submitMusic(domain::ArtistQuery{std::move(artist)});
}
quint64 BackendFacade::artistSongs(domain::ArtistSongsQuery query)
{
    return submitMusic(std::move(query));
}
quint64 BackendFacade::artistAlbums(domain::ArtistAlbumsQuery query)
{
    return submitMusic(std::move(query));
}
quint64 BackendFacade::playlistDetails(domain::PlaylistId playlist)
{
    return submitMusic(domain::PlaylistQuery{std::move(playlist)});
}
quint64 BackendFacade::playlistSongs(domain::PlaylistSongsQuery query)
{
    return submitMusic(std::move(query));
}

quint64 BackendFacade::submitMusic(MusicQuery query)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_state != BackendState::Ready || !m_snapshot.token.isValid() || m_music.size() >= 16 ||
        !validMusicQuery(query) ||
        (m_snapshot.phase != domain::SessionPhase::Guest &&
         m_snapshot.phase != domain::SessionPhase::Authenticated))
        return 0;
    const auto id = ++m_nextId;
    const MusicCommand command{id, m_generation, m_snapshot.token, std::move(query)};
    m_music.insert(id, command);
    emit musicRequested(command);
    return id;
}

void BackendFacade::cancelMusicWith(quint64 id, MusicError error)
{
    if (!m_music.contains(id))
        return;
    const auto command = m_music.take(id);
    emit musicCancelRequested(id);
    QTimer::singleShot(0, this, [this, id, context = command.context, error] {
        emit musicCompleted(id, context, MusicFailure{error});
    });
}

void BackendFacade::invalidateMusic(MusicError error)
{
    for (auto id : m_music.keys())
        cancelMusicWith(id, error);
}

void BackendFacade::cancelMusic(quint64 id)
{
    Q_ASSERT(QThread::currentThread() == thread());
    cancelMusicWith(id, MusicError::Canceled);
}

void BackendFacade::deliverMusic(MusicDelivery delivery)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!m_music.contains(delivery.id))
        return;
    const auto expected = m_music.take(delivery.id);
    if (delivery.generation != expected.generation || delivery.context != expected.context ||
        expected.generation != m_generation || expected.context != m_snapshot.token ||
        m_state != BackendState::Ready)
        delivery.result = MusicFailure{MusicError::StaleContext};
    emit musicCompleted(expected.id, expected.context, std::move(delivery.result));
}

void BackendFacade::invalidatePlayback()
{
    m_playbackGeneration = ++m_nextId;
    m_playback = {};
    // Deferred: account intent invalidation must not reenter submit before its command is emitted.
    const auto generation = m_playbackGeneration;
    QTimer::singleShot(0, this, [this, generation] {
        if (generation == m_playbackGeneration)
            emit playbackChanged(m_playback);
    });
}
quint64 BackendFacade::playSong(domain::SongId song, domain::AudioQuality quality,
                                domain::TransportIntent intent)
{
    PlaybackCommand command;
    command.action = PlaybackAction::Play;
    command.query = {std::move(song), quality};
    command.intent = intent;
    return submitPlayback(command);
}
quint64 BackendFacade::pausePlayback()
{
    PlaybackCommand c;
    c.action = PlaybackAction::Pause;
    return submitPlayback(c);
}
quint64 BackendFacade::resumePlayback()
{
    PlaybackCommand c;
    c.action = PlaybackAction::Resume;
    return submitPlayback(c);
}
quint64 BackendFacade::stopPlayback()
{
    PlaybackCommand c;
    c.action = PlaybackAction::Stop;
    return submitPlayback(c);
}
quint64 BackendFacade::seekPlayback(qint64 positionMs)
{
    PlaybackCommand c;
    c.action = PlaybackAction::Seek;
    c.positionMs = positionMs;
    return submitPlayback(c);
}
quint64 BackendFacade::setPlaybackVolume(float linear)
{
    PlaybackCommand c;
    c.action = PlaybackAction::Volume;
    c.volume = linear;
    return submitPlayback(c);
}
quint64 BackendFacade::submitPlayback(PlaybackCommand command)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_state != BackendState::Ready || m_pending.size() >= 32 || !m_snapshot.token.isValid() ||
        (m_snapshot.phase != domain::SessionPhase::Guest &&
         m_snapshot.phase != domain::SessionPhase::Authenticated))
        return 0;
    if (command.action == PlaybackAction::Play &&
        (!domain::validMusicId(command.query.song.value) ||
         domain::qualityName(command.query.quality).isEmpty() ||
         (command.intent != domain::TransportIntent::Play &&
          command.intent != domain::TransportIntent::Pause)))
        return 0;
    if (command.action == PlaybackAction::Seek &&
        (command.positionMs < 0 || command.positionMs > 7LL * 24 * 60 * 60 * 1000))
        return 0;
    if (command.action == PlaybackAction::Volume &&
        (!std::isfinite(command.volume) || command.volume < 0 || command.volume > 1))
        return 0;
    command.id = ++m_nextId;
    command.generation = m_generation;
    m_pending.insert(command.id);
    const bool replacing =
        command.action == PlaybackAction::Play || command.action == PlaybackAction::Stop;
    if (replacing) {
        m_playbackGeneration = command.id;
        m_playback = {};
        m_playback.token = {m_snapshot.token, m_playbackGeneration};
        if (command.action == PlaybackAction::Play) {
            m_playback.song = command.query.song;
            m_playback.state = domain::PlaybackState::Resolving;
            m_playback.intent = command.intent;
        }
    }
    command.token = {m_snapshot.token, m_playbackGeneration};
    emit playbackRequested(command);
    if (replacing && m_playback.token == command.token)
        emit playbackChanged(m_playback);
    return command.id;
}
void BackendFacade::deliverPlayback(PlaybackDelivery delivery)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_state != BackendState::Ready || delivery.generation != m_generation ||
        delivery.snapshot.token.session != m_snapshot.token ||
        delivery.snapshot.token.generation != m_playbackGeneration)
        return;
    m_playback = std::move(delivery.snapshot);
    emit playbackChanged(m_playback);
}

void BackendFacade::finishClose(bool persistenceFailed)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_state == BackendState::Stopped)
        return;
    m_snapshot = {};
    setState(BackendState::Stopped);
    emit stopped(persistenceFailed);
}

} // namespace cirrus::application
