pragma ComponentBehavior: Bound
import QtQuick
import M3Shapes
import Cirrus.Theme

// MD3E shape (m3shapes MaterialShape). Changing `shape` morphs to it along `spec`;
// for manual control set `manual` and drive `fromShape`/`toShape`/`progress`.
// Under the software renderer it draws a rounded rectangle instead.
Item {
    id: root

    property int shape: MaterialShape.Circle
    property bool manual: false
    property int fromShape: MaterialShape.Circle
    property int toShape: MaterialShape.Circle
    property real progress: 1
    property color color: ColorScope.scheme.primary
    property springSpec spec: Theme.motion.defaultSpatial
    // Corner radius of the software fallback; circles are always fully round.
    property real fallbackRadius: Theme.shape.large

    readonly property bool shaderSupported: GraphicsInfo.api !== GraphicsInfo.Software
                                            && GraphicsInfo.api !== GraphicsInfo.Unknown

    implicitWidth: 48
    implicitHeight: 48
    Accessible.ignored: true

    Loader {
        anchors.fill: parent
        active: root.shaderSupported
        sourceComponent: root.manual ? manualShape : autoShape
    }

    Component {
        id: autoShape
        MaterialShape {
            shape: root.shape
            color: root.color
            animationDuration: Theme.motion.reduced && root.spec.spatial ? 1 : root.spec.duration
            animationEasing: root.spec.easing
        }
    }

    Component {
        id: manualShape
        MaterialShape {
            fromShape: root.fromShape
            toShape: root.toShape
            morphProgress: Math.max(0, Math.min(1, root.progress))
            color: root.color
        }
    }

    Rectangle {
        anchors.fill: parent
        visible: !root.shaderSupported
        color: root.color
        radius: (root.manual ? root.toShape : root.shape) === MaterialShape.Circle
                ? Math.min(width, height) / 2 : root.fallbackRadius
    }
}
