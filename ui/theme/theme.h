#pragma once

#include "color_scheme.h"
#include "cover_palette.h"
#include "tokens.h"

#include <QColor>
#include <QObject>
#include <QThreadPool>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

namespace cirrus::ui
{

// Single source of design tokens for QML (`import Cirrus.Theme`, then `Theme.…`).
// Colors come from one seed: the current cover, a fixed color or the system
// accent. Cover decoding/quantization runs on a private worker thread; stale
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
    Q_PROPERTY(Variant variant READ variant WRITE setVariant NOTIFY variantChanged)
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
    // material-color-utilities scheme variants (2021 spec, see docs/ui/theme.md).
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

    explicit Theme(QObject* parent = nullptr);
    ~Theme() override;

    [[nodiscard]] Mode mode() const { return m_mode; }
    void setMode(Mode mode);
    [[nodiscard]] bool dark() const { return m_dark; }
    [[nodiscard]] SeedSource seedSource() const { return m_seedSource; }
    void setSeedSource(SeedSource source);
    [[nodiscard]] QColor fixedSeed() const { return m_fixedSeed; }
    void setFixedSeed(const QColor& seed);
    [[nodiscard]] QColor seed() const { return m_seed; }
    [[nodiscard]] Variant variant() const { return m_variant; }
    void setVariant(Variant variant);
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
    void contrastChanged();
    void coverChanged();
    void coverPaletteChanged();
    void extractingChanged();

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    void updateDark();
    void updateScheme(bool animate = true);
    void applyPalette(quint64 generation, const CoverPalette& palette);
    [[nodiscard]] QColor resolveSeed() const;
    [[nodiscard]] ColorScheme::Colors computeColors(const QColor& seed, bool dark) const;

    Mode m_mode = Mode::System;
    bool m_dark = false;
    SeedSource m_seedSource = SeedSource::Cover;
    QColor m_fixedSeed{0x67, 0x50, 0xa4}; // MD3 baseline seed
    QColor m_seed;
    Variant m_variant = Variant::TonalSpot;
    qreal m_contrast = 0.0;
    QUrl m_cover;
    CoverPalette m_palette;
    quint64 m_generation = 0;
    quint64 m_pendingGeneration = 0;
    ColorScheme* m_color;
    TypeScale* m_type;
    Motion* m_motion;
    QThreadPool m_worker;
};

} // namespace cirrus::ui
