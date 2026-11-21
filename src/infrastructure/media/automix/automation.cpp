#include "automation.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace cirrus::media::automix
{
namespace
{
constexpr int kFadePieces = 48;

Automation quarterWave(double start, double length, bool rising)
{
    Automation curve;
    for (int i = 0; i <= kFadePieces; ++i) {
        const double t = double(i) / kFadePieces;
        const double angle = t * std::numbers::pi / 2;
        curve.add(start + t * length, float(rising ? std::sin(angle) : std::cos(angle)));
    }
    return curve;
}
} // namespace

Automation& Automation::add(double seconds, float value)
{
    if (!m_points.empty())
        seconds = std::max(seconds, m_points.back().seconds);
    m_points.push_back({seconds, value});
    return *this;
}

float Automation::at(double seconds) const
{
    if (m_points.empty())
        return 1;
    if (seconds <= m_points.front().seconds)
        return m_points.front().value;
    if (seconds >= m_points.back().seconds)
        return m_points.back().value;
    const auto after = std::upper_bound(m_points.begin(), m_points.end(), seconds,
                                        [](double t, const Point& p) { return t < p.seconds; });
    const auto before = after - 1;
    const double span = after->seconds - before->seconds;
    const double t = span > 0 ? (seconds - before->seconds) / span : 1;
    return float(before->value + (after->value - before->value) * t);
}

Automation Automation::fadeOut(double start, double length)
{
    return quarterWave(start, length, false);
}

Automation Automation::fadeIn(double start, double length)
{
    return quarterWave(start, length, true);
}

} // namespace cirrus::media::automix
