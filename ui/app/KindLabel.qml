import QtQuick
import Cirrus.Theme

// What a song page shows (歌单, 专辑, 歌手…) above its title: a word in the accent
// color, or in the info pane (`chip`) a small tinted chip (docs/ui/redesign-md3e.md §7.1).
Item {
    id: label

    property string text
    property color color
    property bool chip: false

    implicitWidth: chip ? word.implicitWidth + 26 : word.implicitWidth
    implicitHeight: chip ? 28 : word.implicitHeight

    Rectangle {
        anchors.fill: parent
        visible: label.chip
        radius: height / 2
        color: Qt.alpha(label.color, 0.16)
    }
    Text {
        id: word
        anchors.centerIn: parent
        text: label.text
        color: label.color
        font: Theme.type.labelLarge.font
    }
}
