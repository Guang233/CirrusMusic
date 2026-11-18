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
    // The icon grows and springs back when a press fills it (a like, expressive.md §5).
    property bool pops: false
    // Standard icon buttons on InverseSurface (a snackbar's close): InverseOnSurface.
    property bool inverse: false
    // Standard icon buttons on a background of their own (the player page): this color
    // for the icon and the state layer, when set.
    property color tint: "transparent"
    // Standard icon buttons with a container of the page's own tone (the player page's
    // Material style): this color unselected; selected, Primary as a filled toggle.
    property color containerTint: "transparent"
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
            return variant === Md.Standard && containerTint.a > 0 ? Qt.alpha(containerTint, containerTint.a * 0.5)
                 : variant === Md.Standard || variant === Md.Outlined && !toggled
                    ? "transparent" : Qt.alpha(ColorScope.scheme.onSurface, Theme.state.disabledContainer);
        switch (variant) {
        case Md.Filled:
            return checkable ? Theme.mix(ColorScope.scheme.surfaceContainer, ColorScope.scheme.primary,
                                         selection.value)
                             : ColorScope.scheme.primary;
        case Md.Tonal:
            return Theme.mix(ColorScope.scheme.secondaryContainer, ColorScope.scheme.secondary, selection.value);
        case Md.Outlined:
            return Theme.mix(Qt.alpha(ColorScope.scheme.inverseSurface, 0), ColorScope.scheme.inverseSurface,
                             selection.value);
        default:
            return containerTint.a > 0 ? Theme.mix(containerTint, ColorScope.scheme.primary, selection.value)
                                       : "transparent";
        }
    }
    readonly property color contentColor: {
        if (!enabled)
            return Qt.alpha(variant === Md.Standard && tint.a > 0 ? tint : ColorScope.scheme.onSurface,
                            Theme.state.disabledContent);
        switch (variant) {
        case Md.Filled:
            return checkable ? Theme.mix(ColorScope.scheme.onSurfaceVariant, ColorScope.scheme.onPrimary,
                                         selection.value)
                             : ColorScope.scheme.onPrimary;
        case Md.Tonal:
            return Theme.mix(ColorScope.scheme.onSecondaryContainer, ColorScope.scheme.onSecondary,
                             selection.value);
        case Md.Outlined:
            return Theme.mix(ColorScope.scheme.onSurfaceVariant, ColorScope.scheme.inverseOnSurface,
                             selection.value);
        default:
            return inverse ? ColorScope.scheme.inverseOnSurface
                 : containerTint.a > 0 ? Theme.mix(tint.a > 0 ? tint : ColorScope.scheme.onSurfaceVariant,
                                                   ColorScope.scheme.onPrimary, selection.value)
                 : tint.a > 0 ? tint
                           : Theme.mix(ColorScope.scheme.onSurfaceVariant, ColorScope.scheme.primary,
                                       selection.value);
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
    Pop {
        id: pop
    }
    onPressedChanged: {
        if (pops && pressed)
            pop.arm();
    }
    onIconFillChanged: {
        if (pops && iconFill >= 1)
            pop.filled();
    }

    // A non-checkable icon fills and empties smoothly too (a like turning on).
    Spring {
        id: fillSpring
        target: control.iconFill
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
            scale: pop.scale
            fill: control.checkable ? selection.value : fillSpring.value
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
        outlineColor: Theme.mix(ColorScope.scheme.outlineVariant,
                                Qt.alpha(ColorScope.scheme.outlineVariant, 0), selection.value)
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

    // A hand pointer wherever the control shows hover (controls.md §2).
    HoverHandler {
        enabled: control.hoverEnabled
        cursorShape: Qt.PointingHandCursor
    }
}
