pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components
import Cirrus.Presentation

// The play queue (docs/ui/pages/player.md §8), read-only for now, in a PlayerSheet:
// the whole queue in play order, opened at the playing song, which is the selected
// row. Double-click or Enter plays a row; the context menu (also the menu key or
// Shift+F10) leads to the artist, the album, or adds to a playlist.
FocusScope {
    id: root

    signal artistRequested(string key)
    signal albumRequested(string key)
    signal addToPlaylistRequested(string key, string title)

    readonly property int playing: App.player.queueIndex
    readonly property Item list: list

    // The playing song in view, near the top.
    function showPlaying(): void {
        if (playing >= 0) {
            list.currentIndex = playing;
            list.positionViewAtIndex(Math.max(0, playing - 1), ListView.Beginning);
        }
    }
    onPlayingChanged: if (!listHover.hovered && !list.moving)
                          Qt.callLater(showPlaying)
    onVisibleChanged: if (visible)
                          Qt.callLater(showPlaying)

    Text {
        x: 24
        visible: list.count === 0
        text: qsTr("播放队列为空")
        color: ColorScope.scheme.onSurfaceVariant
        font: Theme.type.bodyLarge.font
    }

    ListView {
        id: list
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        clip: true
        focus: true
        model: App.player.queue
        keyNavigationEnabled: true
        boundsBehavior: Flickable.StopAtBounds
        activeFocusOnTab: count > 0
        highlightFollowsCurrentItem: false
        Accessible.role: Accessible.List
        Accessible.name: qsTr("播放队列")
        ScrollBar.vertical: ScrollBar {}

        onContentYChanged: Qt.callLater(loadVisible)
        onHeightChanged: Qt.callLater(loadVisible)
        onCountChanged: Qt.callLater(loadVisible)
        function loadVisible(): void {
            const first = Math.max(0, indexAt(0, contentY));
            App.player.ensureQueueLoaded(first - 20, first + Math.ceil(height / 56) + 20);
        }

        Keys.onReturnPressed: App.player.playAt(currentIndex)
        Keys.onEnterPressed: App.player.playAt(currentIndex)

        HoverHandler {
            id: listHover
        }

        delegate: ListItem {
            id: row
            required property int index
            required property string key
            required property string title
            required property string artistText
            required property var artists
            required property string albumKey
            required property string coverKey
            required property bool loaded
            required property bool unavailable

            readonly property bool isPlaying: index === root.playing

            width: ListView.view.width
            focusPolicy: Qt.NoFocus
            highlighted: isPlaying
            enabled: !unavailable
            text: loaded ? title : " "
            supportingText: loaded ? artistText : " "
            trailingGlyph: isPlaying ? Symbols.graphic_eq : ""
            outerRadius: Theme.shape.medium
            first: true
            last: true
            leading: Component {
                CoverArt {
                    width: 40
                    height: 40
                    coverKey: row.coverKey
                }
            }
            onClicked: list.currentIndex = index
            onDoubleClicked: App.player.playAt(index)

            // The list is one Tab stop; ↑/↓ move this ring.
            FocusRing {
                shown: list.activeFocus && InputMode.keyboard && list.currentIndex === row.index
                offset: -2
                containerRadius: Theme.shape.medium
            }
            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: eventPoint => {
                    list.currentIndex = row.index;
                    menu.row = row;
                    menu.popup(row, eventPoint.position.x, eventPoint.position.y);
                }
            }
        }
    }

    Keys.onPressed: event => {
        if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10
                                          && event.modifiers & Qt.ShiftModifier)) {
            const item = list.currentItem;
            if (!item)
                return;
            menu.row = item;
            menu.popup(item, 24, item.height);
            event.accepted = true;
        }
    }

    Menu {
        id: menu
        // The row the menu is for (its model roles).
        property var row
        readonly property var song: row ?? ({})

        MenuItem {
            text: qsTr("播放")
            glyph: Symbols.play_arrow
            onTriggered: App.player.playAt(menu.song.index)
        }
        MenuItem {
            text: qsTr("查看歌手")
            glyph: Symbols.person
            enabled: (menu.song.artists?.length ?? 0) > 0 && menu.song.artists[0].key !== ""
            onTriggered: root.artistRequested(menu.song.artists[0].key)
        }
        MenuItem {
            text: qsTr("查看专辑")
            glyph: Symbols.album
            enabled: (menu.song.albumKey ?? "") !== ""
            onTriggered: root.albumRequested(menu.song.albumKey)
        }
        MenuItem {
            text: qsTr("加入歌单")
            glyph: Symbols.playlist_add
            enabled: menu.song.loaded ?? false
            onTriggered: root.addToPlaylistRequested(menu.song.key, menu.song.title)
        }
    }
}
