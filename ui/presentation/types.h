#pragma once

#include "collection.h"

#include <QObject>
#include <QtQml/qqmlregistration.h>

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

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
    // Why it was recommended (daily songs), as the service words it; may be empty.
    QString reason;
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

// The signed-in account's collections.
struct LibraryPage {
    QList<SongInfo> likedSongs;
    QList<CollectionItem> createdPlaylists;
    QList<CollectionItem> savedPlaylists;
    QList<CollectionItem> albums;
    QList<CollectionItem> artists;
    QList<SongInfo> recentSongs;
};

namespace feed
{
Q_NAMESPACE
QML_NAMED_ELEMENT(Feed)
enum Which { Home, Discover };
Q_ENUM_NS(Which)
// Shortcuts: the daily songs and personal FM cards.
enum SectionKind { Shortcuts, Collections, Songs };
Q_ENUM_NS(SectionKind)
// Card rows of the home page: today's playlists (signed in), recommended playlists
// (anyone), the newest albums.
enum Shelf { DailyPlaylists, RecommendedPlaylists, NewAlbums };
Q_ENUM_NS(Shelf)
} // namespace feed
using Feed = feed::Which;

// One section of the home or discover page: a row of cards or a few songs.
struct FeedSection {
    feed::SectionKind kind = feed::Collections;
    QString title;
    QList<CollectionItem> collections;
    QList<SongInfo> songs;
};

// One page of an offset-paged list (search results).
struct PageRequest {
    int offset = 0;
    int limit = 30;
};

template <class T> struct ResultPage {
    QList<T> items;
    // Unknown when the source can't tell: treated as "maybe", until an empty page.
    std::optional<bool> hasMore;
};

} // namespace cirrus::ui
