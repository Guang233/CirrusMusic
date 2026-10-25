pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Cirrus.Theme

// MD3E common button. `checkable` makes it the MD3E toggle button.
//   Button { text: qsTr("播放"); glyph: Symbols.play_arrow; variant: Md.Filled; size: Md.Medium }
T.Button {
    id: control

    property int variant: Md.Filled
    property int size: Md.Small
    property int shape: Md.Round
    property string glyph
    property string trailingGlyph
    // Set by ConnectedButtonGroup / StandardButtonGroup.
    property int groupPosition: Md.Alone
    property real widthDelta: 0

    readonly property int sizeIndex: Math.max(0, Math.min(4, size))
    readonly property real containerHeight: [32, 40, 56, 96, 136][sizeIndex]
    readonly property real sidePadding: [12, 16, 24, 48, 64][sizeIndex]
    readonly property real iconSize: [20, 20, 24, 32, 40][sizeIndex]
    readonly property real iconGap: [4, 8, 8, 12, 16][sizeIndex]
    readonly property real squareRadius: [12, 12, 16, 28, 28][sizeIndex]
    readonly property real pressedRadius: [8, 8, 12, 16, 16][sizeIndex]
    readonly property real outlineWidth: [1, 1, 1, 2, 3][sizeIndex]
    readonly property typeStyle labelStyle: [Theme.type.labelLarge, Theme.type.labelLarge,
        Theme.type.titleMedium, Theme.type.headlineSmall, Theme.type.headlineLarge][sizeIndex]
    readonly property real naturalWidth: Math.max(containerHeight,
                                                  implicitContentWidth + leftPadding + rightPadding)

    readonly property bool toggled: checkable && checked
    readonly property real fullRadius: containerHeight / 2
    readonly property real restRadius: (shape === Md.Round) !== toggled ? fullRadius : squareRadius
    readonly property bool grouped: groupPosition !== Md.Alone
    readonly property real outerTarget: grouped ? fullRadius : down ? pressedRadius : restRadius
    readonly property real innerTarget: grouped ? (toggled ? fullRadius : down ? 4 : 8)
                                                : outerTarget

    readonly property color containerColor: {
        if (!enabled)
            return variant === Md.Outlined && !toggled || variant === Md.Text
                    ? "transparent" : Qt.alpha(Theme.color.onSurface, Theme.state.disabledContainer);
        switch (variant) {
        case Md.Tonal:
            return Theme.mix(Theme.color.secondaryContainer, Theme.color.secondary, selection.value);
        case Md.Outlined:
            return Theme.mix(Qt.alpha(Theme.color.inverseSurface, 0), Theme.color.inverseSurface,
                             selection.value);
        case Md.Text:
            return "transparent";
        case Md.Elevated:
            return Theme.mix(Theme.color.surfaceContainerLow, Theme.color.primary, selection.value);
        default:
            return checkable ? Theme.mix(Theme.color.surfaceContainer, Theme.color.primary,
                                         selection.value)
                             : Theme.color.primary;
        }
    }
    readonly property color contentColor: {
        if (!enabled)
            return Qt.alpha(Theme.color.onSurface, Theme.state.disabledContent);
        switch (variant) {
        case Md.Tonal:
            return Theme.mix(Theme.color.onSecondaryContainer, Theme.color.onSecondary,
                             selection.value);
        case Md.Outlined:
            return Theme.mix(Theme.color.onSurfaceVariant, Theme.color.inverseOnSurface,
                             selection.value);
        case Md.Text:
            return Theme.color.primary;
        case Md.Elevated:
            return Theme.mix(Theme.color.primary, Theme.color.onPrimary, selection.value);
        default:
            return checkable ? Theme.mix(Theme.color.onSurfaceVariant, Theme.color.onPrimary,
                                         selection.value)
                             : Theme.color.onPrimary;
        }
    }

    implicitWidth: naturalWidth + widthSpring.value
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)
    leftPadding: sidePadding
    rightPadding: sidePadding
    topPadding: 0
    bottomPadding: 0
    spacing: iconGap
    font: labelStyle.font

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

    // Implicit width is the natural content; a narrower button elides the label and a
    // wider one (group expansion, explicit width) keeps the content centered.
    contentItem: Item {
        id: content

        readonly property real iconsWidth: (control.glyph !== "" ? control.iconSize + control.spacing : 0)
                                           + (control.trailingGlyph !== "" ? control.iconSize
                                                                            + control.spacing : 0)

        implicitWidth: {
            const parts = [control.glyph !== "" ? control.iconSize : -1,
                           control.text !== "" ? Math.ceil(label.implicitWidth) : -1,
                           control.trailingGlyph !== "" ? control.iconSize : -1].filter(w => w >= 0);
            return parts.reduce((sum, w) => sum + w, 0) + control.spacing * Math.max(0, parts.length - 1);
        }
        implicitHeight: Math.max(control.iconSize, label.implicitHeight)

        Row {
            anchors.centerIn: parent
            spacing: control.spacing

            Icon {
                anchors.verticalCenter: parent.verticalCenter
                visible: control.glyph !== ""
                glyph: control.glyph
                size: control.iconSize
                color: control.contentColor
            }
            Text {
                id: label
                anchors.verticalCenter: parent.verticalCenter
                visible: control.text !== ""
                width: Math.min(Math.ceil(implicitWidth),
                                Math.max(0, control.availableWidth - content.iconsWidth))
                text: control.text
                font: control.font
                color: control.contentColor
                elide: Text.ElideRight
            }
            Icon {
                anchors.verticalCenter: parent.verticalCenter
                visible: control.trailingGlyph !== ""
                glyph: control.trailingGlyph
                size: control.iconSize
                color: control.contentColor
            }
        }
    }

    background: ButtonContainer {
        implicitWidth: control.containerHeight
        implicitHeight: control.containerHeight
        button: control
        color: control.containerColor
        contentColor: control.contentColor
        outlineWidth: control.variant === Md.Outlined ? control.outlineWidth : 0
        outlineColor: Theme.mix(Theme.color.outlineVariant,
                                Qt.alpha(Theme.color.outlineVariant, 0), selection.value)
        elevation: !control.enabled ? 0
                   : control.variant === Md.Elevated ? (control.hovered ? Theme.elevation.level2
                                                                        : Theme.elevation.level1)
                   : (control.variant === Md.Filled || control.variant === Md.Tonal)
                     && control.hovered ? Theme.elevation.level1 : 0
        outerRadius: Math.max(0, outerCorner.value)
        innerRadius: Math.max(0, innerCorner.value)
        groupPosition: control.groupPosition
    }
}
