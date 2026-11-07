pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Presentation

// The persistent mini player (docs/ui/README.md §6.2), one row: the cover and title,
// a seek bar that waves while playing (the times at its ends), the transport buttons
// and the comments. Narrower bars shorten the seek bar, then trade it (and "previous")
// for a progress ring around the play button (`tier`). Flat, no shadow: in the app it
// is part of the frame under the content panel (docs/ui/pages/shell.md §0). It slides
// in once something is queued; the shell keeps `reservedHeight` free below the pages.
Item {
    id: root

    readonly property bool active: App.player.status !== Playback.Empty
    // Space to keep free below the content, including the bottom margin.
    property real bottomMargin: 4
    readonly property real reservedHeight: (implicitHeight + bottomMargin) * reveal.value
    // 2 wide: the seek bar with the times; 1 medium: a shorter one, the time over the
    // handle while dragging; 0 narrow: no seek bar, no "previous", a progress ring
    // around the play button.
    // The thresholds leave the title at least `titleRoom` beside a seek bar of a usable
    // length.
    readonly property int tier: width >= 880 ? 2 : width >= 660 ? 1 : 0
    readonly property real titleRoom: 160
    readonly property bool wide: tier > 0
    // Layout decisions read these, not `visible`: the page captures this layout while
    // the shell is hidden behind it, when every `visible` here reads false.
    readonly property bool hasComments: App.player.current.key !== "" && !App.player.current.voice
    // For the player page's shared elements: the card it grows from, and the cover,
    // title, buttons and seek bar that fly to their places on the page. While `flying`
    // the cover and the times are hidden (the page draws copies of the rest).
    readonly property Item bar: card
    readonly property Item coverItem: cover
    readonly property Item titleItem: texts
    readonly property Item controlsItem: controls
    readonly property Item seekItem: tier > 0 ? seek : null
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
    implicitHeight: 72
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
            anchors.right: root.tier > 0 ? center.left : controls.left
            anchors.rightMargin: root.tier > 0 ? 24 : 16
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

        // Pressing a button widens it and squeezes its neighbours (MD3E button
        // group). The play button is wide, square while playing and round while
        // paused, and spins a loading indicator while the song loads.
        StandardButtonGroup {
            id: controls
            anchors.right: root.hasComments ? commentsButton.left : parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter

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
                widthMode: root.tier > 0 ? Md.Wide : Md.Default
                // Round inside the ring on narrow bars.
                shape: playing && root.tier > 0 ? Md.Square : Md.Round
                glyph: loading ? "" : playing ? Symbols.pause : Symbols.play_arrow
                iconFill: 1
                text: loading ? qsTr("正在加载") : playing ? qsTr("暂停") : qsTr("播放")
                onClicked: App.player.playPause()

                // Narrow bars have no seek bar: a ring around the button shows the
                // progress.
                CircularProgress {
                    anchors.centerIn: parent
                    width: parent.height + 12
                    height: width
                    thickness: 3
                    visible: root.tier === 0 && App.player.durationMs > 0
                    from: 0
                    to: Math.max(1, App.player.durationMs)
                    value: clock.positionMs
                }
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


        // The seek bar between the title and the buttons, the times at its ends on
        // wide bars; narrow bars have none (the play button's ring instead).
        Item {
            id: center
            anchors.right: controls.left
            anchors.rightMargin: 24
            anchors.verticalCenter: parent.verticalCenter
            visible: root.tier > 0
            // Grows with the bar (45% of it) on wide bars, as long as the title keeps its
            // room; at most 280 on medium ones.
            readonly property real room: controls.x - 24 - (cover.x + cover.width + 12 + root.titleRoom + 24)
            width: Math.max(0, root.tier === 2 ? Math.min(root.width * 0.45, room) : Math.min(280, room))
            height: seek.height

            Text {
                anchors.left: parent.left
                anchors.verticalCenter: seek.verticalCenter
                visible: root.tier === 2
                opacity: root.flying ? 0 : 1
                // Back after the flight without popping in.
                Behavior on opacity {
                    NumberAnimation {
                        duration: Theme.motion.fastEffects.duration
                    }
                }
                text: App.format.duration(seek.shownMs)
                color: ColorScope.scheme.onSurfaceVariant
                font: Fonts.tabular(Theme.type.labelSmall.font)
            }

            // The seek bar: a wave while playing (a setting), a line while paused,
            // and a short bar to drag. Dragging only previews; the seek happens on
            // release and the handle stays there until the player has caught up.
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

                anchors.left: parent.left
                anchors.leftMargin: root.tier === 2 ? root.timeWidth : 0
                anchors.right: parent.right
                anchors.rightMargin: root.tier === 2 ? root.timeWidth : 0
                anchors.verticalCenter: parent.verticalCenter
                trackHeight: 4
                handleHeight: 16
                wavy: App.settings.miniWave
                waving: App.player.status === Playback.Playing && !pressed
                enabled: App.player.seekable && App.player.durationMs > 0
                opacity: App.player.durationMs > 0 ? 1 : 0
                live: false
                // Without the times: the time it would go to, over the handle while dragging.
                showValueLabel: root.tier === 1
                valueText: App.format.duration(shownMs)
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
                visible: root.tier === 2
                opacity: root.flying ? 0 : 1
                // Back after the flight without popping in.
                Behavior on opacity {
                    NumberAnimation {
                        duration: Theme.motion.fastEffects.duration
                    }
                }
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
            visible: root.hasComments
            glyph: Symbols.comment
            text: qsTr("评论")
            onClicked: root.commentsRequested()
        }
    }
}
