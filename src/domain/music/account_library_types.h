#pragma once
#include "podcast_types.h"
#include <QSet>

namespace cirrus::domain
{
// Cloud resources are account-bound; matching catalog IDs are separate optional references.
struct CloudSongId {
    QString value;
    friend bool operator==(const CloudSongId&, const CloudSongId&) = default;
};
struct SubscribedPodcastsQuery {
    QString accountId;
    CatalogPage page{30, 0};
    [[nodiscard]] bool isValid() const { return validMusicId(accountId) && page.isValid(); }
};
struct CloudSongsQuery {
    QString accountId;
    CatalogPage page{30, 0};
    [[nodiscard]] bool isValid() const { return validMusicId(accountId) && page.isValid(); }
};
struct CloudSongDetailsQuery {
    QString accountId;
    QList<CloudSongId> songs;
    [[nodiscard]] bool isValid() const
    {
        if (!validMusicId(accountId) || songs.isEmpty() || songs.size() > 100)
            return false;
        QSet<QString> seen;
        for (const auto& id : songs) {
            if (!validMusicId(id.value) || seen.contains(id.value))
                return false;
            seen.insert(id.value);
        }
        return true;
    }
};
struct SubscribedPodcastsPage {
    SubscribedPodcastsQuery query;
    QList<Podcast> podcasts;
    std::optional<qint64> total, providerTime;
    std::optional<bool> hasMore;
};
struct CloudSong {
    CloudSongId id; // candidate provider songId, NOT automatically a SongId or playable asset
    QString title;
    std::optional<QString> artist, album, fileName; // display text; fileName is never a local path
    std::optional<qint64> providerFileSize, providerBitrate, providerAddedTime;
    std::optional<SongId> catalogReference; // explicit positive simpleSong.id; 0/missing = unknown
    // simpleSong as a song: what plays it (the official app plays cloud songs by simpleSong.id
    // through the ordinary stream, docs/api/cloud-drive.md) and shows it. Missing when the row
    // has none or it does not read as one.
    std::optional<Song> track;
    // Linked to a catalog song (matched) or shown with the file's own details; unknown when
    // the row does not say.
    std::optional<bool> matched;
};
struct CloudSongsPage {
    CloudSongsQuery query;
    QList<CloudSong> songs;
    std::optional<qint64> total, providerUsedSpace, providerMaximumSpace;
    std::optional<bool> hasMore;
};
// Removes songs (their files) from the account's cloud drive (docs/api/cloud-drive.md).
struct CloudSongDelete {
    static constexpr qsizetype MaximumSongs = 100; // local budget
    QList<CloudSongId> songs;
    [[nodiscard]] bool isValid() const
    {
        if (songs.isEmpty() || songs.size() > MaximumSongs)
            return false;
        QSet<QString> seen;
        for (const auto& id : songs) {
            if (!validMusicId(id.value) || seen.contains(id.value))
                return false;
            seen.insert(id.value);
        }
        return true;
    }
};
// Links a cloud song to a catalog song (the official app's 纠错), or, with no target, back to
// the file's own details. Bound to the account it was made for.
struct CloudSongMatch {
    QString accountId;
    CloudSongId song;
    std::optional<SongId> target;
    [[nodiscard]] QString sourceAccount() const { return accountId; }
    [[nodiscard]] bool isValid() const
    {
        return validMusicId(accountId) && validMusicId(song.value) &&
               (!target || validMusicId(target->value));
    }
};
struct CloudSongDetails {
    CloudSongDetailsQuery query;
    QList<CloudSong> songs;     // request order, no unsolicited/duplicate provider rows
    QList<CloudSongId> missing; // unknown, not a copyright/deletion conclusion
};
} // namespace cirrus::domain
