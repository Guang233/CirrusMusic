pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Cirrus.Theme

// MD3E menu item: 44 px, 4 px corners (12 px on the menu's outer ends), selected
// (checked) items use TertiaryContainer. `glyph` is the leading icon.
T.MenuItem {
    id: control

    property string glyph
    property string trailingText
    // A second line under the label (bodySmall), e.g. what a choice means; the item
    // grows to 60 px.
    property string supportingText

    readonly property bool first: menu ? menu.count > 0 && menu.itemAt(0) === control : false
    readonly property bool last: menu ? menu.count > 0 && menu.itemAt(menu.count - 1) === control
                                      : false
    readonly property bool selected: checkable && checked
    readonly property color contentColor: !enabled
        ? Qt.alpha(ColorScope.scheme.onSurface, Theme.state.disabledContent)
        : Theme.mix(ColorScope.scheme.onSurface, ColorScope.scheme.onTertiaryContainer, selection.value)
    readonly property color mutedColor: !enabled
        ? Qt.alpha(ColorScope.scheme.onSurface, Theme.state.disabledContent)
        : Theme.mix(ColorScope.scheme.onSurfaceVariant, ColorScope.scheme.onTertiaryContainer,
                    selection.value)
    readonly property string leadingGlyph: glyph !== "" ? glyph : selected ? Symbols.check : ""
    readonly property string shortcutText: action && action.shortcut ? String(action.shortcut)
                                                                     : trailingText

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: supportingText !== "" ? 60 : 44
    leftPadding: 12
    rightPadding: 12
    spacing: 12
    font: Theme.type.bodyLarge.font

    Spring {
        id: selection
        target: control.selected ? 1 : 0
        spec: Theme.motion.defaultEffects
    }

    contentItem: Item {
        implicitWidth: (control.leadingGlyph !== "" ? 20 + control.spacing : 0)
                       + Math.ceil(Math.max(label.implicitWidth, support.implicitWidth))
                       + (control.shortcutText !== "" ? 24 + Math.ceil(shortcut.implicitWidth) : 0)
                       + (control.subMenu ? control.spacing + 20 : 0)

        Icon {
            id: leadingIcon
            anchors.verticalCenter: parent.verticalCenter
            visible: control.leadingGlyph !== ""
            glyph: control.leadingGlyph
            size: 20
            color: control.mutedColor
        }
        Column {
            x: leadingIcon.visible ? 20 + control.spacing : 0
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - x - (control.shortcutText !== "" ? shortcut.implicitWidth + 24 : 0)
                   - (control.subMenu ? control.spacing + 20 : 0)
            Text {
                id: label
                width: parent.width
                text: control.text
                font: control.font
                color: control.contentColor
                elide: Text.ElideRight
            }
            Text {
                id: support
                width: parent.width
                visible: control.supportingText !== ""
                text: control.supportingText
                font: Theme.type.bodySmall.font
                color: control.mutedColor
                elide: Text.ElideRight
            }
        }
        Text {
            id: shortcut
            x: parent.width - width - (control.subMenu ? control.spacing + 20 : 0)
            anchors.verticalCenter: parent.verticalCenter
            visible: control.shortcutText !== ""
            text: control.shortcutText
            font: Theme.type.labelLarge.font
            color: control.mutedColor
        }
        Icon {
            x: parent.width - width
            anchors.verticalCenter: parent.verticalCenter
            visible: control.subMenu !== null
            glyph: Symbols.chevron_right
            size: 20
            color: control.mutedColor
        }
    }

    background: Rectangle {
        readonly property real outer: Theme.shape.medium
        readonly property real inner: control.selected ? Theme.shape.medium
                                                       : Theme.shape.extraSmall
        implicitHeight: control.implicitHeight
        color: Theme.mix(Qt.alpha(ColorScope.scheme.tertiaryContainer, 0),
                         ColorScope.scheme.tertiaryContainer, selection.value)
        topLeftRadius: control.first ? outer : inner
        topRightRadius: control.first ? outer : inner
        bottomLeftRadius: control.last ? outer : inner
        bottomRightRadius: control.last ? outer : inner

        StateLayer {
            anchors.fill: parent
            color: control.contentColor
            hovered: control.highlighted && control.enabled
            pressed: control.down
            focused: false
            pressX: control.pressed ? control.pressX : width / 2
            pressY: control.pressed ? control.pressY : height / 2
            topLeftRadius: parent.topLeftRadius
            topRightRadius: parent.topRightRadius
            bottomRightRadius: parent.bottomRightRadius
            bottomLeftRadius: parent.bottomLeftRadius
        }
    }

    // A hand pointer wherever the control shows hover (controls.md §2).
    HoverHandler {
        enabled: control.hoverEnabled
        cursorShape: Qt.PointingHandCursor
    }
}
