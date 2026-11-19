#pragma once

#include "color_scheme.h"
#include "cover_palette.h"

#include <QList>
#include <QObject>
#include <QPointer>
#include <QQmlParserStatus>
#include <QVariantAnimation>
#include <QtQml/qqmlregistration.h>

namespace cirrus::ui
{

class Theme;

// The player page's colors (docs/ui/pages/player.md §3), all from one cover palette:
//  grid            the cover's 5 x 5 colors with their tones mapped into the background
//                  range (dark 15-35, light 82-94 at 70% chroma), row-major
//  solid           the "solid" background: the background's hue (below) at tone 20 / 80
//  content         text and icons: the background's hue at a low chroma, tone 85 / 15;
//                  contentVariant is the secondary level (tone 68 / 32); with
//                  `coverSeed`, the scheme's OnPrimaryContainer (and 72% of it over
//                  PrimaryContainer)
//  shadow          the cover's shadow: its darkest color, made darker
//  glare           the light sliding over the tilted cover: near white in the content
//                  hue, in both modes
//  scheme          MD3 roles for the player's menus, dialogs and panels
//                  (ColorScope.scheme), seeded with the background's hue; with
//                  `coverSeed`, with the cover's own seed instead (a color that is in
//                  the cover, scored as MD3 does: the Material style, the mini and side
//                  players), in `variant` (a Theme.Variant; -1 follows the theme's)
// The background's hue is the cover grid's chroma-weighted mean hue: one hue for the
// background, the text and the controls on it (player.md §3.1). A cover with little
// color (black and white, or a low mean chroma) makes all three neutral.
// A new palette or a light/dark switch blends everything together (slowEffects;
// immediate with reduced motion).
//   PlayerPalette { id: colors; palette: Theme.coverPalette; dark: Theme.dark }
class PlayerPalette : public QObject, public QQmlParserStatus
{
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)
    QML_ELEMENT
    Q_PROPERTY(cirrus::ui::CoverPalette palette READ palette WRITE setPalette NOTIFY paletteChanged)
    Q_PROPERTY(bool dark READ dark WRITE setDark NOTIFY darkChanged)
    Q_PROPERTY(QList<QColor> grid READ grid NOTIFY colorsChanged)
    Q_PROPERTY(QColor solid READ solid NOTIFY colorsChanged)
    Q_PROPERTY(QColor content READ content NOTIFY colorsChanged)
    Q_PROPERTY(QColor contentVariant READ contentVariant NOTIFY colorsChanged)
    Q_PROPERTY(QColor shadow READ shadow NOTIFY colorsChanged)
    Q_PROPERTY(QColor glare READ glare NOTIFY colorsChanged)
    Q_PROPERTY(cirrus::ui::ColorScheme* scheme READ scheme CONSTANT)
    Q_PROPERTY(bool coverSeed READ coverSeed WRITE setCoverSeed NOTIFY coverSeedChanged)
    Q_PROPERTY(int variant READ variant WRITE setVariant NOTIFY variantChanged)
    // The background's tone range and chroma factor (§3.1), for backgrounds mapped in a
    // shader (the blurred cover).
    Q_PROPERTY(double toneLow READ toneLow NOTIFY darkChanged)
    Q_PROPERTY(double toneHigh READ toneHigh NOTIFY darkChanged)
    Q_PROPERTY(double chromaFactor READ chromaFactor NOTIFY darkChanged)

  public:
    explicit PlayerPalette(QObject* parent = nullptr);

    [[nodiscard]] CoverPalette palette() const { return m_palette; }
    void setPalette(const CoverPalette& palette);
    [[nodiscard]] bool dark() const { return m_dark; }
    void setDark(bool dark);
    [[nodiscard]] bool coverSeed() const { return m_coverSeed; }
    void setCoverSeed(bool coverSeed);
    [[nodiscard]] int variant() const { return m_variant; }
    void setVariant(int variant);

    [[nodiscard]] QList<QColor> grid() const { return m_current.mid(0, kGridCells); }
    [[nodiscard]] QColor solid() const { return m_current.value(Solid); }
    [[nodiscard]] QColor content() const { return m_current.value(Content); }
    [[nodiscard]] QColor contentVariant() const { return m_current.value(ContentVariant); }
    [[nodiscard]] QColor shadow() const { return m_current.value(Shadow); }
    [[nodiscard]] QColor glare() const { return m_current.value(Glare); }
    [[nodiscard]] ColorScheme* scheme() const { return m_scheme; }
    [[nodiscard]] double toneLow() const;
    [[nodiscard]] double toneHigh() const;
    [[nodiscard]] double chromaFactor() const;

    void classBegin() override {}
    void componentComplete() override;

  signals:
    void paletteChanged();
    void darkChanged();
    void coverSeedChanged();
    void variantChanged();
    void colorsChanged();

  private:
    static constexpr int kGridCells = CoverPalette::kGridSize * CoverPalette::kGridSize;
    // Indices after the grid in the color lists.
    enum Extra { Solid = kGridCells, Content, ContentVariant, Shadow, Glare, ColorCount };

    // The background's hue, how colorful it is overall (the chroma-weighted mean as a
    // vector: hues that cancel out make it small), and whether everything goes neutral.
    struct Tint {
        double hue = 0;
        double chroma = 0;
        bool neutral = true;
    };
    [[nodiscard]] Tint tint() const;
    [[nodiscard]] QList<QColor> computeColors() const;
    void update(bool animate);
    [[nodiscard]] ColorScheme::Colors schemeColors() const;
    void updateScheme(bool animate);
    void blend(qreal progress);

    CoverPalette m_palette;
    bool m_dark = true;
    bool m_coverSeed = false;
    int m_variant = -1;
    bool m_complete = false;
    QPointer<Theme> m_theme;
    ColorScheme* m_scheme;
    QList<QColor> m_from;
    QList<QColor> m_to;
    QList<QColor> m_current;
    QVariantAnimation m_animation;
};

} // namespace cirrus::ui
