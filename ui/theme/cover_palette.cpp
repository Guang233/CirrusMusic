#include "cover_palette.h"

#include "cpp/quantize/celebi.h"
#include "cpp/score/score.h"

#include <QImage>
#include <QImageReader>
#include <QLoggingCategory>

#include <algorithm>
#include <vector>

namespace cirrus::ui
{
namespace
{
Q_LOGGING_CATEGORY(lcPalette, "cirrus.ui.palette")

constexpr int kDecodeEdge = 128; // MD3 recommends ~112-128 px for quantization
constexpr int kQuantizeColors = 128;
constexpr int kDominantColors = 5;
} // namespace

CoverPalette extractCoverPalette(const QString& path)
{
    namespace mcu = material_color_utilities;

    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize size = reader.size();
    if (size.isValid())
        reader.setScaledSize(size.scaled(kDecodeEdge, kDecodeEdge, Qt::KeepAspectRatio));
    QImage image = reader.read();
    if (image.isNull()) {
        qCWarning(lcPalette) << "cannot decode cover" << path << reader.errorString();
        return {};
    }
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

    std::vector<std::pair<mcu::Argb, uint32_t>> byCount(quantized.color_to_count.begin(),
                                                        quantized.color_to_count.end());
    std::sort(byCount.begin(), byCount.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    for (std::size_t i = 0; i < byCount.size() && i < kDominantColors; ++i)
        palette.dominant << QColor::fromRgb(byCount[i].first);
    return palette;
}

} // namespace cirrus::ui
