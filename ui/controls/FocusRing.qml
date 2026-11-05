import QtQuick
import Cirrus.Theme

// MD3 keyboard focus indicator: 3 px Secondary outline, 2 px outside the container.
// Put it inside the container and pass the container's corner radii.
Rectangle {
    id: root

    property bool shown
    property real containerRadius
    property real containerTopLeftRadius: containerRadius
    property real containerTopRightRadius: containerRadius
    property real containerBottomRightRadius: containerRadius
    property real containerBottomLeftRadius: containerRadius

    // Gap between container edge and ring; negative draws the ring inside (list items).
    property real offset: 2
    readonly property real thickness: 3

    anchors.fill: parent
    anchors.margins: -(offset + thickness)
    color: "transparent"
    border.width: thickness
    border.color: ColorScope.scheme.secondary
    topLeftRadius: Math.max(0, containerTopLeftRadius + offset + thickness)
    topRightRadius: Math.max(0, containerTopRightRadius + offset + thickness)
    bottomRightRadius: Math.max(0, containerBottomRightRadius + offset + thickness)
    bottomLeftRadius: Math.max(0, containerBottomLeftRadius + offset + thickness)
    visible: fade.value > 0
    opacity: fade.value
    Accessible.ignored: true

    Spring {
        id: fade
        target: root.shown ? 1 : 0
        spec: Theme.motion.fastEffects
    }
}
