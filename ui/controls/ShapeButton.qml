pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import M3Shapes
import Cirrus.Theme

// A round action whose container is an MD3E shape instead of a circle: the one play
// button of a hero (personal FM). The container takes the role's own color (primary,
// secondary or tertiary), presses in a little, and turns slowly while `spinning`.
// `text` is required: the accessible name and the tooltip.
//   ShapeButton { glyph: Symbols.play_arrow; text: qsTr("播放"); tone: Md.Tertiary }
T.Button {
    id: control

    property int shape: MaterialShape.Cookie12Sided
    // The container's role: Md.Primary, Md.Secondary or Md.Tertiary.
    property int tone: Md.Primary
    property string glyph
    property real iconFill: 1
    property real size: 96
    property bool spinning: false
    property bool showToolTip: true

    readonly property color containerColor: tone === Md.Tertiary ? ColorScope.scheme.tertiary
                                          : tone === Md.Secondary ? ColorScope.scheme.secondary
                                                                  : ColorScope.scheme.primary
    readonly property color contentColor: tone === Md.Tertiary ? ColorScope.scheme.onTertiary
                                        : tone === Md.Secondary ? ColorScope.scheme.onSecondary
                                                                : ColorScope.scheme.onPrimary

    implicitWidth: size
    implicitHeight: size
    padding: 0
    display: T.AbstractButton.IconOnly

    Spring {
        id: press
        target: control.down ? 0.92 : 1
        spec: Theme.motion.fastSpatial
        threshold: 0.001
    }
    // One turn in 16 s while spinning; it stops where it is.
    NumberAnimation {
        target: container
        property: "rotation"
        from: container.rotation
        to: container.rotation + 360
        duration: 16000
        loops: Animation.Infinite
        running: control.spinning && control.visible && !Theme.motion.reduced
    }

    contentItem: Item {
        Icon {
            anchors.centerIn: parent
            glyph: control.glyph
            size: Math.round(control.size * 0.4)
            fill: control.iconFill
            color: control.contentColor
        }
    }

    background: Item {
        implicitWidth: control.size
        implicitHeight: control.size

        MorphShape {
            id: container
            anchors.fill: parent
            scale: press.value
            shape: control.shape
            color: control.containerColor
            fallbackRadius: width / 2
        }
        // The state layer and the focus ring are round, inside the shape's lobes.
        StateLayer {
            anchors.centerIn: parent
            width: Math.round(parent.width * 0.84)
            height: width
            radius: width / 2
            color: control.contentColor
            hovered: control.hovered && control.enabled
            pressed: control.down
            focused: control.visualFocus
            pressX: control.pressed ? control.pressX - x : width / 2
            pressY: control.pressed ? control.pressY - y : height / 2
        }
        FocusRing {
            shown: control.visualFocus
            containerRadius: width / 2
        }
    }

    // The tooltip is created on first hover and kept, so it can fade out.
    property bool hoveredOnce: false
    onHoveredChanged: if (hovered)
                          hoveredOnce = true

    Loader {
        active: control.showToolTip && control.text !== "" && control.hoveredOnce
        sourceComponent: ToolTip {
            parent: control
            text: control.text
            visible: control.hovered && !control.pressed
        }
    }

    // A hand pointer wherever the control shows hover (controls.md §2).
    HoverHandler {
        enabled: control.hoverEnabled
        cursorShape: Qt.PointingHandCursor
    }
}
