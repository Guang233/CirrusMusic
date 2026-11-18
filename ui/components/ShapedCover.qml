pragma ComponentBehavior: Bound
import QtQuick
import M3Shapes
import Cirrus.Theme

// A cover cut to an MD3E shape that shows whether the song plays: a 12-sided cookie
// turning slowly while playing, a rounded square, upright, while paused; the shape
// morphs between the two (MorphShape). The side player's and the Material player
// page's cover (docs/ui/pages/player-expressive.md §4).
//   ShapedCover { coverKey: App.player.current.coverKey; playing: … }
CoverArt {
    id: root

    property bool playing: false
    // Turning while playing (never with reduced motion or while hidden).
    property bool turns: true

    shape: playing ? MaterialShape.Cookie12Sided : MaterialShape.Square
    radius: Theme.shape.extraLarge
    glyph: Symbols.music_note

    NumberAnimation on rotation {
        id: turning
        running: root.playing && root.turns && !Theme.motion.reduced && root.visible
        from: root.rotation % 360
        to: root.rotation % 360 + 360
        duration: 24000
        loops: Animation.Infinite
    }
    // Paused: back to the nearest upright turn of the square.
    NumberAnimation {
        id: settle
        target: root
        property: "rotation"
        duration: Theme.motion.defaultSpatial.duration
        easing: Theme.motion.defaultSpatial.easing
    }
    Connections {
        target: turning
        function onRunningChanged() {
            if (turning.running)
                return;
            settle.from = root.rotation % 360;
            settle.to = Math.round(root.rotation % 360 / 90) * 90;
            settle.start();
        }
    }
}
