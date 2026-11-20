#include "transition.h"
#include <algorithm>

namespace cirrus::media::automix
{
using domain::TransitionMode;

namespace
{
// Until the analysis comes (docs/tasks/automix.md §11 phase 2), the smart mode crossfades
// for this long.
constexpr qint64 kSmartFallbackMs = 6000;
} // namespace

Plan plan(const Settings& settings, const Track& out, qint64 positionMs, const Track& in,
          bool sameFile)
{
    const auto gapless = [](Reason reason) {
        Plan plan;
        plan.reason = reason;
        return plan;
    };
    if (settings.mode == TransitionMode::Gapless)
        return gapless(Reason::Setting);
    if (out.preview || in.preview)
        return gapless(Reason::Preview);
    if (sameFile)
        return gapless(Reason::SameFile);
    if (out.durationMs <= 0 || in.durationMs <= 0)
        return gapless(Reason::NoDuration);
    const bool fixed = settings.mode == TransitionMode::Crossfade;
    qint64 overlap = fixed ? settings.crossfadeMs : kSmartFallbackMs;
    // Never more than a third of either track: a short one isn't all fade.
    overlap = std::min({overlap, kMaximumOverlapMs, out.durationMs / 3, in.durationMs / 3});
    // The current track may be close to its end already (a seek there, a late preload).
    const qint64 room = out.durationMs - (positionMs + kStartMarginMs);
    overlap = std::min(overlap, room);
    if (overlap < kMinimumOverlapMs)
        return gapless(Reason::NoRoom);
    Plan crossfade;
    crossfade.style = Style::Crossfade;
    crossfade.reason = fixed ? Reason::Setting : Reason::NoAnalysis;
    crossfade.overlapMs = overlap;
    crossfade.outMs = out.durationMs - overlap;
    crossfade.inMs = 0;
    crossfade.switchMs = overlap / 2;
    const double seconds = double(overlap) / 1000;
    crossfade.gainOut = Automation::fadeOut(0, seconds);
    crossfade.gainIn = Automation::fadeIn(0, seconds);
    return crossfade;
}

} // namespace cirrus::media::automix
