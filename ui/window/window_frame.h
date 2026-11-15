#pragma once

#include <QObject>
#include <QPointer>
#include <QQuickWindow>
#include <QRegion>
#include <QtQml/qqmlregistration.h>

namespace cirrus::ui
{

// Platform side of the custom title bar (docs/ui/README.md §6.1). QML draws the
// frame; this tells the platform about it:
// - On Wayland the window keeps a transparent `margin` around the visible frame for
//   its shadow. That margin is excluded from the window geometry (snapping, tiling)
//   and from input, except a `resizeMargin` band just outside the frame.
// - Maximized, fullscreen and tiled windows get no margin: the frame is square and
//   fills the area the compositor gave it.
// - `showWindowMenu()` opens the compositor's window menu (right click on the title bar).
// Other platforms (X11, Windows) cannot rely on a compositor for translucency here,
// so their frame has no shadow margin. On Windows 11 the system rounds the corners
// (DWM corner preference; square while maximized or fullscreen); X11 stays square.
class WindowFrame : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QQuickWindow* window READ window WRITE setWindow NOTIFY windowChanged)
    // True while the window draws its own frame (custom title bar, not macOS).
    Q_PROPERTY(bool clientSide READ clientSide WRITE setClientSide NOTIFY clientSideChanged)
    Q_PROPERTY(int shadowMargin READ shadowMargin WRITE setShadowMargin NOTIFY shadowMarginChanged)
    Q_PROPERTY(int resizeMargin READ resizeMargin WRITE setResizeMargin NOTIFY resizeMarginChanged)
    // The platform can show a translucent shadow margin around the frame.
    Q_PROPERTY(bool translucent READ translucent CONSTANT)
    // The compositor tiled the window on at least one edge (Wayland only).
    Q_PROPERTY(bool tiled READ tiled NOTIFY tiledChanged)
    // Transparent margin in effect: shadowMargin, or 0 when not applicable.
    Q_PROPERTY(int margin READ margin NOTIFY marginChanged)

  public:
    explicit WindowFrame(QObject* parent = nullptr);

    [[nodiscard]] QQuickWindow* window() const { return m_window; }
    void setWindow(QQuickWindow* window);
    [[nodiscard]] bool clientSide() const { return m_clientSide; }
    void setClientSide(bool clientSide);
    [[nodiscard]] int shadowMargin() const { return m_shadowMargin; }
    void setShadowMargin(int margin);
    [[nodiscard]] int resizeMargin() const { return m_resizeMargin; }
    void setResizeMargin(int margin);
    [[nodiscard]] bool translucent() const { return m_translucent; }
    [[nodiscard]] bool tiled() const { return m_tiled; }
    [[nodiscard]] int margin() const { return m_margin; }

    // Opens the compositor's window menu at the pointer; false when unsupported.
    Q_INVOKABLE bool showWindowMenu();

  signals:
    void windowChanged();
    void clientSideChanged();
    void shadowMarginChanged();
    void resizeMarginChanged();
    void tiledChanged();
    void marginChanged();

  private:
    void update();
    void watchSurface();
    void updateCorners();
    [[nodiscard]] bool readTiled() const;

    QPointer<QQuickWindow> m_window;
    QList<QMetaObject::Connection> m_connections;
    bool m_clientSide = false;
    int m_shadowMargin = 24;
    int m_resizeMargin = 8;
    bool m_translucent = false;
    bool m_tiled = false;
    int m_margin = 0;
    int m_appliedMargin = 0;
    QRegion m_appliedMask;
    WId m_cornerWindow = 0; // the native window the corner preference was set on
    int m_appliedCorner = -1;
    bool m_watchingSurface = false;
};

} // namespace cirrus::ui
