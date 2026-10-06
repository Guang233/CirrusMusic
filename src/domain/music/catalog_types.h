#pragma once
#include "music_types.h"

namespace cirrus::domain
{
struct PlaylistId {
    QString value;
};
struct CatalogPage {
    int limit = 30, offset = 0;
    [[nodiscard]] bool isValid() const
    {
        return limit >= 1 && limit <= 100 && offset >= 0 && offset <= 100000;
    }
};
enum class ArtistSongOrder { Hot, Time };
struct AlbumQuery {
    AlbumId album;
    [[nodiscard]] bool isValid() const { return validMusicId(album.value); }
};
struct ArtistQuery {
    ArtistId artist;
    [[nodiscard]] bool isValid() const { return validMusicId(artist.value); }
};
struct PlaylistQuery {
    PlaylistId playlist;
    [[nodiscard]] bool isValid() const { return validMusicId(playlist.value); }
};
struct ArtistSongsQuery {
    ArtistId artist;
    CatalogPage page;
    ArtistSongOrder order = ArtistSongOrder::Hot;
    [[nodiscard]] bool isValid() const
    {
        return validMusicId(artist.value) && page.isValid() &&
               (order == ArtistSongOrder::Hot || order == ArtistSongOrder::Time);
    }
};
struct ArtistAlbumsQuery {
    ArtistId artist;
    CatalogPage page;
    [[nodiscard]] bool isValid() const { return validMusicId(artist.value) && page.isValid(); }
};
struct PlaylistSongsQuery {
    PlaylistId playlist;
    CatalogPage page;
    [[nodiscard]] bool isValid() const { return validMusicId(playlist.value) && page.isValid(); }
};
struct Album {
    AlbumId id;
    QString title, description;
    QUrl cover;
    QList<ArtistCredit> artists;
    std::optional<qint64> trackCount, publishedAtMs;
    std::optional<QStringList> aliases;
    std::optional<QString> company, type, subType; // provider display text, not rights
};
struct AlbumDetails {
    Album album;
    QList<Song> songs;
    bool complete = false; // only a matching explicit size proves completeness
};
struct ArtistDetails {
    ArtistId id;
    QString name, description;
    QUrl portrait;
    std::optional<qint64> songCount, albumCount;
    std::optional<QStringList> aliases, translatedNames;
};
struct ArtistSongsPage {
    ArtistSongsQuery query;
    QList<Song> songs;
    std::optional<qint64> total;
    std::optional<bool> hasMore;
};
struct ArtistAlbumsPage {
    ArtistAlbumsQuery query;
    QList<Album> albums;
    std::optional<qint64> total;
    std::optional<bool> hasMore;
};
struct PlaylistCreator {
    std::optional<QString> publicUserId; // display identity, NOT a verified account/owner
    QString nickname;
};
struct PlaylistDetails {
    PlaylistId id;
    QString title, description;
    QUrl cover;
    std::optional<qint64> trackCount, updatedAtMs;
    std::optional<QList<SongId>>
        trackIds;      // absence != empty; duplicates and provider order retained
    QList<Song> songs; // provider's partial embedded metadata, NOT all tracks
    bool trackIdsComplete = false;
    std::optional<QStringList> tags;
    std::optional<PlaylistCreator> creator;
    std::optional<qint64> createdAtMs, subscriberCount, commentCount, shareCount;
    std::optional<double> playCount; // raw nonnegative provider value, may be fractional
};
struct PlaylistSongsPage {
    PlaylistSongsQuery query;
    QList<SongId> ids; // index slice, including missing/duplicate songs
    QList<Song> songs;
    QList<SongId> missing;
    qint64 total = 0;
    bool hasMore = false;
    std::optional<qint64>
        indexUpdatedAtMs; // compare across pages; no cross-request snapshot promise
};
} // namespace cirrus::domain
