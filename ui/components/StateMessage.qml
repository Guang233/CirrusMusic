import QtQuick
import Cirrus.Theme
import Cirrus.Controls

// Centered icon, message and optional action: the empty, error, offline and signed-out
// views of PageState, also usable inside a page (e.g. an empty playlist under its header).
Column {
    id: root

    property string glyph
    property string title
    property string message
    property string actionText
    property string actionGlyph

    signal actionTriggered

    spacing: 12

    Icon {
        anchors.horizontalCenter: parent.horizontalCenter
        glyph: root.glyph
        size: 48
        color: Theme.color.onSurfaceVariant
    }
    Text {
        width: parent.width
        visible: text !== ""
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        text: root.title
        color: Theme.color.onSurface
        font: Theme.type.titleMedium.font
    }
    Text {
        width: parent.width
        visible: text !== ""
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        text: root.message
        color: Theme.color.onSurfaceVariant
        font: Theme.type.bodyMedium.font
    }
    Item {
        width: 1
        height: 4
        visible: action.visible
    }
    Button {
        id: action
        anchors.horizontalCenter: parent.horizontalCenter
        visible: root.actionText !== ""
        variant: Md.Tonal
        glyph: root.actionGlyph
        text: root.actionText
        onClicked: root.actionTriggered()
    }
}
