#include "cover_palette.h"
#include "logging/log.h"

#include "cpp/cam/hct.h"
#include "cpp/quantize/celebi.h"
#include "cpp/utils/utils.h"

#include <QImage>
#include <QImageReader>
#include <QLoggingCategory>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <vector>

namespace cirrus::ui
{
namespace
{
constexpr int kDecodeEdge = 128; // MD3 recommends ~112-128 px for quantization
constexpr int kQuantizeColors = 128;
constexpr int kDominantColors = 5;
// A pixel with real color, and how much of the cover must have it not to count as black
// and white. MD3's scoring picks a colorful minority (a small green patch on a black and
// white cover, 2026-11-10) and a tinted gray passes its chroma 5 cut; the player turned
// such covers green.
constexpr double kColorfulChroma = 15.0;
constexpr double kColorfulShare = 0.10;

namespace mcu = material_color_utilities;

// MCU's theme color scoring (cpp/score/score.cc, Apache License 2.0), with one change:
// colors under the chroma cut don't vote for a hue. Every neutral is hue ~209 in HCT,
// so in the original a white or gray background voted for cyan: a white cover with a
// red title got a pale cyan seed (2026-11-12). The cut still measures shares of the
// whole cover. `filter` off keeps every color (a monochrome cover's ranking).
std::vector<mcu::Argb> rankThemeColors(const std::map<mcu::Argb, uint32_t>& population,
                                       std::size_t desired, bool filter)
{
    constexpr double kTargetChroma = 48.0;
    constexpr double kWeightProportion = 0.7;
    constexpr double kWeightChromaAbove = 0.3;
    constexpr double kWeightChromaBelow = 0.1;
    constexpr double kCutoffChroma = 5.0;
    constexpr double kCutoffExcitedProportion = 0.01;

    std::vector<mcu::Hct> colors;
    std::vector<double> huePopulation(360, 0.0);
    double total = 0;
    for (const auto& [argb, count] : population) {
        const mcu::Hct hct(argb);
        colors.push_back(hct);
        total += count;
        if (hct.get_chroma() >= kCutoffChroma)
            huePopulation[static_cast<int>(std::floor(hct.get_hue())) % 360] += count;
    }
    if (total <= 0)
        return {};

    // A hue's share of the cover within the 30 degrees around it.
    std::vector<double> excited(360, 0.0);
    for (int hue = 0; hue < 360; ++hue) {
        const double share = huePopulation[hue] / total;
        for (int i = hue - 14; i < hue + 16; ++i)
            excited[mcu::SanitizeDegreesInt(i)] += share;
    }

    std::vector<std::pair<mcu::Hct, double>> scored;
    for (const mcu::Hct& hct : colors) {
        const double share =
            excited[mcu::SanitizeDegreesInt(static_cast<int>(std::round(hct.get_hue())))];
        if (filter && (hct.get_chroma() < kCutoffChroma || share <= kCutoffExcitedProportion))
            continue;
        const double chromaWeight =
            hct.get_chroma() < kTargetChroma ? kWeightChromaBelow : kWeightChromaAbove;
        scored.emplace_back(hct, share * 100.0 * kWeightProportion +
                                     (hct.get_chroma() - kTargetChroma) * chromaWeight);
    }
    std::stable_sort(scored.begin(), scored.end(),
                     [](const auto& a, const auto& b) { return a.second > b.second; });

    // As far apart in hue as possible: 90 degrees for four colors, down to 15.
    std::vector<mcu::Hct> chosen;
    for (int difference = 90; difference >= 15; --difference) {
        chosen.clear();
        for (const auto& [hct, score] : scored) {
            const bool near = std::any_of(chosen.begin(), chosen.end(), [&](const mcu::Hct& c) {
                return mcu::DiffDegrees(hct.get_hue(), c.get_hue()) < difference;
            });
            if (!near) {
                chosen.push_back(hct);
                if (chosen.size() >= desired)
                    break;
            }
        }
        if (chosen.size() >= desired)
            break;
    }
    std::vector<mcu::Argb> ranked;
    for (const mcu::Hct& hct : chosen)
        ranked.push_back(hct.ToInt());
    return ranked;
}
} // namespace

CoverPalette extractCoverPalette(const QString& path)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize size = reader.size();
    if (size.isValid())
        reader.setScaledSize(size.scaled(kDecodeEdge, kDecodeEdge, Qt::KeepAspectRatio));
    QImage image = reader.read();
    if (image.isNull()) {
        log::warn(lcUiTheme, "palette.decode_failed",
                  {{"file", log::path(path)}, {"err", reader.error()}});
        return {};
    }
    return extractCoverPalette(std::move(image));
}

CoverPalette extractCoverPalette(QImage image)
{
    if (image.isNull())
        return {};
    if (image.width() > kDecodeEdge || image.height() > kDecodeEdge)
        image =
            image.scaled(kDecodeEdge, kDecodeEdge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    image = image.convertToFormat(QImage::Format_ARGB32);

    std::vector<mcu::Argb> pixels;
    pixels.reserve(static_cast<std::size_t>(image.width()) * image.height());
    for (int y = 0; y < image.height(); ++y) {
        const auto* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        pixels.insert(pixels.end(), line, line + image.width());
    }

    const mcu::QuantizerResult quantized = mcu::QuantizeCelebi(pixels, kQuantizeColors);
    if (quantized.color_to_count.empty())
        return {};

    // Nothing passes the filter when every color is near-gray (MCU would fall back to
    // Google Blue); a monochrome cover should get a near-neutral theme, so rank again
    // unfiltered.
    std::vector<mcu::Argb> ranked = rankThemeColors(quantized.color_to_count, 4, true);
    if (ranked.empty())
        ranked = rankThemeColors(quantized.color_to_count, 4, false);
    if (ranked.empty())
        return {};

    CoverPalette palette;
    palette.valid = true;
    for (mcu::Argb argb : ranked)
        palette.accents << QColor::fromRgb(argb);
    palette.seed = palette.accents.constFirst();

    std::uint64_t total = 0, colorful = 0;
    double tone = 0;
    for (const auto& [argb, count] : quantized.color_to_count) {
        const mcu::Hct hct(argb);
        total += count;
        tone += hct.get_tone() * count;
        if (hct.get_chroma() >= kColorfulChroma)
            colorful += count;
    }
    palette.monochrome = total == 0 || double(colorful) / double(total) < kColorfulShare;
    if (palette.monochrome) // the cover's mean tone, no hue: the monochrome scheme
        palette.seed = QColor::fromRgb(mcu::Hct(0, 0, total ? tone / double(total) : 50).ToInt());

    std::vector<std::pair<mcu::Argb, uint32_t>> byCount(quantized.color_to_count.begin(),
                                                        quantized.color_to_count.end());
    std::sort(byCount.begin(), byCount.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    for (std::size_t i = 0; i < byCount.size() && i < kDominantColors; ++i)
        palette.dominant << QColor::fromRgb(byCount[i].first);

    // Smooth downscaling averages each cell's area.
    const QImage grid = image.scaled(CoverPalette::kGridSize, CoverPalette::kGridSize,
                                     Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    for (int y = 0; y < grid.height(); ++y) {
        for (int x = 0; x < grid.width(); ++x)
            palette.grid << QColor::fromRgb(grid.pixel(x, y) | 0xff000000u);
    }
    return palette;
}

} // namespace cirrus::ui
