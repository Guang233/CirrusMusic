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
};
struct MyPlaylistsPage {
    MyPlaylistsQuery query;
    QList<LibraryPlaylist> playlists;
    std::optional<qint64> total;
    std::optional<bool> hasMore;
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
