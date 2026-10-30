pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Cirrus.Theme

// MD3E icon button. `text` is required: it is the accessible name and the tooltip.
//   IconButton { glyph: Symbols.favorite; text: qsTr("收藏"); checkable: true }
T.Button {
    id: control

    property int variant: Md.Standard
    property int size: Md.Small
    property int widthMode: Md.Default
    property int shape: Md.Round
    property string glyph
    // Icon FILL axis when not checkable; checkable buttons go from 0 to 1 when checked.
    property real iconFill: 0
    property bool showToolTip: true
    // Set by ConnectedButtonGroup / StandardButtonGroup.
    property int groupPosition: Md.Alone
    property real widthDelta: 0

    readonly property int sizeIndex: Math.max(0, Math.min(4, size))
    readonly property real containerHeight: [32, 40, 56, 96, 136][sizeIndex]
    readonly property real iconSize: [20, 24, 24, 32, 40][sizeIndex]
    readonly property real sidePadding: [[4, 6, 10], [4, 8, 14], [12, 16, 24], [16, 32, 48],
        [32, 48, 72]][sizeIndex][Math.max(0, Math.min(2, widthMode))]
    readonly property real squareRadius: [12, 12, 16, 28, 28][sizeIndex]
    readonly property real pressedRadius: [8, 8, 12, 16, 16][sizeIndex]
    readonly property real outlineWidth: [1, 1, 1, 2, 3][sizeIndex]
    readonly property real naturalWidth: iconSize + 2 * sidePadding
    // A group neighbour gives up at most this much width, keeping 4 px beside the icon.
    readonly property real shrinkLimit: Math.max(0, 2 * (sidePadding - 4))

    readonly property bool toggled: checkable && checked
    readonly property real fullRadius: containerHeight / 2
    readonly property real restRadius: (shape === Md.Round) !== toggled ? fullRadius : squareRadius
    readonly property bool grouped: groupPosition !== Md.Alone
    readonly property real outerTarget: grouped ? fullRadius : down ? pressedRadius : restRadius
    readonly property real innerTarget: grouped ? (toggled ? fullRadius : down ? 4 : 8)
                                                : outerTarget

    readonly property color containerColor: {
        if (!enabled)
            return variant === Md.Standard || variant === Md.Outlined && !toggled
                    ? "transparent" : Qt.alpha(Theme.color.onSurface, Theme.state.disabledContainer);
        switch (variant) {
        case Md.Filled:
            return checkable ? Theme.mix(Theme.color.surfaceContainer, Theme.color.primary,
                                         selection.value)
                             : Theme.color.primary;
        case Md.Tonal:
            return Theme.mix(Theme.color.secondaryContainer, Theme.color.secondary, selection.value);
        case Md.Outlined:
            return Theme.mix(Qt.alpha(Theme.color.inverseSurface, 0), Theme.color.inverseSurface,
                             selection.value);
        default:
            return "transparent";
        }
    }
    readonly property color contentColor: {
        if (!enabled)
            return Qt.alpha(Theme.color.onSurface, Theme.state.disabledContent);
        switch (variant) {
        case Md.Filled:
            return checkable ? Theme.mix(Theme.color.onSurfaceVariant, Theme.color.onPrimary,
                                         selection.value)
                             : Theme.color.onPrimary;
        case Md.Tonal:
            return Theme.mix(Theme.color.onSecondaryContainer, Theme.color.onSecondary,
                             selection.value);
        case Md.Outlined:
            return Theme.mix(Theme.color.onSurfaceVariant, Theme.color.inverseOnSurface,
                             selection.value);
        default:
            return Theme.mix(Theme.color.onSurfaceVariant, Theme.color.primary, selection.value);
        }
    }

    implicitWidth: naturalWidth + widthSpring.value
    implicitHeight: containerHeight
    padding: 0
    display: T.AbstractButton.IconOnly

    Spring {
        id: selection
        target: control.toggled ? 1 : 0
        spec: Theme.motion.defaultEffects
    }
    Spring {
        id: outerCorner
        target: control.outerTarget
        spec: control.checkable ? Theme.motion.fastSpatial : Theme.motion.defaultEffects
        threshold: 0.1
    }
    Spring {
        id: innerCorner
        target: control.innerTarget
        spec: control.checkable ? Theme.motion.fastSpatial : Theme.motion.defaultEffects
        threshold: 0.1
    }
    Spring {
        id: widthSpring
        target: control.widthDelta
        spec: Theme.motion.fastSpatial
        threshold: 0.1
    }

    contentItem: Item {
        Icon {
            anchors.centerIn: parent
            glyph: control.glyph
            size: control.iconSize
            fill: control.checkable ? selection.value : control.iconFill
            color: control.contentColor
        }
    }

    background: ButtonContainer {
        implicitWidth: control.naturalWidth
        implicitHeight: control.containerHeight
        button: control
        color: control.containerColor
        contentColor: control.contentColor
        outlineWidth: control.variant === Md.Outlined ? control.outlineWidth : 0
        outlineColor: Theme.mix(Theme.color.outlineVariant,
                                Qt.alpha(Theme.color.outlineVariant, 0), selection.value)
        outerRadius: Math.max(0, outerCorner.value)
        innerRadius: Math.max(0, innerCorner.value)
        groupPosition: control.groupPosition
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
}
