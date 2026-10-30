pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Cirrus.Theme

// MD3 chip, 32 px high with 8 px corners. Plain chips are assist/suggestion chips
// (outlined); `checkable` makes a filter chip, which fills with SecondaryContainer and
// shows a check when selected.
//   Chip { text: qsTr("落日航线"); onClicked: search(text) }
//   Chip { text: qsTr("只看可播放"); checkable: true }
T.Button {
    id: control

    property string glyph

    readonly property bool selected: checkable && checked
    readonly property string leadingGlyph: selected ? Symbols.check : glyph
    readonly property color labelColor: !enabled
        ? Qt.alpha(Theme.color.onSurface, Theme.state.disabledContent)
        : Theme.mix(checkable ? Theme.color.onSurfaceVariant : Theme.color.onSurface,
                    Theme.color.onSecondaryContainer, selection.value)
    readonly property color iconColor: !enabled
        ? Qt.alpha(Theme.color.onSurface, Theme.state.disabledContent)
        : Theme.mix(Theme.color.primary, Theme.color.onSecondaryContainer, selection.value)

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: 32
    leftPadding: leadingGlyph !== "" ? 8 : 16
    rightPadding: 16
    spacing: 8
    font: Theme.type.labelLarge.font

    Spring {
        id: selection
        target: control.selected ? 1 : 0
        spec: Theme.motion.defaultEffects
    }

    contentItem: Row {
        spacing: control.spacing

        Icon {
            anchors.verticalCenter: parent.verticalCenter
            visible: control.leadingGlyph !== ""
            glyph: control.leadingGlyph
            size: 18
            color: control.iconColor
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: control.text
            font: control.font
            color: control.labelColor
        }
    }

    background: Rectangle {
        implicitHeight: 32
        radius: Theme.shape.small
        color: !control.enabled && control.selected
               ? Qt.alpha(Theme.color.onSurface, Theme.state.disabledContainer)
               : Qt.alpha(Theme.color.secondaryContainer, selection.value)
        border.width: 1 - selection.value
        border.color: control.enabled ? Theme.color.outlineVariant
                                      : Qt.alpha(Theme.color.onSurface,
                                                 Theme.state.disabledContainer)

        StateLayer {
            anchors.fill: parent
            radius: parent.radius
            color: control.selected ? Theme.color.onSecondaryContainer : Theme.color.onSurfaceVariant
            hovered: control.hovered && control.enabled
            pressed: control.down
            focused: control.visualFocus
            pressX: control.pressed ? control.pressX : width / 2
            pressY: control.pressed ? control.pressY : height / 2
        }
        FocusRing {
            shown: control.visualFocus
            containerRadius: parent.radius
        }
    }
}
