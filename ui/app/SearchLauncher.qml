import QtQuick
import QtQuick.Templates as T
import Cirrus.Theme
import Cirrus.Controls

// The search field of the title bar row (docs/ui/pages/shell.md §3). It looks like a
// search bar but only opens the search page, whose own bar takes the typing.
T.AbstractButton {
    id: control

    implicitWidth: 360
    implicitHeight: 40
    focusPolicy: Qt.TabFocus
    Accessible.role: Accessible.Button
    Accessible.name: qsTr("搜索")

    background: Rectangle {
        radius: height / 2
        color: Theme.color.surfaceContainerHigh

        StateLayer {
            anchors.fill: parent
            radius: parent.radius
            color: Theme.color.onSurface
            hovered: control.hovered
            pressed: control.down
            focused: control.visualFocus
            pressX: control.pressX
            pressY: control.pressY
        }
        FocusRing {
            shown: control.visualFocus
            containerTopLeftRadius: parent.radius
            containerTopRightRadius: parent.radius
            containerBottomRightRadius: parent.radius
            containerBottomLeftRadius: parent.radius
        }
    }

    contentItem: Item {
        Icon {
            x: 12
            anchors.verticalCenter: parent.verticalCenter
            glyph: Symbols.search
            size: 20
            color: Theme.color.onSurfaceVariant
        }
        Text {
            x: 44
            width: shortcutHint.x - x - 8
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("搜索歌曲、歌手、专辑、歌单")
            elide: Text.ElideRight
            color: Theme.color.onSurfaceVariant
            font: Theme.type.bodyMedium.font
        }
        Text {
            id: shortcutHint
            x: parent.width - width - 16
            anchors.verticalCenter: parent.verticalCenter
            text: "Ctrl+K"
            color: Theme.color.outline
            font: Theme.type.labelMedium.font
        }
    }
}
