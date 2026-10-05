#pragma once

#include "domain/account/session_types.h"
#include "domain/music/catalog_types.h"
#include "domain/music/chart_types.h"
#include "domain/music/cloud_history_types.h"
#include "domain/music/discovery_types.h"
#include "domain/music/entity_search_types.h"
#include "domain/music/library_types.h"
#include "domain/music/playlist_discovery_types.h"

#include <QObject>
#include <type_traits>
#include <variant>

namespace cirrus::application
{

enum class MusicError {
    InvalidRequest,
    Busy,
    Canceled,
    StaleContext,
    Network,
    Timeout,
    RateLimited,
    ProviderRejected,
    InvalidResponse,
    ProtocolFailure,
    IncompleteCatalog,
    AuthenticationRequired
};
struct MusicFailure {
    MusicError error{};
    std::optional<int> providerCode;
    std::optional<qint64> retryAfterMs;
};
struct DetailQuery {
    QList<domain::SongId> songs;
    [[nodiscard]] bool isValid() const
    {
        if (songs.isEmpty() || songs.size() > 100)
            return false;
        QStringList seen;
        for (const auto& id : songs) {
            if (!domain::validMusicId(id.value) || seen.contains(id.value))
                return false;
            seen.append(id.value);
        }
        return true;
    }
};
struct LyricQuery {
    domain::SongId song;
    [[nodiscard]] bool isValid() const { return domain::validMusicId(song.value); }
};
using MusicQuery =
    std::variant<domain::SongSearch, DetailQuery, LyricQuery, domain::AlbumQuery,
                 domain::ArtistQuery, domain::ArtistSongsQuery, domain::ArtistAlbumsQuery,
                 domain::PlaylistQuery, domain::PlaylistSongsQuery,
                 domain::DailyRecommendationsQuery, domain::PersonalFmQuery,
                 domain::MyPlaylistsQuery, domain::LikedSongIdsQuery, domain::SavedAlbumsQuery,
                 domain::SavedArtistsQuery, domain::RecentSongsQuery, domain::ListeningRankingQuery,
                 domain::ChartsQuery, domain::RecommendedPlaylistsQuery, domain::PlaylistListQuery,
                 domain::PremiumPlaylistsQuery, domain::PlaylistTagsQuery,
                 domain::EntitySearchQuery, domain::PodcastSearchQuery, domain::VoiceSearchQuery>;
using MusicResult = std::variant<
    domain::SongSearchPage, domain::SongDetails, domain::SongLyrics, MusicFailure,
    domain::AlbumDetails, domain::ArtistDetails, domain::ArtistSongsPage, domain::ArtistAlbumsPage,
    domain::PlaylistDetails, domain::PlaylistSongsPage, domain::DailyRecommendations,
    domain::PersonalFmBatch, domain::MyPlaylistsPage, domain::LikedSongIds, domain::SavedAlbumsPage,
    domain::SavedArtistsPage, domain::CloudRecentSongs, domain::ListeningRanking, domain::Charts,
    domain::RecommendedPlaylists, domain::PlaylistListPage, domain::PremiumPlaylistsPage,
    domain::PlaylistTags, domain::AlbumSearchPage, domain::ArtistSearchPage,
    domain::PlaylistSearchPage, domain::PodcastSearchPage, domain::VoiceSearchPage>;

inline bool validMusicQuery(const MusicQuery& query)
{
    return std::visit([](const auto& value) { return value.isValid(); }, query);
}

inline bool isEntitySearchQuery(const MusicQuery& query)
{
    return std::holds_alternative<domain::EntitySearchQuery>(query) ||
           std::holds_alternative<domain::PodcastSearchQuery>(query) ||
           std::holds_alternative<domain::VoiceSearchQuery>(query);
}

inline bool isPlaylistDiscoveryQuery(const MusicQuery& query)
{
    return std::holds_alternative<domain::RecommendedPlaylistsQuery>(query) ||
           std::holds_alternative<domain::PlaylistListQuery>(query) ||
           std::holds_alternative<domain::PremiumPlaylistsQuery>(query) ||
           std::holds_alternative<domain::PlaylistTagsQuery>(query);
}

inline std::optional<QString> libraryQueryAccount(const MusicQuery& query)
{
    return std::visit(
        [](const auto& value) -> std::optional<QString> {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, domain::MyPlaylistsQuery> ||
                          std::is_same_v<T, domain::LikedSongIdsQuery> ||
                          std::is_same_v<T, domain::SavedAlbumsQuery> ||
                          std::is_same_v<T, domain::SavedArtistsQuery>)
                return value.accountId;
            return std::nullopt;
        },
        query);
}
inline std::optional<QString> historyQueryAccount(const MusicQuery& query)
{
    return std::visit(
        [](const auto& value) -> std::optional<QString> {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, domain::RecentSongsQuery> ||
                          std::is_same_v<T, domain::ListeningRankingQuery>)
                return value.accountId;
            return std::nullopt;
        },
        query);
}
inline std::optional<QString> musicQueryAccount(const MusicQuery& query)
{
    if (const auto account = libraryQueryAccount(query))
        return account;
    return historyQueryAccount(query);
}
inline bool requiresAuthenticatedMusicQuery(const MusicQuery& query)
{
    return std::holds_alternative<domain::DailyRecommendationsQuery>(query) ||
           std::holds_alternative<domain::PersonalFmQuery>(query) ||
           musicQueryAccount(query).has_value();
}

// Same-thread, deferred, value-only port; no arbitrary endpoints, JSON or Cookie.
class IMusicGateway : public QObject
{
    Q_OBJECT
  public:
    using QObject::QObject;
    [[nodiscard]] virtual quint64 request(domain::SessionToken context, MusicQuery query) = 0;
    virtual void cancel(quint64 id) = 0;
  signals:
    void completed(quint64 id, cirrus::domain::SessionToken context,
                   cirrus::application::MusicResult result);
};

} // namespace cirrus::application
Q_DECLARE_METATYPE(cirrus::application::MusicResult)
Q_DECLARE_METATYPE(cirrus::application::MusicFailure)
