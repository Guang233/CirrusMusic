pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Effects
import Cirrus.Theme
import Cirrus.Controls

// Application window. The custom title bar (default) draws the window frame itself:
// shadow, rounded corners, resize bands and window buttons (Linux, Windows), or
// keeps the macOS traffic lights over an expanded client area. The system title bar
// uses native decorations. See docs/ui/README.md §6.1.
Window {
    id: window

    // Set by main.cpp: false with --system-titlebar or CIRRUS_SYSTEM_TITLEBAR=1.
    property bool customTitleBar: true

    readonly property bool macOS: Qt.platform.os === "osx"
    readonly property bool drawsFrame: customTitleBar && !macOS
    readonly property bool maximized: visibility === Window.Maximized
                                      || visibility === Window.FullScreen
    readonly property real cornerRadius: frame.margin > 0 ? Theme.shape.medium : 0

    // Switches title bar mode by recreating the native window (hide, change flags,
    // show). Prototype: whether this works everywhere decides if the setting needs
    // a restart (docs/ui/README.md §6.1).
    function switchTitleBar() {
        visible = false;
        customTitleBar = !customTitleBar;
        visible = true;
    }

    width: 1200
    height: 800
    minimumWidth: 480 + 2 * frame.margin
    minimumHeight: 360 + 2 * frame.margin
    visible: true
    title: qsTr("Cirrus Music")
    flags: !customTitleBar ? Qt.Window
         : macOS ? Qt.Window | Qt.ExpandedClientAreaHint | Qt.NoTitleBarBackgroundHint
         : Qt.Window | Qt.FramelessWindowHint
    // Always translucent: the alpha channel is chosen when the native window is
    // created, and the shadow margin needs it. `surface` paints the opaque part.
    color: "transparent"

    WindowFrame {
        id: frame
        window: window
        clientSide: window.drawsFrame
    }

    RectangularShadow {
        anchors.fill: surface
        visible: frame.margin > 0
        radius: window.cornerRadius
        offset.y: window.active ? 4 : 2
        blur: window.active ? 20 : 12
        color: Qt.alpha(Theme.color.shadow, window.active ? 0.4 : 0.24)
    }

    // The visible window. Children must keep clear of the rounded corners (only the
    // surface color is painted there); full-bleed content such as the player
    // background rounds its own corners with `window.cornerRadius`.
    Rectangle {
        id: surface

        anchors.fill: parent
        anchors.margins: frame.margin
        radius: window.cornerRadius
        color: Theme.color.surface

        TitleBar {
            id: titleBar

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            frame: frame
            windowControls: window.customTitleBar
            windowButtons: window.drawsFrame
            leadingInset: window.customTitleBar && window.macOS ? 72 : 16

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: window.title
                color: Theme.color.onSurfaceVariant
                font: Theme.type.titleSmall.font
            }
        }

        // Prototype content until the app shell exists (UI phase 3, step 3).
        Column {
            anchors.centerIn: parent
            anchors.verticalCenterOffset: titleBar.height / 2
            spacing: 16

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("窗口原型")
                color: Theme.color.onSurface
                font: Theme.type.headlineSmall.font
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                horizontalAlignment: Text.AlignHCenter
                lineHeight: Theme.type.bodyMedium.lineHeight
                lineHeightMode: Text.FixedHeight
                color: Theme.color.onSurfaceVariant
                font: Theme.type.bodyMedium.font
                text: [qsTr("标题栏：%1").arg(window.customTitleBar ? qsTr("自绘") : qsTr("系统")),
                    qsTr("平台：%1").arg(Qt.platform.pluginName),
                    qsTr("窗口：%1").arg(window.maximized ? qsTr("最大化")
                                                         : frame.tiled ? qsTr("平铺") : qsTr("普通")),
                    qsTr("阴影边距：%1 px").arg(frame.margin)].join("\n")
            }
            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                variant: Md.Tonal
                text: window.customTitleBar ? qsTr("切换为系统标题栏") : qsTr("切换为自绘标题栏")
                onClicked: window.switchTitleBar()
            }
        }
    }

    // Hairline edge so the frame stays visible on dark backgrounds, where the
    // shadow is weak. Above the content, which never paints there anyway.
    Rectangle {
        anchors.fill: surface
        visible: frame.margin > 0
        radius: window.cornerRadius
        color: "transparent"
        border.width: 1
        border.color: Qt.alpha(Theme.color.outlineVariant, 0.6)
    }

    ResizeHandles {
        anchors.fill: surface
        visible: window.drawsFrame && !window.maximized
        outside: frame.margin > 0 ? frame.resizeMargin : 0
    }
}
