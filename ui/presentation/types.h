#pragma once

#include "collection.h"

#include <QList>
#include <QString>
#include <QStringList>

// Display values the data sources hand to ViewModels. Defined by the UI rather than
// reusing backend domain types, so fakes need no backend and QML never sees DTOs
// (docs/ui/presentation.md §2). Keys are opaque strings (§4.3).
namespace cirrus::ui
{

struct ArtistRef {
    QString key; // may be empty
    QString name;
};

struct SongInfo {
    QString key;
    QString title;
    QString secondaryTitle; // alias or translated title, may be empty
    QList<ArtistRef> artists;
    QString albumKey;
    QString albumTitle;
    QString coverKey;
    qint64 durationMs = -1; // -1 when unknown
    bool unavailable = false;
};

struct PlaylistInfo {
    QString key;
    QString title;
    QString description;
    QString creator;
    QString coverKey;
};

// A playlist's header and tracks. `songs` may hold details for only some of
// `songKeys` (large playlists); the rest are fetched as they scroll into view.
struct PlaylistPage {
    PlaylistInfo info;
    QStringList songKeys;
    QList<SongInfo> songs;
};

struct AlbumInfo {
    QString key;
    QString title;
    QList<ArtistRef> artists;
    int year = 0; // 0 when unknown
    QString coverKey;
    QString description;
};

struct AlbumPage {
    AlbumInfo info;
    QStringList songKeys;
    QList<SongInfo> songs;
};

struct ArtistInfo {
    QString key;
    QString name;
    QString description;
    QString avatarKey;   // a cover key, may be empty
    int songCount = -1;  // -1 when unknown
    int albumCount = -1; // -1 when unknown
};

// A playlist, album or artist as a card. Raw fields; the list model words the subtitle.
struct CollectionItem {
    QString key;
    collection::Kind kind = collection::Playlist;
    QString title;
    QString byline; // album artists or playlist creator
    int year = 0;   // albums, 0 when unknown
    int count = -1; // songs (playlists, albums) or albums (artists), -1 when unknown
    QString coverKey;
};

// An artist's header, popular songs and albums.
struct ArtistPage {
    ArtistInfo info;
    QList<SongInfo> topSongs;
    QList<CollectionItem> albums;
};

} // namespace cirrus::ui
