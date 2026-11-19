import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Presentation

// The seek bar of the Material player page and the side player
// (docs/ui/pages/player-expressive.md §2.1): an MD3E wavy slider in the surface's
// tones that waves while playing and lies flat while paused. Dragging previews; the
// seek happens on release (as the mini player's).
Slider {
    id: seek

    required property PlayerTones tones
    property real positionMs: 0
    property bool playing: false
    property real heldMs: -1
    readonly property real shownMs: pressed ? valueAt(position) : value

    function seekTo(ms: real): void {
        heldMs = ms;
        App.player.seek(ms);
        caughtUp.restart();
    }

    // The player page's: a long, slow wave (MD3's linear wavelength 40, half of it a
    // second) as PixelPlayer's, on a track of 6 with MD3E's bar handle (5 looked frail
    // on the page, 8 with a short fast wave heavy and restless; a round handle on a
    // thick track looked clumsy). The side player sets its own sizes and the dot.
    trackHeight: 6
    handleHeight: 28
    waveAmplitude: 4
    waveLength: 40
    waveSpeed: 20
    inactiveTint: tones.track
    wavy: true
    waving: playing && !pressed && !Theme.motion.reduced
    enabled: App.player.seekable && App.player.durationMs > 0
    live: false
    from: 0
    to: Math.max(1, App.player.durationMs)
    stepSize: 5000
    // Bounded by `to`, see MiniPlayer's seek bar.
    value: Math.min(to, pressed ? value : heldMs >= 0 ? heldMs : positionMs)
    Accessible.name: qsTr("播放进度")
    onPressedChanged: if (!pressed)
                          seekTo(valueAt(position))
    onMoved: if (!pressed)
                 seekTo(value)

    Connections {
        target: App.player
        function onChanged() {
            if (!seek.pressed)
                seek.heldMs = -1;
        }
    }
    Timer {
        id: caughtUp
        interval: 1500
        onTriggered: seek.heldMs = -1
    }
}
