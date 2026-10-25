import QtQuick
import QtQuick.Templates as T
import Cirrus.Theme

// MD3 radio button. Radio buttons sharing a parent are exclusive (Qt's autoExclusive).
T.RadioButton {
    id: control

    readonly property color ringColor: !enabled
        ? Qt.alpha(Theme.color.onSurface, Theme.state.disabledContent)
        : checked ? Theme.color.primary
        : hovered || down || visualFocus ? Theme.color.onSurface : Theme.color.onSurfaceVariant

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding,
                             implicitIndicatorHeight + topPadding + bottomPadding)
    padding: 0
    spacing: 4
    font: Theme.type.bodyMedium.font

    Spring {
        id: dot
        target: control.checked ? 1 : 0
        spec: Theme.motion.fastSpatial
        threshold: 0.002
    }

    indicator: Item {
        implicitWidth: 40
        implicitHeight: 40
        x: control.text ? (control.mirrored ? control.width - width - control.rightPadding
                                            : control.leftPadding)
                        : control.leftPadding + (control.availableWidth - width) / 2
        y: control.topPadding + (control.availableHeight - height) / 2

        StateLayer {
            anchors.fill: parent
            radius: width / 2
            color: control.checked ? Theme.color.primary : Theme.color.onSurface
            hovered: control.hovered && control.enabled
            pressed: control.down
            focused: false
            pressX: width / 2
            pressY: height / 2
        }

        Rectangle {
            id: ring
            anchors.centerIn: parent
            width: 20
            height: 20
            radius: 10
            color: "transparent"
            border.width: 2
            border.color: control.ringColor

            FocusRing {
                shown: control.visualFocus
                containerRadius: ring.radius
            }

            Rectangle {
                anchors.centerIn: parent
                width: 10 * Math.max(0, dot.value)
                height: width
                radius: width / 2
                visible: width > 0.5
                color: control.ringColor
            }
        }
    }

    contentItem: Text {
        leftPadding: control.indicator && !control.mirrored ? control.indicator.width
                                                              + control.spacing : 0
        rightPadding: control.indicator && control.mirrored ? control.indicator.width
                                                              + control.spacing : 0
        text: control.text
        font: control.font
        color: control.enabled ? Theme.color.onSurface
                               : Qt.alpha(Theme.color.onSurface, Theme.state.disabledContent)
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
    }
}
