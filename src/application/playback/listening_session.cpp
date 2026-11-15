#include "listening_session.h"
#include "application/music/personal_fm_source.h"
#include "logging/log.h"
#include <QPointer>
#include <QThread>
#include <QTimer>
#include <chrono>
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
        if (m_queueContext.isValid() &&
            (value.token != m_queueContext || value.accountId != m_sourceSession.accountId ||
             value.phase != m_sourceSession.phase))
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
    m_sourceSession = {};
    m_positionObservedAt.reset();
    m_auto = false;
    m_autoVisited.clear();
    m_terminalHandled = false;
    m_preload.reset();
    m_preloadChecked = false;
    cancelLyrics();
    if (revision != m_revision)
        return;
    if (wasFm && m_fm)
        m_fm->stop();
    if (revision != m_revision)
        return;
    if (!notifyHistory())
        return;
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
    if (!entry || m_backend.state() != BackendState::Ready) {
        qCWarning(lcPlayback) << log::event("queue.begin_refused")
                              << log::field("reason", entry ? "backend_not_ready" : "no_entry");
        return false;
    }
    const auto sourceSession = m_backend.snapshot();
    const auto revision = ++m_revision;
    m_starting = true;
    const auto id = m_backend.playSong(entry->song, quality, intent);
    m_starting = false;
    qCDebug(lcPlayback) << log::event("queue.begin") << log::field("song", entry->song.value)
                        << log::field("intent", intent) << log::field("automatic", automatic)
                        << log::field("fm", fm) << log::field("command", id);
    if (!acceptLoadCommand(id, revision)) {
        qCWarning(lcPlayback) << log::event("queue.begin_refused")
                              << log::field("reason", "command") << log::field("command", id);
        return false;
    }
    commit(std::move(candidate), intent, quality, automatic, fm, sourceSession, revision);
    return true;
}
void ListeningSession::commit(PlaybackQueue candidate, TransportIntent intent, AudioQuality quality,
                              bool automatic, bool fm, SessionSnapshot sourceSession,
                              quint64 revision)
{
    const auto entry = candidate.current();
    m_preload.reset();
    m_preloadChecked = false;
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
    m_sourceSession = sourceSession;
    m_positionObservedAt.reset();
    m_history.begin(m_token, m_backend.snapshot().accountId, entry->song,
                    fm ? ListeningMode::PersonalFm : ListeningMode::Queue, fm ? QUuid{} : entry->id,
                    fm ? PlayOrigin{} : entry->origin);
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
        return;
    const auto lyricRequest = m_backend.lyrics(entry->song);
    if (revision != m_revision) {
        if (lyricRequest)
            m_backend.cancelMusic(lyricRequest);
        return;
    }
    m_lyricRequest = lyricRequest;
    m_lyricsState = m_lyricRequest ? LyricsState::Loading : LyricsState::Failed;
    if (!notifyHistory())
        return;
    if (revision != m_revision)
        return;
    emit fmChanged();
    if (revision != m_revision)
        return;
    emit queueChanged(queue());
    if (revision != m_revision)
        return;
    emit playbackChanged(m_playback);
    if (revision != m_revision)
        return;
    emit lyricsChanged(m_lyricsState);
    if (revision == m_revision)
        refreshCursors();
    return;
}
bool ListeningSession::replaceQueue(const QList<SongId>& songs, qsizetype startIndex,
                                    AudioQuality quality, const QList<PlayOrigin>& origins)
{
    qCInfo(lcPlayback) << log::event("queue.replace") << log::field("count", songs.size())
                       << log::field("index", startIndex) << log::field("quality", quality);
    if (startIndex < 0 || startIndex >= songs.size())
        return false;
    auto candidate = m_queue;
    if (!candidate.replace(songs, origins) ||
        !candidate.select(candidate.snapshot().entries[startIndex].id))
        return false;
    return begin(std::move(candidate), TransportIntent::Play, quality, false);
}
bool ListeningSession::restoreQueue(const QueueSnapshot& saved, SessionSnapshot expected,
                                    std::pair<quint64, quint64> expectedIntent)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_backend.state() != BackendState::Ready || !expected.token.isValid() ||
        m_backend.snapshot() != expected || intentRevision() != expectedIntent || current() ||
        m_mode != ListeningMode::Queue || !m_queue.snapshot().entries.isEmpty() ||
        m_revision == std::numeric_limits<quint64>::max())
        return false;
    auto candidate = m_queue;
    if (!candidate.restore(saved))
        return false;
    qCInfo(lcPlayback) << log::event("queue.restored") << log::field("count", saved.entries.size());
    const auto revision = ++m_revision;
    m_queue = std::move(candidate);
    m_queueContext = expected.token;
    m_sourceSession = expected;
    m_intent = TransportIntent::Play; // only a future explicit user command may start media
    const QPointer<ListeningSession> guard(this);
    emit queueChanged(m_queue.snapshot());
    return guard && m_revision == revision && m_backend.snapshot() == expected;
}
bool ListeningSession::append(const QList<SongId>& songs, bool playNext, const PlayOrigin& origin)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_mode == ListeningMode::PersonalFm || m_backend.state() != BackendState::Ready ||
        !m_backend.snapshot().token.isValid())
        return false;
    if (m_queueContext.isValid() && !queueContextMatches())
        return false;
    if (!m_queue.append(songs, playNext, origin))
        return false;
    m_queueContext = m_backend.snapshot().token;
    m_sourceSession = m_backend.snapshot();
    refreshPreload();
    emit queueChanged(m_queue.snapshot());
    return true;
}
bool ListeningSession::select(QUuid entry)
{
    qCInfo(lcPlayback) << log::event("queue.select") << log::field("entry", entry);
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
    qCInfo(lcPlayback) << log::event("queue.next") << log::field("mode", m_mode);
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
bool ListeningSession::canGoNext() const
{
    if (m_backend.state() != BackendState::Ready || !queueContextMatches())
        return false;
    return m_mode == ListeningMode::PersonalFm ? current() && !m_fmWaiting && m_fm
                                               : m_queue.canNext();
}
bool ListeningSession::canGoPrevious() const
{
    if (m_backend.state() != BackendState::Ready || !queueContextMatches())
        return false;
    const bool restart = current() && m_playback.positionMs > 3000 && m_playback.seekable;
    return m_mode == ListeningMode::PersonalFm ? restart && !m_fmWaiting
                                               : restart || m_queue.canPrevious();
}
bool ListeningSession::previous()
{
    qCInfo(lcPlayback) << log::event("queue.previous") << log::field("mode", m_mode);
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
    refreshPreload();
    emit queueChanged(m_queue.snapshot());
    return true;
}
bool ListeningSession::clear()
{
    const auto source = m_backend.snapshot();
    const auto revision = m_revision;
    if (current()) {
        if (!stop())
            return false;
        if (m_revision != revision + 1)
            return true;
    }
    reset();
    if (m_backend.state() == BackendState::Ready && m_backend.snapshot() == source)
        emit queueCleared(source);
    return true;
}
bool ListeningSession::stop()
{
    if (!current())
        return false;
    qCInfo(lcPlayback) << log::event("queue.stop");
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
    m_positionObservedAt.reset();
    m_intent = TransportIntent::Stop;
    m_auto = false;
    m_autoVisited.clear();
    if (!notifyHistory())
        return true;
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
        if (m_intent == TransportIntent::Pause) {
            recordControlIntent();
            return true;
        }
        recordControlIntent();
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
    recordControlIntent();
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
        if (m_intent == TransportIntent::Play && !m_fmBlocked) {
            recordControlIntent();
            return true;
        }
        recordControlIntent();
        m_intent = TransportIntent::Play;
        m_playback.intent = m_intent;
        emit playbackChanged(m_playback);
        return retryFm();
    }
    if (current() && m_playback.state != PlaybackState::Empty &&
        m_playback.state != PlaybackState::Failed && m_playback.state != PlaybackState::Ended) {
        if (!m_backend.resumePlayback())
            return false;
        recordControlIntent();
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
    if (!current() || !m_backend.seekPlayback(position))
        return false;
    recordControlIntent();
    return true;
}
bool ListeningSession::setVolume(float linear)
{
    if (!m_backend.setPlaybackVolume(linear))
        return false;
    recordControlIntent();
    return true;
}
bool ListeningSession::move(QUuid entry, qsizetype to)
{
    if (m_mode == ListeningMode::PersonalFm || (m_queueContext.isValid() && !queueContextMatches()))
        return false;
    if (!m_queue.move(entry, to))
        return false;
    recordControlIntent();
    refreshPreload();
    emit queueChanged(m_queue.snapshot());
    return true;
}
bool ListeningSession::setModes(QueueOrder order, QueueRepeat repeat)
{
    if (m_mode == ListeningMode::PersonalFm || (m_queueContext.isValid() && !queueContextMatches()))
        return false;
    if (!m_queue.setModes(order, repeat))
        return false;
    recordControlIntent();
    refreshPreload();
    emit queueChanged(m_queue.snapshot());
    return true;
}
bool ListeningSession::setLyricOffset(qint64 offset)
{
    if (offset < -120000 || offset > 120000)
        return false;
    m_userOffset = offset;
    emit lyricOffsetChanged(offset); // even an empty/unchanged cursor needs a new observation
    refreshCursors();
    return true;
}
bool ListeningSession::notifyHistory(bool notifyView)
{
    const QPointer<ListeningSession> guard(this);
    const auto epoch = m_historyClearEpoch;
    const auto updates = m_history.takeUpdates();
    const bool overflow = m_history.takeUpdateOverflow();
    for (const auto& row : updates) {
        emit listeningRecordChanged(row);
        if (!guard || m_historyClearEpoch != epoch)
            return bool(guard);
    }
    if (overflow)
        emit historyStorageOverflow();
    if (!guard)
        return false;
    if (notifyView)
        emit historyChanged();
    return bool(guard);
}
void ListeningSession::finishHistoryForStorage()
{
    Q_ASSERT(QThread::currentThread() == thread());
    m_history.finish(ListeningEnd::Detached);
    notifyHistory(false); // no player command or restored reporting event
}
void ListeningSession::clearHistory()
{
    Q_ASSERT(QThread::currentThread() == thread());
    const QPointer<ListeningSession> guard(this);
    const auto source = m_backend.snapshot();
    const bool ready = m_backend.state() == BackendState::Ready;
    const bool changed = !m_history.records().isEmpty();
    ++m_historyClearEpoch;
    if (m_history.context() != source.token || !ready)
        m_history.reset();
    else
        m_history.clear();
    if (ready && source.token.isValid())
        emit historyCleared(source); // delete precedes any newly reentrant post-clear load
    if (guard)
        notifyHistory(changed);
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
    if (m_preload && snapshot.token.generation == m_preload->generation &&
        snapshot.token.session == m_token.session && m_backend.playback().token == snapshot.token &&
        m_backend.state() == BackendState::Ready && m_backend.snapshot().token == m_token.session) {
        adoptAdvance(); // played on into the preloaded song
        return;
    }
    if (!current()) {
        reset();
        return;
    } // external facade intent/session change disowns this queue
    if (snapshot.token != m_token)
        return;
    const auto revision = m_revision;
    if (snapshot.positionRevision != m_playback.positionRevision) {
        m_positionObservedAt = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now().time_since_epoch())
                                   .count(); // owner arrival, not decoder/acoustic time
    }
    m_playback = snapshot;
    m_playback.intent = m_intent;
    if (m_history.observe(snapshot)) {
        if (!notifyHistory())
            return;
        if (revision != m_revision)
            return;
    }
    emit playbackChanged(m_playback);
    if (revision != m_revision)
        return;
    refreshCursors();
    if (revision != m_revision)
        return;
    if (!m_preloadChecked && snapshot.state == PlaybackState::Playing) {
        m_preloadChecked = true; // once playing, so it doesn't compete with this one's start
        refreshPreload();
    }
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
void ListeningSession::refreshPreload()
{
    if (!m_preloadChecked || m_mode != ListeningMode::Queue || !current() || !queueContextMatches())
        return;
    const auto state = m_playback.state;
    if (state != PlaybackState::Playing && state != PlaybackState::Paused &&
        state != PlaybackState::Ready)
        return;
    // What advance() would play at the natural end.
    auto candidate = m_queue;
    std::optional<QueueEntry> entry;
    if (candidate.next(true))
        entry = candidate.current();
    if (!entry) {
        if (m_preload) {
            m_preload.reset();
            static_cast<void>(m_backend.preloadSong({}, m_quality));
        }
        return;
    }
    if (m_preload && m_preload->entry == entry->id && m_preload->song == entry->song) {
        m_preload->queue = std::move(candidate); // same song; the rest of the queue changed
        return;
    }
    const auto id = m_backend.preloadSong(entry->song, m_quality);
    if (!id) {
        m_preload.reset();
        return;
    }
    m_preload = Preload{id, entry->id, entry->song, std::move(candidate)};
}
void ListeningSession::adoptAdvance()
{
    auto preload = *std::exchange(m_preload, std::nullopt);
    const auto revision = ++m_revision;
    m_history.finish(ListeningEnd::Ended); // the last song played to its end
    m_autoVisited.clear();                 // as advance() after a natural end
    commit(std::move(preload.queue), m_intent, m_quality, true, false, m_backend.snapshot(),
           revision);
}
void ListeningSession::advance(bool naturalEnd)
{
    // After an end (natural) or a failure (skips at most 5 entries in a row).
    qCDebug(lcPlayback) << log::event("queue.advance") << log::field("natural", naturalEnd)
                        << log::field("skipped", m_autoVisited.size());
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
