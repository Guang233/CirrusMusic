pragma ComponentBehavior: Bound
import QtQuick
import M3Shapes
import Cirrus.Theme

// MD3E loading indicator: a shape that morphs through seven MaterialShapes every
// 650 ms while rotating. `contained` adds the PrimaryContainer circle behind it.
// With reduced motion it only rotates slowly.
Item {
    id: root

    property bool running: visible
    property bool contained: false
    property color color: contained ? Theme.color.onPrimaryContainer : Theme.color.primary
    property color containerColor: Theme.color.primaryContainer

    readonly property list<int> sequence: [MaterialShape.SoftBurst, MaterialShape.Cookie9Sided,
        MaterialShape.Pentagon, MaterialShape.Pill, MaterialShape.Sunny,
        MaterialShape.Cookie4Sided, MaterialShape.Oval]
    property int step: 1 // index of the shape being morphed to
    property real morphAngle: 0
    property real globalAngle: 0

    implicitWidth: 48
    implicitHeight: 48
    Accessible.role: Accessible.Animation
    Accessible.name: qsTr("正在加载")

    Spring {
        id: morph
        target: 1
        spec: Theme.motion.custom(0.6, 200, false)
    }

    Timer {
        interval: 650
        repeat: true
        running: root.running && !Theme.motion.reduced
        onTriggered: {
            // Order keeps the outline continuous: the new "from" equals the shape on screen.
            root.step = (root.step + 1) % root.sequence.length;
            shape.fromShape = shape.toShape;
            morph.snapTo(0);
            root.morphAngle = (root.morphAngle + 90) % 360;
            shape.toShape = root.sequence[root.step];
            morph.target = 1;
        }
    }

    FrameAnimation {
        running: root.running
        onTriggered: root.globalAngle = (root.globalAngle + frameTime * 360
                                         / (Theme.motion.reduced ? 9.332 : 4.666)) % 360
    }

    Rectangle {
        anchors.fill: parent
        visible: root.contained
        radius: width / 2
        color: root.containerColor
    }

    MorphShape {
        id: shape
        anchors.centerIn: parent
        width: Math.min(parent.width, parent.height) * 38 / 48
        height: width
        manual: true
        fromShape: root.sequence[0]
        toShape: root.sequence[1]
        progress: morph.value
        color: root.color
        fallbackRadius: width / 4
        rotation: morph.value * 90 + root.morphAngle + root.globalAngle
    }
}
