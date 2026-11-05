pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Cirrus.Theme

// MD3E slider (horizontal): a bar handle with gaps to both track halves and a stop
// dot at the end. `showValueLabel` shows `valueText` above the handle while dragging.
//   Slider { from: 0; to: 100; value: 40; showValueLabel: true }
T.Slider {
    id: control

    property real trackHeight: 16
    property bool showValueLabel: false
    property string valueText: Math.round(value).toString()

    readonly property real gap: 6
    readonly property real innerRadius: 2
    readonly property real handleX: leftPadding + 2 + visualPosition * (availableWidth - 4)
    readonly property color activeColor: enabled ? ColorScope.scheme.primary
                                                 : Qt.alpha(ColorScope.scheme.onSurface,
                                                            Theme.state.disabledContent)
    readonly property color inactiveColor: enabled ? ColorScope.scheme.secondaryContainer
                                                   : Qt.alpha(ColorScope.scheme.onSurface,
                                                              Theme.state.disabledContainer)

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitHandleWidth + leftPadding + rightPadding, 200)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitHandleHeight + topPadding + bottomPadding)
    padding: 0
    orientation: Qt.Horizontal

    Spring {
        id: handleWidth
        target: control.pressed || control.visualFocus ? 2 : 4
        spec: Theme.motion.fastSpatial
        threshold: 0.02
    }

    background: Item {
        implicitWidth: 200
        implicitHeight: control.trackHeight
        x: control.leftPadding
        y: control.topPadding + (control.availableHeight - height) / 2
        width: control.availableWidth
        height: control.trackHeight

        // Active part, left of the handle.
        Rectangle {
            readonly property real end: control.handleX - control.leftPadding - control.gap
            visible: end > 0
            width: Math.max(0, end)
            height: parent.height
            color: control.activeColor
            topLeftRadius: height / 2
            bottomLeftRadius: height / 2
            topRightRadius: Math.min(control.innerRadius, width / 2)
            bottomRightRadius: Math.min(control.innerRadius, width / 2)
        }

        // Inactive part, right of the handle.
        Rectangle {
            readonly property real start: control.handleX - control.leftPadding + control.gap
            visible: start < parent.width
            x: start
            width: Math.max(0, parent.width - start)
            height: parent.height
            color: control.inactiveColor
            topRightRadius: height / 2
            bottomRightRadius: height / 2
            topLeftRadius: Math.min(control.innerRadius, width / 2)
            bottomLeftRadius: Math.min(control.innerRadius, width / 2)

            Rectangle {
                width: 4
                height: 4
                radius: 2
                x: parent.width - parent.height / 2 - 2
                anchors.verticalCenter: parent.verticalCenter
                visible: parent.width > parent.height
                color: control.activeColor
            }
        }
    }

    handle: Rectangle {
        implicitWidth: 4
        implicitHeight: 44
        x: control.handleX - width / 2
        y: control.topPadding + (control.availableHeight - height) / 2
        width: Math.max(1, handleWidth.value)
        radius: width / 2
        color: control.activeColor

        FocusRing {
            shown: control.visualFocus
            containerRadius: parent.radius
        }

        Rectangle {
            id: bubble
            visible: control.showValueLabel && opacity > 0
            opacity: control.pressed ? 1 : 0
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.top
            anchors.bottomMargin: 4
            width: Math.max(48, valueLabel.implicitWidth + 32)
            height: 44
            radius: height / 2
            color: ColorScope.scheme.inverseSurface

            Behavior on opacity {
                NumberAnimation {
                    duration: Theme.motion.fastEffects.duration
                }
            }

            Text {
                id: valueLabel
                anchors.centerIn: parent
                text: control.valueText
                font: Theme.type.labelLarge.font
                color: ColorScope.scheme.inverseOnSurface
            }
        }
    }
}
