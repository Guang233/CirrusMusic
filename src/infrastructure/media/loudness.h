#pragma once
#include "domain/playback/media_source.h"
#include <algorithm>
#include <cmath>

namespace cirrus::media
{

// Volume normalization (docs/tasks/audio-engine.md §6.5). The provider's gain brings a
// track to −18 LUFS (ReplayGain 2); we aim 4 dB higher, at −14 LUFS, so normalized
// music is not much quieter than the rest. The peak keeps a boost from clipping. No
// gain known: unchanged.
constexpr float kNormalizationTargetOffsetDb = 4.0f; // −18 → −14 LUFS

[[nodiscard]] inline float normalizationGain(const domain::MediaSource& source)
{
    if (!source.gainDb || !std::isfinite(*source.gainDb))
        return 1.0f;
    float linear = std::pow(10.0f, (*source.gainDb + kNormalizationTargetOffsetDb) / 20.0f);
    if (source.peak > 0)
        linear = std::min(linear, 1.0f / source.peak);
    return std::clamp(linear, 0.01f, 10.0f);
}

} // namespace cirrus::media
