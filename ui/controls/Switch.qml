import QtQuick
import QtQuick.Templates as T
import Cirrus.Theme

// MD3 switch. `showIcons` puts ✓ / ✕ in the handle.
T.Switch {
    id: control

    property bool showIcons: false

    readonly property real handleSize: down ? 28 : checked || showIcons ? 24 : 16
    readonly property color trackColor: !enabled
        ? Qt.alpha(checked ? ColorScope.scheme.onSurface : ColorScope.scheme.surfaceContainerHighest,
                   Theme.state.disabledContainer)
        : Theme.mix(ColorScope.scheme.surfaceContainerHighest, ColorScope.scheme.primary, selection.value)
    readonly property color handleColor: {
        if (!enabled)
            return checked ? ColorScope.scheme.surface
                           : Qt.alpha(ColorScope.scheme.onSurface, Theme.state.disabledContent);
        const active = hovered || down || visualFocus;
        return Theme.mix(active ? ColorScope.scheme.onSurfaceVariant : ColorScope.scheme.outline,
                         active ? ColorScope.scheme.primaryContainer : ColorScope.scheme.onPrimary,
                         selection.value);
    }

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding,
                             implicitIndicatorHeight + topPadding + bottomPadding)
    padding: 4
    spacing: 16
    font: Theme.type.bodyMedium.font

    Spring {
        id: selection
        target: control.checked ? 1 : 0
        spec: Theme.motion.defaultEffects
    }
    Spring {
        id: position
        target: control.visualPosition
        spec: Theme.motion.fastSpatial
        threshold: 0.002
    }
    Spring {
        id: handle
        target: control.handleSize
        spec: Theme.motion.fastSpatial
        threshold: 0.05
    }

    indicator: Item {
        implicitWidth: 52
        implicitHeight: 32
        x: control.text ? (control.mirrored ? control.width - width - control.rightPadding
                                            : control.leftPadding)
                        : control.leftPadding + (control.availableWidth - width) / 2
        y: control.topPadding + (control.availableHeight - height) / 2

        Rectangle {
            id: track
            anchors.fill: parent
            radius: height / 2
            color: control.trackColor
            border.width: 2 * (1 - selection.value)
            border.color: control.enabled ? ColorScope.scheme.outline
                                          : Qt.alpha(ColorScope.scheme.onSurface,
                                                     Theme.state.disabledContainer)

            FocusRing {
                shown: control.visualFocus
                containerRadius: track.radius
            }
        }

        Item {
            width: 40
            height: 40
            x: 16 + position.value * 20 - width / 2
            anchors.verticalCenter: parent.verticalCenter

            StateLayer {
                anchors.fill: parent
                radius: width / 2
                color: control.checked ? ColorScope.scheme.primary : ColorScope.scheme.onSurface
                hovered: control.hovered && control.enabled
                pressed: control.pressed
                focused: false
            }

            Rectangle {
                anchors.centerIn: parent
                width: Math.max(0, handle.value)
                height: width
                radius: width / 2
                color: control.handleColor

                Icon {
                    anchors.centerIn: parent
                    visible: control.showIcons
                    glyph: control.checked ? Symbols.check : Symbols.close
                    size: 16
                    weight: 600
                    color: !control.enabled
                           ? (control.checked ? Qt.alpha(ColorScope.scheme.onSurface, 0.38)
                                              : ColorScope.scheme.surfaceContainerHighest)
                           : control.checked ? ColorScope.scheme.onPrimaryContainer
                                             : ColorScope.scheme.surfaceContainerHighest
                }
            }
        }
    }

    contentItem: Text {
        leftPadding: control.indicator && !control.mirrored ? control.indicator.width
                                                              + control.spacing : 0
        rightPadding: control.indicator && control.mirrored ? control.indicator.width
                                                              + control.spacing : 0
        text: control.text
        font: control.font
        color: control.enabled ? ColorScope.scheme.onSurface
                               : Qt.alpha(ColorScope.scheme.onSurface, Theme.state.disabledContent)
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
    }

    // A hand pointer wherever the control shows hover (controls.md §2).
    HoverHandler {
        enabled: control.hoverEnabled
        cursorShape: Qt.PointingHandCursor
    }
}
