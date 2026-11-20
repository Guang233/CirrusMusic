#pragma once
#include "domain/playback/playback_types.h"
#include <QObject>
#include <variant>

namespace cirrus::application
{
using StreamResult = std::variant<domain::StreamSource, domain::PlaybackFailure>;
class IStreamResolver : public QObject
{
    Q_OBJECT
  public:
    using QObject::QObject;
    // Nonzero request ID; completion is always deferred, with ORIGINAL context.
    virtual quint64 resolve(domain::SessionToken context, domain::StreamQuery query) = 0;
    virtual void cancel(quint64 id) = 0;
  signals:
    void completed(quint64 id, cirrus::domain::SessionToken context,
                   cirrus::application::StreamResult result);
};
struct PlaybackEvent {
    enum class Kind {
        Ready,
        State,
        Position,
        Duration,
        Seekable,
        Ended,
        Failure,
        Observation,
        SeekConfirmed,
        Volume,
        Advanced,     // token: the preloaded load, now playing (gapless); its Duration follows
        SourceExpired // token: the load whose address must be refreshed (refresh())
    };
    domain::LoadToken token;
    Kind kind{};
    domain::PlaybackState state{};
    qint64 value = 0;
    domain::PlaybackFailure failure;
    std::optional<domain::ObservedPlayback> observed; // backend cumulative evidence, not position
    std::optional<float> volumeLinear;                // only actual application output feedback
};
class IPlaybackBackend : public QObject
{
    Q_OBJECT
  public:
    using QObject::QObject;
    // Media cannot see or reinterpret a logical identity. Owner retains LoadToken fences.
    virtual void load(domain::MediaSource source, domain::LoadToken token) = 0;
    virtual void play() = 0;
    virtual void pause() = 0;
    virtual void stop() = 0; // synchronous retirement of callbacks, no completion event
    virtual void seek(qint64 positionMs) = 0; // clip-local, not original-song coordinates
    virtual void setVolume(float linear) = 0;
    // Volume normalization from the source's loudness (MediaSource::gainDb), for what
    // plays from now on.
    virtual void setNormalization(bool on) { Q_UNUSED(on); }
    // How one track goes into the next (docs/tasks/automix.md §3): a setting like the
    // volume, for the transitions from now on. `crossfadeMs`: the Crossfade mode's length.
    virtual void setTransition(domain::TransitionMode mode, qint64 crossfadeMs)
    {
        Q_UNUSED(mode);
        Q_UNUSED(crossfadeMs);
    }
    // The load to play right after the current one, without a gap (docs/tasks/audio-engine.md
    // §4.5); an empty token cancels. It becomes current with Advanced{next}; a new load,
    // stop or preload drops it.
    virtual void preload(domain::MediaSource source, domain::LoadToken next)
    {
        Q_UNUSED(source);
        Q_UNUSED(next);
    }
    // A fresh address for a load that reported SourceExpired (the current or the preloaded).
    virtual void refresh(domain::LoadToken token, domain::MediaSource source)
    {
        Q_UNUSED(token);
        Q_UNUSED(source);
    }
  signals:
    void event(cirrus::application::PlaybackEvent event);
};
} // namespace cirrus::application
Q_DECLARE_METATYPE(cirrus::application::StreamResult)
Q_DECLARE_METATYPE(cirrus::application::PlaybackEvent)
