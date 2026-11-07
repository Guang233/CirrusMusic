pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Cirrus.Theme

// Destination of a NavigationRail (androidx WideNavigationRailItem). Collapsed: icon
// in a 56×32 indicator with the label below. Expanded: icon and label side by side
// in a 56 tall indicator. `progress` (0 collapsed, 1 expanded) is set by the rail
// and morphs one layout into the other (docs/ui/motion-choreography.md §5).
//   NavigationRailItem { glyph: Symbols.home; text: qsTr("首页") }
T.AbstractButton {
    id: control

    property string glyph
    // Set by NavigationRail.
    property Item rail
    property real progress
    property int railIndex: -1
    property bool selected

    // Collapsed (top icon) and expanded (start icon) geometry, androidx tokens.
    readonly property real sidePadding: 20
    readonly property real collapsedIndicatorWidth: 56
    readonly property real expandedIndicatorWidth: 16 + 24 + 8 + Math.ceil(sideLabel.implicitWidth) + 16
    // Width the expanded rail needs for this item.
    readonly property real expandedWidth: sidePadding + expandedIndicatorWidth + sidePadding

    readonly property color iconColor: !enabled
        ? Qt.alpha(ColorScope.scheme.onSurface, Theme.state.disabledContent)
        : Theme.mix(ColorScope.scheme.onSurfaceVariant, ColorScope.scheme.onSecondaryContainer, selection.value)

    implicitWidth: Theme.motion.lerp(96, expandedWidth, progress)
    implicitHeight: Theme.motion.lerp(64, 56, progress)
    focusPolicy: Qt.TabFocus
    Accessible.role: Accessible.PageTab
    Accessible.name: text
    Accessible.checkable: true
    Accessible.checked: selected

    Spring {
        id: selection
        target: control.selected ? 1 : 0
        spec: Theme.motion.defaultEffects
    }
    // The indicator grows out from its center when selected (androidx: fast spatial).
    Spring {
        id: indicatorScale
        target: control.selected ? 1 : 0
        spec: Theme.motion.fastSpatial
        threshold: 0.002
    }

    contentItem: Item {}

    background: Item {
        Item {
            id: indicatorArea
            x: control.sidePadding
            y: Theme.motion.lerp(6, 0, control.progress)
            width: Theme.motion.lerp(control.collapsedIndicatorWidth,
                                     control.expandedIndicatorWidth, control.progress)
            height: Theme.motion.lerp(32, 56, control.progress)

            Rectangle {
                anchors.centerIn: parent
                width: parent.width * Math.max(0, indicatorScale.value)
                height: parent.height
                radius: height / 2
                visible: indicatorScale.value > 0.001
                color: ColorScope.scheme.secondaryContainer
                opacity: selection.value
            }
            StateLayer {
                anchors.fill: parent
                radius: height / 2
                color: ColorScope.scheme.onSecondaryContainer
                hovered: control.hovered && control.enabled
                pressed: control.down
                focused: control.visualFocus
                pressX: control.pressed ? control.pressX - indicatorArea.x : width / 2
                pressY: control.pressed ? control.pressY - indicatorArea.y : height / 2
            }
            FocusRing {
                shown: control.visualFocus
                containerTopLeftRadius: indicatorArea.height / 2
                containerTopRightRadius: indicatorArea.height / 2
                containerBottomRightRadius: indicatorArea.height / 2
                containerBottomLeftRadius: indicatorArea.height / 2
            }
        }

        Icon {
            x: control.sidePadding + 16
            y: Theme.motion.lerp(10, 16, control.progress)
            glyph: control.glyph
            size: 24
            fill: selection.value
            color: control.iconColor
        }

        // Below the icon while collapsed; fades out early in the expansion.
        Text {
            x: control.sidePadding + (control.collapsedIndicatorWidth - width) / 2
            y: 6 + 32 + 4
            width: Math.min(implicitWidth, control.width - 8)
            opacity: 1 - Theme.motion.segment(control.progress, 0, 0.3)
            visible: opacity > 0
            text: control.text
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
            font: Theme.type.labelMedium.font
            color: !control.enabled ? control.iconColor
                                    : Theme.mix(ColorScope.scheme.onSurfaceVariant, ColorScope.scheme.secondary,
                                                selection.value)
        }

        // Beside the icon while expanded; fades in late.
        Text {
            id: sideLabel
            x: control.sidePadding + 16 + 24 + 8
            y: (control.height - height) / 2
            width: Math.max(0, Math.min(implicitWidth, control.width - x - control.sidePadding - 16))
            opacity: Theme.motion.segment(control.progress, 0.6, 1)
            visible: opacity > 0
            text: control.text
            elide: Text.ElideRight
            font: Theme.type.labelLarge.font
            color: control.iconColor
        }
    }

    // A hand pointer wherever the control shows hover (controls.md §2).
    HoverHandler {
        enabled: control.hoverEnabled
        cursorShape: Qt.PointingHandCursor
    }
}
