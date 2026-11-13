import QtQuick
import QtQuick.Shapes
import QtQuick.Templates as T
import Cirrus.Theme

// MD3 checkbox with tristate and error support.
T.CheckBox {
    id: control

    property bool error: false

    readonly property bool marked: checkState !== Qt.Unchecked
    readonly property color accent: error ? ColorScope.scheme.error : ColorScope.scheme.primary
    readonly property color boxColor: !enabled
        ? (marked ? Qt.alpha(ColorScope.scheme.onSurface, Theme.state.disabledContent) : "transparent")
        : Theme.mix(Qt.alpha(accent, 0), accent, selection.value)
    readonly property color outlineColor: !enabled
        ? Qt.alpha(ColorScope.scheme.onSurface, Theme.state.disabledContent)
        : error ? ColorScope.scheme.error
        : hovered || down || visualFocus ? ColorScope.scheme.onSurface : ColorScope.scheme.onSurfaceVariant
    readonly property color markColor: !enabled ? ColorScope.scheme.surface
                                     : error ? ColorScope.scheme.onError : ColorScope.scheme.onPrimary

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding,
                             implicitIndicatorHeight + topPadding + bottomPadding)
    padding: 0
    spacing: 4
    font: Theme.type.bodyMedium.font

    Spring {
        id: selection
        target: control.marked ? 1 : 0
        spec: Theme.motion.defaultEffects
    }
    // How much of the check mark is drawn (0..1).
    Spring {
        id: stroke
        target: control.marked ? 1 : 0
        spec: Theme.motion.fastSpatial
        threshold: 0.002
    }

    indicator: Item {
        implicitWidth: 40
        implicitHeight: 40
        x: control.text ? (control.mirrored ? control.width - width - control.rightPadding
                                            : control.leftPadding)
                        : control.leftPadding + (control.availableWidth - width) / 2
        y: control.topPadding + (control.availableHeight - height) / 2

        StateLayer {
            anchors.fill: parent
            radius: width / 2
            color: control.marked ? control.accent : ColorScope.scheme.onSurface
            hovered: control.hovered && control.enabled
            pressed: control.down
            focused: false
            pressX: width / 2
            pressY: height / 2
        }

        Rectangle {
            id: box
            anchors.centerIn: parent
            width: 18
            height: 18
            radius: 2
            color: control.boxColor
            border.width: 2 * (1 - selection.value)
            border.color: control.outlineColor

            FocusRing {
                shown: control.visualFocus
                containerRadius: box.radius
            }

            Shape {
                id: mark
                anchors.fill: parent
                visible: stroke.value > 0.01
                preferredRendererType: Shape.CurveRenderer

                // Drawn again after the window comes back from the tray (a Qt bug).
                ShapeRefresh {
                    id: refresh
                    target: mark
                }

                ShapePath {
                    startX: refresh.offset
                    strokeColor: control.markColor
                    strokeWidth: 2
                    fillColor: "transparent"
                    capStyle: ShapePath.SquareCap
                    joinStyle: ShapePath.MiterJoin

                    PathPolyline {
                        path: control.checkState === Qt.PartiallyChecked
                              ? control.dashPoints(stroke.value) : control.checkPoints(stroke.value)
                    }
                }
            }
        }
    }

    // Check mark (two strokes) drawn up to `t` of its length, in 18x18 box coordinates.
    function checkPoints(t: real): list<point> {
        const a = Qt.point(4.5, 9.2), b = Qt.point(7.6, 12.3), c = Qt.point(13.6, 6.2);
        const l1 = Math.hypot(b.x - a.x, b.y - a.y), l2 = Math.hypot(c.x - b.x, c.y - b.y);
        const d = Math.max(0, Math.min(1, t)) * (l1 + l2);
        if (d <= l1) {
            const k = d / l1;
            return [a, Qt.point(a.x + (b.x - a.x) * k, a.y + (b.y - a.y) * k)];
        }
        const k = (d - l1) / l2;
        return [a, b, Qt.point(b.x + (c.x - b.x) * k, b.y + (c.y - b.y) * k)];
    }

    function dashPoints(t: real): list<point> {
        const half = 5 * Math.max(0, Math.min(1, t));
        return [Qt.point(9 - half, 9), Qt.point(9 + half, 9)];
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
