pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Effects
import Cirrus.Theme

// Top-level window with the Cirrus frame (docs/ui/README.md §6.1). `titleBar`:
// - Custom: draws the frame itself (shadow and rounded corners on Wayland, resize
//   bands, window buttons); macOS keeps the traffic lights over an expanded client area.
// - System: native decorations.
// - Hidden: no decorations and no title bar controls (tiling window managers).
// The title bar row stays in every mode for the app's own content (`titleContent`);
// default children fill the area below it.
//   AppWindow { titleContent: Text { … }  Page { anchors.fill: parent } }
Window {
    id: window

    property int titleBar: TitleBarMode.Custom
    default property alias content: body.data
    property alias titleContent: titleBarItem.content
    property alias frame: windowFrame

    readonly property bool macOS: Qt.platform.os === "osx"
    readonly property bool customFrame: titleBar === TitleBarMode.Custom && !macOS
    readonly property bool maximized: visibility === Window.Maximized
                                      || visibility === Window.FullScreen
    // Children reaching the window's bottom corners round theirs with this.
    readonly property real cornerRadius: windowFrame.margin > 0 ? Theme.shape.medium : 0

    // Changes the title bar mode by recreating the native window (hide, change flags,
    // show). Whether this works everywhere decides if the setting needs a restart.
    function setTitleBar(mode: int) {
        if (mode === titleBar)
            return;
        const shown = visible;
        visible = false;
        titleBar = mode;
        visible = shown;
    }

    minimumWidth: 480 + 2 * windowFrame.margin
    minimumHeight: 360 + 2 * windowFrame.margin
    flags: titleBar === TitleBarMode.System ? Qt.Window
         : titleBar === TitleBarMode.Custom && macOS
           ? Qt.Window | Qt.ExpandedClientAreaHint | Qt.NoTitleBarBackgroundHint
         : Qt.Window | Qt.FramelessWindowHint
    // Always translucent: the alpha channel is chosen when the native window is
    // created, and the shadow margin needs it. `surface` paints the opaque part.
    color: "transparent"

    WindowFrame {
        id: windowFrame
        window: window
        clientSide: window.customFrame
    }

    RectangularShadow {
        anchors.fill: surface
        visible: windowFrame.margin > 0
        radius: window.cornerRadius
        offset.y: window.active ? 4 : 2
        blur: window.active ? 20 : 12
        color: Qt.alpha(Theme.color.shadow, window.active ? 0.4 : 0.24)
    }

    // The visible window. Children must keep clear of the rounded corners (only the
    // surface color is painted there) or round their own with `cornerRadius`.
    Rectangle {
        id: surface

        anchors.fill: parent
        anchors.margins: windowFrame.margin
        radius: window.cornerRadius
        color: Theme.color.surface

        TitleBar {
            id: titleBarItem

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            frame: windowFrame
            windowControls: window.titleBar === TitleBarMode.Custom
            windowButtons: window.customFrame
            leadingInset: window.titleBar === TitleBarMode.Custom && window.macOS ? 72 : 16
        }

        Item {
            id: body

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: titleBarItem.bottom
            anchors.bottom: parent.bottom
        }
    }

    // Hairline edge so the frame stays visible on dark backgrounds, where the
    // shadow is weak. Above the content, which never paints there anyway.
    Rectangle {
        anchors.fill: surface
        visible: windowFrame.margin > 0
        radius: window.cornerRadius
        color: "transparent"
        border.width: 1
        border.color: Qt.alpha(Theme.color.outlineVariant, 0.6)
    }

    // Frameless modes; inside the window only unless there is a shadow margin.
    ResizeHandles {
        anchors.fill: surface
        visible: (window.customFrame || window.titleBar === TitleBarMode.Hidden)
                 && !window.maximized
        outside: windowFrame.margin > 0 ? windowFrame.resizeMargin : 0
    }
}
