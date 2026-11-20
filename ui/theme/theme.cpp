#include "theme.h"
#include "logging/log.h"

#include "fonts.h"

#include "cirrus/color_spec.h"
#include "cpp/cam/hct.h"
#include "cpp/contrast/contrast.h"
#include "cpp/utils/utils.h"

#include <QEvent>
#include <QGuiApplication>
#include <QPalette>
#include <QQmlEngine>
#include <QQuickImageProvider>
#include <QStyleHints>
#include <QtQml/qqmlfile.h>

#include <algorithm>
#include <cmath>

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
    : QObject(parent), m_color(new ColorScheme(this)), m_type(new TypeScale(this)),
      m_motion(new Motion(this)), m_system(new SystemAppearance(this))
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
    if (m_response) {
        disconnect(m_response, nullptr, this, nullptr);
        m_response->cancel();
        m_response->deleteLater();
    }
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

QList<QColor> Theme::seedPresets()
{
    return {QColor(0x6750a4), QColor(0x3f6fd8), QColor(0x00838f), QColor(0x3f8f4a),
            QColor(0xb08a00), QColor(0xe0672e), QColor(0xc8232c), QColor(0xc2185b)};
}

QVariantMap Theme::staticPalette() const
{
    // Tones 90 / 80 (light / dark container) and 30 (on container) of Material's
    // static hues, as the Android 16 Settings homepage uses them.
    struct Hue {
        const char* name;
        QRgb tone90, tone80, tone30;
    };
    static constexpr Hue hues[] = {
        {"blue", 0xd0e4ff, 0xa1c9ff, 0x04409f},   {"blueVariant", 0xbde9ff, 0x67d4ff, 0x004d68},
        {"cyan", 0xacedff, 0x60d5f3, 0x004e5d},   {"green", 0xbeefbb, 0x80da88, 0x00522c},
        {"yellow", 0xffe07c, 0xfcbd00, 0x6d3a01}, {"orange", 0xffdcc3, 0xffb683, 0x753403},
        {"red", 0xffdadc, 0xffb3ae, 0x8a1a16},    {"pink", 0xffd8ef, 0xffaee4, 0x8d0053},
        {"purple", 0xeedcfe, 0xd9bafd, 0x5629a4}, {"grey", 0xe3e3e3, 0xc7c7c7, 0x474747},
    };
    QVariantMap palette;
    for (const Hue& hue : hues) {
        palette.insert(QString::fromLatin1(hue.name),
                       QVariantMap{{QStringLiteral("container"),
                                    QColor::fromRgb(m_dark ? hue.tone80 : hue.tone90)},
                                   {QStringLiteral("onContainer"), QColor::fromRgb(hue.tone30)}});
    }
    return palette;
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
    if (m_response) {
        m_response->cancel();
        m_response = nullptr;
    }
    if (cover.scheme() == QLatin1String("image")) {
        m_pendingGeneration = generation;
        if (!wasExtracting)
            emit extractingChanged();
        m_worker.clear();
        requestProviderImage(generation, cover);
        return;
    }
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

void Theme::requestProviderImage(quint64 generation, const QUrl& url)
{
    // image://<provider>/<id>, decoded at the quantization size by the same provider
    // (and cache) the Image items use.
    QQmlEngine* engine = qmlEngine(this);
    auto* provider =
        engine ? dynamic_cast<QQuickAsyncImageProvider*>(engine->imageProvider(url.host()))
               : nullptr;
    if (!provider) {
        log::warn(lcUiTheme, "cover.no_provider", {"provider", url.host()});
        applyPalette(generation, {});
        return;
    }
    constexpr int kEdge = 128;
    QQuickImageResponse* response =
        provider->requestImageResponse(url.path(QUrl::FullyEncoded).mid(1), QSize(kEdge, kEdge));
    m_response = response;
    connect(response, &QQuickImageResponse::finished, this, [this, response, generation] {
        if (m_response == response)
            m_response = nullptr;
        QImage image;
        if (response->errorString().isEmpty()) {
            if (QQuickTextureFactory* factory = response->textureFactory()) {
                image = factory->image();
                delete factory;
            }
        }
        response->deleteLater();
        if (generation != m_generation)
            return;
        m_worker.start([this, generation, image] {
            CoverPalette palette = extractCoverPalette(image);
            QMetaObject::invokeMethod(
                this, [this, generation, palette] { applyPalette(generation, palette); },
                Qt::QueuedConnection);
        });
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
    return computeColors(seed, dark, m_variant);
}

ColorScheme::Colors Theme::computeColors(const QColor& seed, bool dark, Variant wanted) const
{
    const mcu::Hct source(seed.rgba() | 0xff000000u);
    // A near-gray seed has no meaningful hue; tinted variants would invent one
    // (a gray cover turned cyan), so such seeds always use the monochrome variant.
    constexpr double kAchromaticChroma = 5.0;
    const Variant variant = source.get_chroma() < kAchromaticChroma ? Variant::Monochrome : wanted;
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

QColor Theme::mix(const QColor& a, const QColor& b, qreal t) const
{
    t = std::clamp(t, 0.0, 1.0);
    const float aa = a.alphaF();
    const float ba = b.alphaF();
    const float alpha = aa + (ba - aa) * static_cast<float>(t);
    if (alpha <= 0.0f)
        return Qt::transparent;
    const auto channel = [&](float ca, float cb) {
        return std::clamp((ca * aa + (cb * ba - ca * aa) * static_cast<float>(t)) / alpha, 0.0f,
                          1.0f);
    };
    return QColor::fromRgbF(channel(a.redF(), b.redF()), channel(a.greenF(), b.greenF()),
                            channel(a.blueF(), b.blueF()), alpha);
}

QColor Theme::hct(qreal hue, qreal chroma, qreal tone) const
{
    const mcu::Hct value(std::fmod(std::fmod(hue, 360.0) + 360.0, 360.0), std::max(0.0, chroma),
                         std::clamp(tone, 0.0, 100.0));
    return QColor::fromRgb(value.ToInt() | 0xff000000u);
}

QVariantMap Theme::hctOf(const QColor& color) const
{
    const mcu::Hct value(color.rgba() | 0xff000000u);
    return {{QStringLiteral("hue"), value.get_hue()},
            {QStringLiteral("chroma"), value.get_chroma()},
            {QStringLiteral("tone"), value.get_tone()}};
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
