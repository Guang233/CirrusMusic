pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme

// Skeleton of a page built on a SongTable: the page's own header skeleton
// (`headerSkeleton`), then column titles and rows laid out with the table's columns,
// so the content replaces it without moving. Give PageState one:
//   PageState { skeleton: SongTableSkeleton { table: songTable; headerSkeleton: … } }
Item {
    id: root

    required property SongTable table
    property Component headerSkeleton
    property int rows: 8

    clip: true

    Column {
        width: root.width

        Loader {
            width: parent.width
            sourceComponent: root.headerSkeleton
        }

        // Column titles.
        Item {
            x: root.table.rowX
            width: root.table.rowWidth
            height: 40
            Skeleton {
                x: 16 + root.table.numberWidth - width
                anchors.verticalCenter: parent.verticalCenter
                width: 10
                height: 10
            }
            Skeleton {
                x: root.table.titleX
                anchors.verticalCenter: parent.verticalCenter
                width: 32
                height: 10
            }
            Skeleton {
                x: root.table.albumX
                anchors.verticalCenter: parent.verticalCenter
                visible: root.table.showAlbum
                width: 32
                height: 10
            }
            Skeleton {
                x: root.table.durationX + root.table.durationWidth - width
                anchors.verticalCenter: parent.verticalCenter
                width: 32
                height: 10
            }
        }

        Column {
            x: root.table.rowX
            width: root.table.rowWidth
            spacing: 2

            Repeater {
                model: root.rows
                delegate: Rectangle {
                    id: row
                    required property int index
                    readonly property real outer: Theme.shape.large
                    readonly property real inner: Theme.shape.extraSmall
                    // Varying lengths read as text rather than a grid.
                    readonly property int variant: index % 4
                    width: parent.width
                    height: 56
                    color: Theme.color.surfaceContainer
                    topLeftRadius: index === 0 ? outer : inner
                    topRightRadius: topLeftRadius
                    bottomLeftRadius: index === root.rows - 1 ? outer : inner
                    bottomRightRadius: bottomLeftRadius

                    Skeleton {
                        x: 16 + root.table.numberWidth - width
                        anchors.verticalCenter: parent.verticalCenter
                        width: 12
                        height: 12
                    }
                    Skeleton {
                        x: root.table.titleX
                        anchors.verticalCenter: parent.verticalCenter
                        width: 40
                        height: 40
                        radius: Theme.shape.small
                    }
                    Column {
                        x: root.table.titleX + 52
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 8
                        Skeleton {
                            width: Math.min([180, 240, 150, 210][row.variant],
                                            root.table.titleWidth - 52)
                            height: 14
                        }
                        Skeleton {
                            width: Math.min([120, 90, 140, 110][row.variant],
                                            root.table.titleWidth - 52)
                            height: 12
                        }
                    }
                    Skeleton {
                        x: root.table.albumX
                        anchors.verticalCenter: parent.verticalCenter
                        visible: root.table.showAlbum
                        width: Math.min([140, 100, 160, 120][row.variant], root.table.albumWidth)
                        height: 12
                    }
                    Skeleton {
                        x: root.table.durationX + root.table.durationWidth - width
                        anchors.verticalCenter: parent.verticalCenter
                        width: 32
                        height: 12
                    }
                }
            }
        }
    }
}
