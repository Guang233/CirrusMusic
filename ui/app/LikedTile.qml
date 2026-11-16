pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Presentation

// "我喜欢的音乐" on the home page (docs/ui/redesign-md3e.md §6.2): one click plays the
// account's liked songs; the tile itself opens that playlist. A large faint heart
// behind the words is its mark.
Rectangle {
    id: tile

    required property AppShell shell
    readonly property color content: Theme.color.onSecondaryContainer
    readonly property bool known: App.library.likedPlaylistKey !== ""

    // Plays the liked playlist: its songs load when asked.
    function play(): void {
        liked.playWhenReady = true;
        if (liked.playlistKey === App.library.likedPlaylistKey)
            liked.refresh();
        liked.playlistKey = App.library.likedPlaylistKey;
    }

    radius: 40
    color: Theme.color.secondaryContainer
    clip: true
    Accessible.role: Accessible.Button
    Accessible.name: qsTr("我喜欢的音乐")

    PlaylistPageModel {
        id: liked
        property bool playWhenReady: false
        onStateChanged: {
            if (playWhenReady && (state === LoadState.Ready || state === LoadState.Partial)) {
                playWhenReady = false;
                if (songs.count > 0)
                    App.player.playSongs(songs.keys(), 0, Playback.FromPlaylist, playlistKey, title);
            }
        }
    }

    HoverHandler {
        id: hover
        cursorShape: tile.known ? Qt.PointingHandCursor : Qt.ArrowCursor
    }
    TapHandler {
        enabled: tile.known
        onTapped: tile.shell.openLikedPlaylist()
    }

    Icon {
        x: tile.width - width * 0.72
        y: tile.height - height * 0.74
        glyph: Symbols.favorite
        size: Math.round(Math.min(tile.width, tile.height) * 0.95)
        fill: 1
        color: Qt.alpha(tile.content, 0.12)
    }
    StateLayer {
        anchors.fill: parent
        radius: tile.radius
        color: tile.content
        hovered: hover.hovered && tile.known
    }

    Column {
        x: 24
        y: 22
        width: tile.width - 48
        spacing: 2
        Text {
            width: parent.width
            text: qsTr("我喜欢的音乐")
            elide: Text.ElideRight
            color: tile.content
            font: Theme.type.titleMediumEmphasized.font
        }
        Text {
            width: parent.width
            visible: App.library.likedCount > 0
            text: qsTr("%n 首", "", App.library.likedCount)
            elide: Text.ElideRight
            color: tile.content
            opacity: 0.85
            font: Theme.type.bodyMedium.font
        }
    }

    IconButton {
        x: 24
        y: tile.height - height - 24
        variant: Md.Filled
        size: Md.Medium
        enabled: tile.known && App.library.likedCount > 0
        glyph: Symbols.play_arrow
        iconFill: 1
        text: qsTr("播放我喜欢的音乐")
        onClicked: tile.play()
    }
}
