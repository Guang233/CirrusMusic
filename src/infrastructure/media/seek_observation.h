#pragma once
#include "domain/playback/playback_types.h"

namespace cirrus::media
{
// Conservative native-position proof, not an acknowledgement. One candidate only.
// While playing, a forward move inside elapsed time + 250ms may be ordinary
// progress, so small/late forward seeks intentionally remain unconfirmed.
inline bool confirmedPositionJump(qint64 before, qint64 target, qint64 observed, qint64 elapsedMs,
                                  bool wasPlaying)
{
    constexpr auto max = domain::ObservedPlayback::MaximumMs;
    if (before < 0 || before > max || target < 0 || target > max || observed != target ||
        target == before || elapsedMs < 0 || elapsedMs > max)
        return false;
    return !wasPlaying || target < before || target - before > elapsedMs + 250;
}
} // namespace cirrus::media
