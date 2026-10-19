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

// C++ port by Cirrus; see dynamic_scheme.h.

#include "cirrus/dynamic_scheme.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "cirrus/temperature.h"

namespace material_color_utilities::ts {

namespace {

int RoundJs(double value) { return static_cast<int>(std::floor(value + 0.5)); }

// palettes/tonal_palette.ts KeyColor: the tone closest to 50 that can hold
// the requested chroma.
Hct CreateKeyColor(double hue, double requested_chroma) {
  constexpr int kPivotTone = 50;
  constexpr int kToneStepSize = 1;
  constexpr double kEpsilon = 0.01;
  constexpr double kMaxChroma = 200.0;
  std::map<int, double> cache;
  auto max_chroma = [&](int tone) {
    auto it = cache.find(tone);
    if (it != cache.end()) return it->second;
    const double chroma = Hct(hue, kMaxChroma, tone).get_chroma();
    cache.emplace(tone, chroma);
    return chroma;
  };
  int lower_tone = 0;
  int upper_tone = 100;
  while (lower_tone < upper_tone) {
    const int mid_tone = (lower_tone + upper_tone) / 2;
    const bool is_ascending =
        max_chroma(mid_tone) < max_chroma(mid_tone + kToneStepSize);
    const bool sufficient_chroma =
        max_chroma(mid_tone) >= requested_chroma - kEpsilon;
    if (sufficient_chroma) {
      if (std::abs(lower_tone - kPivotTone) <
          std::abs(upper_tone - kPivotTone)) {
        upper_tone = mid_tone;
      } else {
        if (lower_tone == mid_tone) {
          return Hct(hue, requested_chroma, lower_tone);
        }
        lower_tone = mid_tone;
      }
    } else if (is_ascending) {
      lower_tone = mid_tone + kToneStepSize;
    } else {
      upper_tone = mid_tone;
    }
  }
  return Hct(hue, requested_chroma, lower_tone);
}

Argb AverageArgb(Argb a, Argb b) {
  const int red = RoundJs((RedFromInt(a) + RedFromInt(b)) / 2.0);
  const int green = RoundJs((GreenFromInt(a) + GreenFromInt(b)) / 2.0);
  const int blue = RoundJs((BlueFromInt(a) + BlueFromInt(b)) / 2.0);
  return ArgbFromRgb(red & 255, green & 255, blue & 255);
}

SpecVersion FallbackSpecVersion(SpecVersion version, Variant variant) {
  if (variant == Variant::kExpressive || variant == Variant::kVibrant ||
      variant == Variant::kTonalSpot || variant == Variant::kNeutral) {
    return version;
  }
  return SpecVersion::k2021;
}

using P = TonalPalette;

P Primary2021(Variant v, const Hct& s) {
  switch (v) {
    case Variant::kContent:
    case Variant::kFidelity:
      return P::FromHueAndChroma(s.get_hue(), s.get_chroma());
    case Variant::kFruitSalad:
      return P::FromHueAndChroma(SanitizeDegreesDouble(s.get_hue() - 50.0),
                                 48.0);
    case Variant::kMonochrome:
      return P::FromHueAndChroma(s.get_hue(), 0.0);
    case Variant::kNeutral:
      return P::FromHueAndChroma(s.get_hue(), 12.0);
    case Variant::kRainbow:
      return P::FromHueAndChroma(s.get_hue(), 48.0);
    case Variant::kTonalSpot:
      return P::FromHueAndChroma(s.get_hue(), 36.0);
    case Variant::kExpressive:
      return P::FromHueAndChroma(SanitizeDegreesDouble(s.get_hue() + 240), 40);
    case Variant::kVibrant:
      return P::FromHueAndChroma(s.get_hue(), 200.0);
  }
  return P::FromHueAndChroma(s.get_hue(), 36.0);
}

P Secondary2021(Variant v, const Hct& s) {
  switch (v) {
    case Variant::kContent:
    case Variant::kFidelity:
      return P::FromHueAndChroma(
          s.get_hue(),
          std::max(s.get_chroma() - 32.0, s.get_chroma() * 0.5));
    case Variant::kFruitSalad:
      return P::FromHueAndChroma(SanitizeDegreesDouble(s.get_hue() - 50.0),
                                 36.0);
    case Variant::kMonochrome:
      return P::FromHueAndChroma(s.get_hue(), 0.0);
    case Variant::kNeutral:
      return P::FromHueAndChroma(s.get_hue(), 8.0);
    case Variant::kRainbow:
    case Variant::kTonalSpot:
      return P::FromHueAndChroma(s.get_hue(), 16.0);
    case Variant::kExpressive:
      return P::FromHueAndChroma(
          DynamicScheme::GetRotatedHue(
              s, {0, 21, 51, 121, 151, 191, 271, 321, 360},
              {45, 95, 45, 20, 45, 90, 45, 45, 45}),
          24.0);
    case Variant::kVibrant:
      return P::FromHueAndChroma(
          DynamicScheme::GetRotatedHue(
              s, {0, 41, 61, 101, 131, 181, 251, 301, 360},
              {18, 15, 10, 12, 15, 18, 15, 12, 12}),
          24.0);
  }
  return P::FromHueAndChroma(s.get_hue(), 16.0);
}

P Tertiary2021(Variant v, const Hct& s) {
  switch (v) {
    case Variant::kContent:
      return P::FromHct(
          FixIfDislikedTs(TemperatureCache(s).Analogous(3, 6)[2]));
    case Variant::kFidelity:
      return P::FromHct(FixIfDislikedTs(TemperatureCache(s).Complement()));
    case Variant::kFruitSalad:
      return P::FromHueAndChroma(s.get_hue(), 36.0);
    case Variant::kMonochrome:
      return P::FromHueAndChroma(s.get_hue(), 0.0);
    case Variant::kNeutral:
      return P::FromHueAndChroma(s.get_hue(), 16.0);
    case Variant::kRainbow:
    case Variant::kTonalSpot:
      return P::FromHueAndChroma(SanitizeDegreesDouble(s.get_hue() + 60.0),
                                 24.0);
    case Variant::kExpressive:
      return P::FromHueAndChroma(
          DynamicScheme::GetRotatedHue(
              s, {0, 21, 51, 121, 151, 191, 271, 321, 360},
              {120, 120, 20, 45, 20, 15, 20, 120, 120}),
          32.0);
    case Variant::kVibrant:
      return P::FromHueAndChroma(
          DynamicScheme::GetRotatedHue(
              s, {0, 41, 61, 101, 131, 181, 251, 301, 360},
              {35, 30, 20, 25, 30, 35, 30, 25, 25}),
          32.0);
  }
  return P::FromHueAndChroma(s.get_hue(), 24.0);
}

P Neutral2021(Variant v, const Hct& s) {
  switch (v) {
    case Variant::kContent:
    case Variant::kFidelity:
      return P::FromHueAndChroma(s.get_hue(), s.get_chroma() / 8.0);
    case Variant::kFruitSalad:
      return P::FromHueAndChroma(s.get_hue(), 10.0);
    case Variant::kMonochrome:
    case Variant::kRainbow:
      return P::FromHueAndChroma(s.get_hue(), 0.0);
    case Variant::kNeutral:
      return P::FromHueAndChroma(s.get_hue(), 2.0);
    case Variant::kTonalSpot:
      return P::FromHueAndChroma(s.get_hue(), 6.0);
    case Variant::kExpressive:
      return P::FromHueAndChroma(SanitizeDegreesDouble(s.get_hue() + 15), 8);
    case Variant::kVibrant:
      return P::FromHueAndChroma(s.get_hue(), 10);
  }
  return P::FromHueAndChroma(s.get_hue(), 6.0);
}

P NeutralVariant2021(Variant v, const Hct& s) {
  switch (v) {
    case Variant::kContent:
    case Variant::kFidelity:
      return P::FromHueAndChroma(s.get_hue(), (s.get_chroma() / 8.0) + 4.0);
    case Variant::kFruitSalad:
      return P::FromHueAndChroma(s.get_hue(), 16.0);
    case Variant::kMonochrome:
    case Variant::kRainbow:
      return P::FromHueAndChroma(s.get_hue(), 0.0);
    case Variant::kNeutral:
      return P::FromHueAndChroma(s.get_hue(), 2.0);
    case Variant::kTonalSpot:
      return P::FromHueAndChroma(s.get_hue(), 8.0);
    case Variant::kExpressive:
      return P::FromHueAndChroma(SanitizeDegreesDouble(s.get_hue() + 15), 12);
    case Variant::kVibrant:
      return P::FromHueAndChroma(s.get_hue(), 12);
  }
  return P::FromHueAndChroma(s.get_hue(), 8.0);
}

double ExpressiveNeutralHue(const Hct& s) {
  return DynamicScheme::GetRotatedHue(s, {0, 71, 124, 253, 278, 300, 360},
                                      {10, 0, 10, 0, 10, 0});
}

double ExpressiveNeutralChroma(const Hct& s, bool dark, Platform platform) {
  const double hue = ExpressiveNeutralHue(s);
  return platform == Platform::kPhone ? (dark ? (IsYellow(hue) ? 6 : 14) : 18)
                                      : 12;
}

double VibrantNeutralHue(const Hct& s) {
  return DynamicScheme::GetRotatedHue(s, {0, 38, 105, 140, 333, 360},
                                      {-14, 10, -14, 10, -14});
}

double VibrantNeutralChroma(const Hct& s, Platform platform) {
  const double hue = VibrantNeutralHue(s);
  return platform == Platform::kPhone ? 28 : (IsBlue(hue) ? 28 : 20);
}

struct Palettes {
  P primary, secondary, tertiary, neutral, neutral_variant, error;
};

Palettes Make2021(Variant v, const Hct& s) {
  return {Primary2021(v, s),       Secondary2021(v, s),
          Tertiary2021(v, s),      Neutral2021(v, s),
          NeutralVariant2021(v, s), P::FromHueAndChroma(25.0, 84.0)};
}

// Only Neutral, TonalSpot, Expressive and Vibrant reach this (see fallback).
Palettes Make2025(Variant v, const Hct& s, bool dark, Platform platform) {
  const bool phone = platform == Platform::kPhone;
  const double hue = s.get_hue();
  const double error_hue = DynamicScheme::GetPiecewiseHue(
      s, {0, 3, 13, 23, 33, 43, 153, 273, 360},
      {12, 22, 32, 12, 22, 32, 22, 12});
  switch (v) {
    case Variant::kNeutral:
      return {
          P::FromHueAndChroma(hue, phone ? (IsBlue(hue) ? 12 : 8)
                                         : (IsBlue(hue) ? 16 : 12)),
          P::FromHueAndChroma(hue, phone ? (IsBlue(hue) ? 6 : 4)
                                         : (IsBlue(hue) ? 10 : 6)),
          P::FromHueAndChroma(
              DynamicScheme::GetRotatedHue(
                  s, {0, 38, 105, 161, 204, 278, 333, 360},
                  {-32, 26, 10, -39, 24, -15, -32}),
              phone ? 20 : 36),
          P::FromHueAndChroma(hue, phone ? 1.4 : 6),
          P::FromHueAndChroma(hue, (phone ? 1.4 : 6) * 2.2),
          P::FromHueAndChroma(error_hue, phone ? 50 : 40)};
    case Variant::kTonalSpot:
      return {P::FromHueAndChroma(hue, phone && dark ? 26 : 32),
              P::FromHueAndChroma(hue, 16),
              P::FromHueAndChroma(
                  DynamicScheme::GetRotatedHue(s, {0, 20, 71, 161, 333, 360},
                                               {-40, 48, -32, 40, -32}),
                  phone ? 28 : 32),
              P::FromHueAndChroma(hue, phone ? 5 : 10),
              P::FromHueAndChroma(hue, (phone ? 5 : 10) * 1.7),
              P::FromHueAndChroma(error_hue, phone ? 60 : 48)};
    case Variant::kExpressive: {
      const double neutral_hue = ExpressiveNeutralHue(s);
      const double neutral_chroma = ExpressiveNeutralChroma(s, dark, platform);
      return {
          P::FromHueAndChroma(hue, phone ? (dark ? 36 : 48) : 40),
          P::FromHueAndChroma(
              DynamicScheme::GetRotatedHue(
                  s, {0, 105, 140, 204, 253, 278, 300, 333, 360},
                  {-160, 155, -100, 96, -96, -156, -165, -160}),
              phone ? (dark ? 16 : 24) : 24),
          P::FromHueAndChroma(
              DynamicScheme::GetRotatedHue(
                  s, {0, 105, 140, 204, 253, 278, 300, 333, 360},
                  {-165, 160, -105, 101, -101, -160, -170, -165}),
              48),
          P::FromHueAndChroma(neutral_hue, neutral_chroma),
          P::FromHueAndChroma(
              neutral_hue,
              neutral_chroma *
                  (neutral_hue >= 105 && neutral_hue < 125 ? 1.6 : 2.3)),
          P::FromHueAndChroma(error_hue, phone ? 64 : 48)};
    }
    case Variant::kVibrant: {
      const double neutral_hue = VibrantNeutralHue(s);
      const double neutral_chroma = VibrantNeutralChroma(s, platform);
      return {P::FromHueAndChroma(hue, phone ? 74 : 56),
              P::FromHueAndChroma(
                  DynamicScheme::GetRotatedHue(s, {0, 38, 105, 140, 333, 360},
                                               {-14, 10, -14, 10, -14}),
                  phone ? 56 : 36),
              P::FromHueAndChroma(
                  DynamicScheme::GetRotatedHue(
                      s, {0, 38, 71, 105, 140, 161, 253, 333, 360},
                      {-72, 35, 24, -24, 62, 50, 62, -72}),
                  56),
              P::FromHueAndChroma(neutral_hue, neutral_chroma),
              P::FromHueAndChroma(neutral_hue, neutral_chroma * 1.29),
              P::FromHueAndChroma(error_hue, phone ? 80 : 60)};
    }
    default:
      return Make2021(v, s);
  }
}

Palettes MakePalettes(Variant v, const Hct& s, bool dark, Platform platform,
                      SpecVersion version) {
  return version == SpecVersion::k2025 ? Make2025(v, s, dark, platform)
                                       : Make2021(v, s);
}

}  // namespace

bool IsBlue(double hue) { return hue >= 250 && hue < 270; }
bool IsYellow(double hue) { return hue >= 105 && hue < 125; }
bool IsCyan(double hue) { return hue >= 170 && hue < 207; }

TonalPalette TonalPalette::FromHct(const Hct& hct) {
  return TonalPalette(hct.get_hue(), hct.get_chroma(), hct);
}

TonalPalette TonalPalette::FromHueAndChroma(double hue, double chroma) {
  return TonalPalette(hue, chroma, CreateKeyColor(hue, chroma));
}

Argb TonalPalette::Tone(double tone) const {
  if (tone == 99 && IsYellow(hue_)) {
    return AverageArgb(Tone(98), Tone(100));
  }
  return Hct(hue_, chroma_, tone).ToInt();
}

DynamicScheme::DynamicScheme(Hct source, Variant v, bool dark,
                             double contrast, SpecVersion version,
                             Platform p)
    : source_color_hct(source),
      variant(v),
      is_dark(dark),
      contrast_level(contrast),
      platform(p),
      spec_version(FallbackSpecVersion(version, v)),
      primary_palette(TonalPalette::FromHueAndChroma(0, 0)),
      secondary_palette(primary_palette),
      tertiary_palette(primary_palette),
      neutral_palette(primary_palette),
      neutral_variant_palette(primary_palette),
      error_palette(primary_palette) {
  Palettes palettes = MakePalettes(v, source, dark, p, spec_version);
  primary_palette = palettes.primary;
  secondary_palette = palettes.secondary;
  tertiary_palette = palettes.tertiary;
  neutral_palette = palettes.neutral;
  neutral_variant_palette = palettes.neutral_variant;
  error_palette = palettes.error;
}

double DynamicScheme::GetPiecewiseHue(const Hct& source,
                                      const std::vector<double>& breakpoints,
                                      const std::vector<double>& hues) {
  const std::size_t size = std::min(breakpoints.size() - 1, hues.size());
  const double source_hue = source.get_hue();
  for (std::size_t i = 0; i < size; ++i) {
    if (source_hue >= breakpoints[i] && source_hue < breakpoints[i + 1]) {
      return SanitizeDegreesDouble(hues[i]);
    }
  }
  return source_hue;
}

double DynamicScheme::GetRotatedHue(const Hct& source,
                                    const std::vector<double>& breakpoints,
                                    const std::vector<double>& rotations) {
  double rotation = GetPiecewiseHue(source, breakpoints, rotations);
  if (std::min(breakpoints.size() - 1, rotations.size()) <= 0) {
    rotation = 0;
  }
  return SanitizeDegreesDouble(source.get_hue() + rotation);
}

}  // namespace material_color_utilities::ts
