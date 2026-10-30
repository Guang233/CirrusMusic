#pragma once

#include "sources.h"

namespace cirrus::ui
{

// Sources backed by the fictional sample catalog (Cirrus.Samples, which the
// executable must link), with the delay and failure knobs of FakeControls.
[[nodiscard]] Sources makeFakeSources();

} // namespace cirrus::ui
