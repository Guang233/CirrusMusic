#pragma once

#include <cmath>

namespace cirrus::ui
{

// Damped harmonic oscillator with unit mass, as used by MD3 motion tokens
// (stiffness k, damping ratio zeta). Displacement is measured from the target.
struct SpringState {
    double displacement = 0.0;
    double velocity = 0.0;
};

// Closed-form state after `t` seconds; exact for any frame rate.
inline SpringState springAdvance(double stiffness, double dampingRatio, SpringState s, double t)
{
    const double w = std::sqrt(stiffness);
    const double z = dampingRatio;
    const double d0 = s.displacement;
    const double v0 = s.velocity;
    if (z < 1.0) {
        const double wd = w * std::sqrt(1.0 - z * z);
        const double a = d0;
        const double b = (v0 + z * w * d0) / wd;
        const double e = std::exp(-z * w * t);
        const double c = std::cos(wd * t);
        const double sn = std::sin(wd * t);
        return {e * (a * c + b * sn), e * ((b * wd - a * z * w) * c - (a * wd + b * z * w) * sn)};
    }
    if (z == 1.0) {
        const double b = v0 + w * d0;
        const double e = std::exp(-w * t);
        return {(d0 + b * t) * e, (b - w * (d0 + b * t)) * e};
    }
    const double r = w * std::sqrt(z * z - 1.0);
    const double r1 = -z * w + r;
    const double r2 = -z * w - r;
    const double c2 = (v0 - r1 * d0) / (r2 - r1);
    const double c1 = d0 - c2;
    const double e1 = std::exp(r1 * t);
    const double e2 = std::exp(r2 * t);
    return {c1 * e1 + c2 * e2, c1 * r1 * e1 + c2 * r2 * e2};
}

// Time for a unit step to stay within `threshold` of the target.
inline double springSettleSeconds(double stiffness, double dampingRatio, double threshold = 0.001)
{
    constexpr double step = 0.0005;
    constexpr double limit = 5.0;
    double last = 0.0;
    for (double t = 0.0; t < limit; t += step) {
        const SpringState s = springAdvance(stiffness, dampingRatio, {-1.0, 0.0}, t);
        if (std::abs(s.displacement) > threshold)
            last = t;
    }
    return last + step;
}

} // namespace cirrus::ui
