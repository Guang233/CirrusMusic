pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Cirrus.Theme
import Cirrus.Controls

// The player page's icon button (docs/ui/pages/player.md §1.1): no container, in the
// content color; a faint round tint on hover, and the icon shrinks while pressed.
// `text` is the accessible name and the tooltip.
//   PlayerButton { glyph: Symbols.skip_next; text: qsTr("下一首"); color: colors.content }
T.AbstractButton {
    id: control

    property string glyph
    property real iconSize: 24
    property color color
    // Icon FILL axis (animated); checkable buttons fill when checked.
    property real iconFill: checkable ? (checked ? 1 : 0) : 1
    property bool showToolTip: true

    implicitWidth: iconSize + 20
    implicitHeight: iconSize + 20
    focusPolicy: Qt.StrongFocus
    hoverEnabled: true
    Accessible.name: text

    Spring {
        id: hover
        target: control.enabled && (control.hovered || control.down) ? 1 : 0
        spec: Theme.motion.fastEffects
    }
    Spring {
        id: press
        target: control.down ? 1 : 0
        spec: Theme.motion.fastSpatial
    }
    Spring {
        id: fillSpring
        target: control.iconFill
        spec: Theme.motion.defaultEffects
    }

    background: Rectangle {
        radius: height / 2
        color: Qt.alpha(control.color, 0.12 * hover.value)

        FocusRing {
            shown: InputMode.keyboard && control.visualFocus
            containerRadius: parent.radius
            border.color: control.color
        }
    }

    contentItem: Item {
        Icon {
            anchors.centerIn: parent
            glyph: control.glyph
            size: control.iconSize
            fill: fillSpring.value
            color: control.color
            opacity: control.enabled ? 1 : 0.38
            scale: 1 - 0.12 * press.value
        }
    }

    // Created on first hover and kept, so it can fade out.
    property bool hoveredOnce: false
    onHoveredChanged: if (hovered)
                          hoveredOnce = true

    Loader {
        active: control.showToolTip && control.text !== "" && control.hoveredOnce
        sourceComponent: ToolTip {
            parent: control
            text: control.text
            visible: control.hovered && !control.pressed
        }
    }
}
