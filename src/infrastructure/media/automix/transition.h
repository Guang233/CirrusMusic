#pragma once
#include "automation.h"
#include "domain/playback/playback_types.h"
#include "logging/log.h"
#include <QtGlobal>
#include <vector>

namespace cirrus::media::automix
{
struct Analysis;

// How a transition sounds (docs/tasks/automix.md §5.1).
enum class Style {
    Gapless,    // the next track straight after, to the sample
    Crossfade,  // equal-power, over the current track's outro
    NaturalEnd, // the current track's own fade-out plays, the next comes in under its end
    BeatMix,    // the next track's bars on the current one's, the bass swapped on a downbeat
    FilterOut,  // the current track's last beats filtered away into an echo
};
CIRRUS_LOG_ENUM(Style, Gapless, Crossfade, NaturalEnd, BeatMix, FilterOut)

// Why a plan is what it is (the log, the tuning tool).
enum class Reason {
    Setting,    // what the mode asks for: gapless, or the crossfade's length
    NoAnalysis, // the smart mode without an analysis yet: a plain crossfade
    Preview,    // a preview clip on either side: gapless
    SameFile,   // the same file again (repeat one): gapless
    NoDuration, // a length unknown: gapless
    NoRoom,     // too short, or too close to the end, for the shortest overlap: gapless
    Continuous, // the current track runs straight into the next: gapless
    FadeOut,    // the current track fades out by itself: a natural end
    Outro,      // a crossfade after the current track's singing, before the next one's
    Tight,      // no room between the singing: the shortest crossfade at the very end
    Beats,      // both steady at about the same tempo: on the beat
    FullEnd,    // the current track ends at full level on a beat the next can't take up
};
CIRRUS_LOG_ENUM(Reason, Setting, NoAnalysis, Preview, SameFile, NoDuration, NoRoom, Continuous,
                FadeOut, Outro, Tight, Beats, FullEnd)

// One transition from the current track ("out") into the next ("in"). Times are each
// track's own media time; the automations run over the overlap, in seconds from its start.
struct Plan {
    Style style = Style::Gapless;
    Reason reason = Reason::Setting;
    qint64 outMs = 0;     // where in the current track the overlap starts
    qint64 inMs = 0;      // where in the next track it starts from
    qint64 overlapMs = 0; // how long both sound (0: gapless)
    qint64 switchMs = 0;  // from the overlap's start: the next becomes the current track
    qint64 inDelayMs = 0; // from the overlap's start: the next track begins (silence before)
    Automation gainOut;   // the current track's gain over the overlap
    Automation gainIn;    // the next track's
    // Each track's bass (under 200 Hz) cut: 0 all of it, 1 none. Empty: left alone.
    Automation bassCutOut, bassCutIn;
    // A high-pass on the current track, its cutoff in Hz. Empty: none.
    Automation sweepOut;
    // An echo of the current track: its delay (0: none), feedback, and how much goes in.
    double echoMs = 0;
    float echoFeedback = 0;
    Automation echoSend;
    int bars = 0;        // a beat mix's overlap in bars
    bool onBeat = false; // the next track's bars come on the current one's
};

// A sung line, from the lyrics' timeline (ms of the track's media time).
struct Span {
    qint64 startMs = 0;
    qint64 endMs = 0;
};

// What the planner knows of a track.
struct Track {
    qint64 durationMs = 0;
    bool preview = false;               // a provider's clip: never mixed
    const Analysis* analysis = nullptr; // none yet: the plain crossfade
    std::vector<Span> vocals;           // none: no lyrics, or instrumental
};

struct Settings {
    domain::TransitionMode mode = domain::TransitionMode::Smart;
    qint64 crossfadeMs = 6000; // the Crossfade mode's length (docs/tasks/automix.md AM5)
};

// The transition from `out`, now at `positionMs`, into `in` (docs/tasks/automix.md §5).
// `sameFile`: the same file again (repeat one), never mixed with itself. With both
// analyses the smart mode picks the style and skips the silence at the edges; a
// transition that can't fit (short tracks, the current one near its end) is shortened,
// or becomes gapless.
[[nodiscard]] Plan plan(const Settings& settings, const Track& out, qint64 positionMs,
                        const Track& in, bool sameFile);

// The shortest crossfade worth doing, and how long before its start the next track must be
// open (it may still be downloading its first bytes).
constexpr qint64 kMinimumOverlapMs = 1000;
constexpr qint64 kStartMarginMs = 400;
// The longest overlap any plan uses, for opening the next track in time.
constexpr qint64 kMaximumOverlapMs = 12000;

} // namespace cirrus::media::automix
