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
//  solid           the "solid" background: the main cover color at tone 20 / 90
//  content         text and icons: the background's mean hue at a low chroma, tone
//                  85 / 18; contentVariant is the secondary level (tone 68 / 40)
//  shadow          the cover's shadow: its darkest color, made darker
//  glare           the light sliding over the tilted cover: near white in the content
//                  hue, in both modes
//  scheme          MD3 roles for the player's menus, dialogs and panels
//                  (ColorScope.scheme), from the cover's seed
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
    void colorsChanged();

  private:
    static constexpr int kGridCells = CoverPalette::kGridSize * CoverPalette::kGridSize;
    // Indices after the grid in the color lists.
    enum Extra { Solid = kGridCells, Content, ContentVariant, Shadow, Glare, ColorCount };

    [[nodiscard]] QList<QColor> computeColors() const;
    void update(bool animate);
    void updateScheme(bool animate);
    void blend(qreal progress);

    CoverPalette m_palette;
    bool m_dark = true;
    bool m_complete = false;
    QPointer<Theme> m_theme;
    ColorScheme* m_scheme;
    QList<QColor> m_from;
    QList<QColor> m_to;
    QList<QColor> m_current;
    QVariantAnimation m_animation;
};

} // namespace cirrus::ui
