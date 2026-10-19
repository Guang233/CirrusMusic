/*
 * Copyright 2026 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// C++ port by Cirrus of typescript/dynamiccolor/dynamic_scheme.ts and
// typescript/palettes/tonal_palette.ts (upstream 5b3618b1), limited to the
// 2021 and 2025 specs. See ../README.md.

#ifndef CIRRUS_MCU_DYNAMIC_SCHEME_H_
#define CIRRUS_MCU_DYNAMIC_SCHEME_H_

#include <unordered_map>
#include <vector>

#include "cpp/cam/hct.h"
#include "cpp/utils/utils.h"

namespace material_color_utilities::ts {

enum class Variant {
  kMonochrome,
  kNeutral,
  kTonalSpot,
  kVibrant,
  kExpressive,
  kFidelity,
  kContent,
  kRainbow,
  kFruitSalad,
};

enum class SpecVersion { k2021 = 2021, k2025 = 2025 };
enum class Platform { kPhone, kWatch };

bool IsBlue(double hue);
bool IsYellow(double hue);
bool IsCyan(double hue);

class TonalPalette {
 public:
  static TonalPalette FromHct(const Hct& hct);
  static TonalPalette FromHueAndChroma(double hue, double chroma);

  Argb Tone(double tone) const;
  Hct GetHct(double tone) const { return Hct(Tone(tone)); }

  double hue() const { return hue_; }
  double chroma() const { return chroma_; }
  const Hct& key_color() const { return key_color_; }

 private:
  TonalPalette(double hue, double chroma, Hct key_color)
      : hue_(hue), chroma_(chroma), key_color_(key_color) {}

  double hue_;
  double chroma_;
  Hct key_color_;
};

// Memoized tones of one scheme. Copies start empty: a modified copy of a
// scheme (e.g. forced light mode) must not reuse the original's results.
class ToneCache {
 public:
  ToneCache() = default;
  ToneCache(const ToneCache&) {}
  ToneCache& operator=(const ToneCache&) {
    tones_.clear();
    return *this;
  }
  const double* Find(const void* color) const {
    auto it = tones_.find(color);
    return it == tones_.end() ? nullptr : &it->second;
  }
  void Store(const void* color, double tone) const { tones_[color] = tone; }

 private:
  mutable std::unordered_map<const void*, double> tones_;
};

struct DynamicScheme {
  DynamicScheme(Hct source_color_hct, Variant variant, bool is_dark,
                double contrast_level, SpecVersion spec_version,
                Platform platform = Platform::kPhone);

  static double GetPiecewiseHue(const Hct& source,
                                const std::vector<double>& hue_breakpoints,
                                const std::vector<double>& hues);
  static double GetRotatedHue(const Hct& source,
                              const std::vector<double>& hue_breakpoints,
                              const std::vector<double>& rotations);

  Hct source_color_hct;
  Variant variant;
  bool is_dark;
  double contrast_level;
  Platform platform;
  SpecVersion spec_version;  // after per-variant fallback
  TonalPalette primary_palette;
  TonalPalette secondary_palette;
  TonalPalette tertiary_palette;
  TonalPalette neutral_palette;
  TonalPalette neutral_variant_palette;
  TonalPalette error_palette;
  ToneCache tone_cache;
};

}  // namespace material_color_utilities::ts

#endif  // CIRRUS_MCU_DYNAMIC_SCHEME_H_
