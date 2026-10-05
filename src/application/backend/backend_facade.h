#pragma once

#include "application/account/qr_login_service.h"
#include "application/ports/music_gateway.h"
#include "domain/playback/listening_report.h"
#include "domain/playback/playback_types.h"
#include <QHash>

#include <QSet>

namespace cirrus::application
{

enum class BackendState { Dormant, Starting, Ready, Failed, Stopping, Stopped };
enum class StartupError { None, Configuration, Storage, GuestBootstrap };
enum class AccountAction { BeginLogin, Restore, Poll, Cancel, SignOut };
enum class PersistenceArea { ProtocolKey, AnonymousToken };

struct BackendCommand {
    quint64 id = 0;
    quint64 generation = 0;
    AccountAction action{};
    QrLoginOptions options;
};

struct MusicCommand {
    quint64 id = 0;
    quint64 generation = 0;
    domain::SessionToken context;
    MusicQuery query;
};
struct MusicDelivery {
    quint64 id = 0;
    quint64 generation = 0;
    domain::SessionToken context;
    MusicResult result;
};

struct ListeningReportCommand {
    quint64 id = 0, generation = 0;
    domain::SessionToken context;
    QString accountId;
    domain::ListeningReport report;
};
struct ListeningReportDelivery {
    quint64 id = 0, generation = 0;
    domain::SessionToken context;
    domain::ListeningReportResult result;
};

enum class PlaybackAction { Play, Pause, Resume, Stop, Seek, Volume };
struct PlaybackCommand {
    quint64 id = 0;
    quint64 generation = 0; // account intent barrier
    domain::LoadToken token;
    PlaybackAction action{};
    domain::StreamQuery query;
    qint64 positionMs = 0;
    float volume = 0.5f;
    domain::TransportIntent intent = domain::TransportIntent::Play;
};
struct PlaybackDelivery {
    quint64 generation = 0;
    domain::PlaybackSnapshot snapshot; // NEVER contains a URL or provider payload
};

// A value-only, internal queued delivery envelope. No infrastructure objects or
// credentials cross threads. Generation invalidates events already queued to UI.
struct BackendEvent {
    enum class Kind {
        Started,
        Session,
        Challenge,
        Status,
        SignedIn,
        AuthFailed,
        Saved,
        Acknowledged,
        StorageWarning
    };
    Kind kind{};
    quint64 generation = 0;
    quint64 commandId = 0;
    bool accepted = false;
    StartupError startupError{};
    PersistenceArea persistenceArea{};
    domain::SessionSnapshot session;
    domain::QrLoginChallenge challenge;
    domain::QrLoginStatus status{};
    domain::AccountIdentity identity;
    AuthFailure failure;
    SavedSessionResult saved = false;
};

// Caller/UI-thread asynchronous surface. No QML dependency and no synchronous
// cross-thread reads. Runtime installs queued signal/slot wiring before use.
// One-shot start/close. Command ID 0 means rejected locally (not ready/overload).
// Other IDs receive an acknowledgement, NOT proof that authentication succeeded.
// close() cancels unobserved acknowledgements with false; that is NOT evidence
// that a previously queued command had no side effects. Observe storage results.
class BackendFacade final : public QObject
{
    Q_OBJECT
  public:
    explicit BackendFacade(QObject* parent = nullptr);
    [[nodiscard]] BackendState state() const { return m_state; }
    [[nodiscard]] domain::SessionSnapshot snapshot() const { return m_snapshot; }
    void start();
    void close(); // does NOT sign out; keep event loop alive until stopped()
    [[nodiscard]] quint64 beginLogin(QrLoginOptions options = {});
    [[nodiscard]] quint64 restore();
    [[nodiscard]] quint64 poll();
    [[nodiscard]] quint64 cancel();
    [[nodiscard]] quint64 signOut();
    [[nodiscard]] quint64 searchSongs(domain::SongSearch query);
    [[nodiscard]] quint64 songDetails(QList<domain::SongId> songs);
    [[nodiscard]] quint64 lyrics(domain::SongId song);
    [[nodiscard]] quint64 albumDetails(domain::AlbumId album);
    [[nodiscard]] quint64 artistDetails(domain::ArtistId artist);
    [[nodiscard]] quint64 artistSongs(domain::ArtistSongsQuery query);
    [[nodiscard]] quint64 artistAlbums(domain::ArtistAlbumsQuery query);
    [[nodiscard]] quint64 playlistDetails(domain::PlaylistId playlist);
    [[nodiscard]] quint64 playlistSongs(domain::PlaylistSongsQuery query);
    [[nodiscard]] quint64 dailyRecommendations();
    [[nodiscard]] quint64 personalFmBatch(); // use one PersonalFmSource for dynamic consumption
    [[nodiscard]] quint64 myPlaylists(domain::CatalogPage page = {});
    [[nodiscard]] quint64 likedSongIds();
    [[nodiscard]] quint64 savedAlbums(domain::CatalogPage page = {25, 0});
    [[nodiscard]] quint64 savedArtists(domain::CatalogPage page = {25, 0});
    [[nodiscard]] quint64 recentSongs(int limit = 100);
    [[nodiscard]] quint64
    listeningRanking(domain::RankingPeriod period = domain::RankingPeriod::AllTime);
    void cancelMusic(quint64 id);
    [[nodiscard]] quint64
    reportListening(domain::ListeningReport report); // explicit write, NEVER called by playback
    void cancelListeningReport(quint64 id);
    [[nodiscard]] domain::PlaybackSnapshot playback() const { return m_playback; }
    [[nodiscard]] quint64 playSong(domain::SongId song,
                                   domain::AudioQuality quality = domain::AudioQuality::Standard,
                                   domain::TransportIntent intent = domain::TransportIntent::Play);
    [[nodiscard]] quint64 pausePlayback();
    [[nodiscard]] quint64 resumePlayback();
    [[nodiscard]] quint64 stopPlayback();
    [[nodiscard]] quint64 seekPlayback(qint64 positionMs);
    [[nodiscard]] quint64 setPlaybackVolume(float linear);

    // Runtime delivery endpoints; queued connections only, not public commands.
    void deliver(BackendEvent event);
    void finishClose(bool persistenceFailed);
    void deliverMusic(MusicDelivery delivery);
    void deliverPlayback(PlaybackDelivery delivery);
    void deliverListeningReport(ListeningReportDelivery delivery);

  signals:
    void stateChanged(cirrus::application::BackendState state);
    void started(
        cirrus::application::StartupError error); // GuestBootstrap = degraded, QR still available
    void
    stopped(bool persistenceFailed); // all jobs settled; false is not live backend certification
    void closeDelayed();             // once after 5s; no force termination or false stopped result
    void sessionChanged(cirrus::domain::SessionSnapshot snapshot);
    void challengeReady(cirrus::domain::SessionToken context,
                        cirrus::domain::QrLoginChallenge challenge);
    void statusChanged(cirrus::domain::SessionToken context, cirrus::domain::QrLoginStatus status);
    void signedIn(cirrus::domain::SessionToken context, cirrus::domain::AccountIdentity identity);
    void failed(cirrus::domain::SessionToken context, cirrus::application::AuthFailure failure);
    void savedSessionCompleted(cirrus::domain::SessionToken context,
                               cirrus::application::SavedSessionResult result);
    void acknowledged(quint64 commandId, bool accepted);
    void persistenceWarning(cirrus::application::PersistenceArea area);
    void musicCompleted(quint64 id, cirrus::domain::SessionToken context,
                        cirrus::application::MusicResult result);

    void listeningReportCompleted(quint64 id, cirrus::domain::SessionToken context,
                                  cirrus::domain::ListeningReportResult result);
    void playbackChanged(cirrus::domain::PlaybackSnapshot snapshot);

    // Composition wiring only; emitted on this object's thread.
    void startRequested();
    void closeRequested();
    void commandRequested(cirrus::application::BackendCommand command);
    void musicRequested(cirrus::application::MusicCommand command);
    void musicCancelRequested(quint64 id);
    void playbackRequested(cirrus::application::PlaybackCommand command);
    void listeningReportRequested(cirrus::application::ListeningReportCommand command);
    void listeningReportCancelRequested(quint64 id);

  private:
    void invalidateListeningReports(domain::ReportError error);
    void cancelListeningReportWith(quint64 id, domain::ReportError error);
    QHash<quint64, ListeningReportCommand> m_reports;
    quint64 submit(AccountAction action, QrLoginOptions options = {});
    void setState(BackendState state);
    quint64 submitMusic(MusicQuery query);
    quint64 submitPlayback(PlaybackCommand command);
    void invalidatePlayback();
    domain::PlaybackSnapshot m_playback;
    quint64 m_playbackGeneration = 0;
    void invalidateMusic(MusicError error);
    void cancelMusicWith(quint64 id, MusicError error);
    BackendState m_state = BackendState::Dormant;
    domain::SessionSnapshot m_snapshot;
    QSet<quint64> m_pending;
    QHash<quint64, MusicCommand> m_music;
    quint64 m_nextId = 0;
    quint64 m_generation = 0;
};

} // namespace cirrus::application

Q_DECLARE_METATYPE(cirrus::application::BackendState)
Q_DECLARE_METATYPE(cirrus::application::StartupError)
Q_DECLARE_METATYPE(cirrus::application::PersistenceArea)
Q_DECLARE_METATYPE(cirrus::application::BackendCommand)
Q_DECLARE_METATYPE(cirrus::application::BackendEvent)
Q_DECLARE_METATYPE(cirrus::application::MusicCommand)
Q_DECLARE_METATYPE(cirrus::application::MusicDelivery)
Q_DECLARE_METATYPE(cirrus::application::PlaybackCommand)
Q_DECLARE_METATYPE(cirrus::application::PlaybackDelivery)
Q_DECLARE_METATYPE(cirrus::application::ListeningReportCommand)
Q_DECLARE_METATYPE(cirrus::application::ListeningReportDelivery)
