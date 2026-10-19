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

// C++ port by Cirrus; see dynamic_color.h.

#include "cirrus/dynamic_color.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "cpp/contrast/contrast.h"

namespace material_color_utilities::ts {

namespace {

double Clamp(double min, double max, double value) {
  return value < min ? min : (value > max ? max : value);
}

bool EndsWith(const std::string& text, const std::string& suffix) {
  return text.size() >= suffix.size() &&
         text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Shared tail of both specs: a color with a second background.
double SecondBackgroundTone(const DynamicScheme& s, const DynamicColor& color,
                            double answer, double desired_ratio) {
  const double bg_tone1 = color.background(s)->GetTone(s);
  const double bg_tone2 = color.second_background(s)->GetTone(s);
  const double upper = std::max(bg_tone1, bg_tone2);
  const double lower = std::min(bg_tone1, bg_tone2);
  if (RatioOfTones(upper, answer) >= desired_ratio &&
      RatioOfTones(lower, answer) >= desired_ratio) {
    return answer;
  }
  const double light_option = Lighter(upper, desired_ratio);
  const double dark_option = Darker(lower, desired_ratio);
  std::vector<double> availables;
  if (light_option != -1) availables.push_back(light_option);
  if (dark_option != -1) availables.push_back(dark_option);
  const bool prefers_light =
      DynamicColor::TonePrefersLightForeground(bg_tone1) ||
      DynamicColor::TonePrefersLightForeground(bg_tone2);
  if (prefers_light) {
    return light_option < 0 ? 100 : light_option;
  }
  if (availables.size() == 1) {
    return availables[0];
  }
  return dark_option < 0 ? 0 : dark_option;
}

bool HasSecondBackground(const DynamicScheme& s, const DynamicColor& color) {
  return color.second_background && color.second_background(s) != nullptr;
}

std::optional<ContrastCurve> CurveOf(const DynamicScheme& s,
                                     const DynamicColor& color) {
  return color.contrast_curve ? color.contrast_curve(s) : std::nullopt;
}

const DynamicColor* BackgroundOf(const DynamicScheme& s,
                                 const DynamicColor& color) {
  return color.background ? color.background(s) : nullptr;
}

double Tone2021(const DynamicScheme& s, const DynamicColor& color) {
  const bool decreasing_contrast = s.contrast_level < 0;
  const std::optional<ToneDeltaPair> pair =
      color.tone_delta_pair ? color.tone_delta_pair(s) : std::nullopt;
  if (pair) {
    const DynamicColor& role_a = *pair->role_a;
    const DynamicColor& role_b = *pair->role_b;
    const double delta = pair->delta;
    const TonePolarity polarity = pair->polarity;
    const bool a_is_nearer =
        polarity == TonePolarity::kNearer ||
        (polarity == TonePolarity::kLighter && !s.is_dark) ||
        (polarity == TonePolarity::kDarker && s.is_dark);
    const DynamicColor& nearer = a_is_nearer ? role_a : role_b;
    const DynamicColor& farther = a_is_nearer ? role_b : role_a;
    const bool am_nearer = color.name == nearer.name;
    const double expansion_dir = s.is_dark ? 1 : -1;
    double n_tone = nearer.tone(s);
    double f_tone = farther.tone(s);

    const DynamicColor* bg = BackgroundOf(s, color);
    const std::optional<ContrastCurve> n_curve = CurveOf(s, nearer);
    const std::optional<ContrastCurve> f_curve = CurveOf(s, farther);
    if (bg && n_curve && f_curve) {
      const double bg_tone = bg->GetTone(s);
      const double n_contrast = n_curve->Get(s.contrast_level);
      const double f_contrast = f_curve->Get(s.contrast_level);
      if (RatioOfTones(bg_tone, n_tone) < n_contrast) {
        n_tone = DynamicColor::ForegroundTone(bg_tone, n_contrast);
      }
      if (RatioOfTones(bg_tone, f_tone) < f_contrast) {
        f_tone = DynamicColor::ForegroundTone(bg_tone, f_contrast);
      }
      if (decreasing_contrast) {
        n_tone = DynamicColor::ForegroundTone(bg_tone, n_contrast);
        f_tone = DynamicColor::ForegroundTone(bg_tone, f_contrast);
      }
    }

    if ((f_tone - n_tone) * expansion_dir < delta) {
      f_tone = Clamp(0, 100, n_tone + delta * expansion_dir);
      if ((f_tone - n_tone) * expansion_dir < delta) {
        n_tone = Clamp(0, 100, f_tone - delta * expansion_dir);
      }
    }

    if (50 <= n_tone && n_tone < 60) {
      if (expansion_dir > 0) {
        n_tone = 60;
        f_tone = std::max(f_tone, n_tone + delta * expansion_dir);
      } else {
        n_tone = 49;
        f_tone = std::min(f_tone, n_tone + delta * expansion_dir);
      }
    } else if (50 <= f_tone && f_tone < 60) {
      if (pair->stay_together) {
        if (expansion_dir > 0) {
          n_tone = 60;
          f_tone = std::max(f_tone, n_tone + delta * expansion_dir);
        } else {
          n_tone = 49;
          f_tone = std::min(f_tone, n_tone + delta * expansion_dir);
        }
      } else {
        f_tone = expansion_dir > 0 ? 60 : 49;
      }
    }
    return am_nearer ? n_tone : f_tone;
  }

  double answer = color.tone(s);
  const DynamicColor* bg = BackgroundOf(s, color);
  const std::optional<ContrastCurve> curve = CurveOf(s, color);
  if (!bg || !curve) {
    return answer;
  }
  const double bg_tone = bg->GetTone(s);
  const double desired_ratio = curve->Get(s.contrast_level);
  if (RatioOfTones(bg_tone, answer) < desired_ratio) {
    answer = DynamicColor::ForegroundTone(bg_tone, desired_ratio);
  }
  if (decreasing_contrast) {
    answer = DynamicColor::ForegroundTone(bg_tone, desired_ratio);
  }
  if (color.is_background && 50 <= answer && answer < 60) {
    answer = RatioOfTones(49, bg_tone) >= desired_ratio ? 49 : 60;
  }
  if (!HasSecondBackground(s, color)) {
    return answer;
  }
  return SecondBackgroundTone(s, color, answer, desired_ratio);
}

double ClampBackground2025(const DynamicColor& color, double tone) {
  if (color.is_background && !EndsWith(color.name, "_fixed_dim")) {
    return tone >= 57 ? Clamp(65, 100, tone) : Clamp(0, 49, tone);
  }
  return tone;
}

double Tone2025(const DynamicScheme& s, const DynamicColor& color) {
  const std::optional<ToneDeltaPair> pair =
      color.tone_delta_pair ? color.tone_delta_pair(s) : std::nullopt;
  if (pair) {
    const DynamicColor& role_a = *pair->role_a;
    const DynamicColor& role_b = *pair->role_b;
    const TonePolarity polarity = pair->polarity;
    const double absolute_delta =
        polarity == TonePolarity::kDarker ||
                (polarity == TonePolarity::kRelativeLighter && s.is_dark) ||
                (polarity == TonePolarity::kRelativeDarker && !s.is_dark)
            ? -pair->delta
            : pair->delta;
    const bool am_role_a = color.name == role_a.name;
    const DynamicColor& self_role = am_role_a ? role_a : role_b;
    const DynamicColor& ref_role = am_role_a ? role_b : role_a;
    double self_tone = self_role.tone(s);
    const double ref_tone = ref_role.GetTone(s);
    const double relative_delta = absolute_delta * (am_role_a ? 1 : -1);

    switch (pair->constraint) {
      case DeltaConstraint::kExact:
        self_tone = Clamp(0, 100, ref_tone + relative_delta);
        break;
      case DeltaConstraint::kNearer:
        if (relative_delta > 0) {
          self_tone = Clamp(
              0, 100, Clamp(ref_tone, ref_tone + relative_delta, self_tone));
        } else {
          self_tone = Clamp(
              0, 100, Clamp(ref_tone + relative_delta, ref_tone, self_tone));
        }
        break;
      case DeltaConstraint::kFarther:
        if (relative_delta > 0) {
          self_tone = Clamp(ref_tone + relative_delta, 100, self_tone);
        } else {
          self_tone = Clamp(0, ref_tone + relative_delta, self_tone);
        }
        break;
    }

    const DynamicColor* bg = BackgroundOf(s, color);
    const std::optional<ContrastCurve> curve = CurveOf(s, color);
    if (bg && curve) {
      const double bg_tone = bg->GetTone(s);
      const double self_contrast = curve->Get(s.contrast_level);
      if (!(RatioOfTones(bg_tone, self_tone) >= self_contrast &&
            s.contrast_level >= 0)) {
        self_tone = DynamicColor::ForegroundTone(bg_tone, self_contrast);
      }
    }
    return ClampBackground2025(color, self_tone);
  }

  double answer = color.tone(s);
  const DynamicColor* bg = BackgroundOf(s, color);
  const std::optional<ContrastCurve> curve = CurveOf(s, color);
  if (!bg || !curve) {
    return answer;
  }
  const double bg_tone = bg->GetTone(s);
  const double desired_ratio = curve->Get(s.contrast_level);
  if (!(RatioOfTones(bg_tone, answer) >= desired_ratio &&
        s.contrast_level >= 0)) {
    answer = DynamicColor::ForegroundTone(bg_tone, desired_ratio);
  }
  answer = ClampBackground2025(color, answer);
  if (!HasSecondBackground(s, color)) {
    return answer;
  }
  return SecondBackgroundTone(s, color, answer, desired_ratio);
}

}  // namespace

double ContrastCurve::Get(double contrast_level) const {
  if (contrast_level <= -1.0) return low;
  if (contrast_level < 0.0) return Lerp(low, normal, (contrast_level + 1) / 1);
  if (contrast_level < 0.5) return Lerp(normal, medium, contrast_level / 0.5);
  if (contrast_level < 1.0) {
    return Lerp(medium, high, (contrast_level - 0.5) / 0.5);
  }
  return high;
}

DynamicColor::DynamicColor(DynamicColorOptions o)
    : name(std::move(o.name)),
      palette(std::move(o.palette)),
      tone(std::move(o.tone)),
      is_background(o.is_background),
      chroma_multiplier(std::move(o.chroma_multiplier)),
      background(std::move(o.background)),
      second_background(std::move(o.second_background)),
      contrast_curve(std::move(o.contrast_curve)),
      tone_delta_pair(std::move(o.tone_delta_pair)) {
  if (!tone) {
    // dynamic_color.ts getInitialToneFromBackground
    ColorFn bg = background;
    tone = [bg](const DynamicScheme& s) {
      const DynamicColor* color = bg ? bg(s) : nullptr;
      return color ? color->GetTone(s) : 50.0;
    };
  }
}

Hct DynamicColor::GetHct(const DynamicScheme& s) const {
  const TonalPalette& p = palette(s);
  const double t = GetTone(s);
  if (s.spec_version == SpecVersion::k2021) {
    return p.GetHct(t);
  }
  const double multiplier = chroma_multiplier ? chroma_multiplier(s) : 1;
  if (multiplier == 1) {
    return p.GetHct(t);
  }
  const double chroma = p.chroma() * multiplier;
  if (t == 99 && IsYellow(p.hue())) {
    return TonalPalette::FromHueAndChroma(p.hue(), chroma).GetHct(t);
  }
  return Hct(p.hue(), chroma, t);
}

Argb DynamicColor::GetArgb(const DynamicScheme& s) const {
  return GetHct(s).ToInt();
}

double DynamicColor::GetTone(const DynamicScheme& s) const {
  if (const double* cached = s.tone_cache.Find(this)) {
    return *cached;
  }
  const double tone = s.spec_version == SpecVersion::k2021
                          ? Tone2021(s, *this)
                          : Tone2025(s, *this);
  s.tone_cache.Store(this, tone);
  return tone;
}

double DynamicColor::ForegroundTone(double bg_tone, double ratio) {
  const double lighter_tone = LighterUnsafe(bg_tone, ratio);
  const double darker_tone = DarkerUnsafe(bg_tone, ratio);
  const double lighter_ratio = RatioOfTones(lighter_tone, bg_tone);
  const double darker_ratio = RatioOfTones(darker_tone, bg_tone);
  if (TonePrefersLightForeground(bg_tone)) {
    const bool negligible_difference =
        std::abs(lighter_ratio - darker_ratio) < 0.1 && lighter_ratio < ratio &&
        darker_ratio < ratio;
    return lighter_ratio >= ratio || lighter_ratio >= darker_ratio ||
                   negligible_difference
               ? lighter_tone
               : darker_tone;
  }
  return darker_ratio >= ratio || darker_ratio >= lighter_ratio ? darker_tone
                                                                : lighter_tone;
}

bool DynamicColor::TonePrefersLightForeground(double tone) {
  return std::floor(tone + 0.5) < 60.0;  // JS Math.round
}

bool DynamicColor::ToneAllowsLightForeground(double tone) {
  return std::floor(tone + 0.5) <= 49.0;
}

double DynamicColor::EnableLightForeground(double tone) {
  if (TonePrefersLightForeground(tone) && !ToneAllowsLightForeground(tone)) {
    return 49.0;
  }
  return tone;
}

}  // namespace material_color_utilities::ts
