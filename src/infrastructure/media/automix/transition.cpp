#include "transition.h"
#include "analysis.h"
#include <algorithm>

namespace cirrus::media::automix
{
using domain::TransitionMode;

namespace
{
// Without an analysis the smart mode crossfades for this long.
constexpr qint64 kSmartFallbackMs = 6000;
// The smart crossfade: at most this long, inside the last this-much of the current track
// (never more of it: no cutting songs short), this far from any sung line.
constexpr qint64 kOutroLongestMs = 8000;
constexpr qint64 kOutroUsableMs = 30000;
constexpr qint64 kVocalMarginMs = 500;
// A next track that opens about as loud as it goes comes in quicker.
constexpr float kLoudStartDb = 3;
constexpr qint64 kLoudStartLongestMs = 4000;
// A natural end: the next track comes in once the fade-out is this far down; the rest of
// it sounds under the next for at most this long (and at least the shortest).
constexpr float kNaturalEndDb = 20;
constexpr qint64 kNaturalEndLongestMs = 8000;
constexpr qint64 kNaturalEndShortestMs = 250;
constexpr double kEntranceSeconds = 0.03; // the next track's first sound, unclicked
// Sound this close to a file's edge: the music runs on past it.
constexpr qint64 kEdgeMs = 50;

Plan gapless(Reason reason)
{
    Plan plan;
    plan.reason = reason;
    return plan;
}

// One track running straight into the next (a live album, a DJ mix, movements): the
// first has no silence and no fade-out at its end, the second no silence at its start.
bool continuous(const Analysis& a, const Analysis& b)
{
    const bool runsOn = a.durationMs - a.soundEndMs <= kEdgeMs &&
                        (a.fadeStartMs < 0 || a.fadedBy(kNaturalEndDb) >= a.soundEndMs);
    return runsOn && b.soundStartMs <= kEdgeMs;
}

// An equal-power crossfade of `overlap` ending where the current track's sound ends.
Plan crossfade(qint64 overlap, qint64 outEnd, qint64 inStart, Reason reason)
{
    Plan plan;
    plan.style = Style::Crossfade;
    plan.reason = reason;
    plan.overlapMs = overlap;
    plan.outMs = outEnd - overlap;
    plan.inMs = inStart;
    plan.switchMs = overlap / 2;
    const double seconds = double(overlap) / 1000;
    plan.gainOut = Automation::fadeOut(0, seconds);
    plan.gainIn = Automation::fadeIn(0, seconds);
    return plan;
}

// The current track fades out by itself: let it, and bring the next in from its first
// sound once the fade is well down. The original ending stays.
Plan naturalEnd(const Analysis& a, qint64 outEnd, qint64 inStart, qint64 earliest)
{
    const qint64 from = std::max(a.fadedBy(kNaturalEndDb), earliest);
    const qint64 overlap = std::min(outEnd - from, kNaturalEndLongestMs);
    if (overlap < kNaturalEndShortestMs)
        return gapless(Reason::NoRoom);
    Plan plan;
    plan.style = Style::NaturalEnd;
    plan.reason = Reason::FadeOut;
    plan.overlapMs = overlap;
    plan.outMs = from;
    plan.inMs = inStart;
    plan.switchMs = 0; // the next track carries on from its entrance
    const double seconds = double(overlap) / 1000;
    plan.gainOut = Automation::fadeOut(seconds * 0.75, seconds * 0.25); // what is left of it
    plan.gainIn = Automation::fadeIn(0, kEntranceSeconds);
    return plan;
}

// A crossfade in the current track's outro: after its last sung line and before the
// next track's first.
Plan outro(const Track& out, const Track& in, const Analysis& b, qint64 outEnd, qint64 inStart,
           qint64 earliest)
{
    qint64 from = std::max(outEnd - kOutroUsableMs, earliest);
    for (const Span& line : out.vocals)
        from = std::max(from, line.endMs + kVocalMarginMs);
    qint64 inRoom = kOutroLongestMs;
    for (const Span& line : in.vocals)
        inRoom = std::min(inRoom, line.startMs - kVocalMarginMs - inStart);
    qint64 overlap = std::min({outEnd - from, inRoom, kOutroLongestMs});
    if (b.loudnessOver(inStart, inStart + 2000) >= b.loudness - kLoudStartDb)
        overlap = std::min(overlap, kLoudStartLongestMs);
    if (overlap >= kMinimumOverlapMs)
        return crossfade(overlap, outEnd, inStart, Reason::Outro);
    // No room without singing over singing or cutting a line short: the shortest
    // crossfade, at the very end.
    overlap = std::min(kMinimumOverlapMs, outEnd - earliest);
    if (overlap < kMinimumOverlapMs / 2)
        return gapless(Reason::NoRoom);
    return crossfade(overlap, outEnd, inStart, Reason::Tight);
}
} // namespace

Plan plan(const Settings& settings, const Track& out, qint64 positionMs, const Track& in,
          bool sameFile)
{
    if (settings.mode == TransitionMode::Gapless)
        return gapless(Reason::Setting);
    if (out.preview || in.preview)
        return gapless(Reason::Preview);
    if (sameFile)
        return gapless(Reason::SameFile);
    if (out.durationMs <= 0 || in.durationMs <= 0)
        return gapless(Reason::NoDuration);
    // The current track's analysis is of all of it; the next's may be of its start only.
    const Analysis* a = out.analysis && out.analysis->complete ? out.analysis : nullptr;
    const Analysis* b = in.analysis;
    if (a && b && continuous(*a, *b))
        return gapless(Reason::Continuous);
    // Past these only silence: the current track's last sound, the next one's first.
    const qint64 outEnd = a ? std::clamp<qint64>(a->soundEndMs, 1, out.durationMs) : out.durationMs;
    const qint64 inStart = b ? std::clamp<qint64>(b->soundStartMs, 0, in.durationMs - 1) : 0;
    const qint64 earliest = positionMs + kStartMarginMs; // it can't start sooner

    if (settings.mode == TransitionMode::Smart && a && b) {
        if (a->fadeStartMs >= 0)
            return naturalEnd(*a, outEnd, inStart, earliest);
        return outro(out, in, *b, outEnd, inStart, earliest);
    }
    const bool fixed = settings.mode == TransitionMode::Crossfade;
    qint64 overlap = fixed ? settings.crossfadeMs : kSmartFallbackMs;
    // Never more than a third of either track: a short one isn't all fade.
    overlap = std::min({overlap, kMaximumOverlapMs, out.durationMs / 3, in.durationMs / 3});
    overlap = std::min(overlap, outEnd - earliest); // near the end already (a seek there)
    if (overlap < kMinimumOverlapMs)
        return gapless(Reason::NoRoom);
    return crossfade(overlap, outEnd, inStart, fixed ? Reason::Setting : Reason::NoAnalysis);
}

} // namespace cirrus::media::automix
