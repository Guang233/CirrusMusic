pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls

// A side panel of the player page (docs/ui/pages/player.md §8, §9.1): slides in from
// the right on a see-through surface in the cover's scheme (ColorScope), a title and
// a close button over its content. The page opens one at a time and closes it on a
// click beside it or Esc.
//   PlayerSheet { title: qsTr("播放队列"); shown: page.sheet === 1; PlayerQueue { … } }
Rectangle {
    id: root

    property string title
    property bool shown
    readonly property Item closeButton: close
    default property alias content: body.data

    signal closeRequested

    // Visible at once on opening: the focus moves into it.
    visible: shown || slide.value > 0.001
    opacity: Math.min(1, slide.value * 2)
    radius: Theme.shape.extraLarge
    color: Qt.alpha(ColorScope.scheme.surface, 0.8)
    transform: Translate {
        x: (1 - slide.value) * 48
    }

    Spring {
        id: slide
        target: root.shown ? 1 : 0
        spec: Theme.motion.reduced ? Theme.motion.defaultEffects : Theme.motion.defaultSpatial
    }

    // Takes the clicks over it.
    MouseArea {
        anchors.fill: parent
    }

    Text {
        id: heading
        x: 24
        y: 20
        text: root.title
        color: ColorScope.scheme.onSurface
        font: Theme.type.titleLarge.font
        Accessible.role: Accessible.Heading
    }
    IconButton {
        id: close
        anchors.right: parent.right
        anchors.rightMargin: 12
        anchors.verticalCenter: heading.verticalCenter
        glyph: Symbols.close
        text: qsTr("关闭")
        onClicked: root.closeRequested()
    }
    Item {
        id: body
        anchors.fill: parent
        anchors.topMargin: 64
        anchors.bottomMargin: 12
    }
}
