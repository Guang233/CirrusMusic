pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Presentation

// A playlist, album, artist or podcast card: cover (round for artists), title and subtitle.
// `text` is the title. The state layer covers the whole card. A playlist's play count,
// when known (`plays` >= 0), sits on the cover's lower left (discover.md D4). A square
// cover's corners round further on hover and tighten on press (expressive.md §3.4).
T.AbstractButton {
    id: control

    property int kind: Collection.Playlist
    property string coverKey
    property string subtitle
    property real plays: -1

    readonly property bool artist: kind === Collection.Artist

    implicitWidth: 168
    implicitHeight: implicitContentHeight + topPadding + bottomPadding
    padding: 8
    focusPolicy: Qt.TabFocus
    Accessible.name: [text, subtitle,
                      plays >= 0 ? qsTr("播放 %1 次").arg(App.format.count(plays)) : ""]
        .filter(part => part !== "").join("，")

    Spring {
        id: corner
        target: control.down ? Theme.shape.medium
              : control.hovered ? Theme.shape.extraLarge : Theme.shape.large
        spec: Theme.motion.fastSpatial
    }

    // The shell flies this cover to the page the card opens (expressive.md §5).
    onReleased: CoverHandoff.note(coverArt, artist)

    HoverHandler {
        enabled: control.hoverEnabled
        cursorShape: Qt.PointingHandCursor
    }
    contentItem: Column {
        spacing: 8

        CoverArt {
            id: coverArt
            width: control.availableWidth
            height: width
            radius: control.artist ? width / 2 : Math.max(0, corner.value)
            glyph: control.kind === Collection.Album ? Symbols.album
                 : control.artist ? Symbols.person
                 : control.kind === Collection.Podcast ? Symbols.podcasts : Symbols.queue_music
            coverKey: control.coverKey

            // Text on an image: a small translucent plate keeps it legible on any cover.
            Rectangle {
                x: 6
                y: parent.height - height - 6
                visible: control.plays >= 0 && !control.artist
                width: plays.implicitWidth + 12
                height: 20
                radius: Theme.shape.extraSmall
                color: Qt.alpha(ColorScope.scheme.inverseSurface, 0.72)

                Row {
                    id: plays
                    anchors.centerIn: parent
                    spacing: 2
                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        glyph: Symbols.play_arrow
                        fill: 1
                        size: 14
                        color: ColorScope.scheme.inverseOnSurface
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: App.format.count(control.plays)
                        color: ColorScope.scheme.inverseOnSurface
                        font: Theme.type.labelSmall.font
                    }
                }
            }
        }
        Column {
            width: parent.width
            Text {
                width: parent.width
                horizontalAlignment: control.artist ? Text.AlignHCenter : Text.AlignLeft
                text: control.text
                elide: Text.ElideRight
                color: ColorScope.scheme.onSurface
                font: Theme.type.titleSmall.font
            }
            Text {
                width: parent.width
                visible: text !== ""
                horizontalAlignment: control.artist ? Text.AlignHCenter : Text.AlignLeft
                text: control.subtitle
                elide: Text.ElideRight
                color: ColorScope.scheme.onSurfaceVariant
                font: Theme.type.bodySmall.font
            }
        }
    }

    background: Item {
        StateLayer {
            anchors.fill: parent
            radius: Theme.shape.large
            color: ColorScope.scheme.onSurface
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
