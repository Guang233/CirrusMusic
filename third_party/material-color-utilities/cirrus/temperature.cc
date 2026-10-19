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

// C++ port by Cirrus; see temperature.h.

#include "cirrus/temperature.h"

#include <algorithm>
#include <cmath>
#include <numeric>

#include "cpp/quantize/lab.h"
#include "cpp/utils/utils.h"

namespace material_color_utilities::ts {

namespace {

constexpr double kPi = 3.141592653589793;

int RoundJs(double value) { return static_cast<int>(std::floor(value + 0.5)); }

double RawTemperature(const Hct& color) {
  const Lab lab = LabFromInt(color.ToInt());
  const double hue =
      SanitizeDegreesDouble(std::atan2(lab.b, lab.a) * 180.0 / kPi);
  const double chroma = std::sqrt(lab.a * lab.a + lab.b * lab.b);
  return -0.5 + 0.02 * std::pow(chroma, 1.07) *
                    std::cos(SanitizeDegreesDouble(hue - 50.0) * kPi / 180.0);
}

bool IsBetween(double angle, double a, double b) {
  if (a < b) {
    return a <= angle && angle <= b;
  }
  return a <= angle || angle <= b;
}

}  // namespace

TemperatureCache::TemperatureCache(Hct input) : input_(input) {
  hcts_by_hue_.reserve(361);
  for (double hue = 0.0; hue <= 360.0; hue += 1.0) {
    hcts_by_hue_.emplace_back(hue, input.get_chroma(), input.get_tone());
  }
  temps_.reserve(kInput + 1);
  for (const Hct& hct : hcts_by_hue_) temps_.push_back(RawTemperature(hct));
  temps_.push_back(RawTemperature(input_));

  // hctsByTemp: hues then the input, stable-sorted by temperature (JS sort).
  std::vector<int> by_temp(kInput + 1);
  std::iota(by_temp.begin(), by_temp.end(), 0);
  std::stable_sort(by_temp.begin(), by_temp.end(),
                   [this](int a, int b) { return temps_[a] < temps_[b]; });
  coldest_ = by_temp.front();
  warmest_ = by_temp.back();
}

const Hct& TemperatureCache::At(int index) const {
  return index == kInput ? input_ : hcts_by_hue_[index];
}

double TemperatureCache::RelativeTemperature(int index) const {
  const double range = temps_[warmest_] - temps_[coldest_];
  const double difference = temps_[index] - temps_[coldest_];
  if (range == 0.0) {
    return 0.5;
  }
  return difference / range;
}

std::vector<Hct> TemperatureCache::Analogous(int count, int divisions) const {
  const int start_hue = RoundJs(input_.get_hue());
  const int start = start_hue;
  double last_temp = RelativeTemperature(start);
  std::vector<int> all_colors = {start};

  double absolute_total_temp_delta = 0.0;
  for (int i = 0; i < 360; i++) {
    const int hue = SanitizeDegreesInt(start_hue + i);
    const double temp = RelativeTemperature(hue);
    absolute_total_temp_delta += std::abs(temp - last_temp);
    last_temp = temp;
  }

  int hue_addend = 1;
  const double temp_step = absolute_total_temp_delta / divisions;
  double total_temp_delta = 0.0;
  last_temp = RelativeTemperature(start);
  while (static_cast<int>(all_colors.size()) < divisions) {
    const int hue = SanitizeDegreesInt(start_hue + hue_addend);
    const double temp = RelativeTemperature(hue);
    total_temp_delta += std::abs(temp - last_temp);
    double desired = all_colors.size() * temp_step;
    bool index_satisfied = total_temp_delta >= desired;
    int index_addend = 1;
    while (index_satisfied && static_cast<int>(all_colors.size()) < divisions) {
      all_colors.push_back(hue);
      desired = (all_colors.size() + index_addend) * temp_step;
      index_satisfied = total_temp_delta >= desired;
      index_addend++;
    }
    last_temp = temp;
    hue_addend++;
    if (hue_addend > 360) {
      while (static_cast<int>(all_colors.size()) < divisions) {
        all_colors.push_back(hue);
      }
      break;
    }
  }

  const int size = static_cast<int>(all_colors.size());
  std::vector<Hct> answers = {input_};
  const int increase_hue_count = (count - 1) / 2;
  for (int i = 1; i < increase_hue_count + 1; i++) {
    int index = 0 - i;
    while (index < 0) index = size + index;
    if (index >= size) index = index % size;
    answers.insert(answers.begin(), At(all_colors[index]));
  }
  const int decrease_hue_count = count - increase_hue_count - 1;
  for (int i = 1; i < decrease_hue_count + 1; i++) {
    int index = i;
    if (index >= size) index = index % size;
    answers.push_back(At(all_colors[index]));
  }
  return answers;
}

Hct TemperatureCache::Complement() const {
  const double coldest_hue = At(coldest_).get_hue();
  const double coldest_temp = temps_[coldest_];
  const double warmest_hue = At(warmest_).get_hue();
  const double warmest_temp = temps_[warmest_];
  const double range = warmest_temp - coldest_temp;
  const bool start_is_coldest_to_warmest =
      IsBetween(input_.get_hue(), coldest_hue, warmest_hue);
  const double start_hue =
      start_is_coldest_to_warmest ? warmest_hue : coldest_hue;
  const double end_hue = start_is_coldest_to_warmest ? coldest_hue : warmest_hue;
  double smallest_error = 1000.0;
  int answer = RoundJs(input_.get_hue());
  const double complement_relative_temp = 1.0 - RelativeTemperature(kInput);
  for (double hue_addend = 0.0; hue_addend <= 360.0; hue_addend += 1.0) {
    const double hue = SanitizeDegreesDouble(start_hue + hue_addend);
    if (!IsBetween(hue, start_hue, end_hue)) {
      continue;
    }
    const int possible = RoundJs(hue);
    const double relative_temp = (temps_[possible] - coldest_temp) / range;
    const double error = std::abs(complement_relative_temp - relative_temp);
    if (error < smallest_error) {
      smallest_error = error;
      answer = possible;
    }
  }
  return At(answer);
}

Hct FixIfDislikedTs(const Hct& hct) {
  const int hue = RoundJs(hct.get_hue());
  const bool disliked = hue >= 90 && hue <= 111 &&
                        RoundJs(hct.get_chroma()) > 16 &&
                        RoundJs(hct.get_tone()) < 65;
  return disliked ? Hct(hct.get_hue(), hct.get_chroma(), 70.0) : hct;
}

}  // namespace material_color_utilities::ts
