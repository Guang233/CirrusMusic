#pragma once

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

} // namespace cirrus::ui
