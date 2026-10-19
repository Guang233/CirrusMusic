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

// C++ port by Cirrus of typescript/dynamiccolor/color_spec_2021.ts.

#include <cmath>

#include "cirrus/color_spec.h"
#include "cirrus/spec_helpers.h"
#include "cirrus/temperature.h"

namespace material_color_utilities::ts {

namespace {

bool IsFidelity(const DynamicScheme& s) {
  return s.variant == Variant::kFidelity || s.variant == Variant::kContent;
}

bool IsMonochrome(const DynamicScheme& s) {
  return s.variant == Variant::kMonochrome;
}

double FindDesiredChromaByTone(double hue, double chroma, double tone,
                               bool by_decreasing_tone) {
  double answer = tone;
  Hct closest_to_chroma(hue, chroma, tone);
  if (closest_to_chroma.get_chroma() < chroma) {
    double chroma_peak = closest_to_chroma.get_chroma();
    while (closest_to_chroma.get_chroma() < chroma) {
      answer += by_decreasing_tone ? -1.0 : 1.0;
      const Hct potential_solution(hue, chroma, answer);
      if (chroma_peak > potential_solution.get_chroma()) {
        break;
      }
      if (std::abs(potential_solution.get_chroma() - chroma) < 0.4) {
        break;
      }
      const double potential_delta =
          std::abs(potential_solution.get_chroma() - chroma);
      const double current_delta =
          std::abs(closest_to_chroma.get_chroma() - chroma);
      if (potential_delta < current_delta) {
        closest_to_chroma = potential_solution;
      }
      chroma_peak = std::max(chroma_peak, potential_solution.get_chroma());
    }
  }
  return answer;
}

}  // namespace

const DynamicColor& ColorSpec2021::highestSurface(const DynamicScheme& s) const {
  return s.is_dark ? surfaceBright() : surfaceDim();
}

ColorSpec2021::ColorSpec2021() {
  const ColorFn highest = [this](const DynamicScheme& s) {
    return &highestSurface(s);
  };
  auto ref = [](auto accessor) {
    return [accessor](const DynamicScheme&) { return &accessor(); };
  };
  auto pair = [](auto a, auto b, double delta, TonePolarity polarity,
                 bool stay_together) {
    return [=](const DynamicScheme&) -> std::optional<ToneDeltaPair> {
      return ToneDeltaPair{&a(), &b(), delta, polarity, stay_together};
    };
  };
  const auto primaryContainerRef = [this]() -> const DynamicColor& {
    return primaryContainer();
  };
  const auto primaryRef = [this]() -> const DynamicColor& { return primary(); };
  const auto secondaryContainerRef = [this]() -> const DynamicColor& {
    return secondaryContainer();
  };
  const auto secondaryRef = [this]() -> const DynamicColor& {
    return secondary();
  };
  const auto tertiaryContainerRef = [this]() -> const DynamicColor& {
    return tertiaryContainer();
  };
  const auto tertiaryRef = [this]() -> const DynamicColor& {
    return tertiary();
  };
  const auto errorContainerRef = [this]() -> const DynamicColor& {
    return errorContainer();
  };
  const auto errorRef = [this]() -> const DynamicColor& { return error(); };

  // Main palettes
  primaryPaletteKeyColor_.emplace(DynamicColorOptions{
      .name = "primary_palette_key_color",
      .palette = Palette(&DynamicScheme::primary_palette),
      .tone = [](const DynamicScheme& s) {
        return s.primary_palette.key_color().get_tone();
      }});
  secondaryPaletteKeyColor_.emplace(DynamicColorOptions{
      .name = "secondary_palette_key_color",
      .palette = Palette(&DynamicScheme::secondary_palette),
      .tone = [](const DynamicScheme& s) {
        return s.secondary_palette.key_color().get_tone();
      }});
  tertiaryPaletteKeyColor_.emplace(DynamicColorOptions{
      .name = "tertiary_palette_key_color",
      .palette = Palette(&DynamicScheme::tertiary_palette),
      .tone = [](const DynamicScheme& s) {
        return s.tertiary_palette.key_color().get_tone();
      }});
  neutralPaletteKeyColor_.emplace(DynamicColorOptions{
      .name = "neutral_palette_key_color",
      .palette = Palette(&DynamicScheme::neutral_palette),
      .tone = [](const DynamicScheme& s) {
        return s.neutral_palette.key_color().get_tone();
      }});
  neutralVariantPaletteKeyColor_.emplace(DynamicColorOptions{
      .name = "neutral_variant_palette_key_color",
      .palette = Palette(&DynamicScheme::neutral_variant_palette),
      .tone = [](const DynamicScheme& s) {
        return s.neutral_variant_palette.key_color().get_tone();
      }});
  errorPaletteKeyColor_.emplace(DynamicColorOptions{
      .name = "error_palette_key_color",
      .palette = Palette(&DynamicScheme::error_palette),
      .tone = [](const DynamicScheme& s) {
        return s.error_palette.key_color().get_tone();
      }});

  // Surfaces
  const PaletteFn neutral = Palette(&DynamicScheme::neutral_palette);
  const PaletteFn neutral_variant =
      Palette(&DynamicScheme::neutral_variant_palette);
  background_.emplace(DynamicColorOptions{
      .name = "background",
      .palette = neutral,
      .tone = DarkLight(6, 98),
      .is_background = true});
  onBackground_.emplace(DynamicColorOptions{
      .name = "on_background",
      .palette = neutral,
      .tone = DarkLight(90, 10),
      .background = ref([this]() -> const DynamicColor& { return background(); }),
      .contrast_curve = Curve(3, 3, 4.5, 7)});
  surface_.emplace(DynamicColorOptions{.name = "surface",
                                       .palette = neutral,
                                       .tone = DarkLight(6, 98),
                                       .is_background = true});
  surfaceDim_.emplace(DynamicColorOptions{
      .name = "surface_dim",
      .palette = neutral,
      .tone = [](const DynamicScheme& s) {
        return s.is_dark ? 6 : ContrastCurve{87, 87, 80, 75}.Get(s.contrast_level);
      },
      .is_background = true});
  surfaceBright_.emplace(DynamicColorOptions{
      .name = "surface_bright",
      .palette = neutral,
      .tone = [](const DynamicScheme& s) {
        return s.is_dark ? ContrastCurve{24, 24, 29, 34}.Get(s.contrast_level) : 98;
      },
      .is_background = true});
  surfaceContainerLowest_.emplace(DynamicColorOptions{
      .name = "surface_container_lowest",
      .palette = neutral,
      .tone = [](const DynamicScheme& s) {
        return s.is_dark ? ContrastCurve{4, 4, 2, 0}.Get(s.contrast_level) : 100;
      },
      .is_background = true});
  surfaceContainerLow_.emplace(DynamicColorOptions{
      .name = "surface_container_low",
      .palette = neutral,
      .tone = DarkLightCurve({10, 10, 11, 12}, {96, 96, 96, 95}),
      .is_background = true});
  surfaceContainer_.emplace(DynamicColorOptions{
      .name = "surface_container",
      .palette = neutral,
      .tone = DarkLightCurve({12, 12, 16, 20}, {94, 94, 92, 90}),
      .is_background = true});
  surfaceContainerHigh_.emplace(DynamicColorOptions{
      .name = "surface_container_high",
      .palette = neutral,
      .tone = DarkLightCurve({17, 17, 21, 25}, {92, 92, 88, 85}),
      .is_background = true});
  surfaceContainerHighest_.emplace(DynamicColorOptions{
      .name = "surface_container_highest",
      .palette = neutral,
      .tone = DarkLightCurve({22, 22, 26, 30}, {90, 90, 84, 80}),
      .is_background = true});
  onSurface_.emplace(DynamicColorOptions{
      .name = "on_surface",
      .palette = neutral,
      .tone = DarkLight(90, 10),
      .background = highest,
      .contrast_curve = Curve(4.5, 7, 11, 21)});
  surfaceVariant_.emplace(DynamicColorOptions{
      .name = "surface_variant",
      .palette = neutral_variant,
      .tone = DarkLight(30, 90),
      .is_background = true});
  onSurfaceVariant_.emplace(DynamicColorOptions{
      .name = "on_surface_variant",
      .palette = neutral_variant,
      .tone = DarkLight(80, 30),
      .background = highest,
      .contrast_curve = Curve(3, 4.5, 7, 11)});
  inverseSurface_.emplace(DynamicColorOptions{
      .name = "inverse_surface",
      .palette = neutral,
      .tone = DarkLight(90, 20),
      .is_background = true});
  inverseOnSurface_.emplace(DynamicColorOptions{
      .name = "inverse_on_surface",
      .palette = neutral,
      .tone = DarkLight(20, 95),
      .background =
          ref([this]() -> const DynamicColor& { return inverseSurface(); }),
      .contrast_curve = Curve(4.5, 7, 11, 21)});
  outline_.emplace(DynamicColorOptions{
      .name = "outline",
      .palette = neutral_variant,
      .tone = DarkLight(60, 50),
      .background = highest,
      .contrast_curve = Curve(1.5, 3, 4.5, 7)});
  outlineVariant_.emplace(DynamicColorOptions{
      .name = "outline_variant",
      .palette = neutral_variant,
      .tone = DarkLight(30, 80),
      .background = highest,
      .contrast_curve = Curve(1, 1, 3, 4.5)});
  shadow_.emplace(DynamicColorOptions{
      .name = "shadow", .palette = neutral, .tone = Fixed(0)});
  scrim_.emplace(DynamicColorOptions{
      .name = "scrim", .palette = neutral, .tone = Fixed(0)});
  surfaceTint_.emplace(DynamicColorOptions{
      .name = "surface_tint",
      .palette = Palette(&DynamicScheme::primary_palette),
      .tone = DarkLight(80, 40),
      .is_background = true});

  // Primaries
  const PaletteFn primary_palette = Palette(&DynamicScheme::primary_palette);
  primary_.emplace(DynamicColorOptions{
      .name = "primary",
      .palette = primary_palette,
      .tone = [](const DynamicScheme& s) {
        if (IsMonochrome(s)) return s.is_dark ? 100.0 : 0.0;
        return s.is_dark ? 80.0 : 40.0;
      },
      .is_background = true,
      .background = highest,
      .contrast_curve = Curve(3, 4.5, 7, 7),
      .tone_delta_pair = pair(primaryContainerRef, primaryRef, 10,
                              TonePolarity::kNearer, false)});
  onPrimary_.emplace(DynamicColorOptions{
      .name = "on_primary",
      .palette = primary_palette,
      .tone = [](const DynamicScheme& s) {
        if (IsMonochrome(s)) return s.is_dark ? 10.0 : 90.0;
        return s.is_dark ? 20.0 : 100.0;
      },
      .background = ref(primaryRef),
      .contrast_curve = Curve(4.5, 7, 11, 21)});
  primaryContainer_.emplace(DynamicColorOptions{
      .name = "primary_container",
      .palette = primary_palette,
      .tone = [](const DynamicScheme& s) {
        if (IsFidelity(s)) return s.source_color_hct.get_tone();
        if (IsMonochrome(s)) return s.is_dark ? 85.0 : 25.0;
        return s.is_dark ? 30.0 : 90.0;
      },
      .is_background = true,
      .background = highest,
      .contrast_curve = Curve(1, 1, 3, 4.5),
      .tone_delta_pair = pair(primaryContainerRef, primaryRef, 10,
                              TonePolarity::kNearer, false)});
  onPrimaryContainer_.emplace(DynamicColorOptions{
      .name = "on_primary_container",
      .palette = primary_palette,
      .tone = [this](const DynamicScheme& s) {
        if (IsFidelity(s)) {
          return DynamicColor::ForegroundTone(primaryContainer().tone(s), 4.5);
        }
        if (IsMonochrome(s)) return s.is_dark ? 0.0 : 100.0;
        return s.is_dark ? 90.0 : 30.0;
      },
      .background = ref(primaryContainerRef),
      .contrast_curve = Curve(3, 4.5, 7, 11)});
  inversePrimary_.emplace(DynamicColorOptions{
      .name = "inverse_primary",
      .palette = primary_palette,
      .tone = DarkLight(40, 80),
      .background =
          ref([this]() -> const DynamicColor& { return inverseSurface(); }),
      .contrast_curve = Curve(3, 4.5, 7, 7)});

  // Secondaries
  const PaletteFn secondary_palette =
      Palette(&DynamicScheme::secondary_palette);
  secondary_.emplace(DynamicColorOptions{
      .name = "secondary",
      .palette = secondary_palette,
      .tone = DarkLight(80, 40),
      .is_background = true,
      .background = highest,
      .contrast_curve = Curve(3, 4.5, 7, 7),
      .tone_delta_pair = pair(secondaryContainerRef, secondaryRef, 10,
                              TonePolarity::kNearer, false)});
  onSecondary_.emplace(DynamicColorOptions{
      .name = "on_secondary",
      .palette = secondary_palette,
      .tone = [](const DynamicScheme& s) {
        if (IsMonochrome(s)) return s.is_dark ? 10.0 : 100.0;
        return s.is_dark ? 20.0 : 100.0;
      },
      .background = ref(secondaryRef),
      .contrast_curve = Curve(4.5, 7, 11, 21)});
  secondaryContainer_.emplace(DynamicColorOptions{
      .name = "secondary_container",
      .palette = secondary_palette,
      .tone = [](const DynamicScheme& s) {
        const double initial_tone = s.is_dark ? 30 : 90;
        if (IsMonochrome(s)) return s.is_dark ? 30.0 : 85.0;
        if (!IsFidelity(s)) return initial_tone;
        return FindDesiredChromaByTone(s.secondary_palette.hue(),
                                       s.secondary_palette.chroma(),
                                       initial_tone, !s.is_dark);
      },
      .is_background = true,
      .background = highest,
      .contrast_curve = Curve(1, 1, 3, 4.5),
      .tone_delta_pair = pair(secondaryContainerRef, secondaryRef, 10,
                              TonePolarity::kNearer, false)});
  onSecondaryContainer_.emplace(DynamicColorOptions{
      .name = "on_secondary_container",
      .palette = secondary_palette,
      .tone = [this](const DynamicScheme& s) {
        if (IsMonochrome(s)) return s.is_dark ? 90.0 : 10.0;
        if (!IsFidelity(s)) return s.is_dark ? 90.0 : 30.0;
        return DynamicColor::ForegroundTone(secondaryContainer().tone(s), 4.5);
      },
      .background = ref(secondaryContainerRef),
      .contrast_curve = Curve(3, 4.5, 7, 11)});

  // Tertiaries
  const PaletteFn tertiary_palette = Palette(&DynamicScheme::tertiary_palette);
  tertiary_.emplace(DynamicColorOptions{
      .name = "tertiary",
      .palette = tertiary_palette,
      .tone = [](const DynamicScheme& s) {
        if (IsMonochrome(s)) return s.is_dark ? 90.0 : 25.0;
        return s.is_dark ? 80.0 : 40.0;
      },
      .is_background = true,
      .background = highest,
      .contrast_curve = Curve(3, 4.5, 7, 7),
      .tone_delta_pair = pair(tertiaryContainerRef, tertiaryRef, 10,
                              TonePolarity::kNearer, false)});
  onTertiary_.emplace(DynamicColorOptions{
      .name = "on_tertiary",
      .palette = tertiary_palette,
      .tone = [](const DynamicScheme& s) {
        if (IsMonochrome(s)) return s.is_dark ? 10.0 : 90.0;
        return s.is_dark ? 20.0 : 100.0;
      },
      .background = ref(tertiaryRef),
      .contrast_curve = Curve(4.5, 7, 11, 21)});
  tertiaryContainer_.emplace(DynamicColorOptions{
      .name = "tertiary_container",
      .palette = tertiary_palette,
      .tone = [](const DynamicScheme& s) {
        if (IsMonochrome(s)) return s.is_dark ? 60.0 : 49.0;
        if (!IsFidelity(s)) return s.is_dark ? 30.0 : 90.0;
        const Hct proposed =
            s.tertiary_palette.GetHct(s.source_color_hct.get_tone());
        return FixIfDislikedTs(proposed).get_tone();
      },
      .is_background = true,
      .background = highest,
      .contrast_curve = Curve(1, 1, 3, 4.5),
      .tone_delta_pair = pair(tertiaryContainerRef, tertiaryRef, 10,
                              TonePolarity::kNearer, false)});
  onTertiaryContainer_.emplace(DynamicColorOptions{
      .name = "on_tertiary_container",
      .palette = tertiary_palette,
      .tone = [this](const DynamicScheme& s) {
        if (IsMonochrome(s)) return s.is_dark ? 0.0 : 100.0;
        if (!IsFidelity(s)) return s.is_dark ? 90.0 : 30.0;
        return DynamicColor::ForegroundTone(tertiaryContainer().tone(s), 4.5);
      },
      .background = ref(tertiaryContainerRef),
      .contrast_curve = Curve(3, 4.5, 7, 11)});

  // Errors
  const PaletteFn error_palette = Palette(&DynamicScheme::error_palette);
  error_.emplace(DynamicColorOptions{
      .name = "error",
      .palette = error_palette,
      .tone = DarkLight(80, 40),
      .is_background = true,
      .background = highest,
      .contrast_curve = Curve(3, 4.5, 7, 7),
      .tone_delta_pair = pair(errorContainerRef, errorRef, 10,
                              TonePolarity::kNearer, false)});
  onError_.emplace(DynamicColorOptions{
      .name = "on_error",
      .palette = error_palette,
      .tone = DarkLight(20, 100),
      .background = ref(errorRef),
      .contrast_curve = Curve(4.5, 7, 11, 21)});
  errorContainer_.emplace(DynamicColorOptions{
      .name = "error_container",
      .palette = error_palette,
      .tone = DarkLight(30, 90),
      .is_background = true,
      .background = highest,
      .contrast_curve = Curve(1, 1, 3, 4.5),
      .tone_delta_pair = pair(errorContainerRef, errorRef, 10,
                              TonePolarity::kNearer, false)});
  onErrorContainer_.emplace(DynamicColorOptions{
      .name = "on_error_container",
      .palette = error_palette,
      .tone = [](const DynamicScheme& s) {
        if (IsMonochrome(s)) return s.is_dark ? 90.0 : 10.0;
        return s.is_dark ? 90.0 : 30.0;
      },
      .background = ref(errorContainerRef),
      .contrast_curve = Curve(3, 4.5, 7, 11)});

  // Fixed colors
  auto fixed_pair = [&](auto fixed, auto dim) {
    return pair(fixed, dim, 10, TonePolarity::kLighter, true);
  };
  auto mono = [](double mono_tone, double tone) {
    return [=](const DynamicScheme& s) {
      return IsMonochrome(s) ? mono_tone : tone;
    };
  };
  const auto primaryFixedRef = [this]() -> const DynamicColor& {
    return primaryFixed();
  };
  const auto primaryFixedDimRef = [this]() -> const DynamicColor& {
    return primaryFixedDim();
  };
  const auto secondaryFixedRef = [this]() -> const DynamicColor& {
    return secondaryFixed();
  };
  const auto secondaryFixedDimRef = [this]() -> const DynamicColor& {
    return secondaryFixedDim();
  };
  const auto tertiaryFixedRef = [this]() -> const DynamicColor& {
    return tertiaryFixed();
  };
  const auto tertiaryFixedDimRef = [this]() -> const DynamicColor& {
    return tertiaryFixedDim();
  };

  primaryFixed_.emplace(DynamicColorOptions{
      .name = "primary_fixed",
      .palette = primary_palette,
      .tone = mono(40.0, 90.0),
      .is_background = true,
      .background = highest,
      .contrast_curve = Curve(1, 1, 3, 4.5),
      .tone_delta_pair = fixed_pair(primaryFixedRef, primaryFixedDimRef)});
  primaryFixedDim_.emplace(DynamicColorOptions{
      .name = "primary_fixed_dim",
      .palette = primary_palette,
      .tone = mono(30.0, 80.0),
      .is_background = true,
      .background = highest,
      .contrast_curve = Curve(1, 1, 3, 4.5),
      .tone_delta_pair = fixed_pair(primaryFixedRef, primaryFixedDimRef)});
  onPrimaryFixed_.emplace(DynamicColorOptions{
      .name = "on_primary_fixed",
      .palette = primary_palette,
      .tone = mono(100.0, 10.0),
      .background = ref(primaryFixedDimRef),
      .second_background = ref(primaryFixedRef),
      .contrast_curve = Curve(4.5, 7, 11, 21)});
  onPrimaryFixedVariant_.emplace(DynamicColorOptions{
      .name = "on_primary_fixed_variant",
      .palette = primary_palette,
      .tone = mono(90.0, 30.0),
      .background = ref(primaryFixedDimRef),
      .second_background = ref(primaryFixedRef),
      .contrast_curve = Curve(3, 4.5, 7, 11)});
  secondaryFixed_.emplace(DynamicColorOptions{
      .name = "secondary_fixed",
      .palette = secondary_palette,
      .tone = mono(80.0, 90.0),
      .is_background = true,
      .background = highest,
      .contrast_curve = Curve(1, 1, 3, 4.5),
      .tone_delta_pair = fixed_pair(secondaryFixedRef, secondaryFixedDimRef)});
  secondaryFixedDim_.emplace(DynamicColorOptions{
      .name = "secondary_fixed_dim",
      .palette = secondary_palette,
      .tone = mono(70.0, 80.0),
      .is_background = true,
      .background = highest,
      .contrast_curve = Curve(1, 1, 3, 4.5),
      .tone_delta_pair = fixed_pair(secondaryFixedRef, secondaryFixedDimRef)});
  onSecondaryFixed_.emplace(DynamicColorOptions{
      .name = "on_secondary_fixed",
      .palette = secondary_palette,
      .tone = Fixed(10.0),
      .background = ref(secondaryFixedDimRef),
      .second_background = ref(secondaryFixedRef),
      .contrast_curve = Curve(4.5, 7, 11, 21)});
  onSecondaryFixedVariant_.emplace(DynamicColorOptions{
      .name = "on_secondary_fixed_variant",
      .palette = secondary_palette,
      .tone = mono(25.0, 30.0),
      .background = ref(secondaryFixedDimRef),
      .second_background = ref(secondaryFixedRef),
      .contrast_curve = Curve(3, 4.5, 7, 11)});
  tertiaryFixed_.emplace(DynamicColorOptions{
      .name = "tertiary_fixed",
      .palette = tertiary_palette,
      .tone = mono(40.0, 90.0),
      .is_background = true,
      .background = highest,
      .contrast_curve = Curve(1, 1, 3, 4.5),
      .tone_delta_pair = fixed_pair(tertiaryFixedRef, tertiaryFixedDimRef)});
  tertiaryFixedDim_.emplace(DynamicColorOptions{
      .name = "tertiary_fixed_dim",
      .palette = tertiary_palette,
      .tone = mono(30.0, 80.0),
      .is_background = true,
      .background = highest,
      .contrast_curve = Curve(1, 1, 3, 4.5),
      .tone_delta_pair = fixed_pair(tertiaryFixedRef, tertiaryFixedDimRef)});
  onTertiaryFixed_.emplace(DynamicColorOptions{
      .name = "on_tertiary_fixed",
      .palette = tertiary_palette,
      .tone = mono(100.0, 10.0),
      .background = ref(tertiaryFixedDimRef),
      .second_background = ref(tertiaryFixedRef),
      .contrast_curve = Curve(4.5, 7, 11, 21)});
  onTertiaryFixedVariant_.emplace(DynamicColorOptions{
      .name = "on_tertiary_fixed_variant",
      .palette = tertiary_palette,
      .tone = mono(90.0, 30.0),
      .background = ref(tertiaryFixedDimRef),
      .second_background = ref(tertiaryFixedRef),
      .contrast_curve = Curve(3, 4.5, 7, 11)});
}

}  // namespace material_color_utilities::ts
