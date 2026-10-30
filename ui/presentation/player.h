#pragma once

#include "playback.h"
#include "song_item.h"

#include <QObject>
#include <QtQml/qqmlregistration.h>

namespace cirrus::ui
{

class PlaybackSource;

// Current playback (App.player, docs/ui/presentation.md §5.1): the playback source's
// snapshot for QML, and its commands. Snapshots arrive only when something changes,
// not every frame; smooth positions come from PlaybackClock in QML.
class Player : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Use App.player")
    Q_PROPERTY(cirrus::ui::SongItem current READ current NOTIFY currentChanged)
    Q_PROPERTY(int status READ status NOTIFY changed) // Playback.Status
    // Translated reason when Failed, else empty.
    Q_PROPERTY(QString failureMessage READ failureMessage NOTIFY changed)
    // Position base: `positionMs` at `positionAt` (now()), advancing by `rate`.
    Q_PROPERTY(qint64 positionMs READ positionMs NOTIFY changed)
    Q_PROPERTY(qint64 positionAt READ positionAt NOTIFY changed)
    Q_PROPERTY(double rate READ rate NOTIFY changed)
    Q_PROPERTY(qint64 durationMs READ durationMs NOTIFY changed) // -1 when unknown
    Q_PROPERTY(bool seekable READ seekable NOTIFY changed)
    Q_PROPERTY(double volume READ volume NOTIFY changed)
    Q_PROPERTY(int order READ order NOTIFY changed)   // Playback.Order
    Q_PROPERTY(int repeat READ repeat NOTIFY changed) // Playback.Repeat
    Q_PROPERTY(bool canNext READ canNext NOTIFY changed)
    Q_PROPERTY(bool canPrevious READ canPrevious NOTIFY changed)

  public:
    // Without a source (no playback in this build) the player stays Empty.
    explicit Player(PlaybackSource* source, QObject* parent = nullptr);

    [[nodiscard]] SongItem current() const { return m_current; }
    [[nodiscard]] int status() const { return m_snapshot.status; }
    [[nodiscard]] QString failureMessage() const;
    [[nodiscard]] qint64 positionMs() const { return m_snapshot.positionMs; }
    [[nodiscard]] qint64 positionAt() const { return m_snapshot.positionAt; }
    [[nodiscard]] double rate() const { return m_snapshot.rate; }
    [[nodiscard]] qint64 durationMs() const { return m_snapshot.durationMs; }
    [[nodiscard]] bool seekable() const { return m_snapshot.seekable; }
    [[nodiscard]] double volume() const { return m_snapshot.volume; }
    [[nodiscard]] int order() const { return m_snapshot.order; }
    [[nodiscard]] int repeat() const { return m_snapshot.repeat; }
    [[nodiscard]] bool canNext() const { return m_snapshot.canNext; }
    [[nodiscard]] bool canPrevious() const { return m_snapshot.canPrevious; }

    // The clock of `positionAt`, in ms.
    Q_INVOKABLE static double now() { return double(playback::now()); }

    Q_INVOKABLE void playSongs(const QStringList& keys, int index);
    Q_INVOKABLE void playPause();
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    Q_INVOKABLE void seek(qint64 positionMs);
    Q_INVOKABLE void setVolume(double volume);
    Q_INVOKABLE void setOrder(int order);
    Q_INVOKABLE void setRepeat(int repeat);

  signals:
    void currentChanged();
    void changed();

  private:
    void update();

    PlaybackSource* m_source;
    PlaybackSnapshot m_snapshot;
    SongItem m_current;
};

} // namespace cirrus::ui
