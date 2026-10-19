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

// Small builders shared by the C++ color spec ports (Cirrus).

#ifndef CIRRUS_MCU_SPEC_HELPERS_H_
#define CIRRUS_MCU_SPEC_HELPERS_H_

#include "cirrus/dynamic_color.h"

namespace material_color_utilities::ts {

inline PaletteFn Palette(TonalPalette DynamicScheme::* member) {
  return [member](const DynamicScheme& s) -> const TonalPalette& {
    return s.*member;
  };
}

inline DoubleFn Fixed(double tone) {
  return [tone](const DynamicScheme&) { return tone; };
}

inline DoubleFn DarkLight(double dark, double light) {
  return [dark, light](const DynamicScheme& s) {
    return s.is_dark ? dark : light;
  };
}

inline DoubleFn DarkLightCurve(ContrastCurve dark, ContrastCurve light) {
  return [dark, light](const DynamicScheme& s) {
    return (s.is_dark ? dark : light).Get(s.contrast_level);
  };
}

inline CurveFn Curve(double low, double normal, double medium, double high) {
  return [=](const DynamicScheme&) -> std::optional<ContrastCurve> {
    return ContrastCurve{low, normal, medium, high};
  };
}

}  // namespace material_color_utilities::ts

#endif  // CIRRUS_MCU_SPEC_HELPERS_H_
