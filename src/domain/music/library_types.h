#pragma once
#include "catalog_types.h"

namespace cirrus::domain
{
// Account IDs are captured from the verified session, not chosen by facade callers.
struct MyPlaylistsQuery {
    QString accountId;
    CatalogPage page;
    [[nodiscard]] bool isValid() const { return validMusicId(accountId) && page.isValid(); }
};
struct LikedSongIdsQuery {
    QString accountId;
    [[nodiscard]] bool isValid() const { return validMusicId(accountId); }
};
struct SavedAlbumsQuery {
    QString accountId;
    CatalogPage page{25, 0};
    [[nodiscard]] bool isValid() const { return validMusicId(accountId) && page.isValid(); }
};
struct SavedArtistsQuery {
    QString accountId;
    CatalogPage page{25, 0};
    [[nodiscard]] bool isValid() const { return validMusicId(accountId) && page.isValid(); }
};
struct LibraryPlaylist {
    PlaylistId id;
    QString title;
    QUrl cover;
    std::optional<QString> creatorId;
    std::optional<qint64> trackCount;
    std::optional<bool>
        subscribed; // unknown is not false; never identify liked playlist by position
    // The account's own liked-songs playlist ("我喜欢的音乐"): candidate specialType 5 on a
    // playlist whose creator is the queried account. Unknown (no specialType, or 5 with an
    // unknown creator) is not false; another account's liked playlist is false.
    std::optional<bool> accountLikedSongs;
};
struct MyPlaylistsPage {
    MyPlaylistsQuery query;
    // The first page may carry rows the service does not count toward the offset (the
    // liked-songs playlist on top of `limit`), so a page may hold more than `limit` rows.
    QList<LibraryPlaylist> playlists;
    std::optional<qint64> total;
    std::optional<bool> hasMore;
    // The service offset the next page starts at (not query offset + rows, for the above).
    int nextOffset = 0;
};
struct LikedSongIds {
    QString accountId;
    QList<SongId> ids; // unordered provider list, no playback order or implicit detail hydration
    static constexpr qsizetype MaximumIds = 100000;
};
struct SavedAlbumsPage {
    SavedAlbumsQuery query;
    QList<Album> albums;
    std::optional<qint64> total;
    std::optional<bool> hasMore;
};
struct SavedArtist {
    ArtistId id;
    QString name;
    QUrl portrait;
    std::optional<qint64> albumCount;
};
struct SavedArtistsPage {
    SavedArtistsQuery query;
    QList<SavedArtist> artists;
    std::optional<qint64> total;
    std::optional<bool> hasMore;
};
} // namespace cirrus::domain
