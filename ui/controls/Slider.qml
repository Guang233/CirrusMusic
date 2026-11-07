pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Cirrus.Theme

// MD3E slider (horizontal): a bar handle with gaps to both track halves and a stop
// dot at the end. `showValueLabel` shows `valueText` above the handle while dragging.
// `wavy` draws the part left of the handle as the MD3E wave (WaveLine), flowing while
// `waving`; with a thin track and a short handle it makes a seek bar.
//   Slider { from: 0; to: 100; value: 40; showValueLabel: true }
//   Slider { trackHeight: 4; handleHeight: 20; wavy: true; waving: playing }
T.Slider {
    id: control

    property real trackHeight: 16
    property real handleHeight: 44
    property bool wavy: false
    property bool waving: true
    property real waveAmplitude: 3
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
        implicitHeight: control.wavy ? control.trackHeight + 2 * control.waveAmplitude
                                     : control.trackHeight
        x: control.leftPadding
        y: control.topPadding + (control.availableHeight - height) / 2
        width: control.availableWidth
        height: implicitHeight

        // Active part, left of the handle.
        Rectangle {
            readonly property real end: control.handleX - control.leftPadding - control.gap
            visible: end > 0 && !control.wavy
            y: (parent.height - height) / 2
            width: Math.max(0, end)
            height: control.trackHeight
            color: control.activeColor
            topLeftRadius: height / 2
            bottomLeftRadius: height / 2
            topRightRadius: Math.min(control.innerRadius, width / 2)
            bottomRightRadius: Math.min(control.innerRadius, width / 2)
        }

        WaveLine {
            readonly property real end: control.handleX - control.leftPadding - control.gap
            visible: control.wavy && end > 0
            width: Math.max(control.trackHeight, end)
            height: parent.height
            color: control.activeColor
            thickness: control.trackHeight
            waving: control.waving
            amplitude: control.waveAmplitude
        }

        // Inactive part, right of the handle.
        Rectangle {
            readonly property real start: control.handleX - control.leftPadding + control.gap
            visible: start < parent.width
            x: start
            y: (parent.height - height) / 2
            width: Math.max(0, parent.width - start)
            height: control.trackHeight
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
        implicitHeight: control.handleHeight
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

    // A hand pointer wherever the control shows hover (controls.md §2).
    HoverHandler {
        enabled: control.hoverEnabled
        cursorShape: Qt.PointingHandCursor
    }
}
