#include "observed_playback.h"
#include <algorithm>

namespace cirrus::application
{
domain::ObservedPlayback ObservedPlaybackMeter::sample(qint64 position, qint64 now, bool eligible)
{
    if (!eligible || position < 0 || position > domain::ObservedPlayback::MaximumMs || now < 0) {
        breakInterval();
        return m_progress;
    }
    const Anchor next{position, now};
    if (!m_anchor) {
        m_anchor = next;
        return m_progress;
    }
    const auto previous = *m_anchor;
    if (now <= previous.time || position < previous.position) {
        m_anchor = next;
        return m_progress;
    }
    const auto elapsed = now - previous.time;
    const auto advanced = position - previous.position;
    if (elapsed > MaximumGapMs || advanced > elapsed + PositionToleranceMs) {
        m_anchor = next;
        return m_progress; // stall/suspend/jump: never fill in an unobserved interval
    }
    if (!advanced)
        return m_progress; // retain anchor for coarse position notifications
    const auto credit = std::min(advanced, elapsed);
    const auto remaining = domain::ObservedPlayback::MaximumMs - m_progress.playedMs;
    m_progress.playedMs += std::min(credit, remaining);
    m_progress.capped = m_progress.playedMs == domain::ObservedPlayback::MaximumMs;
    m_anchor = next;
    return m_progress;
}
} // namespace cirrus::application
