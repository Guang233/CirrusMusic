#pragma once
#include "automation.h"
#include "domain/playback/playback_types.h"
#include "logging/log.h"
#include <QtGlobal>

namespace cirrus::media::automix
{

// How a transition sounds (docs/tasks/automix.md §5.1). More styles come with the
// analysis: natural end, beat mix, filter out.
enum class Style { Gapless, Crossfade };
CIRRUS_LOG_ENUM(Style, Gapless, Crossfade)

// Why a plan is what it is (the log, the tuning tool).
enum class Reason {
    Setting,    // what the mode asks for: gapless, or the crossfade's length
    NoAnalysis, // the smart mode without an analysis yet: a plain crossfade
    Preview,    // a preview clip on either side: gapless
    SameFile,   // the same file again (repeat one): gapless
    NoDuration, // a length unknown: gapless
    NoRoom,     // too short, or too close to the end, for the shortest overlap: gapless
};
CIRRUS_LOG_ENUM(Reason, Setting, NoAnalysis, Preview, SameFile, NoDuration, NoRoom)

// One transition from the current track ("out") into the next ("in"). Times are each
// track's own media time; the automations run over the overlap, in seconds from its start.
struct Plan {
    Style style = Style::Gapless;
    Reason reason = Reason::Setting;
    qint64 outMs = 0;     // where in the current track the overlap starts
    qint64 inMs = 0;      // where in the next track it starts from
    qint64 overlapMs = 0; // how long both sound (0: gapless)
    qint64 switchMs = 0;  // from the overlap's start: the next becomes the current track
    Automation gainOut;   // the current track's gain over the overlap
    Automation gainIn;    // the next track's
};

// What the planner knows of a track.
struct Track {
    qint64 durationMs = 0;
    bool preview = false; // a provider's clip: never mixed
};

struct Settings {
    domain::TransitionMode mode = domain::TransitionMode::Smart;
    qint64 crossfadeMs = 6000; // the Crossfade mode's length (docs/tasks/automix.md AM5)
};

// The transition from `out`, now at `positionMs`, into `in`. `sameFile`: the same file
// again (repeat one), never mixed with itself. A crossfade that can't fit (short tracks,
// the current one already near its end) is shortened, or becomes gapless.
[[nodiscard]] Plan plan(const Settings& settings, const Track& out, qint64 positionMs,
                        const Track& in, bool sameFile);

// The shortest crossfade worth doing, and how long before its start the next track must be
// open (it may still be downloading its first bytes).
constexpr qint64 kMinimumOverlapMs = 1000;
constexpr qint64 kStartMarginMs = 400;
// The longest overlap any plan uses, for opening the next track in time.
constexpr qint64 kMaximumOverlapMs = 12000;

} // namespace cirrus::media::automix
