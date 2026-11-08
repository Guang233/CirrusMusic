#pragma once

#include "player.h"

#include <QObject>
#include <QUrl>

#include <algorithm>
#include <functional>
#include <mutex>

// What the system media controls show and do (MPRIS, Windows SMTC, macOS Now Playing),
// read from App.player like the window, so they behave as it does
// (docs/ui/pages/shell.md §7).
namespace cirrus::ui::media
{

enum class Status { Stopped, Playing, Paused };

struct State {
    QString key; // empty: no song
    QString title;
    QString artist;
    QString album;
    QString artUrl; // https, empty when there is none
    Status status = Status::Stopped;
    qint64 durationMs = -1; // -1 when unknown
    qint64 positionMs = 0;  // now
    double rate = 0;        // 1 while playing
    bool canPlay = false;
    bool canNext = false;
    bool canPrevious = false;
    bool canSeek = false;
    bool shuffle = false;
    int repeat = playback::RepeatOff;
};

// The cover at `size` pixels from the image host; empty for the fakes' sample covers.
inline QString coverUrl(const QString& coverKey, int size)
{
    QUrl url(coverKey);
    if (url.scheme() != u"http" && url.scheme() != u"https")
        return {};
    url.setScheme(QStringLiteral("https"));
    url.setQuery(QStringLiteral("param=%1y%1").arg(size));
    return url.toString();
}

inline qint64 positionNow(const Player& player)
{
    qint64 ms =
        player.positionMs() + qint64((Player::now() - double(player.positionAt())) * player.rate());
    if (player.durationMs() >= 0)
        ms = std::min(ms, player.durationMs());
    return std::max<qint64>(ms, 0);
}

inline State stateOf(const Player& player, int coverSize = 512)
{
    State s;
    const SongItem song = player.current();
    s.key = song.key;
    s.title = song.title;
    s.artist = song.artistText;
    s.album = song.albumTitle;
    s.artUrl = coverUrl(song.coverKey, coverSize);
    switch (player.status()) {
    case playback::Playing:
    case playback::Loading:
        s.status = Status::Playing; // loading on to play
        break;
    case playback::Paused:
    case playback::Failed:
        s.status = Status::Paused;
        break;
    default:
        s.status = Status::Stopped;
    }
    s.durationMs = player.durationMs();
    s.positionMs = positionNow(player);
    s.rate = player.rate();
    s.canPlay = !song.key.isEmpty();
    s.canNext = player.canNext();
    s.canPrevious = player.canPrevious();
    s.canSeek = player.seekable() && player.durationMs() > 0;
    s.shuffle = player.order() == playback::Shuffle;
    s.repeat = player.repeat();
    return s;
}

// The commands, as the window's buttons do them.
inline void play(Player& player)
{
    const int status = player.status();
    if (status != playback::Playing && status != playback::Loading)
        player.playPause();
}

inline void pause(Player& player)
{
    const int status = player.status();
    if (status == playback::Playing || status == playback::Loading)
        player.playPause();
}

inline void seek(Player& player, qint64 positionMs)
{
    if (player.seekable() && player.durationMs() > 0)
        player.seek(std::clamp<qint64>(positionMs, 0, player.durationMs()));
}

// Hands a command from a system thread (WinRT, MediaPlayer) to the owner's thread; once
// closed (the owner is going away) commands are dropped. A posted command dies with the
// owner, so it may use it.
class Mailbox
{
  public:
    explicit Mailbox(QObject* owner) : m_owner(owner) {}

    void post(std::function<void()> command)
    {
        const std::lock_guard lock(m_mutex);
        if (m_owner)
            QMetaObject::invokeMethod(m_owner, std::move(command), Qt::QueuedConnection);
    }

    void close()
    {
        const std::lock_guard lock(m_mutex);
        m_owner = nullptr;
    }

  private:
    std::mutex m_mutex;
    QObject* m_owner;
};

} // namespace cirrus::ui::media
