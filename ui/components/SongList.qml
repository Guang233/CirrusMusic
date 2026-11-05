pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Presentation

// Songs of a SongListModel as a segmented list (controls.md §8): cover or track
// number, title, artists and album, duration; the playing song is highlighted.
// Activating a row plays the list from it unless `playsOnActivate` is off. The more
// button (shown on hover) and a right click open the song menu, which asks the page
// to open the song's album or artists.
//   SongList { model: pageModel.songs; header: …; onAlbumRequested: key => … }
ListView {
    id: list

    property bool showCovers: true
    property bool showAlbum: true
    property bool playsOnActivate: true
    property real sideMargin: 24
    // Off on the album's own page.
    property bool showAlbumAction: true
    // Paged lists load the next page as the end comes near (set footer to a
    // LoadMoreFooter for the indicator). A failed page waits for its retry button.
    property PagedModel pager

    function maybeLoadMore(): void {
        if (pager && pager.canLoadMore && pager.loadMoreError.kind < 0
                && contentY + height > contentHeight + originY - 600)
            pager.loadMore();
    }

    signal activated(int index)
    signal albumRequested(string key)
    signal artistRequested(string key)

    readonly property SongListModel songs: model as SongListModel
    // At the top until the user scrolls: a header that grows once its data arrives
    // moves originY, and the view would otherwise keep showing the old top.
    property bool pinnedToTop: true

    onContentYChanged: {
        pinnedToTop = contentY <= originY + 1;
        maybeLoadMore();
    }
    onCountChanged: Qt.callLater(maybeLoadMore)
    onOriginYChanged: {
        if (pinnedToTop)
            contentY = originY;
    }

    clip: true
    spacing: 2
    bottomMargin: 24
    // Overshoots and springs back at the ends.
    boundsBehavior: Flickable.DragAndOvershootBounds
    ScrollBar.vertical: ScrollBar {}

    onActivated: index => {
        if (playsOnActivate && songs)
            App.player.playSongs(songs.keys(), index);
    }

    delegate: ListItem {
        id: row

        required property int index
        required property string title
        required property string secondaryTitle
        required property string artistText
        required property string albumKey
        required property string albumTitle
        required property var artists
        required property string coverKey
        required property string durationText
        required property bool loaded
        required property bool current

        x: list.sideMargin
        width: list.width - 2 * list.sideMargin
        segmented: true
        first: index === 0
        last: index === list.count - 1
        containerColor: ColorScope.scheme.surfaceContainer
        highlighted: current
        text: !loaded ? "" : secondaryTitle !== "" ? qsTr("%1（%2）").arg(title).arg(secondaryTitle)
                                                   : title
        supportingText: list.showAlbum && albumTitle !== "" ? artistText + " · " + albumTitle
                                                            : artistText
        onClicked: list.activated(index)

        leading: list.showCovers ? coverComponent : numberComponent
        trailing: Component {
            Row {
                spacing: 4
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: row.loaded
                    text: row.durationText
                    color: ColorScope.scheme.onSurfaceVariant
                    font: Theme.type.labelSmall.font
                }
                IconButton {
                    id: more
                    anchors.verticalCenter: parent.verticalCenter
                    size: Md.XSmall
                    // Keeps its place, so durations line up across rows.
                    opacity: row.hovered || activeFocus || songMenu.row === row ? 1 : 0
                    enabled: row.loaded
                    glyph: Symbols.more_vert
                    text: qsTr("更多")
                    onClicked: songMenu.openFor(row, row.albumKey, row.artists, more, 0, more.height)
                }
            }
        }

        TapHandler {
            acceptedButtons: Qt.RightButton
            enabled: row.loaded
            onTapped: eventPoint => songMenu.openFor(row, row.albumKey, row.artists, row,
                                                     eventPoint.position.x, eventPoint.position.y)
        }

        Component {
            id: coverComponent
            CoverArt {
                width: 48
                height: 48
                coverKey: row.coverKey
            }
        }
        Component {
            id: numberComponent
            Item {
                width: 32
                height: 48
                Text {
                    anchors.centerIn: parent
                    visible: !row.current
                    text: row.index + 1
                    color: ColorScope.scheme.onSurfaceVariant
                    font: Theme.type.titleSmall.font
                }
                Icon {
                    anchors.centerIn: parent
                    visible: row.current
                    glyph: Symbols.graphic_eq
                    size: 20
                    color: ColorScope.scheme.onSecondaryContainer
                }
            }
        }

        // Details still on their way: placeholder bars instead of text.
        Column {
            x: row.leftPadding + (list.showCovers ? 60 : 44)
            anchors.verticalCenter: parent.verticalCenter
            visible: !row.loaded
            spacing: 8
            Skeleton {
                width: 160
                height: 14
            }
            Skeleton {
                width: 100
                height: 12
            }
        }
    }

    Menu {
        id: songMenu

        // The row the menu is open for.
        property Item row: null
        property string albumKey
        property var artists: []

        function openFor(target: Item, album: string, songArtists: var, anchor: Item, x: real,
                         y: real): void {
            row = target;
            albumKey = album;
            artists = songArtists;
            popup(anchor, x, y);
        }

        onClosed: row = null

        MenuItem {
            visible: list.showAlbumAction
            height: visible ? implicitHeight : 0
            enabled: songMenu.albumKey !== ""
            glyph: Symbols.album
            text: qsTr("查看专辑")
            onTriggered: list.albumRequested(songMenu.albumKey)
        }
        Instantiator {
            model: songMenu.artists
            delegate: MenuItem {
                required property var modelData
                enabled: modelData.key !== ""
                glyph: Symbols.person
                text: qsTr("查看歌手：%1").arg(modelData.name)
                onTriggered: list.artistRequested(modelData.key)
            }
            onObjectAdded: (index, object) => songMenu.insertItem(index + 1, object as MenuItem)
            onObjectRemoved: (index, object) => songMenu.removeItem(object as MenuItem)
        }
    }
}
