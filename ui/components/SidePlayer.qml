pragma ComponentBehavior: Bound
import QtQuick
import M3Shapes
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Presentation

// The mini player docked as a pane on the right of the window (docs/ui/redesign-md3e.md
// §11, the "now playing" pane of proposal B): in the cover's colors, the cover (a
// 12-sided cookie turning slowly while playing, a rounded square while paused), the
// title, a seek bar that waves while playing, the largest transport buttons of the
// shell, a connected group (play mode, like, volume, queue, the player page) and the
// songs up next. A click on the cover or the title opens the player page. Its top
// strip lies under the window's title bar row: the shell puts the pane's own header
// buttons there (TopBar actions).
// For the player page's shared elements it offers the same items as MiniPlayer: the
// pane the page grows from (`bar`), the cover, title, transport buttons and seek bar.
Rectangle {
    id: root

    // The title bar row over the pane's top: the content starts under it.
    property real topInset: 64
    // While the player page's copies fly, the cover is hidden.
    property bool flying: false

    readonly property Item bar: root
    readonly property Item coverItem: coverBox
    readonly property Item titleItem: texts
    readonly property Item controlsItem: transport
    readonly property Item seekItem: seek
    // The cover's shape and turn now, where the player page's flying cover starts.
    readonly property int coverShape: cover.shape
    readonly property real coverRotation: cover.rotation

    readonly property bool isSong: App.player.current.key !== "" && !App.player.current.voice
    readonly property bool playing: App.player.status === Playback.Playing
    readonly property bool loading: App.player.status === Playback.Loading
    // The play mode, cycling as the mini player's does: 0 list repeat, 1 repeat one,
    // 2 shuffle, 3 in order.
    readonly property int mode: App.player.repeat === Playback.RepeatOne ? 1
                              : App.player.order === Playback.Shuffle ? 2
                              : App.player.repeat === Playback.RepeatAll ? 0 : 3
    readonly property real inner: width - 56

    signal openRequested
    signal queueRequested
    signal loginRequested

    function setMode(next: int): void {
        switch (next) {
        case 0:
            App.player.setOrder(Playback.Sequential);
            App.player.setRepeat(Playback.RepeatAll);
            break;
        case 1:
            App.player.setRepeat(Playback.RepeatOne);
            break;
        case 2:
            App.player.setOrder(Playback.Shuffle);
            App.player.setRepeat(Playback.RepeatAll);
            break;
        default:
            App.player.setOrder(Playback.Sequential);
            App.player.setRepeat(Playback.RepeatOff);
        }
    }
    // The focus comes back here when the player page closes.
    function takeFocus(): void {
        playButton.forceActiveFocus(Qt.OtherFocusReason);
    }

    ColorScope.scheme: tint.scheme
    // A step above the frame (SurfaceContainer), so the pane reads as its own card.
    color: ColorScope.scheme.surfaceContainerHigh
    radius: Theme.shape.extraLarge
    clip: true

    CoverScheme {
        id: tint
        cover: App.player.current.coverKey !== ""
               ? "image://cover/" + encodeURIComponent(App.player.current.coverKey) : ""
        dark: Theme.dark
    }

    PlaybackClock {
        id: clock
        active: root.visible
    }

    // In the strip under the title bar row, level with the window buttons.
    Text {
        x: 28
        y: (root.topInset - height) / 2
        text: qsTr("正在播放")
        color: ColorScope.scheme.onSurfaceVariant
        font: Theme.type.labelLarge.font
    }

    Flickable {
        id: flick
        y: root.topInset
        width: parent.width
        height: parent.height - y
        contentHeight: column.height + 24
        interactive: contentHeight > height
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        ScrollBar.vertical: ScrollBar {}

        Column {
            id: column
            x: 28
            width: root.inner
            spacing: 16

            // The cover, as large as the pane leaves room for (the up-next list keeps
            // at least two rows on tall windows).
            Item {
                id: coverBox
                readonly property real rest: texts.height + seekBox.height + transport.height
                                             + group.height + 4 * column.spacing + 24
                width: Math.round(Math.max(160, Math.min(root.inner, flick.height - rest - 160)))
                height: width
                anchors.horizontalCenter: parent.horizontalCenter

                CoverArt {
                    id: cover
                    anchors.fill: parent
                    opacity: root.flying ? 0 : 1
                    coverKey: App.player.current.coverKey
                    shape: root.playing ? MaterialShape.Cookie12Sided : MaterialShape.Square
                    radius: Theme.shape.extraLarge
                    glyph: Symbols.music_note

                    // Turns slowly while playing; settles upright (the square's quarter
                    // turn) when paused.
                    NumberAnimation on rotation {
                        id: turning
                        running: root.playing && !Theme.motion.reduced && root.visible
                        from: cover.rotation % 360
                        to: cover.rotation % 360 + 360
                        duration: 24000
                        loops: Animation.Infinite
                    }
                    NumberAnimation {
                        id: settle
                        target: cover
                        property: "rotation"
                        duration: Theme.motion.defaultSpatial.duration
                        easing: Theme.motion.defaultSpatial.easing
                    }
                    Connections {
                        target: turning
                        function onRunningChanged() {
                            if (turning.running)
                                return;
                            settle.from = cover.rotation % 360;
                            settle.to = Math.round(cover.rotation % 360 / 90) * 90;
                            settle.start();
                        }
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.openRequested()
                }
            }

            Column {
                id: texts
                width: parent.width
                spacing: 2

                Text {
                    width: parent.width
                    text: App.player.current.title
                    elide: Text.ElideRight
                    color: ColorScope.scheme.onSurface
                    font: Fonts.font(700, 22)
                }
                Text {
                    readonly property bool failed: App.player.status === Playback.Failed
                    width: parent.width
                    text: failed ? App.player.failureMessage : App.player.current.artistText
                    elide: Text.ElideRight
                    color: failed ? ColorScope.scheme.error : ColorScope.scheme.onSurfaceVariant
                    font: Theme.type.bodyLarge.font
                }
                TapHandler {
                    onTapped: root.openRequested()
                }
                HoverHandler {
                    cursorShape: Qt.PointingHandCursor
                }
            }

            // The seek bar and the times under it. Dragging only previews; the seek
            // happens on release (as the mini player's).
            Column {
                id: seekBox
                width: parent.width
                spacing: 2

                Slider {
                    id: seek
                    property real heldMs: -1
                    readonly property real shownMs: pressed ? valueAt(position) : value
                    function seekTo(ms: real): void {
                        heldMs = ms;
                        App.player.seek(ms);
                        caughtUp.restart();
                    }
                    width: parent.width
                    trackHeight: 4
                    handleHeight: 16
                    wavy: App.settings.miniWave
                    waving: root.playing && !pressed
                    enabled: App.player.seekable && App.player.durationMs > 0
                    live: false
                    focusPolicy: Qt.TabFocus
                    from: 0
                    to: Math.max(1, App.player.durationMs)
                    stepSize: 5000
                    // Bounded by `to`, see MiniPlayer's seek bar.
                    value: Math.min(to, pressed ? value : heldMs >= 0 ? heldMs : clock.positionMs)
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
                Item {
                    width: parent.width
                    height: elapsed.height
                    Text {
                        id: elapsed
                        text: App.format.duration(seek.shownMs)
                        color: ColorScope.scheme.onSurfaceVariant
                        font: Fonts.tabular(Theme.type.labelSmall.font)
                    }
                    Text {
                        anchors.right: parent.right
                        text: App.player.durationMs <= 0 ? "--:--"
                              : App.settings.playerTimeRight === 1 ? App.format.duration(App.player.durationMs)
                              : "-" + App.format.duration(App.player.durationMs - seek.shownMs)
                        color: ColorScope.scheme.onSurfaceVariant
                        font: Fonts.tabular(Theme.type.labelSmall.font)
                    }
                }
            }

            // Previous, play or pause, next: the largest buttons of the shell.
            Row {
                id: transport
                readonly property real side: Math.min(96, (width - 2 * spacing) / 3.4)
                width: parent.width
                spacing: 8

                IconButton {
                    width: transport.side
                    variant: Md.Tonal
                    size: Md.Large
                    shape: Md.Square
                    enabled: App.player.canPrevious
                    glyph: Symbols.skip_previous
                    iconFill: 1
                    text: qsTr("上一首")
                    onClicked: App.player.previous()
                }
                IconButton {
                    id: playButton
                    width: transport.width - 2 * transport.side - 2 * transport.spacing
                    variant: Md.Filled
                    size: Md.Large
                    shape: root.playing || root.loading ? Md.Square : Md.Round
                    glyph: root.loading ? "" : root.playing ? Symbols.pause : Symbols.play_arrow
                    iconFill: 1
                    text: root.loading ? qsTr("正在加载") : root.playing ? qsTr("暂停") : qsTr("播放")
                    onClicked: App.player.playPause()

                    LoadingIndicator {
                        anchors.centerIn: parent
                        implicitWidth: 40
                        implicitHeight: 40
                        visible: root.loading
                        color: ColorScope.scheme.onPrimary
                    }
                }
                IconButton {
                    width: transport.side
                    variant: Md.Tonal
                    size: Md.Large
                    shape: Md.Square
                    enabled: App.player.canNext
                    glyph: Symbols.skip_next
                    iconFill: 1
                    text: qsTr("下一首")
                    onClicked: App.player.next()
                }
            }

            // Play mode, like, volume, the queue and the player page.
            ConnectedButtonGroup {
                id: group
                readonly property int count: likeButton.visible ? 5 : 4
                readonly property real itemWidth: (width - (count - 1) * spacing) / count
                width: parent.width

                IconButton {
                    width: group.itemWidth
                    variant: Md.Tonal
                    widthMode: Md.Wide
                    glyph: [Symbols.repeat, Symbols.repeat_one, Symbols.shuffle,
                        Symbols.playlist_play][root.mode]
                    text: [qsTr("列表循环"), qsTr("单曲循环"), qsTr("随机播放"),
                        qsTr("顺序播放")][root.mode]
                    onClicked: root.setMode((root.mode + 1) % 4)
                }
                IconButton {
                    id: likeButton
                    readonly property string key: App.player.current.key
                    readonly property bool liked: App.library.likedRevision >= 0 && App.library.isLiked(key)
                    width: group.itemWidth
                    visible: root.isSong
                    variant: Md.Tonal
                    widthMode: Md.Wide
                    checkable: true
                    checked: liked
                    glyph: Symbols.favorite
                    iconFill: liked ? 1 : 0
                    pops: true
                    text: liked ? qsTr("取消喜欢") : qsTr("喜欢")
                    onClicked: {
                        checked = Qt.binding(() => likeButton.liked);
                        if (!App.session.signedIn)
                            root.loginRequested();
                        else
                            App.library.setLiked(key, !liked);
                    }
                }
                MiniVolume {
                    width: group.itemWidth
                    variant: Md.Tonal
                    widthMode: Md.Wide
                }
                IconButton {
                    width: group.itemWidth
                    variant: Md.Tonal
                    widthMode: Md.Wide
                    glyph: Symbols.queue_music
                    text: qsTr("播放队列")
                    onClicked: root.queueRequested()
                }
                IconButton {
                    width: group.itemWidth
                    variant: Md.Tonal
                    widthMode: Md.Wide
                    glyph: Symbols.lyrics
                    text: qsTr("播放页")
                    onClicked: root.openRequested()
                }
            }

            // The next songs of the queue; a click plays one.
            Column {
                id: upNext
                readonly property int first: App.player.queueIndex + 1
                readonly property int shown: Math.max(0, Math.min(6, App.player.queue.count - first))
                width: parent.width
                visible: shown > 0
                spacing: 8
                topPadding: 8
                onFirstChanged: Qt.callLater(load)
                onShownChanged: Qt.callLater(load)
                function load(): void {
                    if (shown > 0)
                        App.player.ensureQueueLoaded(first, first + shown - 1);
                }

                Text {
                    text: qsTr("接下来")
                    color: ColorScope.scheme.onSurfaceVariant
                    font: Theme.type.labelLarge.font
                }
                // Only the rows shown are made: a ListView over the queue, scrolled to
                // the first song up next.
                ListView {
                    id: nextList
                    width: parent.width
                    height: upNext.shown * 58 - 2
                    interactive: false
                    clip: true
                    spacing: 2
                    model: App.player.queue
                    function align(): void {
                        if (upNext.first < count)
                            positionViewAtIndex(upNext.first, ListView.Beginning);
                    }
                    onCountChanged: Qt.callLater(align)
                    onHeightChanged: Qt.callLater(align)
                    Connections {
                        target: upNext
                        function onFirstChanged() {
                            Qt.callLater(nextList.align);
                        }
                    }
                    delegate: ListItem {
                        id: row
                        required property int index
                        required property string title
                        required property string artistText
                        required property string coverKey
                        readonly property int place: index - upNext.first
                        width: nextList.width
                        height: 56
                        segmented: true
                        first: place === 0
                        last: place === upNext.shown - 1
                        containerColor: ColorScope.scheme.surfaceContainerHighest
                        text: title
                        supportingText: artistText
                        onClicked: App.player.playAt(index)
                        leading: Component {
                            CoverArt {
                                width: 40
                                height: 40
                                radius: Theme.shape.small
                                coverKey: row.coverKey
                            }
                        }
                    }
                }
            }
        }
    }
}
