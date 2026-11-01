import QtQuick
import Cirrus.Theme

// Mouse-wheel scrolling for a Flickable (controls.md §8e): `step` px a notch,
// eased instead of jumping, notches in quick succession adding up; past either end
// the content overshoots a little and springs back. Touchpads (pixel deltas) are
// left to the Flickable. Declare it in the Flickable and point it there:
//   ListView { id: list; WheelScroller { flickable: list } }
WheelHandler {
    id: root

    required property Flickable flickable
    property real step: 120
    // How far the content may run past an end before springing back.
    property real overshoot: 48

    readonly property real minY: flickable.originY - flickable.topMargin
    readonly property real maxY: Math.max(minY, flickable.originY + flickable.contentHeight
                                                + flickable.bottomMargin - flickable.height)
    // Where the eased scroll is heading; past an end it springs back once there.
    property real goal: flickable.contentY
    property bool bounce: false

    parent: flickable
    target: null
    acceptedDevices: PointerDevice.Mouse
    orientation: Qt.Vertical

    onWheel: event => {
        if (event.pixelDelta.y !== 0 || event.angleDelta.y === 0) {
            event.accepted = false;
            return;
        }
        const from = root.scroll.running ? goal : flickable.contentY;
        const wanted = from - event.angleDelta.y / 120 * step;
        const atEnd = wanted < minY || wanted > maxY;
        goal = wanted < minY ? minY - Math.min(overshoot, (minY - wanted) / 3)
             : wanted > maxY ? maxY + Math.min(overshoot, (wanted - maxY) / 3)
             : wanted;
        if (Theme.motion.reduced) {
            flickable.contentY = Math.max(minY, Math.min(maxY, wanted));
            return;
        }
        root.scroll.stop();
        root.scroll.to = goal;
        bounce = atEnd;
        root.scroll.start();
    }

    // A handler has no children: the animation is a property.
    readonly property NumberAnimation scroll: NumberAnimation {
        target: root.flickable
        property: "contentY"
        duration: Theme.motion.standardFastSpatial.duration
        easing.type: Easing.OutCubic
        onFinished: {
            if (root.bounce)
                root.flickable.returnToBounds();
        }
    }
}
