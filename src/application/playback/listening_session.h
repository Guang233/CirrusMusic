#pragma once
#include "application/backend/backend_facade.h"
#include "listening_history.h"
#include "lyric_timeline.h"
#include "playback_queue.h"

namespace cirrus::application
{
class PersonalFmSource;
// Caller-thread Core orchestration over the ASYNC facade; never owns network/decoder objects.
// Facade must outlive this object. Use this as the sole playback command producer when using a
// queue.
class ListeningSession final : public QObject
{
    Q_OBJECT
  public:
    explicit ListeningSession(BackendFacade& backend, QObject* parent = nullptr);
    ListeningSession(BackendFacade& backend, PersonalFmSource& fm, QObject* parent = nullptr);
    ~ListeningSession() override;
    bool startFm(domain::AudioQuality quality = domain::AudioQuality::Standard,
                 domain::TransportIntent intent = domain::TransportIntent::Play);
    bool retryFm();
    [[nodiscard]] domain::ListeningMode mode() const
    {
        return current() ? m_mode : domain::ListeningMode::Queue;
    }
    [[nodiscard]] bool waitingForFm() const
    {
        return mode() == domain::ListeningMode::PersonalFm && m_fmWaiting;
    }
    [[nodiscard]] bool fmBlocked() const { return waitingForFm() && m_fmBlocked; }
    bool replaceQueue(const QList<domain::SongId>& songs, qsizetype startIndex = 0,
                      domain::AudioQuality quality = domain::AudioQuality::Standard);
    bool append(const QList<domain::SongId>& songs, bool playNext = false);
    bool select(QUuid entry);
    bool remove(QUuid entry);
    bool clear();
    bool next();
    bool previous();
    bool pause();
    bool resume();
    bool stop();
    bool seek(qint64 clipPositionMs);
    bool setVolume(float linear);
    bool setModes(domain::QueueOrder order, domain::QueueRepeat repeat);
    bool setLyricOffset(qint64 offsetMs);
    void clearHistory();
    [[nodiscard]] QList<domain::ListeningRecord> history() const
    {
        return m_backend.state() == BackendState::Ready &&
                       m_history.context() == m_backend.snapshot().token
                   ? m_history.records()
                   : QList<domain::ListeningRecord>{};
    }
    [[nodiscard]] domain::QueueSnapshot queue() const
    {
        return m_mode == domain::ListeningMode::Queue &&
                       (!m_queueContext.isValid() || queueContextMatches())
                   ? m_queue.snapshot()
                   : domain::QueueSnapshot{};
    }
    [[nodiscard]] domain::PlaybackSnapshot playback() const
    {
        return current() ? m_playback : domain::PlaybackSnapshot{};
    }
    [[nodiscard]] domain::LyricsState lyricsState() const
    {
        return current() ? m_lyricsState : domain::LyricsState::None;
    }
    [[nodiscard]] std::optional<domain::SongLyrics> lyrics() const
    {
        return current() ? m_lyrics : std::nullopt;
    }
    [[nodiscard]] domain::LyricCursors cursors() const
    {
        return current() ? m_cursors : domain::LyricCursors{};
    }
  signals:
    void historyChanged(); // notification only, owning-thread getter; never a reporting event
    void fmChanged();      // notification only; read current mode/waiting/blocked getters
    void queueChanged(cirrus::domain::QueueSnapshot queue);
    void playbackChanged(cirrus::domain::PlaybackSnapshot snapshot);
    void lyricsChanged(cirrus::domain::LyricsState state);
    void lyricsFailed(cirrus::application::MusicFailure failure);
    void cursorsChanged(cirrus::domain::LyricCursors cursors);
    void
    advanceStopped(bool failed); // false=end of queue; true=bounded failure/busy, no retry loop
  private:
    bool begin(PlaybackQueue candidate, domain::TransportIntent intent,
               domain::AudioQuality quality, bool automatic, bool fm = false);
    bool enterFmWait(bool automatic, bool stopMedia);
    void scheduleFm();
    void consumeFm();
    void blockFm();
    void clearFmState();
    bool acceptLoadCommand(quint64 id, quint64 revision);
    void receive(domain::PlaybackSnapshot snapshot);
    void advance(bool naturalEnd);
    void reset();
    void cancelLyrics();
    void refreshCursors();
    bool current() const;
    bool queueContextMatches() const { return m_queueContext == m_backend.snapshot().token; }
    BackendFacade& m_backend;
    PersonalFmSource* m_fm = nullptr; // injected source and facade must outlive this session
    domain::ListeningMode m_mode = domain::ListeningMode::Queue;
    std::optional<domain::Song> m_fmPending;
    quint64 m_fmScheduledRevision = 0;
    int m_fmAttempts = 0;
    bool m_fmWaiting = false, m_fmBlocked = false;
    PlaybackQueue m_queue;
    ListeningHistory m_history;
    LyricTimeline m_timeline;
    domain::PlaybackSnapshot m_playback;
    domain::LoadToken m_token;
    domain::SessionToken m_queueContext;
    domain::AudioQuality m_quality{};
    domain::TransportIntent m_intent{};
    domain::LyricsState m_lyricsState{};
    std::optional<domain::SongLyrics> m_lyrics;
    domain::LyricCursors m_cursors;
    qint64 m_userOffset = 0;
    quint64 m_lyricRequest = 0, m_revision = 0;
    QSet<QUuid> m_autoVisited;
    bool m_starting = false, m_auto = false, m_terminalHandled = false;
};
} // namespace cirrus::application
