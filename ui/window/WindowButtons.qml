pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Shapes
import Cirrus.Theme
import Cirrus.Controls

// Minimize, maximize/restore and close for the custom title bar: extra-small
// IconButtons (same state layer, ripple and press morph as other icon buttons) whose
// symbols are drawn rather than taken from the icon font. Material Symbols' close
// is 13 of 24 units wide against 18–20 for the squares, so the font's set looks
// uneven here; the drawn set shares one box and Material Symbols' rounded stroke
// (2 units of 24, about 1.75 px at the 20 px icon size).
Row {
    id: root

    readonly property bool maximized: Window.visibility === Window.Maximized
                                      || Window.visibility === Window.FullScreen
    // Icon color on a background of their own (the player page), when set.
    property color tint: "transparent"
    // Redraws the symbols after the window comes back from the tray (a Qt bug).
    ShapeRefresh {
        id: refresh
        target: root
    }

    function toggleMaximized() {
        if (maximized)
            Window.window.showNormal();
        else
            Window.window.showMaximized();
    }

    spacing: 4

    component WindowButton: IconButton {
        id: button

        // ShapePaths in the 20×20 icon box; use `stroke` for their style.
        default property alias paths: symbol.data
        readonly property real stroke: 1.75

        size: Md.XSmall
        tint: root.tint

        contentItem: Item {
            Shape {
                id: symbol
                anchors.centerIn: parent
                width: 20
                height: 20
                preferredRendererType: Shape.CurveRenderer
            }
        }
    }

    component Stroke: ShapePath {
        required property WindowButton button
        strokeColor: button.contentColor
        strokeWidth: button.stroke + refresh.offset
        fillColor: "transparent"
        capStyle: ShapePath.RoundCap
        joinStyle: ShapePath.RoundJoin
    }

    WindowButton {
        id: minimizeButton
        text: qsTr("最小化")
        onClicked: root.Window.window.showMinimized()
        Stroke {
            button: minimizeButton
            startX: 4.5
            startY: 10
            PathLine {
                x: 15.5
                y: 10
            }
        }
    }
    WindowButton {
        id: maximizeButton
        text: root.maximized ? qsTr("还原") : qsTr("最大化")
        onClicked: root.toggleMaximized()
        // Maximize: one rounded square.
        Stroke {
            button: maximizeButton
            strokeColor: root.maximized ? "transparent" : maximizeButton.contentColor
            PathRectangle {
                x: 4.5
                y: 4.5
                width: 11
                height: 11
                radius: 2
            }
        }
        // Restore: a smaller square, with the top and right edges of one behind it.
        Stroke {
            button: maximizeButton
            strokeColor: root.maximized ? maximizeButton.contentColor : "transparent"
            PathRectangle {
                x: 4.5
                y: 7.5
                width: 8
                height: 8
                radius: 2
            }
        }
        Stroke {
            button: maximizeButton
            strokeColor: root.maximized ? maximizeButton.contentColor : "transparent"
            startX: 7.5
            startY: 4.5
            PathLine {
                x: 13.5
                y: 4.5
            }
            PathArc {
                x: 15.5
                y: 6.5
                radiusX: 2
                radiusY: 2
            }
            PathLine {
                x: 15.5
                y: 12.5
            }
        }
    }
    WindowButton {
        id: closeButton
        text: qsTr("关闭")
        onClicked: root.Window.window.close()
        Stroke {
            button: closeButton
            startX: 5
            startY: 5
            PathLine {
                x: 15
                y: 15
            }
            PathMove {
                x: 15
                y: 5
            }
            PathLine {
                x: 5
                y: 15
            }
        }
    }
}
