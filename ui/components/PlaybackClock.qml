import QtQuick
import Cirrus.Presentation

// Smooth playback position (docs/ui/presentation.md §5.1). App.player only reports a
// base value when the backend state changes; this extrapolates it every frame while
// playing: positionMs + (now - positionAt) × rate, within the known duration.
//   PlaybackClock { id: clock; active: bar.visible }
//   ProgressBar { value: clock.positionMs }
FrameAnimation {
    id: clock

    // Stops the per-frame updates while nothing shows the position.
    property bool active: true
    readonly property real positionMs: {
        const player = App.player;
        // Re-evaluated every frame while running, and on every snapshot.
        const now = clock.currentFrame >= 0 ? player.now() : 0;
        const position = player.positionMs + (now - player.positionAt) * player.rate;
        const bounded = player.durationMs >= 0 ? Math.min(position, player.durationMs) : position;
        return Math.max(0, bounded);
    }

    running: active && App.player.rate > 0
}
