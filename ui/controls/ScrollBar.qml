import QtQuick
import QtQuick.Templates as T
import Cirrus.Theme

// Overlay scroll bar: a 4 px bar that widens to 8 px under the pointer and fades
// out 1 s after scrolling stops. A vertical one also scrolls its Flickable by the mouse
// wheel (see `wheel` below), so every scroll view gets it.
//   ScrollBar.vertical: ScrollBar {}
T.ScrollBar {
    id: control

    readonly property bool expanded: (hovered || pressed) && interactive
    readonly property bool shown: policy === T.ScrollBar.AlwaysOn
                                  || (active || hovered) && size < 1.0

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)
    padding: 2
    visible: policy !== T.ScrollBar.AlwaysOff
    minimumSize: orientation === Qt.Horizontal ? height / width : width / height

    // Mouse wheel scrolling (docs/ui/pages/playlist.md §12): each notch moves the goal
    // 2 × height^(2/3) px, scaled by wheelScrollLines / 3 (a 500 px view 126 px, 800 px
    // 172 px, 1300 px 238 px: taller views scroll further, as in GTK; logical pixels, so
    // the same on any screen scale). Notches add up, and a critically damped spring
    // follows the goal, keeping its speed when a new notch arrives: steady turning
    // scrolls at a steady speed, and it stops about 0.1 s after the last notch, with no
    // glide. (Qt's wheel either restarts a 0.3 s ease from where the content is, dropping
    // the unfinished part, or flicks and glides; a restarted ease per notch also makes
    // the speed pulse.) Past an end it runs out a little and comes back. Touchpads (pixel
    // deltas) are left to the Flickable; at an end the wheel goes on to the view around
    // it (WheelFilter).
    readonly property Flickable flickable: orientation === Qt.Vertical ? parent as Flickable
                                                                       : null
    readonly property real minY: flickable ? flickable.originY - flickable.topMargin : 0
    readonly property real maxY: flickable
                                 ? Math.max(minY, flickable.originY + flickable.contentHeight
                                                  + flickable.bottomMargin - flickable.height)
                                 : 0
    property real goal: 0
    // The content position last set by the wheel: another change (dragging the bar, the
    // keyboard, a page scrolling itself) hands the content back.
    property real placed: NaN

    Spring {
        id: wheelScroll
        spec: Theme.motion.custom(1, 3600)
        threshold: 0.3
        onValueChanged: {
            if (!isNaN(control.placed) && control.flickable) {
                control.placed = value;
                control.flickable.contentY = value;
            }
        }
        onRunningChanged: {
            if (running)
                return;
            // Ran out past an end: back to it.
            const bound = Math.max(control.minY, Math.min(control.maxY, control.goal));
            if (Math.abs(bound - control.goal) > 0.5 && !isNaN(control.placed)) {
                control.goal = bound;
                target = bound;
            } else if (!control.pressed) {
                control.active = false;
            }
        }
    }

    WheelFilter {
        target: control.flickable
        enabled: control.flickable !== null && control.flickable.interactive
        onNotch: steps => {
            const view = control.flickable;
            if (control.maxY <= control.minY)
                return;
            const following = wheelScroll.running && Math.abs(view.contentY - control.placed) < 0.5;
            const from = following ? Math.max(control.minY, Math.min(control.maxY, control.goal))
                                   : view.contentY;
            // QStyleHints.wheelScrollLines (typed QObject for qmllint).
            const lines = Object(Qt.styleHints).wheelScrollLines ?? 3;
            const wanted = from - steps * lines / 3 * 2 * Math.pow(view.height, 2 / 3);
            // Already at that end: the view around it scrolls instead.
            if (!following && (wanted < control.minY && from <= control.minY + 0.5
                               || wanted > control.maxY && from >= control.maxY - 0.5))
                return;
            if (!following) {
                control.placed = NaN;
                wheelScroll.snapTo(view.contentY);
                control.placed = view.contentY;
            }
            const past = wanted < control.minY ? wanted - control.minY
                       : wanted > control.maxY ? wanted - control.maxY : 0;
            control.goal = past === 0 ? wanted
                         : (past < 0 ? control.minY : control.maxY) + Math.max(-24, Math.min(24, past / 3));
            control.active = true;
            wheelScroll.target = control.goal;
            accepted = true;
        }
    }

    Spring {
        id: thickness
        target: control.expanded ? 8 : 4
        spec: Theme.motion.fastSpatial
        threshold: 0.05
    }

    contentItem: Item {
        implicitWidth: 8
        implicitHeight: 8
        opacity: 0

        Rectangle {
            readonly property bool vertical: control.orientation === Qt.Vertical
            anchors.right: vertical ? parent.right : undefined
            anchors.bottom: vertical ? undefined : parent.bottom
            width: vertical ? Math.max(1, thickness.value) : parent.width
            height: vertical ? parent.height : Math.max(1, thickness.value)
            radius: Math.min(width, height) / 2
            color: control.pressed ? ColorScope.scheme.onSurface
                                   : Qt.alpha(ColorScope.scheme.onSurfaceVariant,
                                              control.expanded ? 0.9 : 0.6)
        }
    }

    states: State {
        name: "shown"
        when: control.shown
        PropertyChanges {
            control.contentItem.opacity: 1
        }
    }
    transitions: [
        Transition {
            to: "shown"
            NumberAnimation {
                target: control.contentItem
                property: "opacity"
                duration: Theme.motion.fastEffects.duration
            }
        },
        Transition {
            from: "shown"
            SequentialAnimation {
                PauseAnimation {
                    duration: 1000
                }
                NumberAnimation {
                    target: control.contentItem
                    property: "opacity"
                    to: 0
                    duration: Theme.motion.slowEffects.duration
                }
            }
        }
    ]
}
