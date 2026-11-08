#include "backend_playback.h"

#include "app_settings.h"
#include "music_requests.h"

#include "application/backend/backend_facade.h"
#include "application/playback/listening_session.h"
#include "infrastructure/cache/cached_music_query_service.h"
#include "infrastructure/runtime/backend_runtime.h"

#include <QTimer>

#include <algorithm>

namespace cirrus::ui
{
namespace
{

using application::ListeningSession;
using domain::PlaybackState;

// Positions within this of the clock's extrapolation don't notify.
constexpr qint64 kDriftMs = 150;
// Details kept of songs that played.
constexpr qsizetype kDetailsKept = 256;

domain::AudioQuality quality()
{
    const int value = AppSettings().audioQuality();
    return domain::AudioQuality(
        std::clamp(value, int(domain::AudioQuality::Standard), int(domain::AudioQuality::HiRes)));
}

failure::Kind kindOf(domain::PlaybackError error)
{
    using domain::PlaybackError;
    switch (error) {
    case PlaybackError::Network:
    case PlaybackError::Expired:
        return failure::Network;
    case PlaybackError::Timeout:
        return failure::Timeout;
    case PlaybackError::RateLimited:
        return failure::RateLimited;
    case PlaybackError::InvalidRequest:
        return failure::Invalid;
    case PlaybackError::Unavailable:
    case PlaybackError::ProtocolKeyMissing:
    case PlaybackError::InvalidResponse:
    case PlaybackError::UnsafeSource:
    case PlaybackError::TooLarge:
    case PlaybackError::UnsupportedFormat:
    case PlaybackError::Decode:
        return failure::Rejected;
    case PlaybackError::StaleContext:
    case PlaybackError::Canceled:
    case PlaybackError::Storage:
    case PlaybackError::NoOutput:
        break;
    }
    return failure::Unknown;
}

playback::Status statusOf(const domain::PlaybackSnapshot& play)
{
    switch (play.state) {
    case PlaybackState::Empty:
        return playback::Empty;
    case PlaybackState::Resolving:
    case PlaybackState::Loading:
        return playback::Loading;
    case PlaybackState::Ready:
        // Loaded; playing starts unless it was paused meanwhile.
        return play.intent == domain::TransportIntent::Play ? playback::Loading : playback::Paused;
    case PlaybackState::Playing:
        return playback::Playing;
    case PlaybackState::Paused:
        return playback::Paused;
    case PlaybackState::Ended:
        return playback::Ended;
    case PlaybackState::Failed:
        return playback::Failed;
    }
    return playback::Empty;
}

// Where the snapshot's clock is at `now`.
qint64 positionAt(const PlaybackSnapshot& s, qint64 now)
{
    const qint64 at = s.positionMs + qint64(double(now - s.positionAt) * s.rate);
    return s.durationMs >= 0 ? std::min(at, s.durationMs) : at;
}

// Everything but the clock; `current` by key (its details notify on their own).
bool sameState(const PlaybackSnapshot& a, const PlaybackSnapshot& b)
{
    const auto failureKind = [](const PlaybackSnapshot& s) {
        return s.failure ? int(s.failure->kind) : -1;
    };
    return a.status == b.status && a.current.key == b.current.key &&
           failureKind(a) == failureKind(b) && a.rate == b.rate && a.durationMs == b.durationMs &&
           a.seekable == b.seekable && a.previewStartMs == b.previewStartMs &&
           a.volume == b.volume && a.order == b.order && a.repeat == b.repeat &&
           a.canNext == b.canNext && a.canPrevious == b.canPrevious && a.mode == b.mode &&
           a.fmWaiting == b.fmWaiting && a.queue == b.queue && a.queueIndex == b.queueIndex;
}

std::optional<LyricTrack> trackOf(const domain::LyricTrack& track)
{
    if (track.availability != domain::LyricAvailability::Present)
        return std::nullopt;
    LyricTrack out;
    switch (track.timing) {
    case domain::LyricTiming::PlainText:
        out.timing = LyricTrack::PlainText;
        break;
    case domain::LyricTiming::Lines:
        out.timing = LyricTrack::Lines;
        break;
    case domain::LyricTiming::Words:
        out.timing = LyricTrack::Words;
        break;
    }
    out.offsetMs = track.providerOffsetMs;
    out.lines.reserve(track.lines.size());
    for (const auto& line : track.lines) {
        LyricLine l{line.startMs.value_or(-1), line.endMs.value_or(-1), line.text, {}};
        l.words.reserve(line.words.size());
        for (const auto& word : line.words)
            l.words.append({word.startMs, word.startMs + word.durationMs, word.text});
        out.lines.append(std::move(l));
    }
    return out;
}

LanguageLyrics languageOf(const domain::LyricVersions& versions)
{
    return {trackOf(versions.standard), trackOf(versions.enhanced)};
}

SongLyrics lyricsOf(const domain::SongLyrics& lyrics)
{
    return {languageOf(lyrics.original), languageOf(lyrics.translation),
            languageOf(lyrics.romanization), lyrics.providerNoLyric.value_or(false),
            lyrics.instrumental.value_or(false)};
}

template <class T> Request replyLater(QObject* context, Reply<T> reply, Outcome<T> outcome)
{
    auto alive = std::make_shared<bool>(true);
    QTimer::singleShot(0, context, [alive, reply = std::move(reply), outcome = std::move(outcome)] {
        if (*alive)
            reply(outcome);
    });
    return Request([alive] { *alive = false; });
}

} // namespace

BackendPlayback::BackendPlayback(runtime::BackendRuntime& runtime, CatalogSource& catalog,
                                 QObject* parent)
    : QObject(parent), m_session(&runtime.listening()), m_catalog(catalog)
{
    const auto changed = [this] { update(); };
    connect(m_session, &ListeningSession::queueChanged, this, changed);
    connect(m_session, &ListeningSession::playbackChanged, this, changed);
    connect(m_session, &ListeningSession::fmChanged, this, changed);
    // A session change retires the queue and playback.
    connect(&runtime.facade(), &application::BackendFacade::sessionChanged, this, changed);
    connect(&runtime.facade(), &application::BackendFacade::stateChanged, this, changed);
}

BackendPlayback::~BackendPlayback() = default;

void BackendPlayback::update()
{
    if (!m_session)
        return;
    const ListeningSession& session = *m_session;
    const auto play = session.playback();
    const qint64 now = playback::now();
    const bool newLoad = !(play.token == m_load);

    PlaybackSnapshot next;
    next.mode = session.mode() == domain::ListeningMode::PersonalFm ? playback::PersonalFm
                                                                    : playback::Queue;
    next.fmWaiting = session.waitingForFm();
    if (next.mode == playback::Queue) {
        m_fmSongs.clear();
        const auto queue = session.queue();
        QHash<QUuid, QString> songs;
        for (const auto& entry : queue.entries)
            songs.insert(entry.id, entry.song.value);
        m_entries = session.playOrder();
        for (const QUuid& id : std::as_const(m_entries))
            next.queue.append(songs.value(id));
        next.queueIndex = int(m_entries.indexOf(queue.active));
        next.order =
            queue.order == domain::QueueOrder::Shuffle ? playback::Shuffle : playback::Sequential;
        next.repeat = queue.repeat == domain::QueueRepeat::All   ? playback::RepeatAll
                      : queue.repeat == domain::QueueRepeat::One ? playback::RepeatOne
                                                                 : playback::RepeatOff;
    } else {
        m_entries.clear();
        if (newLoad && !play.song.value.isEmpty())
            m_fmSongs.append(play.song.value);
        next.queue = m_fmSongs;
        next.queueIndex = int(m_fmSongs.size()) - 1;
        // The queue's modes stay for when it comes back.
        next.order = m_state.order;
        next.repeat = m_state.repeat;
    }

    next.status = statusOf(play);
    if (next.status == playback::Empty && next.fmWaiting)
        next.status = playback::Loading;
    if (next.status == playback::Failed && play.failure)
        next.failure = Failure{kindOf(play.failure->error), std::nullopt};
    const QString key = play.song.value;
    if (!key.isEmpty() && next.status != playback::Empty) {
        const auto known = m_details.constFind(key);
        if (known != m_details.cend()) {
            next.current = *known;
        } else {
            next.current.key = key;
            fetchDetails(key);
        }
    }

    next.durationMs = play.durationMs.value_or(-1);
    next.seekable = play.seekable;
    next.previewStartMs = play.preview ? play.preview->startMs : -1;
    next.volume = m_volume.value_or(play.volumeLinear.value_or(m_state.volume));
    if (newLoad && m_volume && play.volumeLinear && std::abs(*play.volumeLinear - *m_volume) > 1e-3)
        m_session->setVolume(float(*m_volume)); // set while nothing played
    next.canNext = session.canGoNext();
    next.canPrevious = session.canGoPrevious();

    // The clock: the session's base when it moved, else where ours is now.
    next.rate = next.status == playback::Playing ? 1 : 0;
    if (newLoad || play.positionRevision != m_positionRevision) {
        next.positionMs = play.positionMs;
        next.positionAt = newLoad ? now : session.positionObservedAtMs().value_or(now);
    } else {
        next.positionMs = positionAt(m_state, now);
        next.positionAt = now;
    }
    m_load = play.token;
    m_positionRevision = play.positionRevision;

    const bool changed = m_detailsArrived || !sameState(m_state, next) ||
                         std::abs(positionAt(m_state, now) - positionAt(next, now)) > kDriftMs;
    m_detailsArrived = false;
    m_state = std::move(next);
    if (changed)
        notify();
}

void BackendPlayback::fetchDetails(const QString& key)
{
    if (m_detailsKey == key)
        return; // asked (once per song, also when it failed)
    m_detailsKey = key;
    m_detailsRequest =
        m_catalog.songDetails({key}, [this](const Outcome<QList<SongInfo>>& outcome) {
            m_detailsRequest.release();
            if (!outcome.value)
                return;
            if (m_details.size() + outcome.value->size() > kDetailsKept)
                m_details.clear();
            for (const SongInfo& info : *outcome.value)
                m_details.insert(info.key, info);
            m_detailsArrived = true;
            update();
        });
}

void BackendPlayback::notify()
{
    if (m_notifyPending)
        return;
    m_notifyPending = true;
    QMetaObject::invokeMethod(
        this,
        [this] {
            m_notifyPending = false;
            if (m_changed)
                m_changed();
        },
        Qt::QueuedConnection);
}

QUuid BackendPlayback::entryAt(int index) const
{
    return index >= 0 && index < m_entries.size() ? m_entries.at(index) : QUuid();
}

void BackendPlayback::playSongs(const QStringList& keys, int index)
{
    if (!m_session || index < 0 || index >= keys.size())
        return;
    QList<domain::SongId> ids;
    qsizetype start = -1;
    for (qsizetype i = 0; i < keys.size(); ++i) {
        if (!domain::validMusicId(keys.at(i)))
            continue;
        if (i == index)
            start = ids.size();
        ids.append(domain::SongId{keys.at(i)});
    }
    if (start < 0)
        return;
    // The backend's queue holds so many; keep those around the one chosen.
    constexpr qsizetype most = domain::MaximumSavedQueueEntries;
    if (ids.size() > most) {
        const qsizetype from = std::clamp<qsizetype>(start - most / 2, 0, ids.size() - most);
        ids = ids.mid(from, most);
        start -= from;
    }
    m_session->replaceQueue(ids, start, quality());
}

void BackendPlayback::playPause()
{
    if (!m_session)
        return;
    ListeningSession& session = *m_session;
    const auto play = session.playback();
    switch (play.state) {
    case PlaybackState::Empty:
    case PlaybackState::Ended:
    case PlaybackState::Failed:
        // Again from the start (a failed one tries again).
        if (session.mode() == domain::ListeningMode::PersonalFm)
            session.resume();
        else if (const QUuid active = session.queue().active; !active.isNull())
            session.select(active);
        return;
    default:
        if (play.intent == domain::TransportIntent::Play)
            session.pause();
        else
            session.resume();
    }
}

void BackendPlayback::next()
{
    if (m_session)
        m_session->next();
}

void BackendPlayback::previous()
{
    if (m_session)
        m_session->previous();
}

void BackendPlayback::seek(qint64 positionMs)
{
    if (m_session)
        m_session->seek(std::max<qint64>(positionMs, 0));
}

void BackendPlayback::playAt(int index)
{
    if (const QUuid entry = entryAt(index); m_session && !entry.isNull())
        m_session->select(entry);
}

void BackendPlayback::removeAt(int index)
{
    if (const QUuid entry = entryAt(index); m_session && !entry.isNull())
        m_session->remove(entry);
}

void BackendPlayback::setVolume(double volume)
{
    m_volume = std::clamp(volume, 0.0, 1.0);
    if (m_session)
        m_session->setVolume(float(*m_volume));
    update();
}

void BackendPlayback::setOrder(playback::Order order)
{
    if (m_session)
        m_session->setModes(order == playback::Shuffle ? domain::QueueOrder::Shuffle
                                                       : domain::QueueOrder::Sequential,
                            m_session->queue().repeat);
}

void BackendPlayback::setRepeat(playback::Repeat repeat)
{
    if (m_session)
        m_session->setModes(m_session->queue().order,
                            repeat == playback::RepeatAll   ? domain::QueueRepeat::All
                            : repeat == playback::RepeatOne ? domain::QueueRepeat::One
                                                            : domain::QueueRepeat::Off);
}

void BackendPlayback::startFm()
{
    if (m_session)
        m_session->startFm(quality());
}

struct BackendLyrics::Job {
    QString key;
    Reply<SongLyrics> reply;
    quint64 query = 0; // the cache's
    Request service;   // the service's
    void finish(const Outcome<SongLyrics>& outcome)
    {
        if (auto r = std::exchange(reply, {}))
            r(outcome);
    }
};

BackendLyrics::BackendLyrics(runtime::BackendRuntime& runtime, QObject* parent)
    : QObject(parent), m_session(&runtime.listening()), m_queries(runtime.resourceQueries()),
      m_requests(new MusicRequests(runtime.facade(), this))
{
    if (m_queries)
        connect(m_queries, &cache::CachedMusicQueryService::completed, this,
                &BackendLyrics::completed);
    connect(m_session, &ListeningSession::lyricsChanged, this, &BackendLyrics::sessionLyrics);
}

BackendLyrics::~BackendLyrics() = default;

Request BackendLyrics::lyrics(const QString& songKey, Reply<SongLyrics> reply)
{
    if (!domain::validMusicId(songKey))
        return replyLater(
            this, std::move(reply),
            Outcome<SongLyrics>{std::nullopt, false, Failure{failure::Rejected, std::nullopt}});
    // The playing song's are already here, or on their way.
    if (const auto own = m_session ? m_session->lyrics() : std::nullopt;
        own && own->song.value == songKey)
        return replyLater(this, std::move(reply), Outcome<SongLyrics>{lyricsOf(*own)});
    auto job = std::make_shared<Job>(Job{songKey, std::move(reply)});
    if (m_session && m_session->playback().song.value == songKey &&
        m_session->lyricsState() == domain::LyricsState::Loading)
        m_waiting.append(job);
    else
        start(job);
    return Request([self = QPointer<BackendLyrics>(this), job] {
        job->reply = {};
        job->service.cancel();
        if (!self)
            return;
        self->m_waiting.removeOne(job);
        if (job->query && self->m_pending.remove(job->query) && self->m_queries)
            self->m_queries->cancel(job->query);
    });
}

void BackendLyrics::sessionLyrics()
{
    const auto own = m_session ? m_session->lyrics() : std::nullopt;
    const bool loading = m_session && m_session->lyricsState() == domain::LyricsState::Loading;
    const QString playing = m_session ? m_session->playback().song.value : QString();
    for (const auto& job : std::exchange(m_waiting, {})) {
        if (own && own->song.value == job->key)
            job->finish(Outcome<SongLyrics>{lyricsOf(*own)});
        else if (loading && playing == job->key)
            m_waiting.append(job);
        else
            start(job); // the session's failed, or it moved on
    }
}

void BackendLyrics::start(const std::shared_ptr<Job>& job)
{
    job->query =
        m_queries ? m_queries->lyrics(domain::SongId{job->key}, domain::CacheReadMode::PreferCache)
                  : 0;
    if (job->query)
        m_pending.insert(job->query, job);
    else
        fromService(job); // no cache, or it's busy
}

void BackendLyrics::completed(const cache::CachedMusicResult& result)
{
    const auto job = m_pending.take(result.id);
    if (!job)
        return;
    job->query = 0;
    if (result.value) {
        if (const auto* lyrics = std::get_if<domain::SongLyrics>(&*result.value)) {
            job->finish(Outcome<SongLyrics>{lyricsOf(*lyrics)});
            return;
        }
        if (const auto* failed = std::get_if<application::MusicFailure>(&*result.value)) {
            if (const auto kind = MusicRequests::kindOf(failed->error)) {
                job->finish(
                    Outcome<SongLyrics>{std::nullopt, false, Failure{*kind, failed->retryAfterMs}});
                return;
            }
        }
    }
    fromService(job); // overtaken by a session change: the service waits for the next
}

void BackendLyrics::fromService(const std::shared_ptr<Job>& job)
{
    const domain::SongId id{job->key};
    job->service = m_requests->submit(
        [id](application::BackendFacade& facade) { return facade.lyrics(id); },
        [job](const application::MusicResult& result) {
            if (const auto* lyrics = std::get_if<domain::SongLyrics>(&result))
                job->finish(Outcome<SongLyrics>{lyricsOf(*lyrics)});
            else
                job->finish(Outcome<SongLyrics>{std::nullopt, false,
                                                Failure{failure::Rejected, std::nullopt}});
        },
        [job](failure::Kind kind, std::optional<qint64> retryAfterMs) {
            job->finish(Outcome<SongLyrics>{std::nullopt, false, Failure{kind, retryAfterMs}});
        });
}

} // namespace cirrus::ui
