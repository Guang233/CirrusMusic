pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Presentation

// A playlist or album in a Carousel: the cover fills the item and is cropped as the
// item narrows; the title (bold) and `subtitle` sit on a shade at its lower edge and go
// when there is no room for them. While hovered (or current) a large item shows a play
// button (`playRequested`). `text` is the title. The carousel is the tab stop:
// `current` shows its keyboard focus here.
T.AbstractButton {
    id: control

    property int kind: Collection.Playlist
    property string coverKey
    property string subtitle
    // Offers the play button.
    property bool playable: false
    // The widest the item gets: the cover is decoded once, for that.
    property real largeWidth: width
    property bool current: false

    // 0 while small, 1 from about a medium item up: the words fade with it.
    readonly property real room: Math.max(0, Math.min(1, (width - 112) / 40))
    // Words on the darkened cover stay light in both themes (as on a photo).
    readonly property color onShade: "white"

    signal playRequested

    focusPolicy: Qt.NoFocus
    Accessible.name: text

    // The shell flies this cover to the page the card opens (expressive.md §5).
    onReleased: CoverHandoff.note(coverArt, false)

    // Activated from the keyboard, by the carousel.
    function trigger(): void {
        CoverHandoff.note(coverArt, false);
        clicked();
    }

    HoverHandler {
        cursorShape: Qt.PointingHandCursor
    }

    contentItem: Item {
        CoverArt {
            id: coverArt
            anchors.fill: parent
            radius: Math.min(Theme.shape.extraLarge, width / 2)
            sourceSize: Qt.size(decodeLength(control.largeWidth), decodeLength(control.largeWidth))
            glyph: control.kind === Collection.Album ? Symbols.album : Symbols.queue_music
            coverKey: control.coverKey
        }

        // Text on an image: a shade from the lower edge keeps it legible on any cover.
        Item {
            anchors.fill: parent
            visible: opacity > 0
            opacity: control.room

            Rectangle {
                width: parent.width
                height: Math.min(130, parent.height * 0.53)
                y: parent.height - height
                bottomLeftRadius: coverArt.radius
                bottomRightRadius: coverArt.radius
                gradient: Gradient {
                    GradientStop {
                        position: 0
                        color: Qt.alpha(ColorScope.scheme.scrim, 0)
                    }
                    GradientStop {
                        position: 1
                        color: Qt.alpha(ColorScope.scheme.scrim, 0.6)
                    }
                }
            }
            Column {
                x: 20
                width: parent.width - 40 - (play.visible ? play.width + 8 : 0)
                y: parent.height - height - 20
                spacing: 2
                Text {
                    width: parent.width
                    text: control.text
                    elide: Text.ElideRight
                    color: control.onShade
                    font: Fonts.font(700, 16)
                }
                Text {
                    width: parent.width
                    visible: control.subtitle !== ""
                    text: control.subtitle
                    elide: Text.ElideRight
                    color: Qt.alpha(control.onShade, 0.8)
                    font: Theme.type.bodySmall.font
                }
            }
        }

        IconButton {
            id: play
            x: parent.width - width - 16
            y: parent.height - height - 16
            visible: control.playable && control.room >= 1 && (control.hovered || control.current)
            variant: Md.Filled
            size: Md.Medium
            glyph: Symbols.play_arrow
            iconFill: 1
            focusPolicy: Qt.NoFocus
            text: qsTr("播放")
            onClicked: control.playRequested()
        }
    }

    background: Item {
        StateLayer {
            anchors.fill: parent
            radius: Math.min(Theme.shape.extraLarge, width / 2)
            color: ColorScope.scheme.onSurface
            hovered: control.hovered
            pressed: control.down
            pressX: control.pressed ? control.pressX : width / 2
            pressY: control.pressed ? control.pressY : height / 2
        }
        FocusRing {
            shown: control.current
            offset: -2
            containerRadius: Math.min(Theme.shape.extraLarge, width / 2)
        }
    }
}
