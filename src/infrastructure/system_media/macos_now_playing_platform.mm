#if defined(__APPLE__) && defined(__MACH__)
#include "native_media_platform.h"
#import <Foundation/Foundation.h>
#import <MediaPlayer/MediaPlayer.h>
#include <atomic>
#include <cmath>
#include <limits>
#include <vector>

namespace cirrus::infrastructure::system_media
{
namespace
{
std::atomic<bool> claimed{false}; // serializes OUR process-local providers only
NSString* text(const QString& value)
{
    return [[NSString alloc] initWithCharacters:reinterpret_cast<const unichar*>(value.utf16())
                                         length:NSUInteger(value.size())];
}
std::optional<qint64> requestedMs(NSTimeInterval value)
{
    if (!std::isfinite(value) || value < 0)
        return {};
    const long double milliseconds = static_cast<long double>(value) * 1000;
    if (milliseconds >= static_cast<long double>(std::numeric_limits<qint64>::max()))
        return {}; // no overflowing double-to-int cast, no retarget/clipping to another song
    return qint64(std::floor(milliseconds));
}
struct Registration {
    MPRemoteCommand* __strong command = nil;
    id __strong token = nil;
    bool previousEnabled = false, lastEnabled = false;
    NativeMediaAction action{};
};
class MacNowPlayingPlatform final : public NativeMediaPlatform
{
  public:
    ~MacNowPlayingPlatform() override { close(); }
    bool start(const NativeMediaConfig& config, NativeMediaInbox::Callback callback, Lost) override
    {
        if (m_started || !config.enabled || config.ownedTopLevelWindow != 0 ||
            ![NSThread isMainThread])
            return false;
        m_started = true;
        if (@available(macOS 10.12.2, *)) {
            @autoreleasepool {
                @try {
                    m_center = [MPNowPlayingInfoCenter defaultCenter];
                    m_commands = [MPRemoteCommandCenter sharedCommandCenter];
                    if (!m_center || !m_commands || m_center.nowPlayingInfo != nil ||
                        (m_center.playbackState != MPNowPlayingPlaybackStateUnknown &&
                         m_center.playbackState != MPNowPlayingPlaybackStateStopped))
                        return false; // don't clear an unknown existing in-process publisher
                    bool available = false;
                    if (!claimed.compare_exchange_strong(available, true))
                        return false;
                    m_claim = true;
                    m_targets.reserve(7);
                    if (!button(m_commands.playCommand, NativeMediaAction::Play, callback) ||
                        !button(m_commands.pauseCommand, NativeMediaAction::Pause, callback) ||
                        !button(m_commands.togglePlayPauseCommand, NativeMediaAction::Toggle,
                                callback) ||
                        !button(m_commands.stopCommand, NativeMediaAction::Stop, callback) ||
                        !button(m_commands.nextTrackCommand, NativeMediaAction::Next, callback) ||
                        !button(m_commands.previousTrackCommand, NativeMediaAction::Previous,
                                callback))
                        return false;
                    MPRemoteCommand* seek = m_commands.changePlaybackPositionCommand;
                    const bool previous = seek.enabled;
                    id token = [seek addTargetWithHandler:^MPRemoteCommandHandlerStatus(
                                         MPRemoteCommandEvent* event) {
                      @autoreleasepool {
                          if (![event isKindOfClass:[MPChangePlaybackPositionCommandEvent class]])
                              return MPRemoteCommandHandlerStatusCommandFailed;
                          const auto position = requestedMs(
                              ((MPChangePlaybackPositionCommandEvent*)event).positionTime);
                          if (!position)
                              return MPRemoteCommandHandlerStatusCommandFailed;
                          return callback({NativeMediaAction::Seek, *position})
                                     ? MPRemoteCommandHandlerStatusSuccess
                                     : MPRemoteCommandHandlerStatusCommandFailed;
                      }
                    }];
                    if (!token)
                        return false;
                    m_targets.push_back({seek, token, previous, false, NativeMediaAction::Seek});
                    seek.enabled = NO;
                    return clear();
                } @catch (NSException*) {
                    return false;
                }
            }
        }
        return false; // availability is not a promise of the client's minimum macOS/Qt version
    }
    bool current() const override
    {
        if (m_closed || !m_claim || ![NSThread isMainThread])
            return false;
        @try {
            return ownsInfo(); // this is local ownership/equality, NOT account/load provenance
        } @catch (NSException*) {
            return false;
        }
    }
    NativeProjectionResult publish(const domain::PlaybackObservation& s,
                                   const NativeMediaConfig& config, const Permit& permit) override
    {
        if (@available(macOS 10.12.2, *)) {
            @autoreleasepool {
                @try {
                    if (!current() || !permit())
                        return NativeProjectionResult::Retired;
                    NSMutableDictionary* info = [NSMutableDictionary dictionary];
                    if (s.durationMs && *s.durationMs >= 0)
                        info[MPMediaItemPropertyPlaybackDuration] =
                            @(double(*s.durationMs) / 1000.0);
                    if (s.positionMs && *s.positionMs >= 0)
                        info[MPNowPlayingInfoPropertyElapsedPlaybackTime] =
                            @(double(*s.positionMs) / 1000.0);
                    const bool playing = s.state == domain::PlaybackState::Playing;
                    info[MPNowPlayingInfoPropertyPlaybackRate] = @(playing ? 1.0 : 0.0);
                    info[MPNowPlayingInfoPropertyDefaultPlaybackRate] = @1.0;
                    if (config.exportMetadata &&
                        s.metadataStatus == domain::ObservationContentStatus::Ready && s.metadata) {
                        info[MPMediaItemPropertyTitle] = text(s.metadata->title);
                        info[MPMediaItemPropertyArtist] =
                            text(s.metadata->artists.join(QStringLiteral(", ")));
                        if (s.metadata->album)
                            info[MPMediaItemPropertyAlbumTitle] = text(*s.metadata->album);
                    } // no artwork, URL/ID, lyrics, account/token, query or fake metadata
                    NSDictionary* next = [info copy];
                    if (!current() || !permit())
                        return NativeProjectionResult::Retired;
                    m_center.nowPlayingInfo = next;
                    m_info = next;
                    if (!current() || !permit())
                        return NativeProjectionResult::Retired;
                    auto state = MPNowPlayingPlaybackStateStopped;
                    if (playing)
                        state = MPNowPlayingPlaybackStatePlaying;
                    else if (s.state == domain::PlaybackState::Paused)
                        state = MPNowPlayingPlaybackStatePaused;
                    m_center.playbackState =
                        state; // actual state, never changed by command acceptance
                    for (auto& target : m_targets) {
                        if (!current() || !permit())
                            return NativeProjectionResult::Retired;
                        const auto& c = s.capabilities;
                        bool enabled = false;
                        switch (target.action) {
                        case NativeMediaAction::Play:
                            enabled = c.play;
                            break;
                        case NativeMediaAction::Pause:
                            enabled = c.pause;
                            break;
                        case NativeMediaAction::Toggle:
                            enabled = c.play || c.pause;
                            break;
                        case NativeMediaAction::Stop:
                            enabled = c.stop;
                            break;
                        case NativeMediaAction::Next:
                            enabled = c.next;
                            break;
                        case NativeMediaAction::Previous:
                            enabled = c.previous;
                            break;
                        case NativeMediaAction::Seek:
                            enabled = c.seek && s.durationMs && *s.durationMs > 0;
                            break;
                        }
                        target.lastEnabled = config.allowControls && enabled;
                        target.command.enabled = target.lastEnabled;
                    }
                    return NativeProjectionResult::Published;
                } @catch (NSException*) {
                    return NativeProjectionResult::Failed;
                }
            }
        }
        return NativeProjectionResult::Failed;
    }
    bool clear() noexcept override
    {
        if (!m_claim)
            return true;
        bool ok = true;
        @autoreleasepool {
            @try {
                if (!ownsInfo())
                    return false; // foreign replacement: don't alter its metadata OR enable flags
            } @catch (NSException*) {
                return false;
            }
            for (auto& target : m_targets) {
                @try {
                    target.command.enabled = NO;
                    target.lastEnabled = false;
                } @catch (NSException*) {
                    ok = false;
                }
            }
            if (@available(macOS 10.12.2, *)) {
                @try {
                    if (!ownsInfo())
                        ok = false; // foreign replacement: never wipe its dictionary/playbackState
                    else {
                        m_center.nowPlayingInfo = nil;
                        m_info = nil;
                        m_center.playbackState = MPNowPlayingPlaybackStateStopped;
                    }
                } @catch (NSException*) {
                    ok = false;
                }
            } else {
                ok = false;
            }
        }
        return ok; // clearing does NOT revoke OS/UI/consumer caches already admitted
    }
    bool close() noexcept override
    {
        if (m_closed)
            return m_cleanupOk;
        m_closed = true;
        bool ok = clear();
        @autoreleasepool {
            for (auto& target : m_targets) {
                @try {
                    [target.command removeTarget:target.token]; // exact returned opaque token only
                    target.token = nil;
                    if (ownsInfo() && target.command.enabled == target.lastEnabled)
                        target.command.enabled = target.previousEnabled;
                } @catch (NSException*) {
                    ok = false;
                }
            }
            m_targets.clear();
            m_info = nil;
            m_center = nil;
            m_commands = nil;
        }
        if (m_claim && ok)
            claimed.store(false); // do not authorize a replacement after failed cleanup
        m_claim = false;
        m_cleanupOk = ok;
        return ok;
    }

  private:
    bool ownsInfo() const
    {
        if (!m_center)
            return false;
        NSDictionary* actual = m_center.nowPlayingInfo;
        return m_info ? actual == m_info || [actual isEqualToDictionary:m_info] : actual == nil;
    }
    bool button(MPRemoteCommand* command, NativeMediaAction action,
                const NativeMediaInbox::Callback& callback)
    {
        if (!command)
            return false;
        const bool previous = command.enabled;
        const auto post = callback; // block retains a VALUE, never the caller's reference
        id token =
            [command addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent*) {
              @autoreleasepool {
                  // Success means bounded scalar queue admission ONLY. Owner execution
                  // can still reject a stale generation/private intent/binding/capability.
                  return post({action, 0}) ? MPRemoteCommandHandlerStatusSuccess
                                           : MPRemoteCommandHandlerStatusCommandFailed;
              }
            }];
        if (!token)
            return false;
        m_targets.push_back({command, token, previous, false, action});
        command.enabled = NO;
        return true;
    }
    MPNowPlayingInfoCenter* __strong m_center = nil;
    MPRemoteCommandCenter* __strong m_commands = nil;
    NSDictionary* __strong m_info = nil;
    std::vector<Registration> m_targets;
    bool m_started = false, m_closed = false, m_claim = false, m_cleanupOk = true;
};
} // namespace
std::shared_ptr<NativeMediaPlatform> createMacNowPlayingPlatform()
{
    return std::make_shared<MacNowPlayingPlatform>();
}
} // namespace cirrus::infrastructure::system_media
#endif
