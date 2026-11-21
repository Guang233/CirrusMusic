#pragma once
#include "transition.h"
#include <QtGlobal>
#include <array>
#include <cmath>
#include <vector>

namespace cirrus::media::automix
{

// Above −1 dBFS a sample bends smoothly towards full scale instead of clipping. Only the
// overlap of a transition goes through it (two loud tracks can add up past full scale);
// everything else plays untouched.
[[nodiscard]] inline float softLimit(float sample)
{
    constexpr float kKnee = 0.891f; // −1 dBFS
    const float magnitude = std::fabs(sample);
    if (magnitude <= kKnee)
        return sample;
    const float bent = kKnee + (1 - kKnee) * std::tanh((magnitude - kKnee) / (1 - kKnee));
    return std::copysign(bent, sample);
}

// One transition's overlap, mixed piece by piece (docs/tasks/automix.md §5.1): the current
// track ("out") and the next ("in"), interleaved stereo at `rate`, with the plan's gains,
// bass cuts, sweep and echo. Keeps the filters' state from one piece to the next.
class Mixer
{
  public:
    Mixer(const Plan& plan, int rate);
    // Of the next `frames` frames, how many come before the next track begins (it gives
    // nothing for those: pull it for the rest only).
    [[nodiscard]] qsizetype silentIn(qsizetype frames) const;
    // The next `frames` frames into `dst` (which may be `out`). `in` has them all, the
    // silent ones first as zeros.
    void mix(const float* out, const float* in, float* dst, qsizetype frames);
    [[nodiscard]] qint64 offset() const { return m_offset; }

  private:
    struct Biquad {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        float z1 = 0, z2 = 0;
        float run(float x)
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };
    // A 4th-order Butterworth high-pass: two sections, per channel.
    struct HighPass {
        std::array<Biquad, 4> sections; // left 0–1, right 2–3
        void tune(double hz, int rate);
        float run(float x, int channel)
        {
            return sections[size_t(channel * 2 + 1)].run(sections[size_t(channel * 2)].run(x));
        }
    };

    Plan m_plan;
    int m_rate;
    qint64 m_offset = 0;
    qint64 m_delayFrames = 0;
    HighPass m_bassOut, m_bassIn, m_sweep;
    double m_sweepHz = 0;
    std::array<std::vector<float>, 2> m_echo; // per channel, one delay long
    size_t m_echoAt = 0;
};

} // namespace cirrus::media::automix
