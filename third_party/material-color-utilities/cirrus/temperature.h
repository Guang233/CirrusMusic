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

// C++ port by Cirrus of typescript/temperature/temperature_cache.ts and
// typescript/dislike/dislike_analyzer.ts (upstream 5b3618b1). The upstream
// C++ versions give slightly different results; the TS ones are the reference.

#ifndef CIRRUS_MCU_TEMPERATURE_H_
#define CIRRUS_MCU_TEMPERATURE_H_

#include <vector>

#include "cpp/cam/hct.h"

namespace material_color_utilities::ts {

class TemperatureCache {
 public:
  explicit TemperatureCache(Hct input);

  std::vector<Hct> Analogous(int count = 5, int divisions = 12) const;
  Hct Complement() const;

 private:
  // Index kInput refers to the input color, others to hcts_by_hue_.
  static constexpr int kInput = 361;
  double RelativeTemperature(int index) const;
  const Hct& At(int index) const;

  Hct input_;
  std::vector<Hct> hcts_by_hue_;  // hue 0..360 inclusive
  std::vector<double> temps_;     // by index, kInput last
  int coldest_ = 0;
  int warmest_ = 0;
};

Hct FixIfDislikedTs(const Hct& hct);

}  // namespace material_color_utilities::ts

#endif  // CIRRUS_MCU_TEMPERATURE_H_
