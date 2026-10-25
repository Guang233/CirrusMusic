pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Shapes
import QtQuick.Templates as T
import Cirrus.Theme

// MD3E circular progress (T.ProgressBar semantics): 40 px, 4 px stroke, a gap between
// indicator and track. Indeterminate is a rotating arc that grows and shrinks.
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

    property real elapsed: 0
    readonly property real arcRadius: (Math.min(availableWidth, availableHeight) - thickness) / 2
    // Angle covered by the gap plus both round caps.
    readonly property real gapAngle: arcRadius > 0 ? (gap + thickness) / arcRadius * 180 / Math.PI
                                                   : 0

    // Indeterminate: each 1333 ms the head runs 270° ahead, then the tail catches up.
    readonly property int cycleIndex: Math.floor(elapsed / 1333)
    readonly property real cycleTime: (elapsed % 1333) / 1333
    function easeInOut(t: real): real {
        t = Math.max(0, Math.min(1, t));
        return t < 0.5 ? 4 * t * t * t : 1 - Math.pow(-2 * t + 2, 3) / 2;
    }
    readonly property real spinHead: cycleIndex * 270 + easeInOut(cycleTime * 2) * 270
    readonly property real spinTail: cycleIndex * 270 + easeInOut(cycleTime * 2 - 1) * 270
    readonly property real spinRotation: elapsed / 1568 * 360

    readonly property real activeStart: indeterminate ? spinTail + spinRotation - 90 : -90
    readonly property real activeSweep: indeterminate ? Math.max(8, spinHead - spinTail)
                                                      : smooth.value * 360

    implicitWidth: 40
    implicitHeight: 40
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

    contentItem: Shape {
        implicitWidth: 40
        implicitHeight: 40
        preferredRendererType: Shape.CurveRenderer

        // Track: the rest of the circle, determinate only.
        ShapePath {
            strokeColor: !control.indeterminate && control.activeSweep < 360 - control.gapAngle
                         ? control.trackColor : "transparent"
            strokeWidth: control.thickness
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            PathAngleArc {
                centerX: control.availableWidth / 2
                centerY: control.availableHeight / 2
                radiusX: control.arcRadius
                radiusY: control.arcRadius
                startAngle: -90 + Math.max(control.activeSweep, 0.01) + control.gapAngle
                sweepAngle: Math.max(0.01, 360 - Math.max(control.activeSweep, 0.01)
                                     - 2 * control.gapAngle)
            }
        }
        ShapePath {
            strokeColor: control.activeSweep > 0.5 ? control.activeColor : "transparent"
            strokeWidth: control.thickness
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            PathAngleArc {
                centerX: control.availableWidth / 2
                centerY: control.availableHeight / 2
                radiusX: control.arcRadius
                radiusY: control.arcRadius
                startAngle: control.activeStart
                sweepAngle: Math.max(0.01, control.activeSweep)
            }
        }
    }
}
