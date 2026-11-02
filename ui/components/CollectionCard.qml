pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Presentation

// A playlist, album, artist or podcast card: cover (round for artists), title and subtitle.
// `text` is the title. The state layer covers the whole card.
T.AbstractButton {
    id: control

    property int kind: Collection.Playlist
    property string coverKey
    property string subtitle

    readonly property bool artist: kind === Collection.Artist

    implicitWidth: 168
    implicitHeight: implicitContentHeight + topPadding + bottomPadding
    padding: 8
    focusPolicy: Qt.TabFocus
    Accessible.name: subtitle !== "" ? text + "，" + subtitle : text

    contentItem: Column {
        spacing: 8

        CoverArt {
            width: control.availableWidth
            height: width
            radius: control.artist ? width / 2 : Theme.shape.medium
            glyph: control.kind === Collection.Album ? Symbols.album
                 : control.artist ? Symbols.person
                 : control.kind === Collection.Podcast ? Symbols.podcasts : Symbols.queue_music
            coverKey: control.coverKey
        }
        Column {
            width: parent.width
            Text {
                width: parent.width
                horizontalAlignment: control.artist ? Text.AlignHCenter : Text.AlignLeft
                text: control.text
                elide: Text.ElideRight
                color: Theme.color.onSurface
                font: Theme.type.titleSmall.font
            }
            Text {
                width: parent.width
                visible: text !== ""
                horizontalAlignment: control.artist ? Text.AlignHCenter : Text.AlignLeft
                text: control.subtitle
                elide: Text.ElideRight
                color: Theme.color.onSurfaceVariant
                font: Theme.type.bodySmall.font
            }
        }
    }

    background: Item {
        StateLayer {
            anchors.fill: parent
            radius: Theme.shape.large
            color: Theme.color.onSurface
            hovered: control.hovered
            pressed: control.down
            focused: control.visualFocus
            pressX: control.pressed ? control.pressX : width / 2
            pressY: control.pressed ? control.pressY : height / 2
        }
        FocusRing {
            shown: control.visualFocus
            offset: -2
            containerRadius: Theme.shape.large
        }
    }
}
