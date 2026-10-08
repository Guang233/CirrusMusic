#pragma once
#include "entity_search_types.h"
#include <cstddef>
#include <type_traits>
#include <variant>

namespace cirrus::domain
{
enum class AlbumArea { All, Chinese, Western, Korean, Japanese };
inline bool validAlbumArea(AlbumArea a) { return a >= AlbumArea::All && a <= AlbumArea::Japanese; }
enum class AlbumChartOrder { New, Hot };
enum class ArtistCategory { All = -1, Male = 1, Female = 2, Band = 3 };
enum class ArtistArea { All = -1, Chinese = 7, Western = 96, Japanese = 8, Korean = 16, Other = 0 };
struct LatestAlbumsQuery {
    [[nodiscard]] bool isValid() const { return true; }
};
struct NewAlbumsQuery {
    AlbumArea area = AlbumArea::All;
    CatalogPage page;
    [[nodiscard]] bool isValid() const { return validAlbumArea(area) && page.isValid(); }
};
struct AlbumChartsQuery {
    int year = 0, month = 0; // caller's explicit calendar, never local wall-clock defaults
    AlbumArea area = AlbumArea::All;
    CatalogPage page{50, 0};
    AlbumChartOrder order = AlbumChartOrder::New;
    [[nodiscard]] bool isValid() const
    {
        return year >= 1900 && year <= 9999 && month >= 1 && month <= 12 && validAlbumArea(area) &&
               page.isValid() && (order == AlbumChartOrder::New || order == AlbumChartOrder::Hot);
    }
};
struct ArtistBrowseQuery {
    ArtistCategory category = ArtistCategory::All;
    ArtistArea area = ArtistArea::All;
    int initial = -1; // Hot=-1, Other=0, uppercase ASCII A..Z=65..90 only
    CatalogPage page;
    [[nodiscard]] bool isValid() const
    {
        return page.isValid() &&
               (category == ArtistCategory::All || category == ArtistCategory::Male ||
                category == ArtistCategory::Female || category == ArtistCategory::Band) &&
               (area == ArtistArea::All || area == ArtistArea::Chinese ||
                area == ArtistArea::Western || area == ArtistArea::Japanese ||
                area == ArtistArea::Korean || area == ArtistArea::Other) &&
               (initial == -1 || initial == 0 || (initial >= 65 && initial <= 90));
    }
};
struct PopularArtistsQuery {
    CatalogPage page{50, 0};
    [[nodiscard]] bool isValid() const { return page.isValid(); }
};
struct ArtistTopSongsQuery {
    ArtistId artist;
    [[nodiscard]] bool isValid() const { return validMusicId(artist.value); }
};
struct SimilarSongsQuery {
    SongId song;
    CatalogPage page{50, 0};
    [[nodiscard]] bool isValid() const { return validMusicId(song.value) && page.isValid(); }
};
struct SimilarArtistsQuery {
    ArtistId artist;
    [[nodiscard]] bool isValid() const { return validMusicId(artist.value); }
};
struct SimilarPlaylistsQuery {
    SongId song; // similarity seed is a Song, NOT a Playlist
    CatalogPage page{50, 0};
    [[nodiscard]] bool isValid() const { return validMusicId(song.value) && page.isValid(); }
};
using DiscoveryBrowseAction =
    std::variant<LatestAlbumsQuery, NewAlbumsQuery, AlbumChartsQuery, ArtistBrowseQuery,
                 PopularArtistsQuery, ArtistTopSongsQuery, SimilarSongsQuery, SimilarArtistsQuery,
                 SimilarPlaylistsQuery>;
struct DiscoveryBrowseQuery {
    DiscoveryBrowseAction action;
    [[nodiscard]] bool isValid() const
    {
        return std::visit([](const auto& q) { return q.isValid(); }, action);
    }
};
struct DiscoveryAlbums {
    QList<Album> albums;
};
struct DiscoveryArtists {
    QList<SearchArtist> artists;
};
struct DiscoverySongs {
    QList<Song> songs;
    // SimilarSongs only: caller's limit/offset are not proven provider pagination.
    bool requestedPaginationUnverified = false;
};
struct DiscoveryPlaylists {
    QList<DiscoveryPlaylist> playlists;
};
struct DiscoveryAlbumCharts {
    // Missing group is unknown, not an explicit empty group. At least one must be present.
    std::optional<QList<Album>> week, month; // independent groups, never merge into a fake ranking
};
struct DiscoveryBrowsePage {
    DiscoveryBrowseQuery query; // original query correlation, not a provider-signed receipt
    std::variant<DiscoveryAlbums, DiscoveryArtists, DiscoverySongs, DiscoveryPlaylists,
                 DiscoveryAlbumCharts>
        content;
    std::optional<qint64> total;
    std::optional<bool> hasMore;
};
inline bool sameDiscoveryBrowseQuery(const DiscoveryBrowseQuery& left,
                                     const DiscoveryBrowseQuery& right)
{
    if (left.action.index() != right.action.index())
        return false;
    return std::visit(
        [&](const auto& q) {
            const auto& r = std::get<std::decay_t<decltype(q)>>(right.action);
            bool same = true;
            if constexpr (requires { q.page; })
                same = same && q.page.limit == r.page.limit && q.page.offset == r.page.offset;
            if constexpr (requires { q.area; })
                same = same && q.area == r.area;
            if constexpr (requires { q.year; })
                same = same && q.year == r.year && q.month == r.month && q.order == r.order;
            if constexpr (requires { q.initial; })
                same = same && q.initial == r.initial && q.category == r.category;
            if constexpr (requires { q.artist; })
                same = same && q.artist.value == r.artist.value;
            if constexpr (requires { q.song; })
                same = same && q.song.value == r.song.value;
            return same;
        },
        left.action);
}
inline bool matchingDiscoveryBrowsePage(const DiscoveryBrowseQuery& query,
                                        const DiscoveryBrowsePage& page)
{
    const auto kind = query.action.index();
    const std::size_t expected = kind <= 1                               ? 0
                                 : kind == 2                             ? 4
                                 : (kind == 3 || kind == 4 || kind == 7) ? 1
                                 : kind == 8                             ? 3
                                                                         : 2;
    return sameDiscoveryBrowseQuery(query, page.query) && page.content.index() == expected;
}
} // namespace cirrus::domain
