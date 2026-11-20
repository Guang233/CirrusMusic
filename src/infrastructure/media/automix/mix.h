#pragma once
#include "transition.h"
#include <QtGlobal>
#include <cmath>

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

// `frames` frames of the overlap, `offset` frames after its start at `rate`: the current
// track's (`out`) and the next's (`in`), both interleaved stereo, mixed with the plan's
// gains into `dst` (which may be `out`).
void mixOverlap(const Plan& plan, const float* out, const float* in, float* dst, qsizetype frames,
                qint64 offset, int rate);

} // namespace cirrus::media::automix
