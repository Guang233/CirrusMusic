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
    invalidateListeningReports(domain::ReportError::Canceled);
    invalidateCollections(domain::CollectionError::Canceled);
    invalidateCommentWrites(domain::CommentWriteError::Canceled);
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
        invalidateListeningReports(domain::ReportError::StaleContext);
        invalidateCollections(domain::CollectionError::StaleContext);
        invalidateCommentWrites(domain::CommentWriteError::StaleContext);
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
        if (m_snapshot.token != event.session.token ||
            m_snapshot.accountId != event.session.accountId ||
            event.session.phase != domain::SessionPhase::Authenticated) {
            invalidateListeningReports(domain::ReportError::StaleContext);
            invalidateCollections(domain::CollectionError::StaleContext);
            invalidateCommentWrites(domain::CommentWriteError::StaleContext);
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
quint64 BackendFacade::artistIntroduction(domain::ArtistId artist)
{
    return submitMusic(domain::ArtistIntroductionQuery{std::move(artist)});
}
quint64 BackendFacade::songWikiSummary(domain::SongId song)
{
    return submitMusic(domain::SongWikiQuery{std::move(song)});
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

quint64 BackendFacade::charts(domain::ChartListKind kind)
{
    return submitMusic(domain::ChartsQuery{kind});
}

quint64 BackendFacade::recommendedPlaylists(int limit)
{
    return submitMusic(domain::RecommendedPlaylistsQuery{limit});
}
quint64 BackendFacade::playlistList(domain::PlaylistListQuery query)
{
    return submitMusic(std::move(query));
}
quint64 BackendFacade::premiumPlaylists(domain::PremiumPlaylistsQuery query)
{
    return submitMusic(std::move(query));
}
quint64 BackendFacade::playlistTags(domain::PlaylistTagsKind kind)
{
    return submitMusic(domain::PlaylistTagsQuery{kind});
}

quint64 BackendFacade::searchAlbums(domain::SearchPage query)
{
    return submitMusic(domain::EntitySearchQuery{domain::MusicSearchKind::Album, std::move(query)});
}
quint64 BackendFacade::searchArtists(domain::SearchPage query)
{
    return submitMusic(
        domain::EntitySearchQuery{domain::MusicSearchKind::Artist, std::move(query)});
}
quint64 BackendFacade::searchPlaylists(domain::SearchPage query)
{
    return submitMusic(
        domain::EntitySearchQuery{domain::MusicSearchKind::Playlist, std::move(query)});
}
quint64 BackendFacade::searchPodcasts(domain::SearchPage query)
{
    return submitMusic(domain::PodcastSearchQuery{std::move(query)});
}
quint64 BackendFacade::searchVoices(domain::SearchPage query)
{
    return submitMusic(domain::VoiceSearchQuery{std::move(query)});
}

quint64 BackendFacade::podcastDetails(domain::PodcastId podcast)
{
    return submitMusic(domain::PodcastDetailQuery{std::move(podcast)});
}
quint64 BackendFacade::podcastPrograms(domain::PodcastProgramsQuery query)
{
    return submitMusic(std::move(query));
}
quint64 BackendFacade::programDetails(domain::ProgramId program)
{
    return submitMusic(domain::ProgramDetailQuery{std::move(program)});
}

quint64 BackendFacade::comments(domain::CommentsQuery query)
{
    return submitMusic(std::move(query));
}
quint64 BackendFacade::hotComments(domain::HotCommentsQuery query)
{
    return submitMusic(std::move(query));
}
quint64 BackendFacade::sortedComments(domain::SortedCommentsQuery query)
{
    return submitMusic(std::move(query));
}
quint64 BackendFacade::commentFloor(domain::CommentFloorQuery query)
{
    return submitMusic(std::move(query));
}
quint64 BackendFacade::commentInfo(domain::CommentInfoQuery query)
{
    return submitMusic(std::move(query));
}

quint64 BackendFacade::dailyRecommendations()
{
    return submitMusic(domain::DailyRecommendationsQuery{});
}
quint64 BackendFacade::personalFmBatch() { return submitMusic(domain::PersonalFmQuery{}); }

quint64 BackendFacade::myPlaylists(domain::CatalogPage page)
{
    return submitMusic(domain::MyPlaylistsQuery{m_snapshot.accountId, page});
}
quint64 BackendFacade::likedSongIds()
{
    return submitMusic(domain::LikedSongIdsQuery{m_snapshot.accountId});
}
quint64 BackendFacade::savedAlbums(domain::CatalogPage page)
{
    return submitMusic(domain::SavedAlbumsQuery{m_snapshot.accountId, page});
}
quint64 BackendFacade::savedArtists(domain::CatalogPage page)
{
    return submitMusic(domain::SavedArtistsQuery{m_snapshot.accountId, page});
}
quint64 BackendFacade::subscribedPodcasts(domain::CatalogPage page)
{
    return submitMusic(domain::SubscribedPodcastsQuery{m_snapshot.accountId, page});
}
quint64 BackendFacade::cloudSongs(domain::CatalogPage page)
{
    return submitMusic(domain::CloudSongsQuery{m_snapshot.accountId, page});
}
quint64 BackendFacade::cloudSongDetails(QList<domain::CloudSongId> songs)
{
    return submitMusic(domain::CloudSongDetailsQuery{m_snapshot.accountId, std::move(songs)});
}
quint64 BackendFacade::recentSongs(int limit)
{
    return submitMusic(domain::RecentSongsQuery{m_snapshot.accountId, limit});
}
quint64 BackendFacade::recentPlaylists(int limit)
{
    return submitMusic(domain::RecentEntitiesQuery{m_snapshot.accountId,
                                                   domain::RecentEntityKind::Playlist, limit});
}
quint64 BackendFacade::recentAlbums(int limit)
{
    return submitMusic(
        domain::RecentEntitiesQuery{m_snapshot.accountId, domain::RecentEntityKind::Album, limit});
}
quint64 BackendFacade::recentPodcasts(int limit)
{
    return submitMusic(domain::RecentEntitiesQuery{m_snapshot.accountId,
                                                   domain::RecentEntityKind::Podcast, limit});
}
quint64 BackendFacade::recentVoices(int limit)
{
    return submitMusic(
        domain::RecentEntitiesQuery{m_snapshot.accountId, domain::RecentEntityKind::Voice, limit});
}
quint64 BackendFacade::listeningRanking(domain::RankingPeriod period)
{
    return submitMusic(domain::ListeningRankingQuery{m_snapshot.accountId, period});
}
quint64 BackendFacade::submitMusic(MusicQuery query)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_state != BackendState::Ready || !m_snapshot.token.isValid() || m_music.size() >= 16 ||
        !validMusicQuery(query) ||
        (requiresAuthenticatedMusicQuery(query) &&
         m_snapshot.phase != domain::SessionPhase::Authenticated) ||
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

quint64 BackendFacade::reportListening(domain::ListeningReport report)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_state != BackendState::Ready || !m_snapshot.token.isValid() ||
        m_snapshot.phase != domain::SessionPhase::Authenticated ||
        !domain::validMusicId(m_snapshot.accountId) || !report.isValid() || m_reports.size() >= 8)
        return 0;
    for (const auto& pending : std::as_const(m_reports))
        if (pending.report.eventId == report.eventId && pending.report.action == report.action)
            return 0;
    const auto id = ++m_nextId;
    const ListeningReportCommand command{id, m_generation, m_snapshot.token, m_snapshot.accountId,
                                         report};
    m_reports.insert(id, command);
    emit listeningReportRequested(command);
    return id;
}
void BackendFacade::cancelListeningReportWith(quint64 id, domain::ReportError error)
{
    if (!m_reports.contains(id))
        return;
    const auto command = m_reports.take(id);
    emit listeningReportCancelRequested(id);
    // The worker may already have sent it; do not claim cancellation undid a write.
    const domain::ListeningReportResult result{command.report.eventId, command.report.action,
                                               domain::ReportOutcome::Unknown, error};
    QTimer::singleShot(0, this, [this, id, context = command.context, result] {
        emit listeningReportCompleted(id, context, result);
    });
}
void BackendFacade::cancelListeningReport(quint64 id)
{
    Q_ASSERT(QThread::currentThread() == thread());
    cancelListeningReportWith(id, domain::ReportError::Canceled);
}
void BackendFacade::invalidateListeningReports(domain::ReportError error)
{
    for (auto id : m_reports.keys())
        cancelListeningReportWith(id, error);
}
void BackendFacade::deliverListeningReport(ListeningReportDelivery delivery)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!m_reports.contains(delivery.id))
        return;
    const auto expected = m_reports.take(delivery.id);
    if (delivery.generation != expected.generation || delivery.context != expected.context ||
        expected.generation != m_generation || expected.context != m_snapshot.token ||
        expected.accountId != m_snapshot.accountId || m_state != BackendState::Ready) {
        delivery.result = {expected.report.eventId, expected.report.action,
                           domain::ReportOutcome::Unknown, domain::ReportError::StaleContext};
    }
    delivery.result.eventId = expected.report.eventId;
    delivery.result.action = expected.report.action;
    emit listeningReportCompleted(expected.id, expected.context, delivery.result);
}

quint64 BackendFacade::mutateCollection(domain::CollectionMutation mutation)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_state != BackendState::Ready || !m_snapshot.token.isValid() ||
        m_snapshot.phase != domain::SessionPhase::Authenticated ||
        !domain::validMusicId(m_snapshot.accountId) || !mutation.isValid() ||
        m_collections.size() >= 8)
        return 0;
    const auto sourceContext = domain::collectionSourceContext(mutation);
    const auto sourceAccount = domain::collectionSourceAccount(mutation);
    if ((sourceContext && *sourceContext != m_snapshot.token) ||
        (sourceAccount && *sourceAccount != m_snapshot.accountId))
        return 0;
    for (const auto& pending : std::as_const(m_collections))
        if (pending.mutation.eventId == mutation.eventId)
            return 0;
    const auto id = ++m_nextId;
    const CollectionCommand command{id, m_generation, m_snapshot.token, m_snapshot.accountId,
                                    std::move(mutation)};
    m_collections.insert(id, command);
    emit collectionRequested(command);
    return id;
}
void BackendFacade::cancelCollectionWith(quint64 id, domain::CollectionError error)
{
    if (!m_collections.contains(id))
        return;
    const auto command = m_collections.take(id);
    emit collectionCancelRequested(id);
    domain::CollectionResult result{command.mutation.eventId, domain::CollectionOutcome::Unknown,
                                    error};
    if (std::holds_alternative<domain::PlaylistCoverReplace>(command.mutation.change))
        result.coverStep = domain::CoverStep::Unobserved;
    QTimer::singleShot(0, this, [this, id, context = command.context, result] {
        emit collectionCompleted(id, context, result);
    });
}
void BackendFacade::cancelCollection(quint64 id)
{
    Q_ASSERT(QThread::currentThread() == thread());
    cancelCollectionWith(id, domain::CollectionError::Canceled);
}
void BackendFacade::invalidateCollections(domain::CollectionError error)
{
    for (const auto id : m_collections.keys())
        cancelCollectionWith(id, error);
}
void BackendFacade::deliverCollection(CollectionDelivery delivery)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!m_collections.contains(delivery.id))
        return;
    const auto expected = m_collections.take(delivery.id);
    if (delivery.generation != expected.generation || delivery.context != expected.context ||
        expected.generation != m_generation || expected.context != m_snapshot.token ||
        expected.accountId != m_snapshot.accountId || m_state != BackendState::Ready ||
        m_snapshot.phase != domain::SessionPhase::Authenticated)
        delivery.result = {expected.mutation.eventId, domain::CollectionOutcome::Unknown,
                           domain::CollectionError::StaleContext};
    const bool creation = std::holds_alternative<domain::PlaylistCreate>(expected.mutation.change);
    if (creation && delivery.result.outcome == domain::CollectionOutcome::Acknowledged &&
        (!delivery.result.createdPlaylist ||
         !domain::validMusicId(delivery.result.createdPlaylist->value))) {
        delivery.result.outcome = domain::CollectionOutcome::Unknown;
        delivery.result.error = domain::CollectionError::InvalidResponse;
    }
    if (delivery.result.outcome != domain::CollectionOutcome::Acknowledged || !creation)
        delivery.result.createdPlaylist.reset();
    const bool cover =
        std::holds_alternative<domain::PlaylistCoverReplace>(expected.mutation.change);
    if (cover) {
        if (!delivery.result.coverStep ||
            *delivery.result.coverStep < domain::CoverStep::Unobserved ||
            *delivery.result.coverStep > domain::CoverStep::Committing)
            delivery.result.coverStep = domain::CoverStep::Unobserved;
        if (delivery.result.outcome == domain::CollectionOutcome::Acknowledged &&
            delivery.result.coverStep != domain::CoverStep::Committing) {
            delivery.result.outcome = domain::CollectionOutcome::Unknown;
            delivery.result.error = domain::CollectionError::InvalidResponse;
        }
    } else
        delivery.result.coverStep.reset();
    delivery.result.eventId = expected.mutation.eventId;
    emit collectionCompleted(expected.id, expected.context, delivery.result);
}

quint64 BackendFacade::mutateComment(domain::CommentMutation mutation)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_state != BackendState::Ready || !m_snapshot.token.isValid() ||
        m_snapshot.phase != domain::SessionPhase::Authenticated ||
        !domain::validMusicId(m_snapshot.accountId) || !mutation.isValid() ||
        m_commentWrites.size() >= 8)
        return 0;
    for (const auto& p : std::as_const(m_commentWrites))
        if (p.mutation.eventId == mutation.eventId)
            return 0;
    const auto id = ++m_nextId;
    const CommentWriteCommand c{id, m_generation, m_snapshot.token, m_snapshot.accountId,
                                std::move(mutation)};
    m_commentWrites.insert(id, c);
    emit commentWriteRequested(c);
    return id;
}
void BackendFacade::cancelCommentWith(quint64 id, domain::CommentWriteError error)
{
    if (!m_commentWrites.contains(id))
        return;
    const auto c = m_commentWrites.take(id);
    emit commentWriteCancelRequested(id);
    const domain::CommentWriteResult r{c.mutation.eventId, domain::CommentWriteOutcome::Unknown,
                                       error};
    QTimer::singleShot(0, this, [this, id, context = c.context, r] {
        emit commentWriteCompleted(id, context, r);
    });
}
void BackendFacade::cancelComment(quint64 id)
{
    Q_ASSERT(QThread::currentThread() == thread());
    cancelCommentWith(id, domain::CommentWriteError::Canceled);
}
void BackendFacade::invalidateCommentWrites(domain::CommentWriteError error)
{
    for (const auto id : m_commentWrites.keys())
        cancelCommentWith(id, error);
}
void BackendFacade::deliverCommentWrite(CommentWriteDelivery d)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!m_commentWrites.contains(d.id))
        return;
    const auto c = m_commentWrites.take(d.id);
    if (d.generation != c.generation || d.context != c.context || c.generation != m_generation ||
        c.context != m_snapshot.token || c.accountId != m_snapshot.accountId ||
        m_state != BackendState::Ready || m_snapshot.phase != domain::SessionPhase::Authenticated)
        d.result = {c.mutation.eventId, domain::CommentWriteOutcome::Unknown,
                    domain::CommentWriteError::StaleContext};
    if (d.result.step < domain::CommentWriteStep::Unobserved ||
        d.result.step > domain::CommentWriteStep::Writing)
        d.result.step = domain::CommentWriteStep::Unobserved;
    if (d.result.outcome == domain::CommentWriteOutcome::Acknowledged &&
        d.result.step != domain::CommentWriteStep::Writing)
        d.result = {c.mutation.eventId, domain::CommentWriteOutcome::Unknown,
                    domain::CommentWriteError::InvalidResponse};
    d.result.eventId = c.mutation.eventId;
    emit commentWriteCompleted(c.id, c.context, d.result);
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
