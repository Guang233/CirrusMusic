pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Presentation

// The player's progress bar (docs/ui/pages/player.md §1.1): thin, thicker on hover or
// while dragged, the elapsed time under it and the time left or the length (a setting;
// a click on it switches). The fill springs to a new place (a seek, the next song) like
// ProgressBar, and follows the clock otherwise. Dragging only previews; the
// seek happens on release, and the bar keeps the released position until the player
// reports the new one, so it never snaps back.
Item {
    id: root

    required property PlayerPalette colors
    required property real positionMs
    readonly property real durationMs: App.player.durationMs
    readonly property bool seekable: App.player.seekable && durationMs > 0
    // While dragging and until the player has caught up: the position shown; else -1.
    property real previewMs: -1
    readonly property real shownMs: previewMs >= 0 ? previewMs : positionMs
    // Without the time row (the bottom layout puts the times beside the bar).
    property bool showTimes: true
    // The times on either side of the bar, on its line (the bottom layout).
    property bool timesBeside: false
    readonly property real timeWidth: timesBeside ? 52 : 0

    function seekBy(deltaMs: real): void {
        if (!seekable)
            return;
        previewMs = Math.max(0, Math.min(durationMs, shownMs + deltaMs));
        App.player.seek(previewMs);
        caughtUp.restart();
    }

    implicitHeight: showTimes && !timesBeside ? 44 : 20
    activeFocusOnTab: true
    Accessible.role: Accessible.Slider
    Accessible.name: qsTr("播放进度")
    Accessible.description: App.format.duration(shownMs)
    Keys.onLeftPressed: seekBy(-5000)
    Keys.onRightPressed: seekBy(5000)

    // The released position stays until the next snapshot (or a moment at most).
    Connections {
        target: App.player
        function onChanged() {
            if (!area.pressed && root.previewMs >= 0)
                root.previewMs = -1;
        }
    }
    Timer {
        id: caughtUp
        interval: 1500
        onTriggered: if (!area.pressed)
                         root.previewMs = -1
    }

    Spring {
        id: fill
        target: root.durationMs > 0 ? Math.min(1, root.shownMs / root.durationMs) : 0
        spec: Theme.motion.defaultEffects
        threshold: 0.0005
    }
    Spring {
        id: thick
        target: area.containsMouse || area.pressed || root.activeFocus ? 1 : 0
        spec: Theme.motion.fastSpatial
    }

    Item {
        id: bar
        x: root.timeWidth
        width: parent.width - 2 * root.timeWidth
        height: 20

        Rectangle {
            id: track
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width
            height: 4 + 4 * thick.value
            radius: height / 2
            color: Qt.alpha(root.colors.content, 0.25)

            Rectangle {
                height: parent.height
                radius: parent.radius
                width: root.durationMs > 0 ? Math.max(height, parent.width * fill.value) : 0
                color: root.colors.content
            }
        }

        FocusRing {
            anchors.fill: track
            shown: InputMode.keyboard && root.activeFocus
            containerRadius: track.radius
            border.color: root.colors.content
        }

        MouseArea {
            id: area

            function ratioAt(x: real): real {
                return Math.max(0, Math.min(1, x / width));
            }

            anchors.fill: parent
            anchors.topMargin: -6
            anchors.bottomMargin: -6
            enabled: root.seekable
            hoverEnabled: true
            cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
            onPressed: mouse => root.previewMs = ratioAt(mouse.x) * root.durationMs
            onPositionChanged: mouse => {
                if (pressed)
                    root.previewMs = ratioAt(mouse.x) * root.durationMs;
            }
            onReleased: {
                App.player.seek(root.previewMs);
                caughtUp.restart();
            }
            onCanceled: root.previewMs = -1
        }
    }

    Text {
        anchors.left: parent.left
        y: root.timesBeside ? (bar.height - height) / 2 : bar.height
        visible: root.showTimes
        text: App.format.duration(root.shownMs)
        color: root.colors.contentVariant
        font: Fonts.tabular(Theme.type.labelMedium.font)
    }
    Text {
        id: rightTime
        anchors.right: parent.right
        y: root.timesBeside ? (bar.height - height) / 2 : bar.height
        visible: root.showTimes
        text: root.durationMs <= 0 ? "--:--"
              : App.settings.playerTimeRight === 1 ? App.format.duration(root.durationMs)
              : "-" + App.format.duration(root.durationMs - root.shownMs)
        color: root.colors.contentVariant
        font: Fonts.tabular(Theme.type.labelMedium.font)

        // A click switches between the time left and the length.
        TapHandler {
            margin: 6
            onTapped: App.settings.playerTimeRight = 1 - App.settings.playerTimeRight
        }
        HoverHandler {
            margin: 6
            cursorShape: Qt.PointingHandCursor
        }
    }
}
