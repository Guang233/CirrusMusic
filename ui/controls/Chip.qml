pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Cirrus.Theme

// MD3 chip, 32 px high with 8 px corners. Plain chips are assist/suggestion chips
// (outlined); `checkable` makes a filter chip, which fills with SecondaryContainer and
// shows a check when selected; `removable` makes an input chip with a trailing remove
// button (`removeClicked`). `highlighted` shows the focus state layer for a chip a
// list picks while the keyboard focus stays elsewhere (the search field's list).
//   Chip { text: qsTr("落日航线"); onClicked: search(text) }
//   Chip { text: qsTr("只看可播放"); checkable: true }
//   Chip { text: query; removable: true; onRemoveClicked: forget(query) }
T.Button {
    id: control

    property string glyph
    // Input chip: a remove button after the label.
    property bool removable: false
    // Longer labels elide (0: no limit).
    property real maximumLabelWidth: 0

    readonly property bool selected: checkable && checked
    readonly property string leadingGlyph: selected ? Symbols.check : glyph
    readonly property color labelColor: !enabled
        ? Qt.alpha(Theme.color.onSurface, Theme.state.disabledContent)
        : Theme.mix(checkable || removable ? Theme.color.onSurfaceVariant
                                           : Theme.color.onSurface,
                    Theme.color.onSecondaryContainer, selection.value)
    signal removeClicked

    readonly property color iconColor: !enabled
        ? Qt.alpha(Theme.color.onSurface, Theme.state.disabledContent)
        : Theme.mix(Theme.color.primary, Theme.color.onSecondaryContainer, selection.value)

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: 32
    leftPadding: leadingGlyph !== "" ? 8 : removable ? 12 : 16
    rightPadding: removable ? 4 : 16
    spacing: removable ? 4 : 8
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
            width: control.maximumLabelWidth > 0 ? Math.min(implicitWidth, control.maximumLabelWidth)
                                                 : implicitWidth
            anchors.verticalCenter: parent.verticalCenter
            text: control.text
            elide: Text.ElideRight
            font: control.font
            color: control.labelColor
        }
        // 18 px icon with 8 px to the edge; the target is a little larger.
        T.AbstractButton {
            id: remove
            width: 26
            height: 26
            anchors.verticalCenter: parent.verticalCenter
            visible: control.removable
            enabled: control.enabled
            focusPolicy: Qt.NoFocus
            Accessible.name: qsTr("删除")
            onClicked: control.removeClicked()
            contentItem: Icon {
                glyph: Symbols.close
                size: 18
                color: control.enabled ? Theme.color.onSurfaceVariant
                                       : Qt.alpha(Theme.color.onSurface,
                                                  Theme.state.disabledContent)
            }
            background: StateLayer {
                radius: width / 2
                color: Theme.color.onSurfaceVariant
                hovered: remove.hovered
                pressed: remove.pressed
            }
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
            hovered: control.hovered && control.enabled && !remove.hovered
            pressed: control.down
            focused: control.visualFocus || control.highlighted
            pressX: control.pressed ? control.pressX : width / 2
            pressY: control.pressed ? control.pressY : height / 2
        }
        FocusRing {
            shown: control.visualFocus
            containerRadius: parent.radius
        }
    }
}
