#include "now_playing.h"

#include "media_state.h"
#include "player.h"

#include <QNetworkReply>
#include <QNetworkRequest>

#import <AppKit/AppKit.h>
#import <MediaPlayer/MediaPlayer.h>

// Compiled with ARC (-fobjc-arc, ui/app/CMakeLists.txt).

namespace cirrus::ui
{
namespace
{
// The artwork's size (Control Centre shows it small, the lock screen larger).
constexpr int kCoverSize = 512;

MPRepeatType repeatType(int repeat)
{
    switch (repeat) {
    case playback::RepeatAll:
        return MPRepeatTypeAll;
    case playback::RepeatOne:
        return MPRepeatTypeOne;
    default:
        return MPRepeatTypeOff;
    }
}
} // namespace

struct NowPlaying::Native {
    std::shared_ptr<media::Mailbox> mailbox;
    NSMutableArray* targets = [NSMutableArray array]; // [command, target] pairs
    MPMediaItemArtwork* artwork = nil;
    QString artworkUrl; // what `artwork` shows or is loading
};

NowPlaying::NowPlaying(Player* player, QObject* parent)
    : QObject(parent), m_player(player), m_native(std::make_unique<Native>())
{
    auto mailbox = m_native->mailbox = std::make_shared<media::Mailbox>(this);
    NSMutableArray* targets = m_native->targets;
    MPRemoteCommandCenter* center = [MPRemoteCommandCenter sharedCommandCenter];
    // Commands may come on another thread: hand them to ours.
    const auto add = [&](MPRemoteCommand* command,
                         MPRemoteCommandHandlerStatus (^handler)(MPRemoteCommandEvent*)) {
        id target = [command addTargetWithHandler:handler];
        [targets addObject:@[ command, target ]];
    };
    add(center.playCommand, ^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent*) {
      mailbox->post([this] { media::play(*m_player); });
      return MPRemoteCommandHandlerStatusSuccess;
    });
    add(center.pauseCommand, ^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent*) {
      mailbox->post([this] { media::pause(*m_player); });
      return MPRemoteCommandHandlerStatusSuccess;
    });
    add(center.stopCommand, ^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent*) {
      mailbox->post([this] { media::pause(*m_player); });
      return MPRemoteCommandHandlerStatusSuccess;
    });
    add(center.togglePlayPauseCommand, ^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent*) {
      mailbox->post([this] { m_player->playPause(); });
      return MPRemoteCommandHandlerStatusSuccess;
    });
    add(center.nextTrackCommand, ^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent*) {
      mailbox->post([this] { m_player->next(); });
      return MPRemoteCommandHandlerStatusSuccess;
    });
    add(center.previousTrackCommand, ^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent*) {
      mailbox->post([this] { m_player->previous(); });
      return MPRemoteCommandHandlerStatusSuccess;
    });
    add(center.changePlaybackPositionCommand,
        ^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent* event) {
          const qint64 ms =
              qint64(((MPChangePlaybackPositionCommandEvent*)event).positionTime * 1000.0);
          mailbox->post([this, ms] { media::seek(*m_player, ms); });
          return MPRemoteCommandHandlerStatusSuccess;
        });
    add(center.changeShuffleModeCommand,
        ^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent* event) {
          const bool on = ((MPChangeShuffleModeCommandEvent*)event).shuffleType != MPShuffleTypeOff;
          mailbox->post(
              [this, on] { m_player->setOrder(on ? playback::Shuffle : playback::Sequential); });
          return MPRemoteCommandHandlerStatusSuccess;
        });
    add(center.changeRepeatModeCommand, ^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent* event) {
      int repeat = playback::RepeatOff;
      switch (((MPChangeRepeatModeCommandEvent*)event).repeatType) {
      case MPRepeatTypeAll:
          repeat = playback::RepeatAll;
          break;
      case MPRepeatTypeOne:
          repeat = playback::RepeatOne;
          break;
      default:
          break;
      }
      mailbox->post([this, repeat] { m_player->setRepeat(repeat); });
      return MPRemoteCommandHandlerStatusSuccess;
    });

    connect(m_player, &Player::changed, this, &NowPlaying::update);
    connect(m_player, &Player::currentChanged, this, &NowPlaying::update);
    update();
}

NowPlaying::~NowPlaying()
{
    m_native->mailbox->close();
    for (NSArray* pair in m_native->targets)
        [((MPRemoteCommand*)pair[0]) removeTarget:pair[1]];
    MPNowPlayingInfoCenter* info = [MPNowPlayingInfoCenter defaultCenter];
    info.nowPlayingInfo = nil;
    info.playbackState = MPNowPlayingPlaybackStateStopped;
    if (m_artworkReply)
        m_artworkReply->abort();
}

void NowPlaying::update()
{
    const media::State s = media::stateOf(*m_player, kCoverSize);
    MPNowPlayingInfoCenter* info = [MPNowPlayingInfoCenter defaultCenter];
    MPRemoteCommandCenter* center = [MPRemoteCommandCenter sharedCommandCenter];
    center.playCommand.enabled = s.canPlay;
    center.pauseCommand.enabled = s.canPlay;
    center.togglePlayPauseCommand.enabled = s.canPlay;
    center.stopCommand.enabled = s.canPlay;
    center.nextTrackCommand.enabled = s.canNext;
    center.previousTrackCommand.enabled = s.canPrevious;
    center.changePlaybackPositionCommand.enabled = s.canSeek;
    center.changeShuffleModeCommand.currentShuffleType =
        s.shuffle ? MPShuffleTypeItems : MPShuffleTypeOff;
    center.changeRepeatModeCommand.currentRepeatType = repeatType(s.repeat);

    if (s.key.isEmpty()) {
        info.nowPlayingInfo = nil;
        info.playbackState = MPNowPlayingPlaybackStateStopped;
        return;
    }

    if (s.artUrl != m_native->artworkUrl)
        fetchArtwork(s.artUrl);

    NSMutableDictionary* values = [NSMutableDictionary dictionary];
    values[MPNowPlayingInfoPropertyMediaType] = @(MPNowPlayingInfoMediaTypeAudio);
    values[MPMediaItemPropertyTitle] = s.title.toNSString();
    if (!s.artist.isEmpty())
        values[MPMediaItemPropertyArtist] = s.artist.toNSString();
    if (!s.album.isEmpty())
        values[MPMediaItemPropertyAlbumTitle] = s.album.toNSString();
    if (s.durationMs > 0)
        values[MPMediaItemPropertyPlaybackDuration] = @(double(s.durationMs) / 1000.0);
    // The system runs the clock from the elapsed time and the rate.
    values[MPNowPlayingInfoPropertyElapsedPlaybackTime] = @(double(s.positionMs) / 1000.0);
    values[MPNowPlayingInfoPropertyPlaybackRate] =
        @(s.status == media::Status::Playing ? 1.0 : 0.0);
    values[MPNowPlayingInfoPropertyDefaultPlaybackRate] = @1.0;
    if (m_native->artwork)
        values[MPMediaItemPropertyArtwork] = m_native->artwork;
    info.nowPlayingInfo = values;

    switch (s.status) {
    case media::Status::Playing:
        info.playbackState = MPNowPlayingPlaybackStatePlaying;
        break;
    case media::Status::Paused:
        info.playbackState = MPNowPlayingPlaybackStatePaused;
        break;
    default:
        info.playbackState = MPNowPlayingPlaybackStateStopped;
    }
}

void NowPlaying::fetchArtwork(const QString& url)
{
    // The old cover never shows with the new song.
    if (m_artworkReply)
        m_artworkReply->abort();
    m_native->artwork = nil;
    m_native->artworkUrl = url;
    if (url.isEmpty())
        return;
    QNetworkReply* reply = m_network.get(QNetworkRequest(QUrl(url)));
    m_artworkReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, url] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError || url != m_native->artworkUrl)
            return;
        NSImage* image = [[NSImage alloc] initWithData:reply->readAll().toNSData()];
        if (!image)
            return;
        m_native->artwork = [[MPMediaItemArtwork alloc] initWithBoundsSize:image.size
                                                            requestHandler:^NSImage*(CGSize) {
                                                              return image;
                                                            }];
        update();
    });
}

} // namespace cirrus::ui
