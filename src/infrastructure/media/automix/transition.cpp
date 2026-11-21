#include "transition.h"
#include "analysis.h"
#include <algorithm>
#include <cmath>
#include <optional>

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
// A beat mix (no stretching yet): both tracks rhythmic and steady where they meet, their
// bars clear, the tempos so close that the beats stay within kDriftMs of each other at
// the overlap's ends (they are aligned at its middle). 16, 8 or 4 bars, within the last
// kBeatMixUsableMs of the current track, which may ring on after its last bar line for at
// most kTailMs.
constexpr float kRhythmic = 0.3f;
constexpr double kSteadyShare = 0.9;
constexpr double kSteadyRmsMs = 8;
constexpr float kClearBars = 0.15f;
constexpr double kDriftMs = 15;
constexpr double kOctave = 0.05; // a beat this close to twice the other's: the same tempo
constexpr qint64 kBeatMixUsableMs = 32000;
constexpr qint64 kGridMs = 40000; // the stretch of each track the grids are fitted over
constexpr int kBarChoices[] = {16, 8, 4};
// Keys that clash (docs/tasks/automix.md §5.1): melodies over each other only briefly, a
// beat mix of at most kClashBars, a crossfade of at most kClashLongestMs.
constexpr int kClashBars = 4;
constexpr qint64 kClashLongestMs = 3000;
constexpr double kTailMs = 2000;
constexpr double kBassSwapSeconds = 0.06; // the swap's ramp, ending on the middle downbeat
// A joint is continuous only at about one tempo: beats over this either side, this close.
constexpr qint64 kJointBeatsMs = 20000;
constexpr double kJointTempo = 0.03;
// Filter out: a current track that ends at about its usual level (within kFullEndDb over
// its last 2 s) on a steady beat. Its last kSweepBeats go through a high-pass rising from
// kSweepFromHz to kSweepToHz, into a beat-long echo that rings on for kEchoTailMs.
constexpr float kFullEndDb = 6;
constexpr int kSweepBeats = 4;
constexpr double kSweepFromHz = 20, kSweepToHz = 1500;
constexpr float kEchoFeedback = 0.45f;
constexpr float kEchoLevel = 0.6f;
constexpr qint64 kEchoTailMs = 3000;

Plan gapless(Reason reason)
{
    Plan plan;
    plan.reason = reason;
    return plan;
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

// Steady beats in a stretch of a track: their grid, and where a bar starts (the beats'
// index of a downbeat in it).
struct Steady {
    Grid grid;
    int downbeat = -1; // -1: the bars aren't clear
};
std::optional<Steady> steadyBeats(const Rhythm& r, double fromMs, double toMs)
{
    if (r.beats.empty() || r.strength < kRhythmic)
        return std::nullopt;
    Steady steady;
    steady.grid = fitGrid(r.beats, fromMs, toMs);
    const Grid& g = steady.grid;
    if (!g.valid() || g.inliers < kSteadyShare || g.rmsMs > kSteadyRmsMs)
        return std::nullopt;
    const BarPhase bars = barPhase(r, g.first, g.first + g.count);
    if (bars.confidence >= kClearBars) {
        steady.downbeat = g.first;
        while ((steady.downbeat - bars.downbeat) % 4 != 0)
            ++steady.downbeat;
    }
    return steady;
}
// The grid's index of a beat time.
double gridIndex(const Grid& g, double ms) { return std::round((ms - g.phaseMs) / g.periodMs); }

// One track running straight into the next (a live album, a DJ mix, movements): the
// first has no silence and no fade-out at its end, the second no silence at its start.
// Not when both are on a steady beat there at tempos apart (octaves aside): two songs that
// just end and start at full level. A live set or a mix keeps its tempo across.
bool continuous(const Analysis& a, const Analysis& b)
{
    const bool runsOn = a.durationMs - a.soundEndMs <= kEdgeMs &&
                        (a.fadeStartMs < 0 || a.fadedBy(kNaturalEndDb) >= a.soundEndMs);
    if (!runsOn || b.soundStartMs > kEdgeMs)
        return false;
    const auto sa =
        steadyBeats(a.rhythm, double(a.soundEndMs - kJointBeatsMs), double(a.soundEndMs) + 1);
    const auto sb =
        steadyBeats(b.rhythm, double(b.soundStartMs), double(b.soundStartMs + kJointBeatsMs));
    if (!sa || !sb)
        return true;
    double ratio = sb->grid.periodMs / sa->grid.periodMs;
    while (ratio > 1.5)
        ratio /= 2;
    while (ratio < 0.75)
        ratio *= 2;
    return std::fabs(1 - ratio) <= kJointTempo;
}

// Where two tracks' beats can meet: the current track's end and the next one's start both
// steady. Their lines are bar lines when both tracks' bars are clear, else beats. From the
// grids, not the beats found: they may stop short of the edges.
struct Meeting {
    Grid out, in;
    bool bars = false;  // the lines are bar lines
    double downOut = 0; // a grid index of the current track's lines
    double firstIn = 0; // the next track's first line from its first sound on
    double firstBeatIn = 0;
    double drift = 0;                                       // the tempos' relative difference
    [[nodiscard]] int unit() const { return bars ? 4 : 1; } // beats a line
    // The same with beats for lines (where a bar line doesn't fit).
    [[nodiscard]] Meeting byBeats() const
    {
        Meeting m = *this;
        m.bars = false;
        m.downOut = 0;
        m.firstIn = firstBeatIn;
        return m;
    }
    // The current track's line nearest `ms`, the last at or before it, the first at or after.
    [[nodiscard]] double lineNear(double ms) const
    {
        return line(ms, [](double x) { return std::round(x); });
    }
    [[nodiscard]] double lastLineBy(double ms) const
    {
        return line(ms, [](double x) { return std::floor(x); });
    }
    [[nodiscard]] double firstLineFrom(double ms) const
    {
        return line(ms, [](double x) { return std::ceil(x); });
    }

  private:
    template <typename Round> [[nodiscard]] double line(double ms, Round round) const
    {
        const double lines = ((ms - out.phaseMs) / out.periodMs - downOut) / unit();
        return out.at(downOut + unit() * round(lines));
    }
};
std::optional<Meeting> meeting(const Analysis& a, const Analysis& b, qint64 outEnd, qint64 inStart)
{
    const auto sa = steadyBeats(a.rhythm, double(outEnd - kGridMs), double(outEnd) + 1);
    const auto sb =
        steadyBeats(b.rhythm, double(inStart), double(std::min(inStart + kGridMs, b.durationMs)));
    if (!sa || !sb)
        return std::nullopt;
    Meeting m;
    m.out = sa->grid;
    m.in = sb->grid;
    // One beat twice the other (a fast song tracked at half its tempo): the slower grid
    // halved, a beat between each two. Their bars then aren't alike: lines are beats.
    bool halved = false;
    if (const double ratio = m.in.periodMs / m.out.periodMs; std::fabs(ratio / 2 - 1) < kOctave) {
        m.in.periodMs /= 2;
        halved = true;
    } else if (std::fabs(ratio * 2 - 1) < kOctave) {
        m.out.periodMs /= 2;
        halved = true;
    }
    m.bars = !halved && sa->downbeat >= 0 && sb->downbeat >= 0;
    m.drift = std::fabs(1 - m.in.periodMs / m.out.periodMs);
    m.downOut = m.bars ? gridIndex(m.out, a.rhythm.beats[size_t(sa->downbeat)]) : 0;
    const double downIn = m.bars ? gridIndex(m.in, b.rhythm.beats[size_t(sb->downbeat)]) : 0;
    const double fromIn = (double(inStart) - m.in.periodMs / 4 - m.in.phaseMs) / m.in.periodMs;
    m.firstIn = m.in.at(downIn + m.unit() * std::ceil((fromIn - downIn) / m.unit()));
    m.firstBeatIn = m.in.at(std::ceil(fromIn));
    return m;
}

// A crossfade between two tracks whose beats meet: the next track's first line on one of
// the current track's, the overlap between `shortest` and `longest` (as long as it can be).
std::optional<Plan> crossfadeOnTheBeat(const Meeting& m, qint64 shortest, qint64 longest,
                                       qint64 outEnd, qint64 inStart, Reason reason)
{
    for (const Meeting& lines : {m, m.byBeats()}) {
        const double lead = lines.firstIn - double(inStart); // its sound before its first line
        const auto outMs =
            qint64(std::ceil(lines.firstLineFrom(double(outEnd - longest) + lead) - lead));
        const qint64 overlap = outEnd - outMs;
        if (overlap < shortest || overlap > longest || m.drift * double(overlap) > kDriftMs)
            continue;
        Plan plan = crossfade(overlap, outEnd, inStart, reason);
        plan.onBeat = true;
        return plan;
    }
    return std::nullopt;
}

// The current track fades out by itself: let it, and bring the next in from its first
// sound once the fade is well down. The original ending stays. When their beats can meet,
// the next track's first bar line comes on one of the current one's.
Plan naturalEnd(const Analysis& a, const Analysis* b, qint64 outEnd, qint64 inStart,
                qint64 earliest)
{
    qint64 from = std::max(a.fadedBy(kNaturalEndDb), earliest);
    bool onBeat = false;
    if (const auto m = b ? meeting(a, *b, outEnd, inStart) : std::nullopt) {
        for (const Meeting& lines : {*m, m->byBeats()}) {
            const double lead = lines.firstIn - double(inStart); // its sound before its first line
            double at = lines.lineNear(double(from) + lead);
            while (at - lead < double(earliest))
                at += lines.unit() * lines.out.periodMs;
            const auto start = qint64(std::llround(at - lead));
            const qint64 overlap = std::min(outEnd - start, kNaturalEndLongestMs);
            if (overlap >= kNaturalEndShortestMs && m->drift * double(overlap) <= kDriftMs) {
                from = start;
                onBeat = true;
                break;
            }
        }
    }
    const qint64 overlap = std::min(outEnd - from, kNaturalEndLongestMs);
    if (overlap < kNaturalEndShortestMs)
        return gapless(Reason::NoRoom);
    Plan plan;
    plan.style = Style::NaturalEnd;
    plan.reason = Reason::FadeOut;
    plan.onBeat = onBeat;
    plan.overlapMs = overlap;
    plan.outMs = from;
    plan.inMs = inStart;
    plan.switchMs = 0; // the next track carries on from its entrance
    const double seconds = double(overlap) / 1000;
    plan.gainOut = Automation::fadeOut(seconds * 0.75, seconds * 0.25); // what is left of it
    plan.gainIn = Automation::fadeIn(0, kEntranceSeconds);
    return plan;
}

// On the beat: the next track's first bar line on the current one's, `bars` bars before
// the current track's last; the beats aligned at the middle bar line, where the bass is
// swapped; the rest an equal-power crossfade over the whole overlap.
std::optional<Plan> beatMix(const Track& out, const Analysis& a, const Track& in, const Analysis& b,
                            qint64 outEnd, qint64 inStart, qint64 earliest)
{
    const auto m = meeting(a, b, outEnd, inStart);
    if (!m || !m->bars)
        return std::nullopt;
    const double pa = m->out.periodMs, pb = m->in.periodMs;
    // The current track's last bar line: at the end of its sound (half a beat late at most:
    // a last hit on it).
    const double endA = m->lastLineBy(double(outEnd) + pa / 2);
    const double lastA = a.rhythm.beats[size_t(m->out.first + m->out.count) - 1];
    if (double(outEnd) - endA > kTailMs || lastA < endA - 16 * pa)
        return std::nullopt; // a long tail after its last bar line, or beats long gone
    const bool clash = harmony(a.endKey, b.startKey) == Harmony::Clash;
    for (const int bars : kBarChoices) {
        if (clash && bars > kClashBars)
            continue;
        const double half = 2.0 * bars; // beats to the middle
        if (m->drift * half * pa > kDriftMs)
            continue;
        const double middleA = endA - half * pa, middleB = m->firstIn + half * pb;
        const double endB = m->firstIn + 2 * half * pb;
        if (endB > double(std::min(inStart + kGridMs, b.durationMs)))
            continue; // past what the grid (or the analysis) covers
        const auto outMs = qint64(std::llround(middleA - (middleB - double(inStart))));
        if (outMs < earliest || outMs < outEnd - kBeatMixUsableMs)
            continue;
        // Never both singing: the current track done by the middle, the next not before.
        const double shift = double(outMs - inStart); // the next track's time + shift
        bool clash = false;
        for (const Span& line : out.vocals)
            clash |=
                (double(line.endMs + kVocalMarginMs) > middleA) && (double(line.startMs) < endA);
        for (const Span& line : in.vocals)
            clash |= (double(line.startMs - kVocalMarginMs) + shift < middleA) &&
                     (double(line.endMs) + shift > double(outMs));
        if (clash)
            continue;
        Plan plan;
        plan.style = Style::BeatMix;
        plan.reason = Reason::Beats;
        plan.onBeat = true;
        plan.bars = bars;
        plan.outMs = outMs;
        plan.inMs = inStart;
        plan.overlapMs = qint64(std::llround(endA)) - outMs;
        plan.switchMs = qint64(std::llround(middleA)) - outMs;
        const double seconds = double(plan.overlapMs) / 1000, middle = double(plan.switchMs) / 1000;
        plan.gainOut = Automation::fadeOut(0, seconds);
        plan.gainIn = Automation::fadeIn(0, seconds);
        plan.bassCutIn.add(0, 1).add(middle - kBassSwapSeconds, 1).add(middle, 0);
        plan.bassCutOut.add(0, 0).add(middle - kBassSwapSeconds, 0).add(middle, 1);
        return plan;
    }
    return std::nullopt;
}

// The current track ends on a full beat the next can't take up: its last beats filtered
// away into an echo of them, the next coming in on its first bar line (or beat, or sound)
// as the current one stops.
std::optional<Plan> filterOut(const Analysis& a, const Analysis& b, qint64 outEnd, qint64 inStart,
                              qint64 earliest)
{
    if (a.fadeStartMs >= 0 || a.loudnessOver(outEnd - 2000, outEnd) < a.loudness - kFullEndDb)
        return std::nullopt;
    const auto sa = steadyBeats(a.rhythm, double(outEnd - kGridMs / 2), double(outEnd) + 1);
    if (!sa)
        return std::nullopt;
    const double pa = sa->grid.periodMs;
    const double lastA = a.rhythm.beats[size_t(sa->grid.first + sa->grid.count) - 1];
    if (double(outEnd) - lastA > 2 * pa)
        return std::nullopt; // its beat stops before it ends
    const double end = std::min(double(outEnd), lastA + pa);
    const auto outMs = qint64(std::llround(end - kSweepBeats * pa));
    if (outMs < earliest)
        return std::nullopt;
    // The next track's sound before its first bar line (or beat) plays under the sweep.
    double first = double(inStart);
    if (const auto sb = steadyBeats(b.rhythm, double(inStart), double(inStart + kGridMs / 2)))
        first = b.rhythm.beats[size_t(sb->downbeat >= 0 ? sb->downbeat : sb->grid.first)];
    const double lead = std::clamp(first - double(inStart), 0.0, kSweepBeats * pa);
    Plan plan;
    plan.style = Style::FilterOut;
    plan.reason = Reason::FullEnd;
    plan.outMs = outMs;
    plan.inMs = inStart;
    const double endSeconds = (end - double(outMs)) / 1000;
    plan.switchMs = qint64(std::llround(end)) - outMs;
    plan.inDelayMs = std::max<qint64>(0, plan.switchMs - qint64(std::llround(lead)));
    plan.overlapMs = plan.switchMs + kEchoTailMs;
    plan.gainOut.add(0, 1).add(endSeconds - 0.03, 1).add(endSeconds, 0);
    for (int i = 0; i <= 8; ++i)
        plan.sweepOut.add(endSeconds * i / 8,
                          float(kSweepFromHz * std::pow(kSweepToHz / kSweepFromHz, i / 8.0)));
    plan.echoMs = pa;
    plan.echoFeedback = kEchoFeedback;
    plan.echoSend.add(0, 0)
        .add(endSeconds - 2 * pa / 1000, 0)
        .add(endSeconds, kEchoLevel)
        .add(endSeconds + 0.01, 0);
    const double delay = double(plan.inDelayMs) / 1000;
    plan.gainIn.add(delay, 0).add(delay + kEntranceSeconds, 1);
    return plan;
}

// A crossfade in the current track's outro: after its last sung line and before the
// next track's first.
// On the beat when their beats meet.
Plan outro(const Track& out, const Analysis& a, const Track& in, const Analysis& b, qint64 outEnd,
           qint64 inStart, qint64 earliest)
{
    const auto m = meeting(a, b, outEnd, inStart);
    qint64 from = std::max(outEnd - kOutroUsableMs, earliest);
    for (const Span& line : out.vocals)
        from = std::max(from, line.endMs + kVocalMarginMs);
    qint64 inRoom = kOutroLongestMs;
    for (const Span& line : in.vocals)
        inRoom = std::min(inRoom, line.startMs - kVocalMarginMs - inStart);
    qint64 overlap = std::min({outEnd - from, inRoom, kOutroLongestMs});
    if (b.loudnessOver(inStart, inStart + 2000) >= b.loudness - kLoudStartDb)
        overlap = std::min(overlap, kLoudStartLongestMs);
    if (harmony(a.endKey, b.startKey) == Harmony::Clash)
        overlap = std::min(overlap, kClashLongestMs);
    if (overlap >= kMinimumOverlapMs) {
        if (const auto onTheBeat = m ? crossfadeOnTheBeat(*m, kMinimumOverlapMs, overlap, outEnd,
                                                          inStart, Reason::Outro)
                                     : std::nullopt)
            return *onTheBeat;
        return crossfade(overlap, outEnd, inStart, Reason::Outro);
    }
    // No room without singing over singing or cutting a line short: the shortest
    // crossfade, at the very end.
    overlap = std::min(kMinimumOverlapMs, outEnd - earliest);
    if (overlap < kMinimumOverlapMs / 2)
        return gapless(Reason::NoRoom);
    if (const auto onTheBeat = m ? crossfadeOnTheBeat(*m, kMinimumOverlapMs / 2, overlap, outEnd,
                                                      inStart, Reason::Tight)
                                 : std::nullopt)
        return *onTheBeat;
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
        Plan p = [&] {
            if (a->fadeStartMs >= 0)
                return naturalEnd(*a, b, outEnd, inStart, earliest);
            if (auto onTheBeat = beatMix(out, *a, in, *b, outEnd, inStart, earliest))
                return *onTheBeat;
            if (auto filtered = filterOut(*a, *b, outEnd, inStart, earliest))
                return *filtered;
            return outro(out, *a, in, *b, outEnd, inStart, earliest);
        }();
        p.harmony = harmony(a->endKey, b->startKey);
        return p;
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
