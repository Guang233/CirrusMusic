import QtQuick
import M3Shapes
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components

// A symbol as a cover, for song lists without a picture of their own (the recent and
// ranking pages, library.md §3): the symbol on a cookie shape on a tile with the
// extra-large corners of the covers it stands in for (expressive.md §3.6); in the
// compact bar (small) just a tonal tile with the symbol.
Rectangle {
    id: tile

    property string glyph
    property int tone: Md.Secondary
    readonly property bool small: width < 100

    radius: small ? Theme.shape.small : Theme.shape.extraLarge
    color: small ? mark.containerColor : Theme.color.surfaceContainerHigh

    IconContainer {
        id: mark
        anchors.centerIn: parent
        visible: !tile.small
        glyph: tile.glyph
        tone: tile.tone
        size: Math.round(tile.width * 0.6)
        shape: MaterialShape.Cookie12Sided
    }
    Icon {
        anchors.centerIn: parent
        visible: tile.small
        glyph: tile.glyph
        size: Math.round(tile.width * 0.42)
        fill: 1
        color: mark.contentColor
    }
}
