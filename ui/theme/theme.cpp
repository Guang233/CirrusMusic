#include "theme.h"

#include "fonts.h"

#include "cpp/cam/hct.h"
#include "cpp/contrast/contrast.h"
#include "cpp/scheme/scheme_content.h"
#include "cpp/scheme/scheme_expressive.h"
#include "cpp/scheme/scheme_fidelity.h"
#include "cpp/scheme/scheme_fruit_salad.h"
#include "cpp/scheme/scheme_monochrome.h"
#include "cpp/scheme/scheme_neutral.h"
#include "cpp/scheme/scheme_rainbow.h"
#include "cpp/scheme/scheme_tonal_spot.h"
#include "cpp/scheme/scheme_vibrant.h"
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
mcu::DynamicScheme makeScheme(Theme::Variant variant, const mcu::Hct& source, bool dark,
                              double contrast)
{
    switch (variant) {
    case Theme::Variant::TonalSpot:
        return mcu::SchemeTonalSpot(source, dark, contrast);
    case Theme::Variant::Expressive:
        return mcu::SchemeExpressive(source, dark, contrast);
    case Theme::Variant::Vibrant:
        return mcu::SchemeVibrant(source, dark, contrast);
    case Theme::Variant::Content:
        return mcu::SchemeContent(source, dark, contrast);
    case Theme::Variant::Fidelity:
        return mcu::SchemeFidelity(source, dark, contrast);
    case Theme::Variant::Neutral:
        return mcu::SchemeNeutral(source, dark, contrast);
    case Theme::Variant::Monochrome:
        return mcu::SchemeMonochrome(source, dark, contrast);
    case Theme::Variant::Rainbow:
        return mcu::SchemeRainbow(source, dark, contrast);
    case Theme::Variant::FruitSalad:
        return mcu::SchemeFruitSalad(source, dark, contrast);
    }
    return mcu::SchemeTonalSpot(source, dark, contrast);
}
} // namespace

Theme::Theme(QObject* parent)
    : QObject(parent), m_color(new ColorScheme(this)),
      m_type(new TypeScale(textFontFamily(), this)), m_motion(new Motion(this))
{
    m_worker.setMaxThreadCount(1);
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this,
            &Theme::updateDark);
    qApp->installEventFilter(this); // system accent changes arrive as palette changes
    updateDark();
    updateScheme(false);
}

Theme::~Theme()
{
    // Workers post results to this object; finish them before it goes away.
    m_worker.clear();
    m_worker.waitForDone();
}

bool Theme::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == qApp && event->type() == QEvent::ApplicationPaletteChange &&
        m_seedSource == SeedSource::System)
        updateScheme();
    return QObject::eventFilter(watched, event);
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
    const bool dark = m_mode == Mode::System
                          ? QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark
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
    case SeedSource::System: {
        const QColor accent = QGuiApplication::palette().color(QPalette::Accent);
        return accent.isValid() ? accent : m_fixedSeed;
    }
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
    const mcu::DynamicScheme scheme = makeScheme(variant, source, dark, m_contrast);
    ColorScheme::Colors colors;
#define CIRRUS_ROLE_COLOR(name, Name)                                                              \
    colors[ColorScheme::Name] = QColor::fromRgba(scheme.Get##Name());
    CIRRUS_COLOR_ROLES(CIRRUS_ROLE_COLOR)
#undef CIRRUS_ROLE_COLOR
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
