import QtQuick
import QtQuick.Templates as T
import Cirrus.Theme

// Shared background of Button and IconButton: shadow, container, state layer and
// focus ring. Corners are `outerRadius` except the inner side of a connected group.
Item {
    id: root

    required property T.AbstractButton button
    property color color
    property color contentColor
    property real outlineWidth
    property color outlineColor
    property real elevation
    property real outerRadius
    property real innerRadius: outerRadius
    property int groupPosition: Md.Alone

    readonly property bool innerLeft: groupPosition === Md.Middle || groupPosition === Md.Last
    readonly property bool innerRight: groupPosition === Md.Middle || groupPosition === Md.First
    readonly property real tl: innerLeft ? innerRadius : outerRadius
    readonly property real bl: innerLeft ? innerRadius : outerRadius
    readonly property real tr: innerRight ? innerRadius : outerRadius
    readonly property real br: innerRight ? innerRadius : outerRadius

    Elevation {
        anchors.fill: parent
        level: root.elevation
        topLeftRadius: root.tl
        topRightRadius: root.tr
        bottomRightRadius: root.br
        bottomLeftRadius: root.bl
    }

    Rectangle {
        anchors.fill: parent
        color: root.color
        border.width: root.outlineWidth
        border.color: root.outlineColor
        topLeftRadius: root.tl
        topRightRadius: root.tr
        bottomRightRadius: root.br
        bottomLeftRadius: root.bl

        StateLayer {
            anchors.fill: parent
            color: root.contentColor
            hovered: root.button.hovered && root.button.enabled
            pressed: root.button.down
            focused: root.button.visualFocus
            pressX: root.button.pressed ? root.button.pressX : width / 2
            pressY: root.button.pressed ? root.button.pressY : height / 2
            topLeftRadius: root.tl
            topRightRadius: root.tr
            bottomRightRadius: root.br
            bottomLeftRadius: root.bl
        }

        FocusRing {
            shown: root.button.visualFocus
            containerTopLeftRadius: root.tl
            containerTopRightRadius: root.tr
            containerBottomRightRadius: root.br
            containerBottomLeftRadius: root.bl
        }
    }
}
