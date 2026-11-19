import QtQuick
import Cirrus.Theme
import Cirrus.Presentation

// The song a player view shows, changing with a shared-axis move
// (docs/ui/pages/player-expressive.md §3): when the song changes, what shows of the
// old one slides a little toward where it goes and fades (fastEffects), then the new
// one comes in from the other side (defaultSpatial). Forward in the queue moves left,
// back moves right. Views read `song` instead of App.player.current and put
// `offset` (−1…1, times their own distance) and `fade` on the parts that move.
//   SongSwap { id: swap; animate: root.visible }
//   Text { text: swap.song.title; opacity: swap.fade; transform: Translate { x: swap.offset * 24 } }
Item {
    id: root

    // Off: the song changes at once (hidden, reduced motion).
    property bool animate: true
    // Set by hand, not bound: a binding would take the new song before the handler
    // below could slide the old one out.
    property var song
    property real offset: 0
    property real fade: 1
    property int direction: 1
    property int lastIndex: -1

    Component.onCompleted: {
        song = App.player.current;
        lastIndex = App.player.queueIndex;
    }

    Connections {
        target: App.player
        function onCurrentChanged() {
            const next = App.player.current;
            const index = App.player.queueIndex;
            root.direction = index >= root.lastIndex ? 1 : -1;
            root.lastIndex = index;
            if (next.key === root.song.key || !root.animate || Theme.motion.reduced) {
                swap.stop();
                root.offset = 0;
                root.fade = 1;
                root.song = next;
                return;
            }
            swap.restart();
        }
    }

    SequentialAnimation {
        id: swap
        ParallelAnimation {
            NumberAnimation {
                target: root
                property: "offset"
                to: -root.direction
                duration: Theme.motion.fastEffects.duration
                easing: Theme.motion.fastEffects.easing
            }
            NumberAnimation {
                target: root
                property: "fade"
                to: 0
                duration: Theme.motion.fastEffects.duration
                easing: Theme.motion.fastEffects.easing
            }
        }
        ScriptAction {
            script: {
                root.song = App.player.current;
                root.offset = root.direction;
            }
        }
        ParallelAnimation {
            NumberAnimation {
                target: root
                property: "offset"
                to: 0
                duration: Theme.motion.defaultSpatial.duration
                easing: Theme.motion.defaultSpatial.easing
            }
            NumberAnimation {
                target: root
                property: "fade"
                to: 1
                duration: Theme.motion.defaultEffects.duration
                easing: Theme.motion.defaultEffects.easing
            }
        }
    }
}
