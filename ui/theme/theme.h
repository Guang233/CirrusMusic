#pragma once

#include "color_scheme.h"
#include "cover_palette.h"
#include "system_appearance.h"
#include "tokens.h"

#include <QColor>
#include <QObject>
#include <QThreadPool>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

namespace cirrus::ui
{

// Single source of design tokens for QML (`import Cirrus.Theme`, then `Theme.…`).
// Colors come from one seed: the system accent (default), the current cover
// or a fixed color. Cover decoding/quantization runs on a private worker thread; stale
// results are dropped, and the scheme blends to the new colors.
class Theme : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    // Mode and SeedSource share value names: use Theme.Mode.Dark, Theme.SeedSource.System.
    Q_CLASSINFO("RegisterEnumClassesUnscoped", "false")
    Q_PROPERTY(Mode mode READ mode WRITE setMode NOTIFY modeChanged)
    Q_PROPERTY(bool dark READ dark NOTIFY darkChanged)
    Q_PROPERTY(SeedSource seedSource READ seedSource WRITE setSeedSource NOTIFY seedSourceChanged)
    Q_PROPERTY(QColor fixedSeed READ fixedSeed WRITE setFixedSeed NOTIFY fixedSeedChanged)
    Q_PROPERTY(QColor seed READ seed NOTIFY seedChanged)
    // Seeds to offer for a fixed seed (the settings' swatches): eight hues around the
    // wheel, MD3's baseline purple first.
    Q_PROPERTY(QList<QColor> seedPresets READ seedPresets CONSTANT)
    // Material's static palette (md.ref.palette, not seeded): hue name to
    // {container, onContainer}, for colour-coded category icons. See docs/ui/theme.md.
    Q_PROPERTY(QVariantMap staticPalette READ staticPalette NOTIFY darkChanged)
    Q_PROPERTY(Variant variant READ variant WRITE setVariant NOTIFY variantChanged)
    Q_PROPERTY(
        SpecVersion specVersion READ specVersion WRITE setSpecVersion NOTIFY specVersionChanged)
    Q_PROPERTY(QColor systemAccent READ systemAccent NOTIFY systemAccentChanged)
    Q_PROPERTY(qreal contrast READ contrast WRITE setContrast NOTIFY contrastChanged)
    Q_PROPERTY(QUrl cover READ cover WRITE setCover NOTIFY coverChanged)
    Q_PROPERTY(cirrus::ui::CoverPalette coverPalette READ coverPalette NOTIFY coverPaletteChanged)
    Q_PROPERTY(bool extracting READ extracting NOTIFY extractingChanged)
    Q_PROPERTY(cirrus::ui::ColorScheme* color READ color CONSTANT)
    Q_PROPERTY(cirrus::ui::TypeScale* type READ type CONSTANT)
    Q_PROPERTY(cirrus::ui::Motion* motion READ motion CONSTANT)
    Q_PROPERTY(cirrus::ui::ShapeScale shape READ shape CONSTANT)
    Q_PROPERTY(cirrus::ui::StateLayers state READ state CONSTANT)
    Q_PROPERTY(cirrus::ui::ElevationScale elevation READ elevation CONSTANT)

  public:
    enum class Mode { System, Light, Dark };
    Q_ENUM(Mode)
    enum class SeedSource { Cover, Fixed, System };
    Q_ENUM(SeedSource)
    // MD3 scheme variants (see docs/ui/theme.md).
    enum class Variant {
        TonalSpot,
        Expressive,
        Vibrant,
        Content,
        Fidelity,
        Neutral,
        Monochrome,
        Rainbow,
        FruitSalad
    };
    Q_ENUM(Variant)
    // MD3 color spec. 2025 (MD3 Expressive) applies to Neutral, TonalSpot,
    // Expressive and Vibrant; other variants always use 2021.
    enum class SpecVersion { Spec2021, Spec2025 };
    Q_ENUM(SpecVersion)

    explicit Theme(QObject* parent = nullptr);
    ~Theme() override;

    [[nodiscard]] Mode mode() const { return m_mode; }
    void setMode(Mode mode);
    [[nodiscard]] bool dark() const { return m_dark; }
    [[nodiscard]] SeedSource seedSource() const { return m_seedSource; }
    void setSeedSource(SeedSource source);
    [[nodiscard]] QColor fixedSeed() const { return m_fixedSeed; }
    [[nodiscard]] static QList<QColor> seedPresets();
    [[nodiscard]] QVariantMap staticPalette() const;
    void setFixedSeed(const QColor& seed);
    [[nodiscard]] QColor seed() const { return m_seed; }
    [[nodiscard]] Variant variant() const { return m_variant; }
    void setVariant(Variant variant);
    [[nodiscard]] SpecVersion specVersion() const { return m_specVersion; }
    void setSpecVersion(SpecVersion version);
    // Platform accent (invalid when unavailable); the seed for SeedSource.System.
    [[nodiscard]] QColor systemAccent() const { return m_system->accent(); }
    [[nodiscard]] qreal contrast() const { return m_contrast; }
    void setContrast(qreal contrast);
    [[nodiscard]] QUrl cover() const { return m_cover; }
    void setCover(const QUrl& cover);
    [[nodiscard]] CoverPalette coverPalette() const { return m_palette; }
    [[nodiscard]] bool extracting() const { return m_pendingGeneration != 0; }

    [[nodiscard]] ColorScheme* color() const { return m_color; }
    [[nodiscard]] TypeScale* type() const { return m_type; }
    [[nodiscard]] Motion* motion() const { return m_motion; }
    [[nodiscard]] ShapeScale shape() const { return {}; }
    [[nodiscard]] StateLayers state() const { return {}; }
    [[nodiscard]] ElevationScale elevation() const { return {}; }

    // WCAG contrast ratio (1-21) between two colors.
    Q_INVOKABLE qreal contrastRatio(const QColor& a, const QColor& b) const;
    // Linear blend in premultiplied RGBA: t = 0 gives a, 1 gives b. Controls drive
    // `t` with a Spring so state changes animate while theme colors stay live.
    Q_INVOKABLE QColor mix(const QColor& a, const QColor& b, qreal t) const;
    // Full MD3 scheme for an arbitrary seed without touching the live theme,
    // e.g. the dark scheme the player derives from its cover. Keys are role names.
    Q_INVOKABLE QVariantMap schemeFor(const QColor& seed, bool dark) const;

  signals:
    void modeChanged();
    void darkChanged();
    void seedSourceChanged();
    void fixedSeedChanged();
    void seedChanged();
    void variantChanged();
    void specVersionChanged();
    void systemAccentChanged();
    void contrastChanged();
    void coverChanged();
    void coverPaletteChanged();
    void extractingChanged();

  private:
    void updateDark();
    void updateScheme(bool animate = true);
    void applyPalette(quint64 generation, const CoverPalette& palette);
    [[nodiscard]] QColor resolveSeed() const;
    [[nodiscard]] ColorScheme::Colors computeColors(const QColor& seed, bool dark) const;

    Mode m_mode = Mode::System;
    bool m_dark = false;
    SeedSource m_seedSource = SeedSource::System;
    QColor m_fixedSeed{0x67, 0x50, 0xa4}; // MD3 baseline seed
    QColor m_seed;
    Variant m_variant = Variant::TonalSpot;
    SpecVersion m_specVersion = SpecVersion::Spec2025;
    qreal m_contrast = 0.0;
    QUrl m_cover;
    CoverPalette m_palette;
    quint64 m_generation = 0;
    quint64 m_pendingGeneration = 0;
    ColorScheme* m_color;
    TypeScale* m_type;
    Motion* m_motion;
    SystemAppearance* m_system;
    QThreadPool m_worker;
};

} // namespace cirrus::ui
