import QtQuick
import M3Shapes
import Cirrus.Theme
import Cirrus.Controls

// Centered icon, message and optional action: the empty, error, offline and signed-out
// views of PageState, also usable inside a page (e.g. an empty playlist under its header).
// The icon sits on a large expressive shape (expressive.md §3.5): `tone` Md.Secondary for
// empty (the default), Md.Error for errors (a burst), Md.Neutral offline, Md.Primary signed out.
Column {
    id: root

    property string glyph
    property string title
    property string message
    property string actionText
    property string actionGlyph
    property int tone: Md.Secondary

    signal actionTriggered

    spacing: 12

    IconContainer {
        anchors.horizontalCenter: parent.horizontalCenter
        glyph: root.glyph
        size: 96
        shape: root.tone === Md.Error ? MaterialShape.SoftBurst : MaterialShape.Cookie9Sided
        tone: root.tone
    }
    Item {
        width: 1
        height: 4
    }
    Text {
        width: parent.width
        visible: text !== ""
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        text: root.title
        color: ColorScope.scheme.onSurface
        font: Theme.type.titleMedium.font
    }
    Text {
        width: parent.width
        visible: text !== ""
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        text: root.message
        color: ColorScope.scheme.onSurfaceVariant
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
