import QtQuick
import Cirrus.Theme
import Cirrus.Controls

// A symbol as a cover, for song lists without a picture of their own (the recent and
// ranking pages, library.md §3): a filled icon on a tonal container, extra-large
// corners like the covers it stands in for (small in the compact bar).
Rectangle {
    id: tile

    property string glyph
    property color contentColor: Theme.color.onSecondaryContainer
    readonly property bool small: width < 100

    radius: small ? Theme.shape.small : Theme.shape.extraLarge
    color: Theme.color.secondaryContainer

    Icon {
        anchors.centerIn: parent
        glyph: tile.glyph
        size: Math.round(tile.width * 0.42)
        fill: 1
        color: tile.contentColor
    }
}
