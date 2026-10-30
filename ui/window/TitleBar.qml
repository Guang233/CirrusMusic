import QtQuick
import Cirrus.Theme

// Top row of AppWindow. With the custom title bar it shows the window title, moves
// the window (drag), maximizes it (double click), opens the compositor's window
// menu (right click) and shows the window buttons. With the system or hidden title
// bar it only hosts the app's own content (back, search) and collapses without any.
Item {
    id: root

    required property WindowFrame frame
    // Moving, maximizing, the window menu and the title.
    property bool windowControls
    // Minimize/maximize/close; off on macOS, where the system draws them.
    property bool windowButtons
    property string title
    // Space kept free on the leading side, e.g. for the macOS traffic lights.
    property real leadingInset
    default property alias content: content.data

    readonly property bool hasContent: content.visibleChildren.length > 0

    implicitHeight: windowControls || hasContent ? 48 : 0
    visible: implicitHeight > 0

    // Under the content and the buttons, so presses on them never move the window.
    Item {
        anchors.fill: parent
        enabled: root.windowControls

        DragHandler {
            target: null
            onActiveChanged: if (active)
                                 root.Window.window.startSystemMove()
        }
        TapHandler {
            onDoubleTapped: buttons.toggleMaximized()
        }
        TapHandler {
            acceptedButtons: Qt.RightButton
            // The compositor wants the serial of the press, not of the release.
            onPressedChanged: if (pressed)
                                  root.frame.showWindowMenu()
        }
    }

    Text {
        id: titleText
        anchors.left: parent.left
        anchors.leftMargin: root.leadingInset
        anchors.verticalCenter: parent.verticalCenter
        visible: root.windowControls && root.title !== ""
        text: root.title
        color: Theme.color.onSurfaceVariant
        font: Theme.type.titleSmall.font
    }

    Item {
        id: content
        anchors.left: titleText.visible ? titleText.right : parent.left
        anchors.leftMargin: titleText.visible ? 16 : root.leadingInset
        anchors.right: root.windowButtons ? buttons.left : parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
    }

    WindowButtons {
        id: buttons
        anchors.right: parent.right
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        visible: root.windowButtons
    }
}
