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
    enum class Kind { Ready, State, Position, Duration, Seekable, Ended, Failure, Observation };
    domain::LoadToken token;
    Kind kind{};
    domain::PlaybackState state{};
    qint64 value = 0;
    domain::PlaybackFailure failure;
    std::optional<domain::ObservedPlayback> observed; // backend cumulative evidence, not position
};
class IPlaybackBackend : public QObject
{
    Q_OBJECT
  public:
    using QObject::QObject;
    virtual void load(domain::StreamSource source, domain::LoadToken token) = 0;
    virtual void play() = 0;
    virtual void pause() = 0;
    virtual void stop() = 0; // synchronous retirement of callbacks, no completion event
    virtual void seek(qint64 positionMs) = 0; // clip-local, not original-song coordinates
    virtual void setVolume(float linear) = 0;
  signals:
    void event(cirrus::application::PlaybackEvent event);
};
} // namespace cirrus::application
Q_DECLARE_METATYPE(cirrus::application::StreamResult)
Q_DECLARE_METATYPE(cirrus::application::PlaybackEvent)
