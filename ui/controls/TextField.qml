pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Cirrus.Theme

// MD3 text field (filled or outlined) with a floating label.
//   TextField { label: qsTr("昵称"); supportingText: qsTr("2–16 个字符") }
T.TextField {
    id: control

    property int variant: Md.Filled
    property string label
    property string leadingGlyph
    property string trailingGlyph
    property string supportingText
    property bool error: false
    // Outlined only: color behind the floated label's gap; match the surface below.
    property color gapColor: Theme.color.surface

    readonly property bool outlined: variant === Md.Outlined
    readonly property bool floated: activeFocus || length > 0 || preeditText.length > 0
    readonly property bool hasSupporting: supportingText !== ""
    readonly property real fieldHeight: 56
    readonly property color accent: error ? Theme.color.error : Theme.color.primary
    readonly property color mutedColor: enabled ? Theme.color.onSurfaceVariant
                                                : Qt.alpha(Theme.color.onSurface,
                                                           Theme.state.disabledContent)
    readonly property color indicatorColor: !enabled
        ? Qt.alpha(Theme.color.onSurface, Theme.state.disabledContent)
        : error ? Theme.color.error
        : activeFocus ? Theme.color.primary
        : hovered ? Theme.color.onSurface : (outlined ? Theme.color.outline
                                                      : Theme.color.onSurfaceVariant)

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset, 240)
    implicitHeight: fieldHeight + (hasSupporting ? 20 : 0)
    leftPadding: leadingGlyph !== "" ? 52 : 16
    rightPadding: trailingGlyph !== "" ? 52 : 16
    topPadding: label !== "" && !outlined ? 24 : 16
    bottomPadding: (label !== "" && !outlined ? 8 : 16) + (hasSupporting ? 20 : 0)
    verticalAlignment: TextInput.AlignVCenter
    font: Theme.type.bodyLarge.font
    color: enabled ? Theme.color.onSurface : Qt.alpha(Theme.color.onSurface,
                                                      Theme.state.disabledContent)
    selectionColor: Qt.alpha(accent, 0.4)
    selectedTextColor: Theme.color.onSurface
    placeholderTextColor: mutedColor
    Accessible.name: label !== "" ? label : placeholderText
    Accessible.description: supportingText

    Spring {
        id: float
        target: control.floated ? 1 : 0
        spec: Theme.motion.standardFastSpatial
        threshold: 0.002
    }

    cursorDelegate: Rectangle {
        id: cursor
        width: 2
        color: control.accent
        visible: control.activeFocus && !control.readOnly && control.selectionStart
                 === control.selectionEnd && blink.on

        Timer {
            id: blink
            property bool on: true
            interval: 530
            repeat: true
            running: control.activeFocus
            onRunningChanged: blink.on = true
            onTriggered: blink.on = !blink.on
        }
        Connections {
            target: control
            function onCursorPositionChanged() {
                blink.on = true;
                blink.restart();
            }
        }
    }

    // Placeholder only when there is no label, or the label has floated away.
    Text {
        x: control.leftPadding
        y: control.topPadding
        width: control.width - control.leftPadding - control.rightPadding
        height: control.height - control.topPadding - control.bottomPadding
        visible: control.length === 0 && control.preeditText.length === 0
                 && (control.label === "" || control.activeFocus)
        text: control.placeholderText
        font: control.font
        color: control.placeholderTextColor
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Item {
        implicitWidth: 240
        implicitHeight: control.fieldHeight

        Rectangle {
            id: container
            width: parent.width
            height: control.fieldHeight
            color: control.outlined ? "transparent"
                 : control.enabled ? Theme.color.surfaceContainerHighest
                                   : Qt.alpha(Theme.color.onSurface, 0.04)
            topLeftRadius: Theme.shape.extraSmall
            topRightRadius: Theme.shape.extraSmall
            bottomLeftRadius: control.outlined ? Theme.shape.extraSmall : 0
            bottomRightRadius: control.outlined ? Theme.shape.extraSmall : 0
            border.width: control.outlined ? (control.activeFocus ? 2 : 1) : 0
            border.color: control.indicatorColor

            StateLayer {
                anchors.fill: parent
                visible: !control.outlined
                color: Theme.color.onSurface
                hovered: control.hovered && control.enabled && !control.activeFocus
                topLeftRadius: container.topLeftRadius
                topRightRadius: container.topRightRadius
            }

            // Filled: bottom active indicator.
            Rectangle {
                visible: !control.outlined
                anchors.bottom: parent.bottom
                width: parent.width
                height: control.activeFocus ? 2 : 1
                color: control.indicatorColor
            }

            Icon {
                x: 12
                anchors.verticalCenter: parent.verticalCenter
                visible: control.leadingGlyph !== ""
                glyph: control.leadingGlyph
                color: control.mutedColor
            }
            Icon {
                x: parent.width - width - 12
                anchors.verticalCenter: parent.verticalCenter
                visible: control.trailingGlyph !== ""
                glyph: control.trailingGlyph
                color: control.error && control.enabled ? Theme.color.error : control.mutedColor
            }

            // Outlined: the floated label sits on the border over a gap in it.
            Rectangle {
                visible: control.outlined && control.label !== "" && float.value > 0.01
                x: floatingLabel.x - 4
                y: -1
                width: (floatingLabel.implicitWidth * floatingLabel.scale + 8) * float.value
                height: 3
                color: control.gapColor
            }

            Text {
                id: floatingLabel
                readonly property real restY: (control.fieldHeight - implicitHeight) / 2
                readonly property real floatY: control.outlined ? -implicitHeight * 0.75 / 2
                                                                : 7
                visible: control.label !== ""
                x: control.outlined ? 16 + (control.leftPadding - 16) * (1 - float.value)
                                    : control.leftPadding
                y: restY + (floatY - restY) * float.value
                transformOrigin: Item.TopLeft
                scale: 1 - 0.25 * float.value
                text: control.label
                font: Theme.type.bodyLarge.font
                color: !control.enabled ? control.mutedColor
                     : control.error ? Theme.color.error
                     : control.activeFocus ? Theme.color.primary : Theme.color.onSurfaceVariant
            }
        }

        Text {
            visible: control.hasSupporting
            x: 16
            y: control.fieldHeight + 4
            width: parent.width - 32
            text: control.supportingText
            font: Theme.type.bodySmall.font
            color: control.error && control.enabled ? Theme.color.error : control.mutedColor
            elide: Text.ElideRight
        }
    }
}
