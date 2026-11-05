#pragma once

#include "color_scheme.h"

#include <QPointer>
#include <QtQml/qqmlregistration.h>
#include <QtQuickControls2/qquickattachedpropertypropagator.h>

namespace cirrus::ui
{

// The color roles an item and everything inside it use: `ColorScope.scheme.primary`.
// Unset, it is the live theme (Theme.color); setting it on an item hands another scheme
// to its children, popups and windows (like Material.theme), e.g. the player gives its
// menus and panels the scheme derived from the cover:
//   PlayerPage { ColorScope.scheme: playerPalette.scheme }
// Controls (Cirrus.Controls) read ColorScope.scheme; shell pages keep Theme.color.
class ColorScope : public QQuickAttachedPropertyPropagator
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("ColorScope is an attached property")
    QML_ATTACHED(ColorScope)
    Q_PROPERTY(cirrus::ui::ColorScheme* scheme READ scheme WRITE setScheme RESET resetScheme NOTIFY
                   schemeChanged)

  public:
    explicit ColorScope(QObject* parent = nullptr);

    static ColorScope* qmlAttachedProperties(QObject* object);

    [[nodiscard]] ColorScheme* scheme() const;
    void setScheme(ColorScheme* scheme);
    void resetScheme();

  signals:
    void schemeChanged();

  protected:
    void attachedParentChange(QQuickAttachedPropertyPropagator* newParent,
                              QQuickAttachedPropertyPropagator* oldParent) override;

  private:
    void inherit(ColorScheme* scheme);
    void propagate();

    bool m_explicit = false;
    // Explicit or inherited; null means the theme's (looked up lazily).
    QPointer<ColorScheme> m_scheme;
    mutable QPointer<ColorScheme> m_theme;
};

} // namespace cirrus::ui
