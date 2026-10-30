pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme

// Skeleton of a detail page: an optional header (cover and title lines) above
// segmented song rows, laid out like SongList.
Item {
    id: root

    property bool header: true
    property int rows: 8
    property real sideMargin: 24
    // Circular cover in the header, for artists.
    property bool roundCover: false

    clip: true

    Column {
        x: root.sideMargin
        width: root.width - 2 * root.sideMargin
        spacing: 2

        Row {
            visible: root.header
            height: 200
            spacing: 24
            Skeleton {
                width: 160
                height: 160
                radius: root.roundCover ? width / 2 : Theme.shape.large
            }
            Column {
                id: lines
                readonly property real available: root.width - 2 * root.sideMargin - 184
                topPadding: 8
                spacing: 12
                Skeleton {
                    width: Math.min(280, lines.available)
                    height: 28
                }
                Skeleton {
                    width: Math.min(200, lines.available)
                    height: 14
                }
                Skeleton {
                    width: Math.min(120, lines.available)
                    height: 14
                }
                Skeleton {
                    width: 120
                    height: 40
                    radius: height / 2
                }
            }
        }

        Repeater {
            model: root.rows
            delegate: Rectangle {
                id: row
                required property int index
                readonly property real outer: Theme.shape.large
                readonly property real inner: Theme.shape.extraSmall
                width: parent.width
                height: 72
                color: Theme.color.surfaceContainer
                topLeftRadius: index === 0 ? outer : inner
                topRightRadius: topLeftRadius
                bottomLeftRadius: index === root.rows - 1 ? outer : inner
                bottomRightRadius: bottomLeftRadius

                Skeleton {
                    x: 16
                    anchors.verticalCenter: parent.verticalCenter
                    width: 48
                    height: 48
                    radius: Theme.shape.small
                }
                Column {
                    id: text
                    readonly property real available: row.width - x - 72
                    x: 76
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8
                    Skeleton {
                        // Varying lengths read as text rather than a grid.
                        width: Math.min([180, 240, 150, 210][row.index % 4], text.available)
                        height: 14
                    }
                    Skeleton {
                        width: Math.min([120, 90, 140, 110][row.index % 4], text.available)
                        height: 12
                    }
                }
                Skeleton {
                    anchors.right: parent.right
                    anchors.rightMargin: 16
                    anchors.verticalCenter: parent.verticalCenter
                    width: 32
                    height: 12
                }
            }
        }
    }
}
