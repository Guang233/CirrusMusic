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
    Plan gapless;
    if (settings.mode == TransitionMode::Gapless || out.preview || in.preview || sameFile ||
        out.durationMs <= 0 || in.durationMs <= 0)
        return gapless;
    qint64 overlap =
        settings.mode == TransitionMode::Crossfade ? settings.crossfadeMs : kSmartFallbackMs;
    // Never more than a third of either track: a short one isn't all fade.
    overlap = std::min({overlap, kMaximumOverlapMs, out.durationMs / 3, in.durationMs / 3});
    // The current track may be close to its end already (a seek there, a late preload).
    const qint64 room = out.durationMs - (positionMs + kStartMarginMs);
    overlap = std::min(overlap, room);
    if (overlap < kMinimumOverlapMs)
        return gapless;
    Plan crossfade;
    crossfade.style = Style::Crossfade;
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
