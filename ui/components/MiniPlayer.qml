pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Presentation

// The persistent mini player (docs/ui/README.md §6.2): a floating bar with the cover,
// title, transport controls, time and a progress line. It slides in once something is
// queued; the shell keeps `reservedHeight` free below the pages. Narrow bars drop
// "previous" and the time.
Item {
    id: root

    readonly property bool active: App.player.status !== Playback.Empty
    // Space to keep free below the content, including the bottom margin.
    property real bottomMargin: 12
    readonly property real reservedHeight: (implicitHeight + bottomMargin) * reveal.value
    readonly property bool wide: width >= 560

    // Opens the player page (UI phase 4).
    signal openRequested

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
        color: Theme.color.surfaceContainerHigh

        Elevation {
            anchors.fill: parent
            z: -1
            level: Theme.elevation.level2
            radius: card.radius
        }

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
            anchors.right: controls.left
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2

            Text {
                width: parent.width
                text: App.player.current.title
                elide: Text.ElideRight
                color: Theme.color.onSurface
                font: Theme.type.titleSmall.font
            }
            Text {
                readonly property bool failed: App.player.status === Playback.Failed
                width: parent.width
                text: failed ? App.player.failureMessage : App.player.current.artistText
                elide: Text.ElideRight
                color: failed ? Theme.color.error : Theme.color.onSurfaceVariant
                font: Theme.type.bodyMedium.font
            }
        }

        Row {
            id: controls
            anchors.right: time.visible ? time.left : parent.right
            anchors.rightMargin: time.visible ? 16 : 12
            anchors.verticalCenter: parent.verticalCenter
            spacing: 4

            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                visible: root.wide
                enabled: App.player.canPrevious
                glyph: Symbols.skip_previous
                iconFill: 1
                text: qsTr("上一首")
                onClicked: App.player.previous()
            }
            IconButton {
                readonly property bool playing: App.player.status === Playback.Playing
                                                || App.player.status === Playback.Loading
                anchors.verticalCenter: parent.verticalCenter
                enabled: App.player.status !== Playback.Loading
                variant: Md.Filled
                glyph: playing ? Symbols.pause : Symbols.play_arrow
                iconFill: 1
                text: playing ? qsTr("暂停") : qsTr("播放")
                onClicked: App.player.playPause()
            }
            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                enabled: App.player.canNext
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
            color: Theme.color.onSurfaceVariant
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
            visible: App.player.status === Playback.Loading || App.player.durationMs > 0
            indeterminate: App.player.status === Playback.Loading
            from: 0
            to: Math.max(1, App.player.durationMs)
            value: clock.positionMs
        }
    }
}
