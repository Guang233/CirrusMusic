#include "cover_palette.h"
#include "logging/log.h"

#include "cpp/cam/hct.h"
#include "cpp/quantize/celebi.h"
#include "cpp/score/score.h"

#include <QImage>
#include <QImageReader>
#include <QLoggingCategory>

#include <algorithm>
#include <cstdint>
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
        qCWarning(lcUiTheme) << log::event("palette.decode_failed")
                             << log::field("file", log::path(path))
                             << log::field("err", reader.error());
        return {};
    }
    return extractCoverPalette(std::move(image));
}

CoverPalette extractCoverPalette(QImage image)
{
    namespace mcu = material_color_utilities;

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

    // MCU falls back to Google Blue when every color is near-gray; a monochrome
    // cover should get a near-neutral theme instead, so rank again unfiltered.
    constexpr int kNoSuitableColor = 0; // transparent, never a quantized (opaque) color
    std::vector<mcu::Argb> ranked =
        mcu::RankedSuggestions(quantized.color_to_count, {4, kNoSuitableColor, true});
    if (ranked.size() == 1 && ranked.front() == kNoSuitableColor)
        ranked = mcu::RankedSuggestions(quantized.color_to_count, {4, kNoSuitableColor, false});

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
