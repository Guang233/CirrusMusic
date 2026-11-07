pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Presentation

// The persistent mini player (docs/ui/README.md §6.2): the cover and title on the
// left; in the middle the transport controls over a seek bar that waves while playing,
// the times at its ends; the comments on the right. Flat, no shadow: in the app it is
// part of the frame under the content panel (docs/ui/pages/shell.md §0). It slides in
// once something is queued; the shell keeps `reservedHeight` free below the pages.
// Narrow bars drop "previous" and the times.
Item {
    id: root

    readonly property bool active: App.player.status !== Playback.Empty
    // Space to keep free below the content, including the bottom margin.
    property real bottomMargin: 4
    readonly property real reservedHeight: (implicitHeight + bottomMargin) * reveal.value
    readonly property bool wide: width >= 560
    // For the player page's shared elements: the card it grows from, and the cover,
    // title, buttons and seek bar that fly to their places on the page. While `flying`
    // the cover and the times are hidden (the page draws copies of the rest).
    readonly property Item bar: card
    readonly property Item coverItem: cover
    readonly property Item titleItem: texts
    readonly property Item controlsItem: controls
    readonly property Item seekItem: seek
    property bool flying: false

    // Opens the player page (UI phase 4).
    signal openRequested

    // The focus comes back here when the player page closes: its play button.
    function takeFocus(): void {
        playButton.forceActiveFocus(Qt.OtherFocusReason);
    }
    // The current song's comments (not for voices).
    signal commentsRequested

    readonly property real timeWidth: 44
    implicitHeight: 80
    visible: reveal.value > 0

    Spring {
        id: reveal
        target: root.active ? 1 : 0
        spec: Theme.motion.standardDefaultSpatial
        threshold: 0.001
        Component.onCompleted: snapTo(target)
    }

    PlaybackClock {
        id: clock
        active: root.visible
    }

    Rectangle {
        id: card
        width: parent.width
        height: root.implicitHeight
        // Slides up from below the window edge.
        y: (1 - reveal.value) * (height + root.bottomMargin)
        radius: Theme.shape.large
        color: "transparent"

        TapHandler {
            onTapped: root.openRequested()
        }
        // A click anywhere free opens the player page.
        HoverHandler {
            cursorShape: Qt.PointingHandCursor
        }

        CoverArt {
            id: cover
            x: 12
            anchors.verticalCenter: parent.verticalCenter
            width: 48
            height: 48
            coverKey: App.player.current.coverKey
            opacity: root.flying ? 0 : 1
        }

        Column {
            id: texts
            anchors.left: cover.right
            anchors.leftMargin: 12
            anchors.right: center.left
            anchors.rightMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2

            Text {
                width: parent.width
                text: App.player.current.title
                elide: Text.ElideRight
                color: ColorScope.scheme.onSurface
                font: Theme.type.titleSmall.font
            }
            Text {
                readonly property bool failed: App.player.status === Playback.Failed
                width: parent.width
                text: failed ? App.player.failureMessage : App.player.current.artistText
                elide: Text.ElideRight
                color: failed ? ColorScope.scheme.error : ColorScope.scheme.onSurfaceVariant
                font: Theme.type.bodyMedium.font
            }
        }

        // The middle: the buttons over the seek bar (like the player page's bottom
        // layout). Centered on wide bars; on narrow ones at the right, without
        // "previous" and the times.
        Item {
            id: center
            readonly property real barWidth: root.wide ? Math.min(520, root.width * 0.4) : 160
            x: root.wide ? (parent.width - width) / 2
                         : parent.width - width - (commentsButton.visible ? commentsButton.width + 16 : 12)
            y: (parent.height - height) / 2
            width: barWidth + (root.wide ? 2 * root.timeWidth : 0)
            height: controls.height + 2 + seek.height

            // Pressing a button widens it and squeezes its neighbours (MD3E button
            // group). The play button is wide, square while playing and round while
            // paused, and spins a loading indicator while the song loads.
            StandardButtonGroup {
                id: controls
                anchors.horizontalCenter: parent.horizontalCenter

                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: root.wide
                    enabled: App.player.canPrevious
                    variant: Md.Tonal
                    glyph: Symbols.skip_previous
                    iconFill: 1
                    text: qsTr("上一首")
                    onClicked: App.player.previous()
                }
                IconButton {
                    id: playButton
                    readonly property bool loading: App.player.status === Playback.Loading
                    readonly property bool playing: App.player.status === Playback.Playing || loading
                    anchors.verticalCenter: parent.verticalCenter
                    variant: Md.Filled
                    widthMode: Md.Wide
                    shape: playing ? Md.Square : Md.Round
                    glyph: loading ? "" : playing ? Symbols.pause : Symbols.play_arrow
                    iconFill: 1
                    text: loading ? qsTr("正在加载") : playing ? qsTr("暂停") : qsTr("播放")
                    onClicked: App.player.playPause()

                    LoadingIndicator {
                        anchors.centerIn: parent
                        implicitWidth: 28
                        implicitHeight: 28
                        visible: playButton.loading
                        color: ColorScope.scheme.onPrimary
                    }
                }
                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    enabled: App.player.canNext
                    variant: Md.Tonal
                    glyph: Symbols.skip_next
                    iconFill: 1
                    text: qsTr("下一首")
                    onClicked: App.player.next()
                }
            }

            Text {
                anchors.left: parent.left
                anchors.verticalCenter: seek.verticalCenter
                visible: root.wide
                opacity: root.flying ? 0 : 1
                text: App.format.duration(seek.shownMs)
                color: ColorScope.scheme.onSurfaceVariant
                font: Fonts.tabular(Theme.type.labelSmall.font)
            }

            // The seek bar: a wave while playing (a setting), a line while paused, and
            // a short bar to drag. Dragging only previews; the seek happens on release
            // and the handle stays there until the player has caught up.
            Slider {
                id: seek

                // The position held after a seek until the player reports it; else -1.
                property real heldMs: -1
                readonly property real shownMs: pressed ? valueAt(position) : value

                function seekTo(ms: real): void {
                    heldMs = ms;
                    App.player.seek(ms);
                    caughtUp.restart();
                }

                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: controls.bottom
                anchors.topMargin: 2
                width: center.barWidth
                trackHeight: 4
                handleHeight: 16
                wavy: App.settings.miniWave
                waving: App.player.status === Playback.Playing && !pressed
                enabled: App.player.seekable && App.player.durationMs > 0
                opacity: App.player.durationMs > 0 ? 1 : 0
                live: false
                focusPolicy: Qt.TabFocus
                from: 0
                to: Math.max(1, App.player.durationMs)
                stepSize: 5000
                value: pressed ? value : heldMs >= 0 ? heldMs : clock.positionMs
                Accessible.name: qsTr("播放进度")
                onPressedChanged: if (!pressed)
                                      seekTo(valueAt(position))
                onMoved: if (!pressed)
                             seekTo(value)

                Connections {
                    target: App.player
                    function onChanged() {
                        if (!seek.pressed)
                            seek.heldMs = -1;
                    }
                }
                Timer {
                    id: caughtUp
                    interval: 1500
                    onTriggered: seek.heldMs = -1
                }
            }

            Text {
                anchors.right: parent.right
                anchors.verticalCenter: seek.verticalCenter
                visible: root.wide
                opacity: root.flying ? 0 : 1
                horizontalAlignment: Text.AlignRight
                text: App.player.durationMs <= 0 ? "--:--"
                      : App.settings.playerTimeRight === 1 ? App.format.duration(App.player.durationMs)
                      : "-" + App.format.duration(App.player.durationMs - seek.shownMs)
                color: ColorScope.scheme.onSurfaceVariant
                font: Fonts.tabular(Theme.type.labelSmall.font)
            }
        }

        IconButton {
            id: commentsButton
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            visible: App.player.current.key !== "" && !App.player.current.voice
            glyph: Symbols.comment
            text: qsTr("评论")
            onClicked: root.commentsRequested()
        }
    }
}
