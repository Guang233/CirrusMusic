pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Cirrus.Theme

// MD3E linear progress: 4 px, a 4 px gap between indicator and track, and a stop dot
// at the end when determinate. Indeterminate runs two sliding segments.
T.ProgressBar {
    id: control

    property real thickness: 4
    readonly property real gap: 4
    readonly property color activeColor: enabled ? Theme.color.primary
                                                 : Qt.alpha(Theme.color.onSurface,
                                                            Theme.state.disabledContent)
    readonly property color trackColor: enabled ? Theme.color.secondaryContainer
                                                : Qt.alpha(Theme.color.onSurface,
                                                           Theme.state.disabledContainer)

    // Indeterminate cycle time in ms; segments follow the MD3 keyframes.
    property real elapsed: 0
    readonly property real cycle: elapsed % 1750

    function segment(delay: real, duration: real, x1: real, x2: real): real {
        const t = Math.max(0, Math.min(1, (cycle - delay) / duration));
        return bezier(x1, x2, t);
    }
    // y of a CSS-style cubic-bezier(x1, 0, x2, 1) at time t, solved by bisection.
    function bezier(x1: real, x2: real, t: real): real {
        let lo = 0, hi = 1, s = t;
        for (let i = 0; i < 20; ++i) {
            s = (lo + hi) / 2;
            const x = 3 * (1 - s) * (1 - s) * s * x1 + 3 * (1 - s) * s * s * x2 + s * s * s;
            if (x < t)
                lo = s;
            else
                hi = s;
        }
        return 3 * (1 - s) * s * s + s * s * s;
    }

    // Active intervals as [start, end] in px.
    readonly property var activeIntervals: {
        const w = availableWidth;
        if (!indeterminate)
            return smooth.value * w > 0.5 ? [[0, smooth.value * w]] : [];
        const firstHead = segment(0, 750, 0.2, 0.8) * w;
        const firstTail = segment(333, 850, 0.4, 1) * w;
        const secondHead = segment(1000, 567, 0, 0.65) * w;
        const secondTail = segment(1267, 533, 0.1, 0.45) * w;
        return [[secondTail, secondHead], [firstTail, firstHead]].filter(i => i[1] - i[0] > 0.5);
    }
    // Track pieces: the complement of the active intervals, minus the gaps.
    readonly property var trackPieces: {
        const w = availableWidth;
        const pieces = [];
        let x = 0;
        for (const i of activeIntervals) {
            if (i[0] - gap - x > 0.5)
                pieces.push([x, i[0] - gap]);
            x = i[1] + gap;
        }
        if (w - x > 0.5)
            pieces.push([x, w]);
        while (pieces.length < 3)
            pieces.push([0, 0]);
        return pieces;
    }

    implicitWidth: 240
    implicitHeight: thickness
    padding: 0

    Spring {
        id: smooth
        target: control.position
        spec: Theme.motion.defaultEffects
        threshold: 0.001
    }

    FrameAnimation {
        running: control.indeterminate && control.visible
        onTriggered: control.elapsed += frameTime * 1000
    }

    contentItem: Item {
        implicitWidth: 240
        implicitHeight: control.thickness

        Repeater {
            model: 3
            delegate: Rectangle {
                required property int index
                readonly property var piece: control.trackPieces[index]
                x: piece[0]
                width: piece[1] - piece[0]
                height: parent.height
                visible: width > 0
                radius: height / 2
                color: control.trackColor
            }
        }
        Repeater {
            model: 2
            delegate: Rectangle {
                required property int index
                readonly property var interval: control.activeIntervals[index] ?? [0, 0]
                x: interval[0]
                width: interval[1] - interval[0]
                height: parent.height
                visible: width > 0.5
                radius: height / 2
                color: control.activeColor
            }
        }
        // Stop indicator.
        Rectangle {
            visible: !control.indeterminate && smooth.value < 0.999
            x: parent.width - width
            width: control.thickness
            height: control.thickness
            radius: width / 2
            color: control.activeColor
        }
    }
}
