pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Presentation

// A playlist or album in a Carousel: the cover fills the item and is cropped as the
// item narrows; the title sits on a plate at its lower edge and goes when there is no
// room for it. `text` is the title. The carousel is the tab stop: `current` shows its
// keyboard focus here.
T.AbstractButton {
    id: control

    property int kind: Collection.Playlist
    property string coverKey
    // The widest the item gets: the cover is decoded once, for that.
    property real largeWidth: width
    property bool current: false

    // 0 while small, 1 from about a medium item up: the words fade with it.
    readonly property real room: Math.max(0, Math.min(1, (width - 112) / 40))

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

        // Text on an image: a translucent plate keeps it legible on any cover.
        Rectangle {
            x: 10
            y: parent.height - height - 10
            width: Math.min(title.implicitWidth + 20, parent.width - 20)
            height: 28
            radius: height / 2
            visible: opacity > 0
            opacity: control.room
            color: Qt.alpha(ColorScope.scheme.inverseSurface, 0.78)

            Text {
                id: title
                x: 10
                width: parent.width - 20
                anchors.verticalCenter: parent.verticalCenter
                text: control.text
                elide: Text.ElideRight
                color: ColorScope.scheme.inverseOnSurface
                font: Theme.type.labelLarge.font
            }
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
