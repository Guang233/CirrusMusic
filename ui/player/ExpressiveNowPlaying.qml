pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components
import Cirrus.Presentation

// The Material player page's main container (docs/ui/pages/player-expressive.md §2.1):
// PrimaryContainer, 48 corners. The cover (ShapedCover: a turning cookie while playing,
// a rounded square while paused), the title in heavy type with "like" and "more"
// beside it, the artists and an album chip, a wavy seek bar with the times, and the
// button group (previous, play or pause, next). `compact` is a 96 high strip for narrow
// windows with lyrics: a small cover, the title and artists, play and next.
// Laid out with anchors and implicit heights (the page measures the cover for the
// flight while it may be hidden).
Rectangle {
    id: root

    property real coverSize: 400
    property bool compact: false
    property real positionMs: 0
    // While the player page's cover flies, this one is hidden.
    property bool flying: false

    readonly property Item coverItem: compact ? smallCover : coverBox
    readonly property int coverShape: compact ? -1 : cover.shape
    readonly property real coverRotation: compact ? 0 : cover.rotation
    readonly property real coverRadius: compact ? Theme.shape.medium : Theme.shape.extraLarge
    readonly property Item playButton: compact ? compactPlay : playButton
    readonly property bool playing: App.player.status === Playback.Playing
    readonly property bool loading: App.player.status === Playback.Loading
    readonly property var song: App.player.current
    readonly property real pad: 48
    // The height the full card needs below the cover.
    readonly property real restHeight: 32 + 32 + titleRow.height + 8 + artistLine.implicitHeight
                                       + 16 + 32 + 24 + seek.implicitHeight + 4
                                       + elapsed.implicitHeight + 24 + 96 + 32

    signal artistRequested(string key)
    signal albumRequested(string key)
    signal songRequested
    signal commentsRequested
    signal addToPlaylistRequested(string key, string title)
    signal copyRequested
    signal loginRequested

    function playPause(): void {
        App.player.playPause();
    }
    function openArtists(): void {
        if (song.artists.length === 1 && song.artists[0].key !== "")
            artistRequested(song.artists[0].key);
        else if (song.artists.length > 1)
            artistMenu.popup(artistLine, 0, artistLine.height + 4);
    }

    color: ColorScope.scheme.primaryContainer
    radius: compact ? Theme.shape.extraLarge : 48
    implicitHeight: compact ? 96 : coverSize + restHeight

    // --- Full card -----------------------------------------------------------------

    Item {
        id: coverBox
        visible: !root.compact
        x: (root.width - width) / 2
        y: 32
        width: root.coverSize
        height: width

        ShapedCover {
            id: cover
            anchors.fill: parent
            opacity: root.flying ? 0 : 1
            coverKey: root.song.coverKey
            playing: root.playing
        }
    }

    Item {
        id: titleRow
        visible: !root.compact
        x: root.pad
        y: coverBox.y + coverBox.height + 32
        width: root.width - 2 * root.pad
        height: Math.max(title.implicitHeight, likeButton.implicitHeight)

        Text {
            id: title
            width: parent.width - likeButton.width - moreButton.width - 16
            anchors.verticalCenter: parent.verticalCenter
            text: root.song.title
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
            color: ColorScope.scheme.onPrimaryContainer
            font: Fonts.font(800, 40)
            lineHeightMode: Text.FixedHeight
            lineHeight: 48
            Accessible.role: Accessible.Heading
        }
        IconButton {
            id: likeButton
            readonly property bool liked: App.library.likedRevision >= 0 && App.library.isLiked(root.song.key)
            anchors.right: moreButton.left
            anchors.rightMargin: 4
            anchors.verticalCenter: parent.verticalCenter
            visible: !root.song.voice
            variant: Md.Tonal
            size: Md.Medium
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
                    App.library.setLiked(root.song.key, !liked);
            }
        }
        IconButton {
            id: moreButton
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            tint: ColorScope.scheme.onPrimaryContainer
            glyph: Symbols.more_vert
            text: qsTr("更多")
            onClicked: songMenu.popup(moreButton, 0, moreButton.height + 4)
        }
    }

    Text {
        id: artistLine
        visible: !root.compact
        x: root.pad
        y: titleRow.y + titleRow.height + 8
        width: Math.min(implicitWidth, root.width - 2 * root.pad)
        text: root.song.artistText
        elide: Text.ElideRight
        color: Qt.alpha(ColorScope.scheme.onPrimaryContainer, 0.8)
        font: Theme.type.titleMedium.font

        HoverHandler {
            enabled: root.song.artists.length > 0
            cursorShape: Qt.PointingHandCursor
        }
        TapHandler {
            onTapped: root.openArtists()
        }
    }

    Chip {
        id: albumChip
        visible: !root.compact && root.song.albumTitle !== ""
        x: root.pad
        y: artistLine.y + artistLine.implicitHeight + 16
        glyph: Symbols.album
        text: root.song.albumTitle
        maximumLabelWidth: root.width - 2 * root.pad - 64
        enabled: root.song.albumKey !== ""
        onClicked: root.albumRequested(root.song.albumKey)
    }

    // The seek bar: waves while playing, flat while paused. Dragging previews; the seek
    // happens on release (as the mini player's).
    Slider {
        id: seek
        property real heldMs: -1
        readonly property real shownMs: pressed ? valueAt(position) : value
        function seekTo(ms: real): void {
            heldMs = ms;
            App.player.seek(ms);
            caughtUp.restart();
        }
        visible: !root.compact
        x: root.pad
        y: artistLine.y + artistLine.implicitHeight + 16 + 32 + 24
        width: root.width - 2 * root.pad
        trackHeight: 6
        handleHeight: 36
        inactiveTint: Qt.alpha(ColorScope.scheme.onPrimaryContainer, 0.24)
        wavy: true
        waving: root.playing && !pressed && !Theme.motion.reduced
        enabled: App.player.seekable && App.player.durationMs > 0
        live: false
        from: 0
        to: Math.max(1, App.player.durationMs)
        stepSize: 5000
        // Bounded by `to`, see MiniPlayer's seek bar.
        value: Math.min(to, pressed ? value : heldMs >= 0 ? heldMs : root.positionMs)
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
        id: elapsed
        visible: !root.compact
        x: root.pad
        y: seek.y + seek.implicitHeight + 4
        text: App.format.duration(seek.shownMs)
        color: Qt.alpha(ColorScope.scheme.onPrimaryContainer, 0.8)
        font: Fonts.tabular(Theme.type.labelMedium.font)
    }
    Text {
        visible: !root.compact
        anchors.right: parent.right
        anchors.rightMargin: root.pad
        y: elapsed.y
        text: App.player.durationMs <= 0 ? "--:--"
              : App.settings.playerTimeRight === 1 ? App.format.duration(App.player.durationMs)
              : "-" + App.format.duration(App.player.durationMs - seek.shownMs)
        color: Qt.alpha(ColorScope.scheme.onPrimaryContainer, 0.8)
        font: Fonts.tabular(Theme.type.labelMedium.font)
    }

    // Previous, play or pause, next: an MD3E standard button group (a pressed button
    // widens and squeezes its neighbours).
    StandardButtonGroup {
        id: transport
        visible: !root.compact
        x: (root.width - width) / 2
        y: elapsed.y + elapsed.implicitHeight + 24

        IconButton {
            anchors.verticalCenter: parent.verticalCenter
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
            anchors.verticalCenter: parent.verticalCenter
            variant: Md.Filled
            size: Md.Large
            widthMode: Md.Wide
            // 200 wide; the group's press expansion still applies.
            width: 200 + widthDelta
            shape: root.playing || root.loading ? Md.Square : Md.Round
            glyph: root.loading ? "" : root.playing ? Symbols.pause : Symbols.play_arrow
            iconFill: 1
            text: root.loading ? qsTr("正在加载") : root.playing ? qsTr("暂停") : qsTr("播放")
            onClicked: root.playPause()

            LoadingIndicator {
                anchors.centerIn: parent
                implicitWidth: 40
                implicitHeight: 40
                visible: root.loading
                color: ColorScope.scheme.onPrimary
            }
        }
        IconButton {
            anchors.verticalCenter: parent.verticalCenter
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

    // --- Compact strip -------------------------------------------------------------

    CoverArt {
        id: smallCover
        visible: root.compact
        x: 12
        y: 12
        width: 72
        height: 72
        radius: Theme.shape.large
        opacity: root.flying ? 0 : 1
        coverKey: root.song.coverKey
    }
    Item {
        visible: root.compact
        x: smallCover.x + smallCover.width + 16
        width: compactPlay.x - x - 16
        height: compactTitle.implicitHeight + 2 + compactArtist.implicitHeight
        anchors.verticalCenter: parent.verticalCenter

        Text {
            id: compactTitle
            width: parent.width
            text: root.song.title
            elide: Text.ElideRight
            color: ColorScope.scheme.onPrimaryContainer
            font: Fonts.font(700, 18)
        }
        Text {
            id: compactArtist
            y: compactTitle.implicitHeight + 2
            width: parent.width
            text: root.song.artistText
            elide: Text.ElideRight
            color: Qt.alpha(ColorScope.scheme.onPrimaryContainer, 0.8)
            font: Theme.type.bodyMedium.font
        }
    }
    IconButton {
        id: compactPlay
        visible: root.compact
        x: compactNext.x - width - 8
        anchors.verticalCenter: parent.verticalCenter
        variant: Md.Filled
        size: Md.Medium
        widthMode: Md.Wide
        shape: root.playing || root.loading ? Md.Square : Md.Round
        glyph: root.playing || root.loading ? Symbols.pause : Symbols.play_arrow
        iconFill: 1
        text: root.playing ? qsTr("暂停") : qsTr("播放")
        onClicked: root.playPause()
    }
    IconButton {
        id: compactNext
        visible: root.compact
        x: root.width - width - 20
        anchors.verticalCenter: parent.verticalCenter
        variant: Md.Tonal
        size: Md.Medium
        enabled: App.player.canNext
        glyph: Symbols.skip_next
        iconFill: 1
        text: qsTr("下一首")
        onClicked: App.player.next()
    }

    // --- Menus ---------------------------------------------------------------------

    Menu {
        id: songMenu
        MenuItem {
            text: qsTr("歌曲详情")
            glyph: Symbols.info
            enabled: !root.song.voice
            onTriggered: root.songRequested()
        }
        MenuItem {
            text: qsTr("评论")
            glyph: Symbols.comment
            enabled: !root.song.voice
            onTriggered: root.commentsRequested()
        }
        MenuItem {
            text: qsTr("加入歌单")
            glyph: Symbols.playlist_add
            enabled: !root.song.voice
            onTriggered: root.addToPlaylistRequested(root.song.key, root.song.title)
        }
        MenuItem {
            text: qsTr("复制歌曲信息")
            glyph: Symbols.content_copy
            onTriggered: root.copyRequested()
        }
    }
    Menu {
        id: artistMenu
        Instantiator {
            model: root.song.artists
            delegate: MenuItem {
                required property var modelData
                enabled: modelData.key !== ""
                glyph: Symbols.person
                text: modelData.name
                onTriggered: root.artistRequested(modelData.key)
            }
            onObjectAdded: (index, object) => artistMenu.insertItem(index, object as MenuItem)
            onObjectRemoved: (index, object) => artistMenu.removeItem(object as MenuItem)
        }
    }
}
