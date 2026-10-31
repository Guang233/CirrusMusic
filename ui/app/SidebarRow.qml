import QtQuick
import QtQuick.Templates as T
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components

// A row of the expanded navigation rail's own content (my music, playlists): a glyph
// or a small cover and a label, lined up with the rail items' icons and labels.
T.AbstractButton {
    id: control

    property string glyph
    property string coverKey
    property bool cover: false
    property bool selected: false

    readonly property color contentColor: selected ? Theme.color.onSecondaryContainer
                                                   : Theme.color.onSurfaceVariant

    implicitHeight: 44
    focusPolicy: Qt.TabFocus
    Accessible.role: Accessible.Button
    Accessible.name: text

    background: Item {
        Rectangle {
            id: container
            x: 20
            width: control.width - 40
            height: control.height
            radius: height / 2
            color: control.selected ? Theme.color.secondaryContainer : "transparent"

            StateLayer {
                anchors.fill: parent
                radius: parent.radius
                color: control.contentColor
                hovered: control.hovered
                pressed: control.down
                focused: control.visualFocus
                pressX: control.pressed ? control.pressX - container.x : width / 2
                pressY: control.pressed ? control.pressY : height / 2
            }
            FocusRing {
                shown: control.visualFocus
                containerTopLeftRadius: container.radius
                containerTopRightRadius: container.radius
                containerBottomRightRadius: container.radius
                containerBottomLeftRadius: container.radius
            }
        }
    }

    contentItem: Item {
        Icon {
            x: 36
            anchors.verticalCenter: parent.verticalCenter
            visible: !control.cover
            glyph: control.glyph
            size: 24
            fill: control.selected ? 1 : 0
            color: control.contentColor
        }
        CoverArt {
            x: 34
            width: 28
            height: 28
            anchors.verticalCenter: parent.verticalCenter
            visible: control.cover
            coverKey: control.coverKey
            radius: Theme.shape.extraSmall
            glyph: Symbols.queue_music
        }
        Text {
            x: 72
            width: control.width - x - 32
            anchors.verticalCenter: parent.verticalCenter
            text: control.text
            elide: Text.ElideRight
            color: control.contentColor
            font: Theme.type.labelLarge.font
        }
    }
}
