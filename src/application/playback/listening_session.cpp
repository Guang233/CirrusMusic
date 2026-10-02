#include "listening_session.h"
#include "application/music/personal_fm_source.h"
#include <QThread>
#include <QTimer>
#include <utility>

namespace cirrus::application
{
using namespace domain;
ListeningSession::ListeningSession(BackendFacade& backend, QObject* parent)
    : QObject(parent), m_backend(backend)
{
    Q_ASSERT(backend.thread() == thread());
    connect(&backend, &BackendFacade::playbackChanged, this, &ListeningSession::receive);
    connect(&backend, &BackendFacade::stateChanged, this, [this](BackendState state) {
        if (state == BackendState::Stopping || state == BackendState::Stopped ||
            state == BackendState::Failed)
            reset();
    });
    connect(&backend, &BackendFacade::sessionChanged, this, [this](SessionSnapshot value) {
        if (m_queueContext.isValid() && value.token != m_queueContext)
            reset();
    });
    connect(&backend, &BackendFacade::musicCompleted, this,
            [this](quint64 id, SessionToken context, MusicResult result) {
                if (!id || id != m_lyricRequest)
                    return;
                m_lyricRequest = 0;
                if (!current() || context != m_token.session)
                    return;
                const auto revision = m_revision;
                const auto* lyrics = std::get_if<SongLyrics>(&result);
                if (!lyrics || lyrics->song != m_playback.song) {
                    const auto* failure = std::get_if<MusicFailure>(&result);
                    m_lyricsState = LyricsState::Failed;
                    emit lyricsFailed(failure ? *failure
                                              : MusicFailure{MusicError::InvalidResponse});
                    if (revision == m_revision)
                        emit lyricsChanged(m_lyricsState);
                    return;
                }
                m_lyrics = *lyrics;
                m_timeline.reset(*lyrics);
                m_lyricsState = LyricsState::Ready;
                emit lyricsChanged(m_lyricsState);
                if (revision == m_revision)
                    refreshCursors();
            });
}
ListeningSession::~ListeningSession()
{
    if (m_fm) {
        disconnect(m_fm, nullptr, this, nullptr);
        if (m_mode == ListeningMode::PersonalFm)
            m_fm->stop();
    }
    cancelLyrics();
}
bool ListeningSession::current() const
{
    return m_backend.state() == BackendState::Ready && m_token.generation &&
           m_backend.snapshot().token == m_token.session && m_backend.playback().token == m_token;
}
bool ListeningSession::acceptLoadCommand(quint64 id, quint64 revision)
{
    if (revision != m_revision)
        return false;
    if (!id) {
        --m_revision; // local rejection must not invalidate an already scheduled accepted intent
        return false;
    }
    if (m_backend.playback().token.generation != id) {
        reset(); // a reentrant direct facade command won; never adopt its load
        return false;
    }
    return true;
}
void ListeningSession::cancelLyrics()
{
    const auto id = std::exchange(m_lyricRequest, 0);
    m_lyrics.reset();
    m_timeline.clear();
    m_lyricsState = LyricsState::None;
    if (id)
        m_backend.cancelMusic(id);
}
void ListeningSession::reset()
{
    const auto revision = ++m_revision;
    const bool wasFm = m_mode == ListeningMode::PersonalFm;
    if (m_backend.state() != BackendState::Ready ||
        m_history.context() != m_backend.snapshot().token)
        m_history.reset();
    else
        m_history.finish(ListeningEnd::Detached);
    clearFmState();
    m_queue.clear();
    m_playback = {};
    m_token = {};
    m_queueContext = {};
    m_auto = false;
    m_autoVisited.clear();
    m_terminalHandled = false;
    cancelLyrics();
    if (revision != m_revision)
        return;
    if (wasFm && m_fm)
        m_fm->stop();
    if (revision != m_revision)
        return;
    emit historyChanged();
    if (revision != m_revision)
        return;
    emit fmChanged();
    if (revision != m_revision)
        return;
    emit queueChanged(m_queue.snapshot());
    if (revision != m_revision)
        return;
    emit playbackChanged(m_playback);
    if (revision != m_revision)
        return;
    emit lyricsChanged(m_lyricsState);
    if (revision == m_revision)
        refreshCursors();
}
bool ListeningSession::begin(PlaybackQueue candidate, TransportIntent intent, AudioQuality quality,
                             bool automatic, bool fm)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const auto entry = candidate.current();
    if (!entry || m_backend.state() != BackendState::Ready)
        return false;
    const auto revision = ++m_revision;
    m_starting = true;
    const auto id = m_backend.playSong(entry->song, quality, intent);
    m_starting = false;
    if (!acceptLoadCommand(id, revision))
        return false;
    const auto oldLyricRequest = std::exchange(m_lyricRequest, 0);
    m_lyrics.reset();
    m_timeline.clear();
    m_lyricsState = LyricsState::None;
    const bool leaveFm = !fm && m_mode == ListeningMode::PersonalFm;
    if (!fm)
        clearFmState();
    else {
        m_fmWaiting = false;
        m_fmBlocked = false;
        m_fmPending.reset();
        if (automatic)
            ++m_fmAttempts;
        else
            m_fmAttempts = 0;
    }
    m_queue = std::move(candidate);
    m_playback = m_backend.playback();
    m_token = m_playback.token;
    m_queueContext = m_token.session;
    m_history.begin(m_token, m_backend.snapshot().accountId, entry->song,
                    fm ? ListeningMode::PersonalFm : ListeningMode::Queue,
                    fm ? QUuid{} : entry->id);
    m_quality = quality;
    m_intent = intent;
    m_auto = automatic;
    m_terminalHandled = false;
    if (!automatic || fm)
        m_autoVisited.clear();
    else
        m_autoVisited.insert(entry->id);
    if (leaveFm && m_fm)
        m_fm->stop();
    // Captured old query must retire even when source-stop observers submit a newer intent.
    if (oldLyricRequest)
        m_backend.cancelMusic(oldLyricRequest);
    if (revision != m_revision)
        return true;
    const auto lyricRequest = m_backend.lyrics(entry->song);
    if (revision != m_revision) {
        if (lyricRequest)
            m_backend.cancelMusic(lyricRequest);
        return true;
    }
    m_lyricRequest = lyricRequest;
    m_lyricsState = m_lyricRequest ? LyricsState::Loading : LyricsState::Failed;
    emit historyChanged();
    if (revision != m_revision)
        return true;
    emit fmChanged();
    if (revision != m_revision)
        return true;
    emit queueChanged(queue());
    if (revision != m_revision)
        return true;
    emit playbackChanged(m_playback);
    if (revision != m_revision)
        return true;
    emit lyricsChanged(m_lyricsState);
    if (revision == m_revision)
        refreshCursors();
    return true;
}
bool ListeningSession::replaceQueue(const QList<SongId>& songs, qsizetype startIndex,
                                    AudioQuality quality)
{
    if (startIndex < 0 || startIndex >= songs.size())
        return false;
    auto candidate = m_queue;
    if (!candidate.replace(songs) || !candidate.select(candidate.snapshot().entries[startIndex].id))
        return false;
    return begin(std::move(candidate), TransportIntent::Play, quality, false);
}
bool ListeningSession::append(const QList<SongId>& songs, bool playNext)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_mode == ListeningMode::PersonalFm || m_backend.state() != BackendState::Ready ||
        !m_backend.snapshot().token.isValid())
        return false;
    if (m_queueContext.isValid() && !queueContextMatches())
        return false;
    if (!m_queue.append(songs, playNext))
        return false;
    m_queueContext = m_backend.snapshot().token;
    emit queueChanged(m_queue.snapshot());
    return true;
}
bool ListeningSession::select(QUuid entry)
{
    if (m_mode == ListeningMode::PersonalFm || !queueContextMatches())
        return false;
    auto candidate = m_queue;
    return candidate.select(entry) &&
           begin(std::move(candidate),
                 m_intent == TransportIntent::Pause ? m_intent : TransportIntent::Play, m_quality,
                 false);
}
bool ListeningSession::next()
{
    if (m_mode == ListeningMode::PersonalFm)
        return !m_fmWaiting && enterFmWait(false, true);
    if (!queueContextMatches())
        return false;
    auto candidate = m_queue;
    return candidate.next() &&
           begin(std::move(candidate),
                 m_intent == TransportIntent::Pause ? m_intent : TransportIntent::Play, m_quality,
                 false);
}
bool ListeningSession::previous()
{
    if (m_mode == ListeningMode::PersonalFm)
        return current() && !m_fmWaiting && m_playback.positionMs > 3000 && m_playback.seekable &&
               seek(0);
    if (!queueContextMatches())
        return false;
    if (current() && m_playback.positionMs > 3000 && m_playback.seekable)
        return seek(0);
    auto candidate = m_queue;
    return candidate.previous() &&
           begin(std::move(candidate),
                 m_intent == TransportIntent::Pause ? m_intent : TransportIntent::Play, m_quality,
                 false);
}
bool ListeningSession::remove(QUuid entry)
{
    if (m_mode == ListeningMode::PersonalFm || !queueContextMatches())
        return false;
    auto candidate = m_queue;
    const bool active = candidate.snapshot().active == entry;
    const auto successor = candidate.successor();
    if (!candidate.remove(entry))
        return false;
    if (active && !successor.isNull()) {
        candidate.select(successor);
        return begin(std::move(candidate),
                     m_intent == TransportIntent::Pause ? m_intent : TransportIntent::Play,
                     m_quality, false);
    }
    const auto revision = m_revision;
    if (active) {
        if (!stop())
            return false;
        if (m_revision != revision + 1)
            return true; // a later observer intent wins
    }
    m_queue = std::move(candidate);
    emit queueChanged(m_queue.snapshot());
    return true;
}
bool ListeningSession::clear()
{
    const auto revision = m_revision;
    if (current()) {
        if (!stop())
            return false;
        if (m_revision != revision + 1)
            return true;
    }
    reset();
    return true;
}
bool ListeningSession::stop()
{
    if (!current())
        return false;
    const auto revision = ++m_revision;
    m_starting = true;
    const auto id = m_backend.stopPlayback();
    m_starting = false;
    if (!acceptLoadCommand(id, revision))
        return false;
    m_history.finish(ListeningEnd::Stopped);
    if (m_mode == ListeningMode::PersonalFm) {
        reset();
        return true;
    }
    cancelLyrics();
    if (revision != m_revision)
        return true;
    m_token = m_backend.playback().token;
    m_playback = m_backend.playback();
    m_intent = TransportIntent::Stop;
    m_auto = false;
    m_autoVisited.clear();
    emit historyChanged();
    if (revision != m_revision)
        return true;
    emit playbackChanged(m_playback);
    if (revision != m_revision)
        return true;
    emit lyricsChanged(m_lyricsState);
    if (revision == m_revision)
        refreshCursors();
    return true;
}
bool ListeningSession::pause()
{
    if (current() && m_mode == ListeningMode::PersonalFm &&
        (m_fmWaiting || m_playback.state == PlaybackState::Ended ||
         (m_auto && m_playback.state == PlaybackState::Failed))) {
        if (m_intent == TransportIntent::Pause)
            return true;
        m_intent = TransportIntent::Pause;
        m_playback.intent = m_intent;
        emit playbackChanged(m_playback);
        return true;
    }
    if (!current() || m_playback.state == PlaybackState::Empty ||
        m_playback.state == PlaybackState::Failed || m_playback.state == PlaybackState::Ended)
        return false;
    if (!m_backend.pausePlayback())
        return false;
    m_intent = TransportIntent::Pause;
    m_playback.intent = m_intent;
    emit playbackChanged(m_playback);
    return true;
}
bool ListeningSession::resume()
{
    if (!queueContextMatches())
        return false;
    if (m_mode == ListeningMode::PersonalFm && m_fmWaiting) {
        if (m_intent == TransportIntent::Play && !m_fmBlocked)
            return true;
        m_intent = TransportIntent::Play;
        m_playback.intent = m_intent;
        emit playbackChanged(m_playback);
        return retryFm();
    }
    if (current() && m_playback.state != PlaybackState::Empty &&
        m_playback.state != PlaybackState::Failed && m_playback.state != PlaybackState::Ended) {
        if (!m_backend.resumePlayback())
            return false;
        m_intent = TransportIntent::Play;
        m_playback.intent = m_intent;
        emit playbackChanged(m_playback);
        return true;
    }
    return begin(m_queue, TransportIntent::Play, m_quality, false,
                 m_mode == ListeningMode::PersonalFm);
}
bool ListeningSession::seek(qint64 position)
{
    return current() && m_backend.seekPlayback(position) != 0;
}
bool ListeningSession::setVolume(float linear) { return m_backend.setPlaybackVolume(linear) != 0; }
bool ListeningSession::setModes(QueueOrder order, QueueRepeat repeat)
{
    if (m_mode == ListeningMode::PersonalFm || (m_queueContext.isValid() && !queueContextMatches()))
        return false;
    if (!m_queue.setModes(order, repeat))
        return false;
    emit queueChanged(m_queue.snapshot());
    return true;
}
bool ListeningSession::setLyricOffset(qint64 offset)
{
    if (offset < -120000 || offset > 120000)
        return false;
    m_userOffset = offset;
    refreshCursors();
    return true;
}
void ListeningSession::clearHistory()
{
    Q_ASSERT(QThread::currentThread() == thread());
    const bool changed = !m_history.records().isEmpty();
    if (m_history.context() != m_backend.snapshot().token ||
        m_backend.state() != BackendState::Ready)
        m_history.reset();
    else
        m_history.clear();
    if (changed)
        emit historyChanged();
}
void ListeningSession::refreshCursors()
{
    LyricCursors cursor;
    if (current() && m_lyricsState == LyricsState::Ready &&
        (m_playback.state == PlaybackState::Ready || m_playback.state == PlaybackState::Playing ||
         m_playback.state == PlaybackState::Paused)) {
        constexpr qint64 maximum = 7LL * 24 * 60 * 60 * 1000;
        const auto start = m_playback.preview ? m_playback.preview->startMs : 0;
        if (start >= 0 && start <= maximum && m_playback.positionMs >= 0 &&
            m_playback.positionMs <= maximum - start)
            cursor = m_timeline.at(m_playback.positionMs + start, m_userOffset);
    }
    if (cursor != m_cursors) {
        m_cursors = std::move(cursor);
        emit cursorsChanged(m_cursors);
    }
}
void ListeningSession::receive(PlaybackSnapshot snapshot)
{
    if (m_starting)
        return;
    if (!m_token.generation)
        return;
    if (!current()) {
        reset();
        return;
    } // external facade intent/session change disowns this queue
    if (snapshot.token != m_token)
        return;
    const auto revision = m_revision;
    m_playback = snapshot;
    m_playback.intent = m_intent;
    if (m_history.observe(snapshot)) {
        emit historyChanged();
        if (revision != m_revision)
            return;
    }
    emit playbackChanged(m_playback);
    if (revision != m_revision)
        return;
    refreshCursors();
    if (revision != m_revision || m_terminalHandled)
        return;
    const bool end = snapshot.state == PlaybackState::Ended;
    if (!end && snapshot.state != PlaybackState::Failed)
        return;
    if (!end && !m_auto) {
        m_terminalHandled = true;
        return;
    }
    m_terminalHandled = true;
    QTimer::singleShot(0, this, [this, revision, end] {
        if (revision == m_revision && current()) {
            if (m_mode == ListeningMode::PersonalFm) {
                if (end)
                    m_fmAttempts = 0;
                enterFmWait(true, false);
            } else
                advance(end);
        }
    });
}
void ListeningSession::advance(bool naturalEnd)
{
    if (naturalEnd)
        m_autoVisited.clear();
    const auto count = m_queue.snapshot().entries.size();
    if (m_autoVisited.size() >= qMin<qsizetype>(count, 5)) {
        emit advanceStopped(true);
        return;
    }
    auto candidate = m_queue;
    for (qsizetype i = 0; i < count; ++i) {
        if (!candidate.next(naturalEnd, !naturalEnd)) {
            emit advanceStopped(!naturalEnd);
            return;
        }
        naturalEnd = false;
        if (m_autoVisited.contains(candidate.snapshot().active))
            continue;
        if (!begin(std::move(candidate),
                   m_intent == TransportIntent::Pause ? m_intent : TransportIntent::Play, m_quality,
                   true))
            emit advanceStopped(true);
        return;
    }
    emit advanceStopped(true);
}
} // namespace cirrus::application
