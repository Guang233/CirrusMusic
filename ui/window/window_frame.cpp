#include "window_frame.h"

#include <QGuiApplication>

#ifdef CIRRUS_HAVE_WAYLAND
#include <QtWaylandClient/private/qwaylanddisplay_p.h>
#include <QtWaylandClient/private/qwaylandshellsurface_p.h>
#include <QtWaylandClient/private/qwaylandwindow_p.h>
#endif

namespace cirrus::ui
{

namespace
{
#ifdef CIRRUS_HAVE_WAYLAND
// Qt's Wayland platform window. Tiling states, custom margins and the window menu
// have no public Qt API, so this is the one place that uses QtWaylandClient privates.
QtWaylandClient::QWaylandWindow* waylandWindow(QWindow* window)
{
    if (!window || !window->handle() ||
        !QGuiApplication::platformName().startsWith(QLatin1String("wayland")))
        return nullptr;
    return static_cast<QtWaylandClient::QWaylandWindow*>(window->handle());
}
#endif
} // namespace

WindowFrame::WindowFrame(QObject* parent) : QObject(parent)
{
#ifdef CIRRUS_HAVE_WAYLAND
    m_translucent = QGuiApplication::platformName().startsWith(QLatin1String("wayland"));
#endif
}

void WindowFrame::setWindow(QQuickWindow* window)
{
    if (m_window == window)
        return;
    for (const auto& connection : std::as_const(m_connections))
        disconnect(connection);
    m_connections.clear();
    m_watchingSurface = false;
    m_appliedMargin = 0;
    m_appliedMask = {};
    m_window = window;
    if (window) {
        m_connections = {
            connect(window, &QWindow::widthChanged, this, &WindowFrame::update),
            connect(window, &QWindow::heightChanged, this, &WindowFrame::update),
            connect(window, &QWindow::windowStateChanged, this, &WindowFrame::update),
            connect(window, &QWindow::visibleChanged, this,
                    [this]() {
                        watchSurface();
                        update();
                    }),
        };
        watchSurface();
    }
    emit windowChanged();
    update();
}

void WindowFrame::setClientSide(bool clientSide)
{
    if (m_clientSide == clientSide)
        return;
    m_clientSide = clientSide;
    emit clientSideChanged();
    update();
}

void WindowFrame::setShadowMargin(int margin)
{
    if (m_shadowMargin == margin)
        return;
    m_shadowMargin = margin;
    emit shadowMarginChanged();
    update();
}

void WindowFrame::setResizeMargin(int margin)
{
    if (m_resizeMargin == margin)
        return;
    m_resizeMargin = margin;
    emit resizeMarginChanged();
    update();
}

bool WindowFrame::showWindowMenu()
{
#ifdef CIRRUS_HAVE_WAYLAND
    if (auto* window = waylandWindow(m_window)) {
        auto* shell = window->shellSurface();
        auto* seat = window->display()->lastInputDevice();
        if (shell && seat)
            return shell->showWindowMenu(seat);
    }
#endif
    return false;
}

// Tiling changes arrive with a configure that also resizes the window, so the size
// and state signals are enough to notice them; a new shell surface (shown again
// after a title bar switch) is watched as well.
void WindowFrame::watchSurface()
{
#ifdef CIRRUS_HAVE_WAYLAND
    if (m_watchingSurface)
        return;
    if (auto* window = waylandWindow(m_window)) {
        m_connections.append(connect(window, &QtWaylandClient::QWaylandWindow::surfaceRoleCreated,
                                     this, &WindowFrame::update));
        m_watchingSurface = true;
    }
#endif
}

bool WindowFrame::readTiled() const
{
#ifdef CIRRUS_HAVE_WAYLAND
    if (auto* window = waylandWindow(m_window))
        return window->toplevelWindowTilingStates() !=
               QtWaylandClient::QWaylandWindow::WindowNoState;
#endif
    return false;
}

void WindowFrame::update()
{
    const bool tiled = readTiled();
    if (tiled != m_tiled) {
        m_tiled = tiled;
        emit tiledChanged();
    }

    int margin = 0;
    if (m_window && m_clientSide && m_translucent && !m_tiled &&
        !(m_window->windowStates() & (Qt::WindowMaximized | Qt::WindowFullScreen)))
        margin = m_shadowMargin;
    if (margin != m_margin) {
        m_margin = margin;
        emit marginChanged();
    }
    if (!m_window)
        return;

#ifdef CIRRUS_HAVE_WAYLAND
    // The window geometry the compositor sees is the surface minus these margins.
    if (auto* window = waylandWindow(m_window); window && margin != m_appliedMargin) {
        window->setCustomMargins(QMargins(margin, margin, margin, margin));
        m_appliedMargin = margin;
    }
#endif

    // On Wayland the mask is the input region; the shadow outside the resize band
    // lets clicks through to whatever is below.
    QRegion mask;
    if (margin > 0) {
        const int inset = qMax(0, margin - m_resizeMargin);
        mask = QRegion(
            QRect(QPoint(), m_window->size()).marginsRemoved(QMargins(inset, inset, inset, inset)));
    }
    if (mask != m_appliedMask) {
        m_window->setMask(mask);
        m_appliedMask = mask;
    }
}

} // namespace cirrus::ui
