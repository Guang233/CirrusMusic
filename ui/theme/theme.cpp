#include "theme.h"

#include "fonts.h"

#include "cirrus/color_spec.h"
#include "cpp/cam/hct.h"
#include "cpp/contrast/contrast.h"
#include "cpp/utils/utils.h"

#include <QEvent>
#include <QGuiApplication>
#include <QPalette>
#include <QStyleHints>
#include <QtQml/qqmlfile.h>

namespace cirrus::ui
{
namespace mcu = material_color_utilities;

namespace
{
mcu::ts::Variant toMcu(Theme::Variant variant)
{
    using V = mcu::ts::Variant;
    switch (variant) {
    case Theme::Variant::TonalSpot:
        return V::kTonalSpot;
    case Theme::Variant::Expressive:
        return V::kExpressive;
    case Theme::Variant::Vibrant:
        return V::kVibrant;
    case Theme::Variant::Content:
        return V::kContent;
    case Theme::Variant::Fidelity:
        return V::kFidelity;
    case Theme::Variant::Neutral:
        return V::kNeutral;
    case Theme::Variant::Monochrome:
        return V::kMonochrome;
    case Theme::Variant::Rainbow:
        return V::kRainbow;
    case Theme::Variant::FruitSalad:
        return V::kFruitSalad;
    }
    return V::kTonalSpot;
}
} // namespace

Theme::Theme(QObject* parent)
    : QObject(parent), m_color(new ColorScheme(this)),
      m_type(new TypeScale(textFontFamily(), this)), m_motion(new Motion(this)),
      m_system(new SystemAppearance(this))
{
    m_worker.setMaxThreadCount(1);
    connect(m_system, &SystemAppearance::colorSchemeChanged, this, &Theme::updateDark);
    connect(m_system, &SystemAppearance::accentChanged, this, [this] {
        emit systemAccentChanged();
        if (m_seedSource == SeedSource::System)
            updateScheme();
    });
    updateDark();
    updateScheme(false);
}

Theme::~Theme()
{
    // Workers post results to this object; finish them before it goes away.
    m_worker.clear();
    m_worker.waitForDone();
}

void Theme::setMode(Mode mode)
{
    if (mode == m_mode)
        return;
    m_mode = mode;
    emit modeChanged();
    updateDark();
}

void Theme::updateDark()
{
    const bool dark = m_mode == Mode::System ? m_system->colorScheme() == Qt::ColorScheme::Dark
                                             : m_mode == Mode::Dark;
    if (dark == m_dark)
        return;
    m_dark = dark;
    emit darkChanged();
    updateScheme();
}

void Theme::setSeedSource(SeedSource source)
{
    if (source == m_seedSource)
        return;
    m_seedSource = source;
    emit seedSourceChanged();
    updateScheme();
}

void Theme::setFixedSeed(const QColor& seed)
{
    if (seed == m_fixedSeed || !seed.isValid())
        return;
    m_fixedSeed = seed;
    emit fixedSeedChanged();
    updateScheme();
}

void Theme::setSpecVersion(SpecVersion version)
{
    if (version == m_specVersion)
        return;
    m_specVersion = version;
    emit specVersionChanged();
    updateScheme();
}

void Theme::setVariant(Variant variant)
{
    if (variant == m_variant)
        return;
    m_variant = variant;
    emit variantChanged();
    updateScheme();
}

void Theme::setContrast(qreal contrast)
{
    contrast = std::clamp(contrast, -1.0, 1.0);
    if (qFuzzyCompare(1.0 + contrast, 1.0 + m_contrast))
        return;
    m_contrast = contrast;
    emit contrastChanged();
    updateScheme();
}

void Theme::setCover(const QUrl& cover)
{
    if (cover == m_cover)
        return;
    m_cover = cover;
    emit coverChanged();

    const quint64 generation = ++m_generation;
    const bool wasExtracting = extracting();
    const QString path = QQmlFile::urlToLocalFileOrQrc(cover);
    if (path.isEmpty()) {
        m_pendingGeneration = 0;
        if (wasExtracting)
            emit extractingChanged();
        applyPalette(generation, {});
        return;
    }
    m_pendingGeneration = generation;
    if (!wasExtracting)
        emit extractingChanged();
    m_worker.clear(); // drop queued, not yet started extractions
    m_worker.start([this, generation, path] {
        CoverPalette palette = extractCoverPalette(path);
        QMetaObject::invokeMethod(
            this, [this, generation, palette] { applyPalette(generation, palette); },
            Qt::QueuedConnection);
    });
}

void Theme::applyPalette(quint64 generation, const CoverPalette& palette)
{
    if (generation != m_generation)
        return; // a newer cover was requested meanwhile
    if (m_pendingGeneration == generation) {
        m_pendingGeneration = 0;
        emit extractingChanged();
    }
    m_palette = palette;
    emit coverPaletteChanged();
    if (m_seedSource == SeedSource::Cover)
        updateScheme();
}

QColor Theme::resolveSeed() const
{
    switch (m_seedSource) {
    case SeedSource::Cover:
        return m_palette.valid ? m_palette.seed : m_fixedSeed;
    case SeedSource::System:
        return m_system->accent().isValid() ? m_system->accent() : m_fixedSeed;
    case SeedSource::Fixed:
        break;
    }
    return m_fixedSeed;
}

ColorScheme::Colors Theme::computeColors(const QColor& seed, bool dark) const
{
    const mcu::Hct source(seed.rgba() | 0xff000000u);
    // A near-gray seed has no meaningful hue; tinted variants would invent one
    // (a gray cover turned cyan), so such seeds always use the monochrome variant.
    constexpr double kAchromaticChroma = 5.0;
    const Variant variant =
        source.get_chroma() < kAchromaticChroma ? Variant::Monochrome : m_variant;
    const mcu::ts::DynamicScheme scheme(source, toMcu(variant), dark, m_contrast,
                                        m_specVersion == SpecVersion::Spec2025
                                            ? mcu::ts::SpecVersion::k2025
                                            : mcu::ts::SpecVersion::k2021);
    const mcu::ts::ColorSpec2021& spec = mcu::ts::SpecFor(scheme);
    ColorScheme::Colors colors;
#define CIRRUS_ROLE_COLOR(name, Name)                                                              \
    colors[ColorScheme::Name] = QColor::fromRgba(spec.name().GetArgb(scheme));
    CIRRUS_COLOR_ROLES(CIRRUS_ROLE_COLOR)
#undef CIRRUS_ROLE_COLOR
#define CIRRUS_DIM_ROLE_COLOR(name, Name, base)                                                    \
    colors[ColorScheme::Name] =                                                                    \
        QColor::fromRgba((spec.name() ? *spec.name() : spec.base()).GetArgb(scheme));
    CIRRUS_COLOR_DIM_ROLES(CIRRUS_DIM_ROLE_COLOR)
#undef CIRRUS_DIM_ROLE_COLOR
    return colors;
}

void Theme::updateScheme(bool animate)
{
    const QColor seed = resolveSeed();
    if (seed != m_seed) {
        m_seed = seed;
        emit seedChanged();
    }
    const SpringSpec spec = Motion::spec(Motion::DefaultEffects);
    m_color->transitionTo(computeColors(seed, m_dark),
                          animate && !Motion::reducedMotion() ? spec.duration() : 0, spec.easing());
}

qreal Theme::contrastRatio(const QColor& a, const QColor& b) const
{
    return mcu::RatioOfTones(mcu::LstarFromArgb(a.rgba() | 0xff000000u),
                             mcu::LstarFromArgb(b.rgba() | 0xff000000u));
}

QVariantMap Theme::schemeFor(const QColor& seed, bool dark) const
{
    const ColorScheme::Colors colors = computeColors(seed.isValid() ? seed : m_fixedSeed, dark);
    const QStringList names = ColorScheme::roles();
    QVariantMap result;
    for (int i = 0; i < ColorScheme::RoleCount; ++i)
        result.insert(names[i], colors[i]);
    return result;
}

} // namespace cirrus::ui
