pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Presentation

// The persistent mini player (docs/ui/README.md §6.2): a bar with the cover, title,
// transport controls, time and a progress line. Flat, no shadow: in the app it is
// part of the frame under the content panel (docs/ui/pages/shell.md §0). It slides in once something is
// queued; the shell keeps `reservedHeight` free below the pages. Narrow bars drop
// "previous" and the time.
Item {
    id: root

    readonly property bool active: App.player.status !== Playback.Empty
    // Space to keep free below the content, including the bottom margin.
    property real bottomMargin: 4
    readonly property real reservedHeight: (implicitHeight + bottomMargin) * reveal.value
    readonly property bool wide: width >= 560

    // Opens the player page (UI phase 4).
    signal openRequested
    // The current song's comments (not for voices).
    signal commentsRequested

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

        CoverArt {
            id: cover
            x: 12
            anchors.verticalCenter: parent.verticalCenter
            width: 48
            height: 48
            coverKey: App.player.current.coverKey
        }

        Column {
            anchors.left: cover.right
            anchors.leftMargin: 12
            anchors.right: commentsButton.visible ? commentsButton.left : controls.left
            anchors.rightMargin: commentsButton.visible ? 4 : 12
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

        IconButton {
            id: commentsButton
            anchors.right: controls.left
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            visible: App.player.current.key !== "" && !App.player.current.voice
            glyph: Symbols.comment
            text: qsTr("评论")
            onClicked: root.commentsRequested()
        }

        // Pressing a button widens it and squeezes its neighbours (MD3E button group).
        // The play button is wide, square while playing and round while paused, and
        // spins a loading indicator while the song loads.
        StandardButtonGroup {
            id: controls
            anchors.right: time.visible ? time.left : parent.right
            anchors.rightMargin: time.visible ? 16 : 12
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
            id: time
            anchors.right: parent.right
            anchors.rightMargin: 20
            anchors.verticalCenter: parent.verticalCenter
            visible: root.wide
            text: App.format.duration(clock.positionMs) + " / " + App.format.duration(App.player.durationMs)
            color: ColorScope.scheme.onSurfaceVariant
            font: Theme.type.labelMedium.font
            // As wide as the longest time of this song, so the controls stay put.
            width: Math.ceil(longest.advanceWidth)
            horizontalAlignment: Text.AlignRight

            TextMetrics {
                id: longest
                font: time.font
                text: {
                    const total = App.format.duration(App.player.durationMs).replace(/\d/g, "0");
                    return total + " / " + total;
                }
            }
        }

        ProgressBar {
            anchors.left: cover.right
            anchors.leftMargin: 12
            anchors.right: parent.right
            anchors.rightMargin: 20
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 7
            thickness: 3
            visible: App.player.durationMs > 0
            from: 0
            to: Math.max(1, App.player.durationMs)
            value: clock.positionMs
        }
    }
}
