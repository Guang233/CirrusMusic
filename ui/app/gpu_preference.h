#pragma once

// Which GPU renders the window on a hybrid-graphics machine (an integrated GPU drives
// the display, a discrete one renders on request). Linux only: Windows has a
// per-app graphics setting, and macOS picks the GPU itself.
namespace cirrus::ui::gpu
{

// Whether the machine has a discrete GPU that apply() can select.
[[nodiscard]] bool switchable();

// Asks for the discrete GPU when `preferDiscrete` and switchable(): sets the variables
// prime-run sets (NVIDIA driver) or DRI_PRIME (Mesa). Call before the application
// object exists. Leaves everything alone when the user already set one of them, or
// CIRRUS_GPU=default.
void apply(bool preferDiscrete);

} // namespace cirrus::ui::gpu
