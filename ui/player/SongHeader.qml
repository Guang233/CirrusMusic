pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Presentation

// Title and artist of the playing song with its "more" menu (docs/ui/pages/player.md
// §1.1). The artist opens the artist page; the menu has comments, the artist, the
// album and adding to a playlist. Each leaves the player first (the page's signals).
Item {
    id: root

    required property PlayerPalette colors
    // Smaller type in the narrow header row.
    property bool compact: false
    // The title's size; the page grows it with the cover.
    property real titleSize: 24
    // The "more" button right after the text instead of at the right edge (the bottom
    // row, where the header is wide).
    property bool moreAfterText: false

    signal commentsRequested
    signal artistRequested(string key)
    signal albumRequested(string key)
    signal addToPlaylistRequested(string key, string title)

    readonly property var song: App.player.current
    readonly property bool menuOpen: menu.opened

    implicitHeight: Math.max(texts.implicitHeight, more.implicitHeight)

    Column {
        id: texts
        readonly property real room: root.width - more.width - 8
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        width: root.moreAfterText ? Math.min(room, Math.max(titleText.implicitWidth, artist.implicitWidth))
                                  : room
        spacing: 2

        Text {
            id: titleText
            width: parent.width
            text: root.song.title
            elide: Text.ElideRight
            color: root.colors.content
            font: Fonts.font(700, root.compact ? 17 : root.titleSize)
            Accessible.role: Accessible.Heading
        }
        Text {
            id: artist
            width: Math.min(parent.width, implicitWidth)
            text: App.player.status === Playback.Failed ? App.player.failureMessage
                                                        : root.song.artistText
            elide: Text.ElideRight
            color: root.colors.contentVariant
            font: Fonts.font(500, root.compact ? 14 : root.titleSize * 0.75)

            HoverHandler {
                id: artistHover
                enabled: root.song.artistKeys.length > 0
                cursorShape: Qt.PointingHandCursor
            }
            TapHandler {
                enabled: root.song.artistKeys.length > 0
                onTapped: root.artistRequested(root.song.artistKeys[0])
            }
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: parent.color
                visible: artistHover.hovered
            }
        }
    }

    PlayerButton {
        id: more
        x: root.moreAfterText ? texts.width + 8 : root.width - width
        anchors.verticalCenter: parent.verticalCenter
        visible: root.song.key !== ""
        color: root.colors.content
        glyph: Symbols.more_horiz
        text: qsTr("更多")
        onClicked: menu.popup(more, 0, more.height + 4)
    }

    Menu {
        id: menu
        MenuItem {
            text: qsTr("评论")
            glyph: Symbols.comment
            enabled: !root.song.voice
            onTriggered: root.commentsRequested()
        }
        MenuItem {
            text: qsTr("查看歌手")
            glyph: Symbols.person
            enabled: root.song.artistKeys.length > 0
            onTriggered: root.artistRequested(root.song.artistKeys[0])
        }
        MenuItem {
            text: qsTr("查看专辑")
            glyph: Symbols.album
            enabled: root.song.albumKey !== ""
            onTriggered: root.albumRequested(root.song.albumKey)
        }
        MenuItem {
            text: qsTr("加入歌单")
            glyph: Symbols.playlist_add
            enabled: !root.song.voice
            onTriggered: root.addToPlaylistRequested(root.song.key, root.song.title)
        }
    }
}
