pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components
import Cirrus.Presentation

// The queue view (docs/ui/pages/player.md §8), read-only for now: the whole queue in
// play order, opened at the playing song. Played songs above it are dimmed; labels
// mark the playing song and what comes next. In the player's language: no containers,
// content-colored text. Double-click or Enter plays a row; the context menu leads to
// the artist, the album, or adds to a playlist.
FocusScope {
    id: root

    required property PlayerPalette colors

    signal artistRequested(string key)
    signal albumRequested(string key)
    signal addToPlaylistRequested(string key, string title)

    readonly property int playing: App.player.queueIndex
    readonly property real rowHeight: 64

    // Back to the playing song, unless the pointer is over the list (someone reading).
    function showPlaying(): void {
        if (playing >= 0)
            list.positionViewAtIndex(playing, ListView.Beginning);
    }
    onPlayingChanged: if (!listHover.hovered && !list.moving)
                          Qt.callLater(showPlaying)
    onVisibleChanged: if (visible)
                          Qt.callLater(showPlaying)

    Text {
        anchors.left: parent.left
        y: parent.height * 0.3
        visible: list.count === 0
        text: qsTr("播放队列为空")
        color: root.colors.contentVariant
        font: Fonts.font(700, 28)
    }

    ListView {
        id: list
        anchors.fill: parent
        clip: true
        focus: true
        model: App.player.queue
        currentIndex: Math.max(0, root.playing)
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
            const first = indexAt(0, contentY);
            App.player.ensureQueueLoaded((first < 0 ? 0 : first) - 20,
                                         (first < 0 ? 0 : first) + Math.ceil(height / root.rowHeight) + 20);
        }

        Keys.onReturnPressed: App.player.playAt(currentIndex)
        Keys.onEnterPressed: App.player.playAt(currentIndex)

        HoverHandler {
            id: listHover
        }

        delegate: Item {
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
            readonly property bool played: root.playing >= 0 && index < root.playing
            // A label above the first played song, the playing one and the next.
            readonly property string label: isPlaying ? qsTr("正在播放")
                                            : index === root.playing + 1 ? qsTr("接下来播放")
                                            : index === 0 && played ? qsTr("已播放") : ""

            width: ListView.view.width
            height: (label !== "" ? labelText.implicitHeight + 16 : 0) + root.rowHeight
            Accessible.role: Accessible.ListItem
            Accessible.name: title + " " + artistText

            Text {
                id: labelText
                y: 8
                visible: row.label !== ""
                text: row.label
                color: root.colors.contentVariant
                font: Fonts.font(700, 14)
            }

            Item {
                id: body
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: root.rowHeight
                opacity: row.played || row.unavailable ? 0.5 : 1

                Rectangle {
                    anchors.fill: parent
                    radius: Theme.shape.medium
                    color: Qt.alpha(root.colors.content, 0.08)
                    visible: rowHover.hovered || focusShown
                    readonly property bool focusShown: list.activeFocus && InputMode.keyboard
                                                       && list.currentIndex === row.index

                    FocusRing {
                        shown: parent.focusShown
                        offset: -2
                        containerRadius: parent.radius
                        border.color: root.colors.content
                    }
                }

                CoverArt {
                    id: cover
                    x: 8
                    anchors.verticalCenter: parent.verticalCenter
                    width: 48
                    height: 48
                    coverKey: row.coverKey
                }
                Column {
                    anchors.left: cover.right
                    anchors.leftMargin: 16
                    anchors.right: playingIcon.left
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2

                    Text {
                        width: parent.width
                        text: row.loaded ? row.title : " "
                        elide: Text.ElideRight
                        color: root.colors.content
                        font: Fonts.font(row.isPlaying ? 700 : 600, 16)
                    }
                    Text {
                        width: parent.width
                        text: row.loaded ? row.artistText : " "
                        elide: Text.ElideRight
                        color: root.colors.contentVariant
                        font: Fonts.font(500, 14)
                    }
                }
                Icon {
                    id: playingIcon
                    anchors.right: parent.right
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    visible: row.isPlaying
                    width: visible ? implicitWidth : 0
                    glyph: Symbols.graphic_eq
                    color: root.colors.content
                }

                HoverHandler {
                    id: rowHover
                }
                TapHandler {
                    acceptedButtons: Qt.LeftButton
                    onTapped: list.currentIndex = row.index
                    onDoubleTapped: App.player.playAt(row.index)
                }
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: eventPoint => {
                        list.currentIndex = row.index;
                        menu.row = row;
                        menu.popup(body, eventPoint.position.x, eventPoint.position.y);
                    }
                }
            }
        }
    }

    // Menu key / Shift+F10 opens the menu at the current row.
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
