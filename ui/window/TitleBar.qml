import QtQuick

// Top row of AppWindow. With the custom title bar it moves the window (drag),
// maximizes it (double click), opens the compositor's window menu (right click)
// and shows the window buttons. With the system or hidden title bar it only hosts
// the app's own content (back, search), which stays at the top of the content area.
Item {
    id: root

    required property WindowFrame frame
    // Moving, maximizing and the window menu.
    property bool windowControls
    // Minimize/maximize/close; off on macOS, where the system draws them.
    property bool windowButtons
    // Space kept free on the leading side, e.g. for the macOS traffic lights.
    property real leadingInset
    default property alias content: content.data

    implicitHeight: 48

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

    Item {
        id: content
        anchors.left: parent.left
        anchors.leftMargin: root.leadingInset
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
