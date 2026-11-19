pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Cirrus.Theme

// MD3E floating action button, the baseline size: 56 square, large corners, a 24 icon
// on a tonal container, lifted by a shadow (androidx FabBaselineTokens,
// FabPrimaryContainerTokens). `text` is required: the accessible name and the tooltip.
//   Fab { glyph: Symbols.lyrics; text: qsTr("歌词"); tone: Md.Tertiary }
T.Button {
    id: control

    // The container's role: Md.Primary, Md.Secondary or Md.Tertiary.
    property int tone: Md.Primary
    property string glyph
    property real iconFill: 0
    property bool showToolTip: true
    // The shadow at rest and under the pointer, in dp. Beside a floating toolbar the
    // spec lowers both by one level (level2, level3).
    property real restElevation: Theme.elevation.level3
    property real hoverElevation: Theme.elevation.level4

    readonly property color containerColor: tone === Md.Tertiary ? ColorScope.scheme.tertiaryContainer
                                          : tone === Md.Secondary ? ColorScope.scheme.secondaryContainer
                                                                  : ColorScope.scheme.primaryContainer
    readonly property color contentColor: tone === Md.Tertiary ? ColorScope.scheme.onTertiaryContainer
                                        : tone === Md.Secondary ? ColorScope.scheme.onSecondaryContainer
                                                                : ColorScope.scheme.onPrimaryContainer

    implicitWidth: 56
    implicitHeight: 56
    padding: 0
    display: T.AbstractButton.IconOnly

    contentItem: Item {
        Icon {
            anchors.centerIn: parent
            glyph: control.glyph
            size: 24
            fill: control.iconFill
            color: control.contentColor
        }
    }

    background: ButtonContainer {
        implicitWidth: 56
        implicitHeight: 56
        button: control
        color: control.containerColor
        contentColor: control.contentColor
        elevation: control.hovered ? control.hoverElevation : control.restElevation
        outerRadius: Theme.shape.large
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
