import QtQuick
import Cirrus.Theme
import Cirrus.Controls

// A one-line notice above content, e.g. for partial data (LoadState.Partial).
Rectangle {
    id: root

    property string text
    property string glyph: Symbols.info

    implicitWidth: row.implicitWidth + 32
    implicitHeight: Math.max(48, label.implicitHeight + 24)
    radius: Theme.shape.medium
    color: Theme.color.tertiaryContainer

    Row {
        id: row
        x: 16
        width: parent.width - 32
        anchors.verticalCenter: parent.verticalCenter
        spacing: 12
        Icon {
            id: icon
            anchors.verticalCenter: parent.verticalCenter
            glyph: root.glyph
            size: 20
            color: Theme.color.onTertiaryContainer
        }
        Text {
            id: label
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - icon.width - parent.spacing
            text: root.text
            wrapMode: Text.Wrap
            color: Theme.color.onTertiaryContainer
            font: Theme.type.bodyMedium.font
        }
    }
}
