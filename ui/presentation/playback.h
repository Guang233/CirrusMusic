#pragma once

#include "request.h"
#include "types.h"

#include <QObject>
#include <QtQml/qqmlregistration.h>

#include <chrono>
#include <optional>

// Playback state as the playback source reports it (docs/ui/presentation.md §5.1).
namespace cirrus::ui::playback
{
Q_NAMESPACE
QML_NAMED_ELEMENT(Playback)

enum Status { Empty, Loading, Playing, Paused, Ended, Failed };
Q_ENUM_NS(Status)
enum Order { Sequential, Shuffle };
Q_ENUM_NS(Order)
enum Repeat { RepeatOff, RepeatAll, RepeatOne };
Q_ENUM_NS(Repeat)
enum Mode { Queue, PersonalFm };
Q_ENUM_NS(Mode)

// Milliseconds on the monotonic clock that `positionAt` refers to.
[[nodiscard]] inline qint64 now()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}
} // namespace cirrus::ui::playback

namespace cirrus::ui
{

// The whole playback state; sources replace it as a unit. The position is a base
// value: at `positionAt` (playback::now()) it was `positionMs`, and it advances by
// `rate` ms per ms (1 while playing, 0 otherwise).
struct PlaybackSnapshot {
    playback::Status status = playback::Empty;
    SongInfo current; // empty key when Empty
    // Why the current song stopped (Failed); `unavailable` songs fail as Rejected.
    std::optional<Failure> failure;
    qint64 positionMs = 0;
    qint64 positionAt = 0;
    double rate = 0;
    qint64 durationMs = -1; // -1 when unknown
    bool seekable = false;
    double volume = 1;
    playback::Order order = playback::Sequential;
    playback::Repeat repeat = playback::RepeatOff;
    bool canNext = false;
    bool canPrevious = false;
    playback::Mode mode = playback::Queue;
    // Personal FM is fetching its next song.
    bool fmWaiting = false;
};

} // namespace cirrus::ui
