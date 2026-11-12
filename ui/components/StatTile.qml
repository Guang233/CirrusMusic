import QtQuick
import Cirrus.Theme

// A number and what it counts (docs/ui/pages/stats.md §2.4), on a tonal card.
//   StatTile { value: "86"; label: qsTr("次播放") }
Rectangle {
    id: tile

    property string value
    property string label

    implicitHeight: 96
    radius: Theme.shape.large
    color: ColorScope.scheme.surfaceContainerLow
    Accessible.role: Accessible.StaticText
    Accessible.name: value + " " + label

    Column {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 20
        anchors.rightMargin: 20
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2
        Text {
            width: parent.width
            text: tile.value
            elide: Text.ElideRight
            color: ColorScope.scheme.onSurface
            font: Theme.type.headlineMedium.font
        }
        Text {
            width: parent.width
            text: tile.label
            elide: Text.ElideRight
            color: ColorScope.scheme.onSurfaceVariant
            font: Theme.type.bodyMedium.font
        }
    }
}
