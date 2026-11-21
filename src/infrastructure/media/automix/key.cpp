#include "key.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace cirrus::media::automix
{
namespace
{
// Krumhansl & Kessler's probe-tone ratings (Krumhansl 1990): how well each pitch class
// fits a key, from its tonic up.
constexpr std::array<double, 12> kMajor = {6.35, 2.23, 3.48, 2.33, 4.38, 4.09,
                                           2.52, 5.19, 2.39, 3.66, 2.29, 2.88};
constexpr std::array<double, 12> kMinor = {6.33, 2.68, 3.52, 5.38, 2.60, 3.53,
                                           2.54, 4.75, 3.98, 2.69, 3.34, 3.17};
// Frames quieter than this (their pitch classes' length; full scale is about 1) are left
// out, and a key needs this many seconds of the rest.
constexpr double kQuietest = 1e-3;
constexpr double kFewestSeconds = 5;
// A key this sure, or more, counts for harmony.
constexpr float kSure = 0.1f;

double correlation(const std::array<double, 12>& x, const std::array<double, 12>& profile,
                   int tonic)
{
    double mx = 0, mp = 0;
    for (int k = 0; k < 12; ++k) {
        mx += x[size_t((tonic + k) % 12)];
        mp += profile[size_t(k)];
    }
    mx /= 12;
    mp /= 12;
    double sxy = 0, sxx = 0, spp = 0;
    for (int k = 0; k < 12; ++k) {
        const double dx = x[size_t((tonic + k) % 12)] - mx, dp = profile[size_t(k)] - mp;
        sxy += dx * dp;
        sxx += dx * dx;
        spp += dp * dp;
    }
    return sxx > 0 && spp > 0 ? sxy / std::sqrt(sxx * spp) : 0;
}

// Its place on the circle of fifths (C major and A minor 0, G major and E minor 1, ...).
int fifths(const Key& key) { return (7 * (key.tonic + (key.minor ? 3 : 0))) % 12; }
} // namespace

Key findKey(const RhythmFeatures& f, double fromMs, double toMs)
{
    Key key;
    const auto frames = qsizetype(f.chroma.size());
    if (frames == 0 || f.chromaFrameMs <= 0)
        return key;
    const auto first = std::clamp<qsizetype>(
        qsizetype(std::ceil((fromMs - f.chromaOriginMs) / f.chromaFrameMs)), 0, frames);
    const auto end = std::clamp<qsizetype>(
        qsizetype(std::ceil((toMs - f.chromaOriginMs) / f.chromaFrameMs)), first, frames);
    // Each frame's pitch classes as a direction (loud frames don't outweigh the rest).
    std::array<double, 12> sum{};
    qsizetype counted = 0;
    for (qsizetype t = first; t < end; ++t) {
        const auto& classes = f.chroma[size_t(t)];
        double length = 0;
        for (const float c : classes)
            length += double(c) * c;
        length = std::sqrt(length);
        if (length < kQuietest)
            continue;
        for (size_t k = 0; k < 12; ++k)
            sum[k] += classes[k] / length;
        ++counted;
    }
    if (double(counted) * f.chromaFrameMs < kFewestSeconds * 1000)
        return key;
    std::array<double, 24> fit{}; // majors from C, then minors
    for (int tonic = 0; tonic < 12; ++tonic) {
        fit[size_t(tonic)] = correlation(sum, kMajor, tonic);
        fit[size_t(12 + tonic)] = correlation(sum, kMinor, tonic);
    }
    const auto best = int(std::max_element(fit.begin(), fit.end()) - fit.begin());
    key.tonic = best % 12;
    key.minor = best >= 12;
    double rival = -1;
    for (int k = 0; k < 24; ++k) {
        const Key other{k % 12, k >= 12};
        if (fifthsApart(key, other) >= 2)
            rival = std::max(rival, fit[size_t(k)]);
    }
    key.confidence = float(std::max(0.0, fit[size_t(best)] - rival));
    return key;
}

QString keyName(const Key& key)
{
    static const char* const names[12] = {"C",  "C#", "D",  "Eb", "E",  "F",
                                          "F#", "G",  "Ab", "A",  "Bb", "B"};
    if (!key.valid())
        return {};
    return QStringLiteral("%1%2 %3")
        .arg(QLatin1String(names[key.tonic]), key.minor ? QStringLiteral("m") : QString())
        .arg(double(key.confidence), 0, 'f', 2);
}

int fifthsApart(const Key& a, const Key& b)
{
    const int d = std::abs(fifths(a) - fifths(b));
    return std::min(d, 12 - d);
}

Harmony harmony(const Key& a, const Key& b)
{
    if (!a.valid() || !b.valid() || a.confidence < kSure || b.confidence < kSure)
        return Harmony::Unknown;
    return fifthsApart(a, b) <= 1 ? Harmony::Close : Harmony::Clash;
}

} // namespace cirrus::media::automix
