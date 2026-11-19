pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Cirrus.Theme

// MD3E slider (horizontal): a bar handle with gaps to both track halves and a stop
// dot at the end. `showValueLabel` shows `valueText` above the handle while dragging.
// `wavy` draws the part left of the handle as the MD3E wave (WaveLine), flowing while
// `waving`; with a thin track and a short handle it makes a seek bar. `trackColors`
// paints the whole track as a gradient of those colors instead (picking a color: hue,
// chroma); the handle is then OnSurface and there is no stop dot.
//   Slider { from: 0; to: 100; value: 40; showValueLabel: true }
//   Slider { from: 0; to: 360; trackColors: ["red", "yellow", "lime", "cyan", "blue", "magenta", "red"] }
// `roundHandle` makes the handle a dot at rest that stretches into the bar while
// pressed or focused (the seek bars of the players; other sliders keep the bar).
//   Slider { trackHeight: 4; handleHeight: 20; wavy: true; waving: playing; roundHandle: true }
T.Slider {
    id: control

    property real trackHeight: 16
    property real handleHeight: 44
    property bool roundHandle: false
    // The dot's diameter while `roundHandle` rests.
    property real dotSize: Math.max(12, trackHeight + 10)
    property bool wavy: false
    property bool waving: true
    property real waveAmplitude: 3
    property bool showValueLabel: false
    property string valueText: Math.round(value).toString()
    property list<color> trackColors
    // The rest of the track on a container of another role (the mini player on
    // PrimaryContainer): this color, when set.
    property color inactiveTint: "transparent"

    readonly property real gap: 6
    readonly property real innerRadius: 2
    readonly property real handleX: leftPadding + 2 + visualPosition * (availableWidth - 4)
    readonly property bool gradient: trackColors.length > 1
    readonly property color activeColor: gradient ? ColorScope.scheme.onSurface
                                       : enabled ? ColorScope.scheme.primary
                                                 : Qt.alpha(ColorScope.scheme.onSurface,
                                                            Theme.state.disabledContent)
    readonly property color inactiveColor: inactiveTint.a > 0 ? inactiveTint
                                         : enabled ? ColorScope.scheme.secondaryContainer
                                                   : Qt.alpha(ColorScope.scheme.onSurface,
                                                              Theme.state.disabledContainer)

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitHandleWidth + leftPadding + rightPadding, 200)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitHandleHeight + topPadding + bottomPadding)
    padding: 0
    orientation: Qt.Horizontal

    // 0 the dot, 1 the bar.
    Spring {
        id: barShape
        target: !control.roundHandle || control.pressed || control.visualFocus ? 1 : 0
        spec: Theme.motion.fastSpatial
        threshold: 0.01
    }
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

        // The gradient track, both halves, with the gaps around the handle.
        Canvas {
            id: gradientTrack
            readonly property real cut: control.handleX - control.leftPadding
            visible: control.gradient
            anchors.fill: parent
            opacity: control.enabled ? 1 : Theme.state.disabledContent
            onCutChanged: requestPaint()
            onWidthChanged: requestPaint()
            Connections {
                target: control
                function onTrackColorsChanged() {
                    gradientTrack.requestPaint();
                }
            }
            onPaint: {
                const ctx = getContext("2d");
                ctx.reset();
                if (!control.gradient)
                    return;
                const h = control.trackHeight, y = (height - h) / 2;
                const fill = ctx.createLinearGradient(0, 0, width, 0);
                const n = control.trackColors.length;
                for (let i = 0; i < n; ++i)
                    fill.addColorStop(i / (n - 1), control.trackColors[i]);
                ctx.fillStyle = fill;
                // A bar with outer (round) and inner (small) corners.
                const bar = (x0, x1, leftRound) => {
                    if (x1 - x0 <= 0)
                        return;
                    const r = Math.min(h / 2, (x1 - x0) / 2);
                    const ri = Math.min(control.innerRadius, (x1 - x0) / 2);
                    const rl = leftRound ? r : ri, rr = leftRound ? ri : r;
                    ctx.beginPath();
                    ctx.moveTo(x0 + rl, y);
                    ctx.lineTo(x1 - rr, y);
                    ctx.arcTo(x1, y, x1, y + rr, rr);
                    ctx.lineTo(x1, y + h - rr);
                    ctx.arcTo(x1, y + h, x1 - rr, y + h, rr);
                    ctx.lineTo(x0 + rl, y + h);
                    ctx.arcTo(x0, y + h, x0, y + h - rl, rl);
                    ctx.lineTo(x0, y + rl);
                    ctx.arcTo(x0, y, x0 + rl, y, rl);
                    ctx.closePath();
                    ctx.fill();
                };
                bar(0, cut - control.gap, true);
                bar(cut + control.gap, width, false);
            }
        }

        // Active part, left of the handle.
        Rectangle {
            readonly property real end: control.handleX - control.leftPadding - control.gap
            visible: end > 0 && !control.wavy && !control.gradient
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
            visible: control.wavy && end > 0 && !control.gradient
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
            visible: start < parent.width && !control.gradient
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
        width: Math.max(1, Theme.motion.lerp(control.dotSize, handleWidth.value, barShape.value))
        height: Theme.motion.lerp(control.dotSize, control.handleHeight, barShape.value)
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
