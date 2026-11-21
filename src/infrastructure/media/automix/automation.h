#pragma once
#include <vector>

namespace cirrus::media::automix
{

// A value over time (seconds from a transition's start): straight lines between the
// points, the first value before them and the last after. Gains now; filter settings and
// tempo later (docs/tasks/automix.md §5).
class Automation
{
  public:
    Automation() = default;
    explicit Automation(float constant) { m_points.push_back({0, constant}); }
    // Points in time order (an earlier time is taken as the last one's); two at the same
    // time make a jump: the second value from then on.
    Automation& add(double seconds, float value);
    [[nodiscard]] float at(double seconds) const;
    [[nodiscard]] bool empty() const { return m_points.empty(); }

    // Equal-power fades over [start, start + length]: quarter cosine and sine waves in
    // short straight pieces (error below −60 dB). Uncorrelated tracks keep their power
    // through a crossfade made of one of each.
    [[nodiscard]] static Automation fadeOut(double start, double length);
    [[nodiscard]] static Automation fadeIn(double start, double length);

  private:
    struct Point {
        double seconds;
        float value;
    };
    std::vector<Point> m_points;
};

} // namespace cirrus::media::automix
