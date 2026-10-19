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

// C++ port by Cirrus of typescript/dynamiccolor/color_spec_2021.ts and
// color_spec_2025.ts (upstream 5b3618b1). As in TypeScript, the 2025 spec
// derives from 2021 and colors refer to each other through virtual accessors.

#ifndef CIRRUS_MCU_COLOR_SPEC_H_
#define CIRRUS_MCU_COLOR_SPEC_H_

#include <optional>

#include "cirrus/dynamic_color.h"

namespace material_color_utilities::ts {

// Every color defined by both specs (the *Dim colors exist only in 2025).
#define CIRRUS_MCU_SPEC_COLORS(X) \
  X(primaryPaletteKeyColor)       \
  X(secondaryPaletteKeyColor)     \
  X(tertiaryPaletteKeyColor)      \
  X(neutralPaletteKeyColor)       \
  X(neutralVariantPaletteKeyColor) \
  X(errorPaletteKeyColor)         \
  X(background)                   \
  X(onBackground)                 \
  X(surface)                      \
  X(surfaceDim)                   \
  X(surfaceBright)                \
  X(surfaceContainerLowest)       \
  X(surfaceContainerLow)          \
  X(surfaceContainer)             \
  X(surfaceContainerHigh)         \
  X(surfaceContainerHighest)      \
  X(onSurface)                    \
  X(surfaceVariant)               \
  X(onSurfaceVariant)             \
  X(inverseSurface)               \
  X(inverseOnSurface)             \
  X(outline)                      \
  X(outlineVariant)               \
  X(shadow)                       \
  X(scrim)                        \
  X(surfaceTint)                  \
  X(primary)                      \
  X(onPrimary)                    \
  X(primaryContainer)             \
  X(onPrimaryContainer)           \
  X(inversePrimary)               \
  X(secondary)                    \
  X(onSecondary)                  \
  X(secondaryContainer)           \
  X(onSecondaryContainer)         \
  X(tertiary)                     \
  X(onTertiary)                   \
  X(tertiaryContainer)            \
  X(onTertiaryContainer)          \
  X(error)                        \
  X(onError)                      \
  X(errorContainer)               \
  X(onErrorContainer)             \
  X(primaryFixed)                 \
  X(primaryFixedDim)              \
  X(onPrimaryFixed)               \
  X(onPrimaryFixedVariant)        \
  X(secondaryFixed)               \
  X(secondaryFixedDim)            \
  X(onSecondaryFixed)             \
  X(onSecondaryFixedVariant)      \
  X(tertiaryFixed)                \
  X(tertiaryFixedDim)             \
  X(onTertiaryFixed)              \
  X(onTertiaryFixedVariant)

#define CIRRUS_MCU_DIM_COLORS(X) \
  X(primaryDim)                  \
  X(secondaryDim)                \
  X(tertiaryDim)                 \
  X(errorDim)

class ColorSpec2021 {
 public:
  ColorSpec2021();
  virtual ~ColorSpec2021() = default;
  ColorSpec2021(const ColorSpec2021&) = delete;
  ColorSpec2021& operator=(const ColorSpec2021&) = delete;

#define CIRRUS_MCU_ACCESSOR(name) \
  virtual const DynamicColor& name() const { return *name##_; }
  CIRRUS_MCU_SPEC_COLORS(CIRRUS_MCU_ACCESSOR)
#undef CIRRUS_MCU_ACCESSOR

  // Undefined (nullptr) in the 2021 spec.
  virtual const DynamicColor* primaryDim() const { return nullptr; }
  virtual const DynamicColor* secondaryDim() const { return nullptr; }
  virtual const DynamicColor* tertiaryDim() const { return nullptr; }
  virtual const DynamicColor* errorDim() const { return nullptr; }

  virtual const DynamicColor& highestSurface(const DynamicScheme& s) const;

 private:
#define CIRRUS_MCU_MEMBER(name) std::optional<DynamicColor> name##_;
  CIRRUS_MCU_SPEC_COLORS(CIRRUS_MCU_MEMBER)
#undef CIRRUS_MCU_MEMBER
};

class ColorSpec2025 : public ColorSpec2021 {
 public:
  ColorSpec2025();

  // Colors the 2025 spec does not redefine fall back to the 2021 definition.
#define CIRRUS_MCU_ACCESSOR(name)                                  \
  const DynamicColor& name() const override {                      \
    return name##_ ? *name##_ : ColorSpec2021::name();             \
  }
  CIRRUS_MCU_SPEC_COLORS(CIRRUS_MCU_ACCESSOR)
#undef CIRRUS_MCU_ACCESSOR
#define CIRRUS_MCU_ACCESSOR(name) \
  const DynamicColor* name() const override { return &*name##_; }
  CIRRUS_MCU_DIM_COLORS(CIRRUS_MCU_ACCESSOR)
#undef CIRRUS_MCU_ACCESSOR

  const DynamicColor& highestSurface(const DynamicScheme& s) const override;

 private:
#define CIRRUS_MCU_MEMBER(name) std::optional<DynamicColor> name##_;
  CIRRUS_MCU_SPEC_COLORS(CIRRUS_MCU_MEMBER)
  CIRRUS_MCU_DIM_COLORS(CIRRUS_MCU_MEMBER)
#undef CIRRUS_MCU_MEMBER
};

// The spec matching scheme.spec_version.
const ColorSpec2021& SpecFor(const DynamicScheme& scheme);

}  // namespace material_color_utilities::ts

#endif  // CIRRUS_MCU_COLOR_SPEC_H_
