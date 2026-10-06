#pragma once

#include "infrastructure/netease/protocol/official_route.h"
#include "infrastructure/netease/protocol/response_decoder.h"

namespace cirrus::netease
{

// Closed native registry. No string-based forwarding from application/QML.
// AnonymousRegister (xeapi): username is derived from the client's device ID;
// its MUSIC_A cookie is NOT staged until the anonymous credential scope exists.
enum class ApiOperation {
    QrKey,
    QrCheck,
    AccountIdentity,
    AnonymousRegister,
    SearchSongs,
    SongDetails,
    SongLyrics,
    SongStream,
    AlbumDetails,
    ArtistDetails,
    ArtistSongs,
    ArtistAlbums,
    PlaylistDetails,
    DailyRecommendations,
    PersonalFm,
    MyPlaylists,
    LikedSongIds,
    SavedAlbums,
    SavedArtists,
    ListeningStart,
    ListeningPlay,
    RecentSongs,
    ListeningRanking,
    Charts,
    ChartsWithPreviews,
    RecommendedPlaylists,
    PlaylistList,
    PremiumPlaylists,
    PlaylistCatalogue,
    HotPlaylistTags,
    PremiumPlaylistTags,
    SearchAlbums,
    SearchArtists,
    SearchPlaylists,
    SearchPodcasts,
    SearchVoices,
    PodcastDetails,
    PodcastPrograms,
    ProgramDetails,
    RecentPlaylists,
    RecentAlbums,
    RecentPodcasts,
    RecentVoices,
    SongComments,
    AlbumComments,
    PlaylistComments,
    ProgramComments,
    HotComments,
    SortedComments,
    CommentFloor,
    CommentInfo,
    SubscribedPodcasts,
    CloudSongs,
    CloudSongDetails
};

struct ApiOperationContract {
    QString logicalPath;
    Protocol protocol = Protocol::Eapi;
    ResponseEncoding responseEncoding = ResponseEncoding::Json;
    qsizetype responseLimit = 64 * 1024;
    OfficialDestination destination = OfficialDestination::Default;
};

[[nodiscard]] std::optional<ApiOperationContract>
operationContract(ApiOperation operation, const QJsonObject& parameters = {});
[[nodiscard]] bool requiresAccountCookie(ApiOperation operation);
[[nodiscard]] bool validQrKey(QStringView key);
[[nodiscard]] std::optional<QJsonObject> operationPayload(ApiOperation operation,
                                                          const QJsonObject& parameters);
// Only recognized operation outcomes may stage response cookies. Not proof of
// account identity; in particular QR 803 still requires identity verification.
[[nodiscard]] bool acceptsResponseCookies(ApiOperation operation, const DecodedResponse& response);

} // namespace cirrus::netease
