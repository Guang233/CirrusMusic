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

// C++ port by Cirrus of typescript/dynamiccolor/color_spec_2025.ts.
// In TypeScript every 2025 color is wrapped by extendSpecVersion(original,
// '2025', color2025); this delegate is only used for schemes whose spec
// version is 2025, so the 2025 definition is used directly.

#include <algorithm>
#include <cmath>

#include "cirrus/color_spec.h"
#include "cirrus/spec_helpers.h"

namespace material_color_utilities::ts {

namespace {

double Clamp(double min, double max, double value) {
  return value < min ? min : (value > max ? max : value);
}

double FindBestToneForChroma(double hue, double chroma, double tone,
                             bool by_decreasing_tone) {
  double answer = tone;
  Hct best_candidate(hue, chroma, answer);
  while (best_candidate.get_chroma() < chroma) {
    if (tone < 0 || tone > 100) {
      break;
    }
    tone += by_decreasing_tone ? -1.0 : 1.0;
    const Hct new_candidate(hue, chroma, tone);
    if (best_candidate.get_chroma() < new_candidate.get_chroma()) {
      best_candidate = new_candidate;
      answer = tone;
    }
  }
  return answer;
}

double TMaxC(const TonalPalette& palette, double lower = 0, double upper = 100,
             double chroma_multiplier = 1) {
  const double answer = FindBestToneForChroma(
      palette.hue(), palette.chroma() * chroma_multiplier, 100, true);
  return Clamp(lower, upper, answer);
}

double TMinC(const TonalPalette& palette, double lower = 0,
             double upper = 100) {
  const double answer =
      FindBestToneForChroma(palette.hue(), palette.chroma(), 0, false);
  return Clamp(lower, upper, answer);
}

ContrastCurve GetCurve(double default_contrast) {
  if (default_contrast == 1.5) return {1.5, 1.5, 3, 5.5};
  if (default_contrast == 3) return {3, 3, 4.5, 7};
  if (default_contrast == 4.5) return {4.5, 4.5, 7, 11};
  if (default_contrast == 6) return {6, 6, 7, 11};
  if (default_contrast == 7) return {7, 7, 11, 21};
  if (default_contrast == 9) return {9, 9, 11, 21};
  if (default_contrast == 11) return {11, 11, 21, 21};
  if (default_contrast == 21) return {21, 21, 21, 21};
  return {default_contrast, default_contrast, 7, 21};
}

CurveFn CurveOf(double default_contrast) {
  return [default_contrast](const DynamicScheme&) -> std::optional<ContrastCurve> {
    return GetCurve(default_contrast);
  };
}

bool IsPhone(const DynamicScheme& s) { return s.platform == Platform::kPhone; }

bool NeutralIsYellow(const DynamicScheme& s) {
  return IsYellow(s.neutral_palette.hue());
}

// Light-mode tone for the phone surfaces: yellow neutral / vibrant / other.
double SurfaceTone(const DynamicScheme& s, double yellow, double vibrant,
                   double other) {
  if (NeutralIsYellow(s)) return yellow;
  return s.variant == Variant::kVibrant ? vibrant : other;
}

// Per-variant chroma multiplier used by several surfaces.
double VariantMultiplier(const DynamicScheme& s, double neutral,
                         double tonal_spot, double expressive_yellow,
                         double expressive, double vibrant) {
  switch (s.variant) {
    case Variant::kNeutral:
      return neutral;
    case Variant::kTonalSpot:
      return tonal_spot;
    case Variant::kExpressive:
      return NeutralIsYellow(s) ? expressive_yellow : expressive;
    case Variant::kVibrant:
      return vibrant;
    default:
      return 1;
  }
}

}  // namespace

const DynamicColor& ColorSpec2025::highestSurface(const DynamicScheme& s) const {
  return s.is_dark ? surfaceBright() : surfaceDim();
}

ColorSpec2025::ColorSpec2025() {
  const PaletteFn neutral = Palette(&DynamicScheme::neutral_palette);
  const PaletteFn neutral_variant =
      Palette(&DynamicScheme::neutral_variant_palette);
  auto ref = [](auto accessor) {
    return [accessor](const DynamicScheme&) { return &accessor(); };
  };

  // Surfaces
  surface_.emplace(DynamicColorOptions{
      .name = "surface",
      .palette = neutral,
      .tone = [](const DynamicScheme& s) {
        if (!IsPhone(s)) return 0.0;
        return s.is_dark ? 4.0 : SurfaceTone(s, 99, 97, 98);
      },
      .is_background = true});
  surfaceDim_.emplace(DynamicColorOptions{
      .name = "surface_dim",
      .palette = neutral,
      .tone = [](const DynamicScheme& s) {
        return s.is_dark ? 4.0 : SurfaceTone(s, 90, 85, 87);
      },
      .is_background = true,
      .chroma_multiplier = [](const DynamicScheme& s) {
        return s.is_dark ? 1.0 : VariantMultiplier(s, 2.5, 1.7, 2.7, 1.75, 1.36);
      }});
  surfaceBright_.emplace(DynamicColorOptions{
      .name = "surface_bright",
      .palette = neutral,
      .tone = [](const DynamicScheme& s) {
        return s.is_dark ? 18.0 : SurfaceTone(s, 99, 97, 98);
      },
      .is_background = true,
      .chroma_multiplier = [](const DynamicScheme& s) {
        return s.is_dark ? VariantMultiplier(s, 2.5, 1.7, 2.7, 1.75, 1.36) : 1.0;
      }});
  surfaceContainerLowest_.emplace(DynamicColorOptions{
      .name = "surface_container_lowest",
      .palette = neutral,
      .tone = DarkLight(0, 100),
      .is_background = true});
  surfaceContainerLow_.emplace(DynamicColorOptions{
      .name = "surface_container_low",
      .palette = neutral,
      .tone = [](const DynamicScheme& s) {
        if (!IsPhone(s)) return 15.0;
        return s.is_dark ? 6.0 : SurfaceTone(s, 98, 95, 96);
      },
      .is_background = true,
      .chroma_multiplier = [](const DynamicScheme& s) {
        return IsPhone(s) ? VariantMultiplier(s, 1.3, 1.25, 1.3, 1.15, 1.08) : 1.0;
      }});
  surfaceContainer_.emplace(DynamicColorOptions{
      .name = "surface_container",
      .palette = neutral,
      .tone = [](const DynamicScheme& s) {
        if (!IsPhone(s)) return 20.0;
        return s.is_dark ? 9.0 : SurfaceTone(s, 96, 92, 94);
      },
      .is_background = true,
      .chroma_multiplier = [](const DynamicScheme& s) {
        return IsPhone(s) ? VariantMultiplier(s, 1.6, 1.4, 1.6, 1.3, 1.15) : 1.0;
      }});
  surfaceContainerHigh_.emplace(DynamicColorOptions{
      .name = "surface_container_high",
      .palette = neutral,
      .tone = [](const DynamicScheme& s) {
        if (!IsPhone(s)) return 25.0;
        return s.is_dark ? 12.0 : SurfaceTone(s, 94, 90, 92);
      },
      .is_background = true,
      .chroma_multiplier = [](const DynamicScheme& s) {
        return IsPhone(s) ? VariantMultiplier(s, 1.9, 1.5, 1.95, 1.45, 1.22) : 1.0;
      }});
  surfaceContainerHighest_.emplace(DynamicColorOptions{
      .name = "surface_container_highest",
      .palette = neutral,
      .tone = [](const DynamicScheme& s) {
        return s.is_dark ? 15.0 : SurfaceTone(s, 92, 88, 90);
      },
      .is_background = true,
      .chroma_multiplier = [](const DynamicScheme& s) {
        return VariantMultiplier(s, 2.2, 1.7, 2.3, 1.6, 1.29);
      }});

  // Text and outlines on surfaces share one background and chroma rule.
  const ColorFn surface_background = [this](const DynamicScheme& s) {
    return IsPhone(s) ? &highestSurface(s) : &surfaceContainerHigh();
  };
  const DoubleFn on_surface_multiplier = [](const DynamicScheme& s) {
    if (!IsPhone(s)) return 1.0;
    switch (s.variant) {
      case Variant::kNeutral:
        return 2.2;
      case Variant::kTonalSpot:
        return 1.7;
      case Variant::kExpressive:
        return NeutralIsYellow(s) ? (s.is_dark ? 3.0 : 2.3) : 1.6;
      default:
        return 1.0;
    }
  };
  onSurface_.emplace(DynamicColorOptions{
      .name = "on_surface",
      .palette = neutral,
      .tone = [surface_background](const DynamicScheme& s) {
        if (s.variant == Variant::kVibrant) {
          return TMaxC(s.neutral_palette, 0, 100, 1.1);
        }
        return surface_background(s)->GetTone(s);
      },
      .chroma_multiplier = on_surface_multiplier,
      .background = surface_background,
      .contrast_curve = [](const DynamicScheme& s) -> std::optional<ContrastCurve> {
        return s.is_dark && IsPhone(s) ? GetCurve(11) : GetCurve(9);
      }});
  onSurfaceVariant_.emplace(DynamicColorOptions{
      .name = "on_surface_variant",
      .palette = neutral,
      .chroma_multiplier = on_surface_multiplier,
      .background = surface_background,
      .contrast_curve = [](const DynamicScheme& s) -> std::optional<ContrastCurve> {
        return IsPhone(s) ? (s.is_dark ? GetCurve(6) : GetCurve(4.5)) : GetCurve(7);
      }});
  outline_.emplace(DynamicColorOptions{
      .name = "outline",
      .palette = neutral,
      .chroma_multiplier = on_surface_multiplier,
      .background = surface_background,
      .contrast_curve = [](const DynamicScheme& s) -> std::optional<ContrastCurve> {
        return IsPhone(s) ? GetCurve(3) : GetCurve(4.5);
      }});
  outlineVariant_.emplace(DynamicColorOptions{
      .name = "outline_variant",
      .palette = neutral,
      .chroma_multiplier = on_surface_multiplier,
      .background = surface_background,
      .contrast_curve = [](const DynamicScheme& s) -> std::optional<ContrastCurve> {
        return IsPhone(s) ? GetCurve(1.5) : GetCurve(3);
      }});
  inverseSurface_.emplace(DynamicColorOptions{
      .name = "inverse_surface",
      .palette = neutral,
      .tone = DarkLight(98, 4),
      .is_background = true});
  inverseOnSurface_.emplace(DynamicColorOptions{
      .name = "inverse_on_surface",
      .palette = neutral,
      .background = ref([this]() -> const DynamicColor& { return inverseSurface(); }),
      .contrast_curve = CurveOf(7)});

  // Primaries
  const PaletteFn primary_palette = Palette(&DynamicScheme::primary_palette);
  const auto primaryRef = [this]() -> const DynamicColor& { return primary(); };
  const auto primaryContainerRef = [this]() -> const DynamicColor& {
    return primaryContainer();
  };
  const auto primaryDimRef = [this]() -> const DynamicColor& {
    return *primaryDim();
  };
  primary_.emplace(DynamicColorOptions{
      .name = "primary",
      .palette = primary_palette,
      .tone = [](const DynamicScheme& s) {
        const TonalPalette& p = s.primary_palette;
        switch (s.variant) {
          case Variant::kNeutral:
            return IsPhone(s) ? (s.is_dark ? 80.0 : 40.0) : 90.0;
          case Variant::kTonalSpot:
            if (IsPhone(s)) return s.is_dark ? 80.0 : TMaxC(p);
            return TMaxC(p, 0, 90);
          case Variant::kExpressive:
            if (!IsPhone(s)) return TMaxC(p);
            return TMaxC(p, 0,
                         s.is_dark ? (IsCyan(p.hue()) ? 88 : 98)
                                   : (IsYellow(p.hue()) ? 25 : 98));
          default:  // Vibrant
            if (!IsPhone(s)) return TMaxC(p);
            return TMaxC(p, 0, IsCyan(p.hue()) ? 88 : 98);
        }
      },
      .is_background = true,
      .background = surface_background,
      .contrast_curve = [](const DynamicScheme& s) -> std::optional<ContrastCurve> {
        return IsPhone(s) ? GetCurve(4.5) : GetCurve(7);
      },
      .tone_delta_pair = [this](const DynamicScheme& s) -> std::optional<ToneDeltaPair> {
        if (!IsPhone(s)) return std::nullopt;
        return ToneDeltaPair{&primaryContainer(), &primary(), 5,
                             TonePolarity::kRelativeLighter, true,
                             DeltaConstraint::kFarther};
      }});
  primaryDim_.emplace(DynamicColorOptions{
      .name = "primary_dim",
      .palette = primary_palette,
      .tone = [](const DynamicScheme& s) {
        if (s.variant == Variant::kNeutral) return 85.0;
        if (s.variant == Variant::kTonalSpot) return TMaxC(s.primary_palette, 0, 90);
        return TMaxC(s.primary_palette);
      },
      .is_background = true,
      .background = ref([this]() -> const DynamicColor& { return surfaceContainerHigh(); }),
      .contrast_curve = CurveOf(4.5),
      .tone_delta_pair = [this](const DynamicScheme&) -> std::optional<ToneDeltaPair> {
        return ToneDeltaPair{primaryDim(), &primary(), 5, TonePolarity::kDarker, true,
                             DeltaConstraint::kFarther};
      }});
  onPrimary_.emplace(DynamicColorOptions{
      .name = "on_primary",
      .palette = primary_palette,
      .background = [this](const DynamicScheme& s) {
        return IsPhone(s) ? &primary() : primaryDim();
      },
      .contrast_curve = [](const DynamicScheme& s) -> std::optional<ContrastCurve> {
        return IsPhone(s) ? GetCurve(6) : GetCurve(7);
      }});
  // Containers: background only on phone; contrast only when raised.
  const ColorFn phone_highest = [this](const DynamicScheme& s) -> const DynamicColor* {
    return IsPhone(s) ? &highestSurface(s) : nullptr;
  };
  const CurveFn raised_contrast = [](const DynamicScheme& s) -> std::optional<ContrastCurve> {
    if (IsPhone(s) && s.contrast_level > 0) return GetCurve(1.5);
    return std::nullopt;
  };
  const CurveFn phone6_watch7 = [](const DynamicScheme& s) -> std::optional<ContrastCurve> {
    return IsPhone(s) ? GetCurve(6) : GetCurve(7);
  };
  const CurveFn phone45_watch7 = [](const DynamicScheme& s) -> std::optional<ContrastCurve> {
    return IsPhone(s) ? GetCurve(4.5) : GetCurve(7);
  };
  // The fixed colors take the light, standard-contrast container tone.
  auto light_standard = [](const DynamicScheme& s) {
    DynamicScheme copy = s;
    copy.is_dark = false;
    copy.contrast_level = 0;
    return copy;
  };

  primaryContainer_.emplace(DynamicColorOptions{
      .name = "primary_container",
      .palette = primary_palette,
      .tone = [](const DynamicScheme& s) {
        const TonalPalette& p = s.primary_palette;
        if (!IsPhone(s)) return 30.0;
        switch (s.variant) {
          case Variant::kNeutral:
            return s.is_dark ? 30.0 : 90.0;
          case Variant::kTonalSpot:
            return s.is_dark ? TMinC(p, 35, 93) : TMaxC(p, 0, 90);
          case Variant::kExpressive:
            return s.is_dark ? TMinC(p, 30, 93)
                             : TMaxC(p, 78, IsCyan(p.hue()) ? 88 : 90);
          default:  // Vibrant
            return s.is_dark ? TMinC(p, 66, 93)
                             : TMaxC(p, 66, IsCyan(p.hue()) ? 88 : 93);
        }
      },
      .is_background = true,
      .background = phone_highest,
      .contrast_curve = raised_contrast,
      .tone_delta_pair = [this](const DynamicScheme& s) -> std::optional<ToneDeltaPair> {
        if (IsPhone(s)) return std::nullopt;
        return ToneDeltaPair{&primaryContainer(), primaryDim(), 10, TonePolarity::kDarker,
                             true, DeltaConstraint::kFarther};
      }});
  onPrimaryContainer_.emplace(DynamicColorOptions{
      .name = "on_primary_container",
      .palette = primary_palette,
      .background = ref(primaryContainerRef),
      .contrast_curve = phone6_watch7});
  primaryFixed_.emplace(DynamicColorOptions{
      .name = "primary_fixed",
      .palette = primary_palette,
      .tone = [this, light_standard](const DynamicScheme& s) {
        return primaryContainer().GetTone(light_standard(s));
      },
      .is_background = true,
      .background = phone_highest,
      .contrast_curve = raised_contrast});
  primaryFixedDim_.emplace(DynamicColorOptions{
      .name = "primary_fixed_dim",
      .palette = primary_palette,
      .tone = [this](const DynamicScheme& s) { return primaryFixed().GetTone(s); },
      .is_background = true,
      .tone_delta_pair = [this](const DynamicScheme&) -> std::optional<ToneDeltaPair> {
        return ToneDeltaPair{&primaryFixedDim(), &primaryFixed(), 5, TonePolarity::kDarker,
                             true, DeltaConstraint::kExact};
      }});
  onPrimaryFixed_.emplace(DynamicColorOptions{
      .name = "on_primary_fixed",
      .palette = primary_palette,
      .background = ref([this]() -> const DynamicColor& { return primaryFixedDim(); }),
      .contrast_curve = CurveOf(7)});
  onPrimaryFixedVariant_.emplace(DynamicColorOptions{
      .name = "on_primary_fixed_variant",
      .palette = primary_palette,
      .background = ref([this]() -> const DynamicColor& { return primaryFixedDim(); }),
      .contrast_curve = CurveOf(4.5)});
  inversePrimary_.emplace(DynamicColorOptions{
      .name = "inverse_primary",
      .palette = primary_palette,
      .tone = [](const DynamicScheme& s) { return TMaxC(s.primary_palette); },
      .background = ref([this]() -> const DynamicColor& { return inverseSurface(); }),
      .contrast_curve = phone6_watch7});

  // Secondaries
  const PaletteFn secondary_palette = Palette(&DynamicScheme::secondary_palette);
  secondary_.emplace(DynamicColorOptions{
      .name = "secondary",
      .palette = secondary_palette,
      .tone = [](const DynamicScheme& s) {
        const TonalPalette& p = s.secondary_palette;
        if (!IsPhone(s)) return s.variant == Variant::kNeutral ? 90.0 : TMaxC(p, 0, 90);
        if (s.variant == Variant::kNeutral) return s.is_dark ? TMinC(p, 0, 98) : TMaxC(p);
        if (s.variant == Variant::kVibrant) return TMaxC(p, 0, s.is_dark ? 90 : 98);
        return s.is_dark ? 80.0 : TMaxC(p);  // Expressive and TonalSpot
      },
      .is_background = true,
      .background = surface_background,
      .contrast_curve = phone45_watch7,
      .tone_delta_pair = [this](const DynamicScheme& s) -> std::optional<ToneDeltaPair> {
        if (!IsPhone(s)) return std::nullopt;
        return ToneDeltaPair{&secondaryContainer(), &secondary(), 5,
                             TonePolarity::kRelativeLighter, true, DeltaConstraint::kFarther};
      }});
  secondaryDim_.emplace(DynamicColorOptions{
      .name = "secondary_dim",
      .palette = secondary_palette,
      .tone = [](const DynamicScheme& s) {
        return s.variant == Variant::kNeutral ? 85.0 : TMaxC(s.secondary_palette, 0, 90);
      },
      .is_background = true,
      .background = ref([this]() -> const DynamicColor& { return surfaceContainerHigh(); }),
      .contrast_curve = CurveOf(4.5),
      .tone_delta_pair = [this](const DynamicScheme&) -> std::optional<ToneDeltaPair> {
        return ToneDeltaPair{secondaryDim(), &secondary(), 5, TonePolarity::kDarker, true,
                             DeltaConstraint::kFarther};
      }});
  onSecondary_.emplace(DynamicColorOptions{
      .name = "on_secondary",
      .palette = secondary_palette,
      .background = [this](const DynamicScheme& s) {
        return IsPhone(s) ? &secondary() : secondaryDim();
      },
      .contrast_curve = phone6_watch7});
  secondaryContainer_.emplace(DynamicColorOptions{
      .name = "secondary_container",
      .palette = secondary_palette,
      .tone = [](const DynamicScheme& s) {
        const TonalPalette& p = s.secondary_palette;
        if (!IsPhone(s)) return 30.0;
        if (s.variant == Variant::kVibrant) {
          return s.is_dark ? TMinC(p, 30, 40) : TMaxC(p, 84, 90);
        }
        if (s.variant == Variant::kExpressive) return s.is_dark ? 15.0 : TMaxC(p, 90, 95);
        return s.is_dark ? 25.0 : 90.0;
      },
      .is_background = true,
      .background = phone_highest,
      .contrast_curve = raised_contrast,
      .tone_delta_pair = [this](const DynamicScheme& s) -> std::optional<ToneDeltaPair> {
        if (IsPhone(s)) return std::nullopt;
        return ToneDeltaPair{&secondaryContainer(), secondaryDim(), 10, TonePolarity::kDarker,
                             true, DeltaConstraint::kFarther};
      }});
  onSecondaryContainer_.emplace(DynamicColorOptions{
      .name = "on_secondary_container",
      .palette = secondary_palette,
      .background = ref([this]() -> const DynamicColor& { return secondaryContainer(); }),
      .contrast_curve = phone6_watch7});
  secondaryFixed_.emplace(DynamicColorOptions{
      .name = "secondary_fixed",
      .palette = secondary_palette,
      .tone = [this, light_standard](const DynamicScheme& s) {
        return secondaryContainer().GetTone(light_standard(s));
      },
      .is_background = true,
      .background = phone_highest,
      .contrast_curve = raised_contrast});
  secondaryFixedDim_.emplace(DynamicColorOptions{
      .name = "secondary_fixed_dim",
      .palette = secondary_palette,
      .tone = [this](const DynamicScheme& s) { return secondaryFixed().GetTone(s); },
      .is_background = true,
      .tone_delta_pair = [this](const DynamicScheme&) -> std::optional<ToneDeltaPair> {
        return ToneDeltaPair{&secondaryFixedDim(), &secondaryFixed(), 5,
                             TonePolarity::kDarker, true, DeltaConstraint::kExact};
      }});
  onSecondaryFixed_.emplace(DynamicColorOptions{
      .name = "on_secondary_fixed",
      .palette = secondary_palette,
      .background = ref([this]() -> const DynamicColor& { return secondaryFixedDim(); }),
      .contrast_curve = CurveOf(7)});
  onSecondaryFixedVariant_.emplace(DynamicColorOptions{
      .name = "on_secondary_fixed_variant",
      .palette = secondary_palette,
      .background = ref([this]() -> const DynamicColor& { return secondaryFixedDim(); }),
      .contrast_curve = CurveOf(4.5)});

  // Tertiaries
  const PaletteFn tertiary_palette = Palette(&DynamicScheme::tertiary_palette);
  tertiary_.emplace(DynamicColorOptions{
      .name = "tertiary",
      .palette = tertiary_palette,
      .tone = [](const DynamicScheme& s) {
        const TonalPalette& p = s.tertiary_palette;
        if (!IsPhone(s)) {
          return s.variant == Variant::kTonalSpot ? TMaxC(p, 0, 90) : TMaxC(p);
        }
        if (s.variant == Variant::kExpressive || s.variant == Variant::kVibrant) {
          return TMaxC(p, 0, IsCyan(p.hue()) ? 88 : (s.is_dark ? 98 : 100));
        }
        return s.is_dark ? TMaxC(p, 0, 98) : TMaxC(p);  // Neutral and TonalSpot
      },
      .is_background = true,
      .background = surface_background,
      .contrast_curve = phone45_watch7,
      .tone_delta_pair = [this](const DynamicScheme& s) -> std::optional<ToneDeltaPair> {
        if (!IsPhone(s)) return std::nullopt;
        return ToneDeltaPair{&tertiaryContainer(), &tertiary(), 5,
                             TonePolarity::kRelativeLighter, true, DeltaConstraint::kFarther};
      }});
  tertiaryDim_.emplace(DynamicColorOptions{
      .name = "tertiary_dim",
      .palette = tertiary_palette,
      .tone = [](const DynamicScheme& s) {
        return s.variant == Variant::kTonalSpot ? TMaxC(s.tertiary_palette, 0, 90)
                                                : TMaxC(s.tertiary_palette);
      },
      .is_background = true,
      .background = ref([this]() -> const DynamicColor& { return surfaceContainerHigh(); }),
      .contrast_curve = CurveOf(4.5),
      .tone_delta_pair = [this](const DynamicScheme&) -> std::optional<ToneDeltaPair> {
        return ToneDeltaPair{tertiaryDim(), &tertiary(), 5, TonePolarity::kDarker, true,
                             DeltaConstraint::kFarther};
      }});
  onTertiary_.emplace(DynamicColorOptions{
      .name = "on_tertiary",
      .palette = tertiary_palette,
      .background = [this](const DynamicScheme& s) {
        return IsPhone(s) ? &tertiary() : tertiaryDim();
      },
      .contrast_curve = phone6_watch7});
  tertiaryContainer_.emplace(DynamicColorOptions{
      .name = "tertiary_container",
      .palette = tertiary_palette,
      .tone = [](const DynamicScheme& s) {
        const TonalPalette& p = s.tertiary_palette;
        if (!IsPhone(s)) {
          return s.variant == Variant::kTonalSpot ? TMaxC(p, 0, 90) : TMaxC(p);
        }
        switch (s.variant) {
          case Variant::kNeutral:
            return s.is_dark ? TMaxC(p, 0, 93) : TMaxC(p, 0, 96);
          case Variant::kTonalSpot:
            return TMaxC(p, 0, s.is_dark ? 93 : 100);
          case Variant::kExpressive:
            return TMaxC(p, 75, IsCyan(p.hue()) ? 88 : (s.is_dark ? 93 : 100));
          default:  // Vibrant
            return s.is_dark ? TMaxC(p, 0, 93) : TMaxC(p, 72, 100);
        }
      },
      .is_background = true,
      .background = phone_highest,
      .contrast_curve = raised_contrast,
      .tone_delta_pair = [this](const DynamicScheme& s) -> std::optional<ToneDeltaPair> {
        if (IsPhone(s)) return std::nullopt;
        return ToneDeltaPair{&tertiaryContainer(), tertiaryDim(), 10, TonePolarity::kDarker,
                             true, DeltaConstraint::kFarther};
      }});
  onTertiaryContainer_.emplace(DynamicColorOptions{
      .name = "on_tertiary_container",
      .palette = tertiary_palette,
      .background = ref([this]() -> const DynamicColor& { return tertiaryContainer(); }),
      .contrast_curve = phone6_watch7});
  tertiaryFixed_.emplace(DynamicColorOptions{
      .name = "tertiary_fixed",
      .palette = tertiary_palette,
      .tone = [this, light_standard](const DynamicScheme& s) {
        return tertiaryContainer().GetTone(light_standard(s));
      },
      .is_background = true,
      .background = phone_highest,
      .contrast_curve = raised_contrast});
  tertiaryFixedDim_.emplace(DynamicColorOptions{
      .name = "tertiary_fixed_dim",
      .palette = tertiary_palette,
      .tone = [this](const DynamicScheme& s) { return tertiaryFixed().GetTone(s); },
      .is_background = true,
      .tone_delta_pair = [this](const DynamicScheme&) -> std::optional<ToneDeltaPair> {
        return ToneDeltaPair{&tertiaryFixedDim(), &tertiaryFixed(), 5, TonePolarity::kDarker,
                             true, DeltaConstraint::kExact};
      }});
  onTertiaryFixed_.emplace(DynamicColorOptions{
      .name = "on_tertiary_fixed",
      .palette = tertiary_palette,
      .background = ref([this]() -> const DynamicColor& { return tertiaryFixedDim(); }),
      .contrast_curve = CurveOf(7)});
  onTertiaryFixedVariant_.emplace(DynamicColorOptions{
      .name = "on_tertiary_fixed_variant",
      .palette = tertiary_palette,
      .background = ref([this]() -> const DynamicColor& { return tertiaryFixedDim(); }),
      .contrast_curve = CurveOf(4.5)});

  // Errors
  const PaletteFn error_palette = Palette(&DynamicScheme::error_palette);
  error_.emplace(DynamicColorOptions{
      .name = "error",
      .palette = error_palette,
      .tone = [](const DynamicScheme& s) {
        if (!IsPhone(s)) return TMinC(s.error_palette);
        return s.is_dark ? TMinC(s.error_palette, 0, 98) : TMaxC(s.error_palette);
      },
      .is_background = true,
      .background = surface_background,
      .contrast_curve = phone45_watch7,
      .tone_delta_pair = [this](const DynamicScheme& s) -> std::optional<ToneDeltaPair> {
        if (!IsPhone(s)) return std::nullopt;
        return ToneDeltaPair{&errorContainer(), &error(), 5, TonePolarity::kRelativeLighter,
                             true, DeltaConstraint::kFarther};
      }});
  errorDim_.emplace(DynamicColorOptions{
      .name = "error_dim",
      .palette = error_palette,
      .tone = [](const DynamicScheme& s) { return TMinC(s.error_palette); },
      .is_background = true,
      .background = ref([this]() -> const DynamicColor& { return surfaceContainerHigh(); }),
      .contrast_curve = CurveOf(4.5),
      .tone_delta_pair = [this](const DynamicScheme&) -> std::optional<ToneDeltaPair> {
        return ToneDeltaPair{errorDim(), &error(), 5, TonePolarity::kDarker, true,
                             DeltaConstraint::kFarther};
      }});
  onError_.emplace(DynamicColorOptions{
      .name = "on_error",
      .palette = error_palette,
      .background = [this](const DynamicScheme& s) {
        return IsPhone(s) ? &error() : errorDim();
      },
      .contrast_curve = phone6_watch7});
  errorContainer_.emplace(DynamicColorOptions{
      .name = "error_container",
      .palette = error_palette,
      .tone = [](const DynamicScheme& s) {
        if (!IsPhone(s)) return 30.0;
        return s.is_dark ? TMinC(s.error_palette, 30, 93) : TMaxC(s.error_palette, 0, 90);
      },
      .is_background = true,
      .background = phone_highest,
      .contrast_curve = raised_contrast,
      .tone_delta_pair = [this](const DynamicScheme& s) -> std::optional<ToneDeltaPair> {
        if (IsPhone(s)) return std::nullopt;
        return ToneDeltaPair{&errorContainer(), errorDim(), 10, TonePolarity::kDarker, true,
                             DeltaConstraint::kFarther};
      }});
  onErrorContainer_.emplace(DynamicColorOptions{
      .name = "on_error_container",
      .palette = error_palette,
      .background = ref([this]() -> const DynamicColor& { return errorContainer(); }),
      .contrast_curve = phone45_watch7});

  // Aliases defined as renamed copies of other 2025 colors.
  surfaceVariant_.emplace(surfaceContainerHighest());
  surfaceVariant_->name = "surface_variant";
  surfaceTint_.emplace(primary());
  surfaceTint_->name = "surface_tint";
  background_.emplace(surface());
  background_->name = "background";
  onBackground_.emplace(onSurface());
  onBackground_->name = "on_background";
  onBackground_->tone = [this](const DynamicScheme& s) {
    return IsPhone(s) ? onSurface().GetTone(s) : 100.0;
  };
}

const ColorSpec2021& SpecFor(const DynamicScheme& scheme) {
  static const ColorSpec2021 spec2021;
  static const ColorSpec2025 spec2025;
  return scheme.spec_version == SpecVersion::k2025 ? spec2025 : spec2021;
}

}  // namespace material_color_utilities::ts
