#pragma once

#include "formatter.h"
#include "types.h"

#include <QObject>
#include <QtQml/qqmlregistration.h>

namespace cirrus::ui
{

// One song for QML outside list models, e.g. `App.player.current`. Same names as the
// SongListModel roles (docs/ui/presentation.md §5.3).
class SongItem
{
    Q_GADGET
    QML_VALUE_TYPE(songItem)
    Q_PROPERTY(QString key MEMBER key CONSTANT) // empty for no song
    Q_PROPERTY(QString title MEMBER title CONSTANT)
    Q_PROPERTY(QString secondaryTitle MEMBER secondaryTitle CONSTANT)
    Q_PROPERTY(QString artistText MEMBER artistText CONSTANT)
    Q_PROPERTY(QString albumKey MEMBER albumKey CONSTANT)
    Q_PROPERTY(QString albumTitle MEMBER albumTitle CONSTANT)
    Q_PROPERTY(QString coverKey MEMBER coverKey CONSTANT)
    Q_PROPERTY(qint64 durationMs MEMBER durationMs CONSTANT) // -1 when unknown
    Q_PROPERTY(bool unavailable MEMBER unavailable CONSTANT)
    Q_PROPERTY(QString reason MEMBER reason CONSTANT) // recommendation reason, may be empty

  public:
    SongItem() = default;
    explicit SongItem(const SongInfo& song)
        : key(song.key), title(song.title), secondaryTitle(song.secondaryTitle),
          artistText(Formatter::artists(song.artists)), albumKey(song.albumKey),
          albumTitle(song.albumTitle), coverKey(song.coverKey), durationMs(song.durationMs),
          unavailable(song.unavailable), reason(song.reason)
    {
    }

    QString key;
    QString title;
    QString secondaryTitle;
    QString artistText;
    QString albumKey;
    QString albumTitle;
    QString coverKey;
    qint64 durationMs = -1;
    bool unavailable = false;
    QString reason;
    friend bool operator==(const SongItem&, const SongItem&) = default;
};

} // namespace cirrus::ui
