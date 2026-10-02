#pragma once
#include "domain/playback/playback_types.h"
#include <optional>

namespace cirrus::application
{
// Conservative 1x media-position progress, not acoustic proof or wall-clock listening.
// Caller supplies monotonic milliseconds and breaks on every transport discontinuity.
class ObservedPlaybackMeter
{
  public:
    static constexpr qint64 MaximumGapMs = 2000;
    static constexpr qint64 PositionToleranceMs = 250;
    domain::ObservedPlayback sample(qint64 positionMs, qint64 monotonicMs, bool eligible);
    void breakInterval() { m_anchor.reset(); }
    void reset()
    {
        m_progress = {};
        breakInterval();
    }
    [[nodiscard]] domain::ObservedPlayback progress() const { return m_progress; }

  private:
    struct Anchor {
        qint64 position, time;
    };
    std::optional<Anchor> m_anchor;
    domain::ObservedPlayback m_progress;
};
} // namespace cirrus::application
