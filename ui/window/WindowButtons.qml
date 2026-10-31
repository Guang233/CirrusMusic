pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Cirrus.Theme
import Cirrus.Controls

// Minimize, maximize/restore and close for the custom title bar. The symbols are
// drawn, not icon-font glyphs: Material Symbols' close is 13 of 24 units wide against
// 18–20 for the squares, so the font's set looks uneven at window-button size. Here
// all four fit the same 10 px box with the same 1 px stroke, as desktop themes do.
Row {
    id: root

    readonly property bool maximized: Window.visibility === Window.Maximized
                                      || Window.visibility === Window.FullScreen

    function toggleMaximized() {
        if (maximized)
            Window.window.showNormal();
        else
            Window.window.showMaximized();
    }

    spacing: 4

    // 32 px round button with a state layer, like an extra-small IconButton.
    component WindowButton: T.AbstractButton {
        id: button

        default property alias symbol: symbolBox.data
        readonly property color symbolColor: Theme.color.onSurfaceVariant

        implicitWidth: 32
        implicitHeight: 32
        focusPolicy: Qt.NoFocus
        Accessible.role: Accessible.Button
        Accessible.name: text

        ToolTip {
            parent: button
            text: button.text
            visible: button.hovered && !button.pressed
        }

        background: StateLayer {
            radius: height / 2
            color: Theme.color.onSurfaceVariant
            hovered: button.hovered
            pressed: button.down
            pressX: button.pressX
            pressY: button.pressY
        }
        contentItem: Item {
            Item {
                id: symbolBox
                anchors.centerIn: parent
                width: 10
                height: 10
            }
        }
    }

    WindowButton {
        id: minimizeButton
        text: qsTr("最小化")
        onClicked: root.Window.window.showMinimized()
        Rectangle {
            y: 5
            width: 10
            height: 1
            color: minimizeButton.symbolColor
        }
    }
    WindowButton {
        id: maximizeButton
        text: root.maximized ? qsTr("还原") : qsTr("最大化")
        onClicked: root.toggleMaximized()
        // Maximize: one square. Restore: a square with another behind it.
        Rectangle {
            visible: !root.maximized
            width: 10
            height: 10
            radius: 1
            color: "transparent"
            border.width: 1
            border.color: maximizeButton.symbolColor
        }
        // Restore: the front square, and the top and right edges of the one behind.
        Item {
            anchors.fill: parent
            visible: root.maximized
            Rectangle {
                y: 2
                width: 8
                height: 8
                radius: 1
                color: "transparent"
                border.width: 1
                border.color: maximizeButton.symbolColor
            }
            Rectangle {
                x: 2
                width: 8
                height: 1
                color: maximizeButton.symbolColor
            }
            Rectangle {
                x: 9
                width: 1
                height: 8
                color: maximizeButton.symbolColor
            }
        }
    }
    WindowButton {
        id: closeButton
        text: qsTr("关闭")
        onClicked: root.Window.window.close()
        Repeater {
            model: [45, -45]
            Rectangle {
                required property int modelData
                x: (10 - width) / 2
                y: 4.5
                width: 13
                height: 1
                rotation: modelData
                antialiasing: true
                color: closeButton.symbolColor
            }
        }
    }
}
