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

// C++ port by Cirrus of typescript/dynamiccolor/{dynamic_color,
// contrast_curve,tone_delta_pair}.ts (upstream 5b3618b1), 2021 and 2025 specs.

#ifndef CIRRUS_MCU_DYNAMIC_COLOR_H_
#define CIRRUS_MCU_DYNAMIC_COLOR_H_

#include <functional>
#include <optional>
#include <string>

#include "cirrus/dynamic_scheme.h"

namespace material_color_utilities::ts {

class DynamicColor;

struct ContrastCurve {
  double low;
  double normal;
  double medium;
  double high;
  double Get(double contrast_level) const;
};

enum class TonePolarity {
  kDarker,
  kLighter,
  kNearer,
  kFarther,
  kRelativeDarker,
  kRelativeLighter
};
enum class DeltaConstraint { kExact, kNearer, kFarther };

struct ToneDeltaPair {
  const DynamicColor* role_a;
  const DynamicColor* role_b;
  double delta;
  TonePolarity polarity;
  bool stay_together;
  DeltaConstraint constraint = DeltaConstraint::kExact;
};

using PaletteFn = std::function<const TonalPalette&(const DynamicScheme&)>;
using DoubleFn = std::function<double(const DynamicScheme&)>;
using ColorFn = std::function<const DynamicColor*(const DynamicScheme&)>;
using CurveFn =
    std::function<std::optional<ContrastCurve>(const DynamicScheme&)>;
using PairFn =
    std::function<std::optional<ToneDeltaPair>(const DynamicScheme&)>;

struct DynamicColorOptions {
  std::string name;
  PaletteFn palette;
  DoubleFn tone;  // empty: tone of `background`, or 50
  bool is_background = false;
  DoubleFn chroma_multiplier;  // empty: 1
  ColorFn background;
  ColorFn second_background;
  CurveFn contrast_curve;
  PairFn tone_delta_pair;
};

class DynamicColor {
 public:
  explicit DynamicColor(DynamicColorOptions options);

  Argb GetArgb(const DynamicScheme& scheme) const;
  Hct GetHct(const DynamicScheme& scheme) const;
  double GetTone(const DynamicScheme& scheme) const;

  static double ForegroundTone(double bg_tone, double ratio);
  static bool TonePrefersLightForeground(double tone);
  static bool ToneAllowsLightForeground(double tone);
  static double EnableLightForeground(double tone);

  std::string name;
  PaletteFn palette;
  DoubleFn tone;
  bool is_background;
  DoubleFn chroma_multiplier;
  ColorFn background;
  ColorFn second_background;
  CurveFn contrast_curve;
  PairFn tone_delta_pair;
};

}  // namespace material_color_utilities::ts

#endif  // CIRRUS_MCU_DYNAMIC_COLOR_H_
