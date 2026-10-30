pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Presentation

// Songs of a SongListModel as a segmented list (controls.md §8): cover or track
// number, title, artists and album, duration; the playing song is highlighted.
// Activating a row plays the list from it unless `playsOnActivate` is off.
//   SongList { model: pageModel.songs; header: … }
ListView {
    id: list

    property bool showCovers: true
    property bool showAlbum: true
    property bool playsOnActivate: true
    property real sideMargin: 24

    signal activated(int index)

    readonly property SongListModel songs: model as SongListModel

    clip: true
    spacing: 2
    bottomMargin: 24
    boundsBehavior: Flickable.StopAtBounds
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
        required property string albumTitle
        required property string coverKey
        required property string durationText
        required property bool loaded
        required property bool current

        x: list.sideMargin
        width: list.width - 2 * list.sideMargin
        segmented: true
        first: index === 0
        last: index === list.count - 1
        containerColor: Theme.color.surfaceContainer
        highlighted: current
        text: !loaded ? "" : secondaryTitle !== "" ? qsTr("%1（%2）").arg(title).arg(secondaryTitle)
                                                   : title
        supportingText: list.showAlbum && albumTitle !== "" ? artistText + " · " + albumTitle
                                                            : artistText
        trailingText: loaded ? durationText : ""
        onClicked: list.activated(index)

        leading: list.showCovers ? coverComponent : numberComponent

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
                    color: Theme.color.onSurfaceVariant
                    font: Theme.type.titleSmall.font
                }
                Icon {
                    anchors.centerIn: parent
                    visible: row.current
                    glyph: Symbols.graphic_eq
                    size: 20
                    color: Theme.color.onSecondaryContainer
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
}
