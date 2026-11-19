import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Presentation

// The seek bar of the Material player page and the side player
// (docs/ui/pages/player-expressive.md §2.1): a thick MD3E wavy slider in the surface's
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

    // A thin track and a long, slow wave (MD3's linear wavelength 40, half of it a
    // second), a dot of 16 that stretches to a 24 tall bar: PixelPlayer's proportions;
    // thicker and faster, it looked heavy and restless beside the type.
    trackHeight: 5
    handleHeight: 24
    dotSize: 16
    waveAmplitude: 4
    waveLength: 40
    waveSpeed: 20
    inactiveTint: tones.track
    wavy: true
    roundHandle: true
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
