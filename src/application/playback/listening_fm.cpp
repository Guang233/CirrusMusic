#include "application/music/personal_fm_source.h"
#include "listening_session.h"
#include <QThread>
#include <QTimer>

namespace cirrus::application
{
using namespace domain;
ListeningSession::ListeningSession(BackendFacade& backend, PersonalFmSource& fm, QObject* parent)
    : ListeningSession(backend, parent)
{
    Q_ASSERT(fm.thread() == thread());
    if (!fm.isBoundTo(backend))
        return; // never consume another backend/account's supply
    m_fm = &fm;
    connect(&fm, &PersonalFmSource::changed, this, [this] {
        if (m_mode == ListeningMode::PersonalFm) {
            if (!current())
                reset();
            else
                scheduleFm();
        }
    });
}
void ListeningSession::clearFmState()
{
    m_mode = ListeningMode::Queue;
    m_fmWaiting = m_fmBlocked = false;
    m_fmPending.reset();
    m_fmAttempts = 0;
    m_fmScheduledRevision = 0;
}
bool ListeningSession::startFm(AudioQuality quality, TransportIntent intent)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!m_fm || m_backend.state() != BackendState::Ready ||
        m_backend.snapshot().phase != SessionPhase::Authenticated ||
        !m_backend.snapshot().token.isValid() || qualityName(quality).isEmpty() ||
        (intent != TransportIntent::Play && intent != TransportIntent::Pause))
        return false;
    const auto revision = ++m_revision;
    m_starting = true;
    const auto id = m_backend.stopPlayback();
    m_starting = false;
    if (!acceptLoadCommand(id, revision))
        return false;
    clearFmState();
    m_mode = ListeningMode::PersonalFm;
    m_fmWaiting = true;
    m_queue.clear();
    m_token = m_backend.playback().token;
    m_playback = m_backend.playback();
    m_queueContext = m_token.session;
    m_quality = quality;
    m_intent = intent;
    m_playback.intent = intent;
    m_auto = false;
    m_autoVisited.clear();
    m_terminalHandled = true;
    cancelLyrics();
    if (revision != m_revision)
        return true;
    m_fm->stop();
    if (revision != m_revision)
        return true;
    if (!m_fm->start())
        blockFm();
    if (revision != m_revision)
        return true;
    emit queueChanged(queue());
    if (revision != m_revision)
        return true;
    emit playbackChanged(m_playback);
    if (revision != m_revision)
        return true;
    emit lyricsChanged(m_lyricsState);
    if (revision != m_revision)
        return true;
    refreshCursors();
    if (revision != m_revision)
        return true;
    emit fmChanged();
    if (revision == m_revision)
        scheduleFm();
    return true;
}
bool ListeningSession::enterFmWait(bool automatic, bool stopMedia)
{
    if (!m_fm || m_mode != ListeningMode::PersonalFm || !current())
        return false;
    const auto revision = ++m_revision;
    if (stopMedia) {
        m_starting = true;
        const auto id = m_backend.stopPlayback();
        m_starting = false;
        if (!acceptLoadCommand(id, revision))
            return false;
        m_token = m_backend.playback().token;
        m_playback = m_backend.playback();
        m_playback.intent = m_intent;
    }
    m_fmWaiting = true;
    m_fmBlocked = false;
    m_fmPending.reset();
    m_auto = automatic;
    if (!automatic)
        m_fmAttempts = 0;
    m_terminalHandled = true;
    cancelLyrics();
    if (revision != m_revision)
        return true;
    emit playbackChanged(m_playback);
    if (revision != m_revision)
        return true;
    emit lyricsChanged(m_lyricsState);
    if (revision != m_revision)
        return true;
    refreshCursors();
    if (revision != m_revision)
        return true;
    emit fmChanged();
    if (revision == m_revision)
        scheduleFm();
    return true;
}
void ListeningSession::scheduleFm()
{
    if (m_mode != ListeningMode::PersonalFm || !current() || !m_fmWaiting || m_fmBlocked ||
        m_fmScheduledRevision == m_revision)
        return;
    const auto revision = m_revision;
    m_fmScheduledRevision = revision;
    QTimer::singleShot(0, this, [this, revision] {
        if (m_fmScheduledRevision == revision)
            m_fmScheduledRevision = 0;
        if (revision == m_revision)
            consumeFm();
    });
}
void ListeningSession::blockFm()
{
    if (m_fmBlocked)
        return;
    m_fmBlocked = true;
    const auto revision = m_revision;
    emit fmChanged();
    if (revision == m_revision)
        emit advanceStopped(true);
}
void ListeningSession::consumeFm()
{
    if (!m_fm || m_mode != ListeningMode::PersonalFm || !current() || !m_fmWaiting || m_fmBlocked)
        return;
    if (m_auto && m_fmAttempts >= 5) {
        blockFm();
        return;
    }
    auto revision = m_revision;
    if (!m_fmPending) {
        auto song = m_fm->takeNext();
        if (revision != m_revision || !current())
            return;
        if (!song) {
            if (!m_fm->snapshot().fetching)
                blockFm();
            return;
        }
        m_fmPending = std::move(song);
    }
    auto candidate = m_queue;
    candidate.replace({m_fmPending->id});
    candidate.select(candidate.snapshot().entries.first().id);
    const auto intent = m_intent == TransportIntent::Pause ? m_intent : TransportIntent::Play;
    // Keep the taken item until local Play acceptance. Busy must not silently lose a song.
    if (!begin(std::move(candidate), intent, m_quality, m_auto, true) && m_revision == revision &&
        current() && m_mode == ListeningMode::PersonalFm)
        blockFm();
}
bool ListeningSession::retryFm()
{
    if (!m_fm || m_mode != ListeningMode::PersonalFm || !current() || !m_fmWaiting)
        return false;
    const auto revision = ++m_revision;
    m_fmBlocked = false;
    m_auto = false; // explicit user retry starts a manual chain, never an endless failure loop
    m_fmAttempts = 0;
    const auto supply = m_fm->snapshot();
    if (!m_fmPending && supply.upcoming.isEmpty() && !supply.fetching) {
        if (!m_fm->retry())
            blockFm();
    }
    if (revision != m_revision)
        return true;
    emit fmChanged();
    if (revision == m_revision)
        scheduleFm();
    return true;
}
} // namespace cirrus::application
