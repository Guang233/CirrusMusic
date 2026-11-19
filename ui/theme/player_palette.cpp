#include "player_palette.h"

#include "theme.h"
#include "tokens.h"

#include "cpp/blend/blend.h"
#include "cpp/cam/hct.h"

#include <QQmlEngine>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace cirrus::ui
{
namespace
{
namespace mcu = material_color_utilities;

// Tone ranges and levels; docs/ui/pages/player.md §3.1 explains the contrast budget.
struct Levels {
    double gridLow, gridHigh, gridChroma; // the cover's tones map linearly into the range
    double solidTone;
    double content, contentVariant, contentChroma;
    double shadowTone;
};
// 2026-11-10: the light set was near white (82–94 at 70% chroma, "white too thick");
// now 66–86 at a little more than the cover's chroma, content darker to keep 5:1 / 3:1
// against tone 66 (15 → 5.8:1, 32 → 3.3:1).
constexpr Levels kDark{15, 35, 1.15, 20, 85, 68, 16, 10};
constexpr Levels kLight{66, 86, 1.15, 80, 15, 32, 18, 35};
// Below this mean chroma the cover counts as black and white: neutral content.
constexpr double kNeutralChroma = 5.0;
constexpr double kGlareTone = 98;
// The glare stays near white whatever the content chroma.
constexpr double kGlareChroma = 6;
// The cover's own tone span is stretched over the grid range, but a span narrower than
// this counts as this wide: a flat cover stays calm, a busy one uses the whole range.
constexpr double kMinToneSpan = 35.0;

QColor fromHct(double hue, double chroma, double tone)
{
    return QColor::fromRgb(mcu::Hct(hue, chroma, tone).ToInt());
}

mcu::Hct toHct(const QColor& color) { return mcu::Hct(color.rgba() | 0xff000000u); }
} // namespace

PlayerPalette::PlayerPalette(QObject* parent) : QObject(parent), m_scheme(new ColorScheme(this))
{
    m_animation.setStartValue(0.0);
    m_animation.setEndValue(1.0);
    connect(&m_animation, &QVariantAnimation::valueChanged, this,
            [this](const QVariant& value) { blend(value.toReal()); });
    m_current = computeColors();
}

void PlayerPalette::componentComplete()
{
    m_complete = true;
    if (QQmlEngine* engine = qmlEngine(this))
        m_theme = engine->singletonInstance<Theme*>("Cirrus.Theme", "Theme");
    if (m_theme) {
        // The scheme follows the theme's variant, spec and contrast.
        for (auto signal :
             {&Theme::variantChanged, &Theme::specVersionChanged, &Theme::contrastChanged})
            connect(m_theme, signal, this, [this] { update(true); });
    }
    update(false);
}

void PlayerPalette::setPalette(const CoverPalette& palette)
{
    m_palette = palette;
    emit paletteChanged();
    update(true);
}

void PlayerPalette::setDark(bool dark)
{
    if (dark == m_dark)
        return;
    m_dark = dark;
    emit darkChanged();
    update(true);
}

void PlayerPalette::setCoverSeed(bool coverSeed)
{
    if (coverSeed == m_coverSeed)
        return;
    m_coverSeed = coverSeed;
    emit coverSeedChanged();
    update(true);
}

void PlayerPalette::setVariant(int variant)
{
    if (variant == m_variant)
        return;
    m_variant = variant;
    emit variantChanged();
    update(true);
}

double PlayerPalette::toneLow() const { return (m_dark ? kDark : kLight).gridLow; }
double PlayerPalette::toneHigh() const { return (m_dark ? kDark : kLight).gridHigh; }
double PlayerPalette::chromaFactor() const { return (m_dark ? kDark : kLight).gridChroma; }

PlayerPalette::Tint PlayerPalette::tint() const
{
    Tint tint;
    if (!m_palette.valid || m_palette.grid.size() != kGridCells)
        return tint;
    // Gray cells barely vote: their hue is noise (every neutral is hue 209 in HCT).
    double x = 0, y = 0, chromaSum = 0;
    for (const QColor& color : m_palette.grid) {
        const mcu::Hct cell = toHct(color);
        const double angle = cell.get_hue() * std::numbers::pi / 180.0;
        x += cell.get_chroma() * std::cos(angle);
        y += cell.get_chroma() * std::sin(angle);
        chromaSum += cell.get_chroma();
    }
    tint.hue = std::atan2(y, x) * 180.0 / std::numbers::pi;
    if (tint.hue < 0)
        tint.hue += 360.0;
    tint.chroma = std::hypot(x, y) / kGridCells;
    tint.neutral = m_palette.monochrome || chromaSum / kGridCells < kNeutralChroma;
    return tint;
}

QList<QColor> PlayerPalette::computeColors() const
{
    const Levels& levels = m_dark ? kDark : kLight;
    QList<QColor> colors(ColorCount);
    const bool valid = m_palette.valid && m_palette.grid.size() == kGridCells;

    // Grid; meanwhile the darkest cell.
    QList<mcu::Hct> cells;
    double lowest = 100, highest = 0;
    for (int i = 0; i < kGridCells; ++i) {
        cells.append(valid ? toHct(m_palette.grid[i]) : mcu::Hct(0, 0, 40));
        lowest = std::min(lowest, cells.last().get_tone());
        highest = std::max(highest, cells.last().get_tone());
    }
    const double span = std::max(highest - lowest, kMinToneSpan);
    const double middle = (lowest + highest) / 2;

    mcu::Hct darkest(0, 0, 100);
    for (int i = 0; i < kGridCells; ++i) {
        const mcu::Hct& cell = cells[i];
        const double tone = levels.gridLow + (0.5 + (cell.get_tone() - middle) / span) *
                                                 (levels.gridHigh - levels.gridLow);
        // A black and white cover stays gray (its slight tints would turn green or cyan).
        const double chroma = m_palette.monochrome ? 0.0 : cell.get_chroma() * levels.gridChroma;
        colors[i] = fromHct(cell.get_hue(), chroma, tone);
        if (cell.get_tone() < darkest.get_tone())
            darkest = cell;
    }

    const Tint tint = this->tint();
    const double contentChroma = tint.neutral ? 0.0 : levels.contentChroma;
    colors[Content] = fromHct(tint.hue, contentChroma, levels.content);
    colors[ContentVariant] = fromHct(tint.hue, contentChroma, levels.contentVariant);
    colors[Glare] = fromHct(tint.hue, std::min(contentChroma, kGlareChroma), kGlareTone);
    // On the cover-seeded scheme's PrimaryContainer (the Material style), the text is
    // its OnPrimaryContainer, and the secondary level that at 72% over the container.
    if (m_coverSeed && m_theme) {
        const ColorScheme::Colors scheme = schemeColors();
        const QRgb ink = scheme[ColorScheme::OnPrimaryContainer].rgb();
        const QRgb ground = scheme[ColorScheme::PrimaryContainer].rgb();
        colors[Content] = QColor::fromRgb(ink);
        colors[ContentVariant] = QColor::fromRgb(mcu::BlendCam16Ucs(ground, ink, 0.72));
    }
    // The background's own hue and colorfulness, not the most common color: that is
    // often white or black, whose hue is noise.
    colors[Solid] =
        fromHct(tint.hue, tint.neutral ? 0.0 : tint.chroma * levels.gridChroma, levels.solidTone);
    colors[Shadow] = fromHct(darkest.get_hue(), m_palette.monochrome ? 0.0 : darkest.get_chroma(),
                             levels.shadowTone);
    return colors;
}

void PlayerPalette::update(bool animate)
{
    if (!m_complete)
        return;
    const SpringSpec spec = Motion::spec(Motion::SlowEffects);
    const bool animated = animate && !Motion::reducedMotion();
    m_animation.stop();
    m_to = computeColors();
    if (!animated) {
        m_current = m_to;
        emit colorsChanged();
    } else {
        m_from = m_current; // an interrupted blend continues from what is on screen
        m_animation.setDuration(spec.duration());
        m_animation.setEasingCurve(spec.easing());
        m_animation.start();
    }
    updateScheme(animate);
}

ColorScheme::Colors PlayerPalette::schemeColors() const
{
    // The background's hue, so the panels match the background and the text. Neutral
    // with them: a gray seed gets the monochrome scheme (Theme::computeColors). The
    // chroma matters to the variants that follow the source's (Content, Fidelity); it
    // stays above the monochrome cut.
    const Tint tint = this->tint();
    constexpr double kMinSeedChroma = 8.0;
    // The cover's own seed is a color in it (CoverPalette scores the quantized colors
    // as MD3 does; gray for a monochrome cover); the background's hue is a mean, which
    // washes a cover of a few strong colors out to a dull one.
    const QColor seed = m_coverSeed && m_palette.valid ? m_palette.seed
                        : tint.neutral
                            ? QColor(0x80, 0x80, 0x80)
                            : fromHct(tint.hue, std::max(tint.chroma, kMinSeedChroma), 50);
    return m_variant >= 0 ? m_theme->computeColors(seed, m_dark, Theme::Variant(m_variant))
                          : m_theme->computeColors(seed, m_dark);
}

void PlayerPalette::updateScheme(bool animate)
{
    if (!m_theme)
        return;
    const SpringSpec spec = Motion::spec(Motion::SlowEffects);
    m_scheme->transitionTo(
        schemeColors(), animate && !Motion::reducedMotion() ? spec.duration() : 0, spec.easing());
}

void PlayerPalette::blend(qreal progress)
{
    const double amount = std::clamp(progress, 0.0, 1.0);
    for (int i = 0; i < ColorCount; ++i) {
        const QRgb from = m_from[i].rgb();
        const QRgb to = m_to[i].rgb();
        m_current[i] = from == to ? m_to[i] : QColor::fromRgb(mcu::BlendCam16Ucs(from, to, amount));
    }
    emit colorsChanged();
}

} // namespace cirrus::ui
