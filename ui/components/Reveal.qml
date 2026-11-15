pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme

// Rises `target` in when its content arrives (expressive.md §5): 16 up and a fade in,
// `order` steps of 40 ms after the blocks shown at the same moment, so a page's sections
// come in one after another. The rise is a standard spatial spring (the blocks are large),
// the fade an effects spring, so with reduced motion it only fades
// (motion-choreography.md §1.3). It takes over the target's opacity and transform.
//   Reveal { target: card; shown: pageState.showsContent; order: 2 }
QtObject {
    id: root

    property Item target
    property bool shown: false
    property int order: 0
    property real distance: 16
    // Plays the first time only, not again after a refresh or a reload.
    property bool once: true

    property bool revealed: false

    function update(): void {
        if (shown && !revealed) {
            if (order > 0)
                delay.restart();
            else
                revealed = true;
        } else if (!shown && !once) {
            delay.stop();
            revealed = false;
        }
    }

    onShownChanged: update()
    onTargetChanged: {
        if (target)
            target.transform = [shift];
    }
    Component.onCompleted: {
        if (target)
            target.transform = [shift];
        update();
    }

    readonly property Timer delay: Timer {
        interval: root.order * 40
        onTriggered: root.revealed = true
    }
    readonly property Spring rise: Spring {
        target: root.revealed ? 1 : 0
        spec: Theme.motion.standardDefaultSpatial
        threshold: 0.002
    }
    readonly property Spring fade: Spring {
        target: root.revealed ? 1 : 0
        spec: Theme.motion.defaultEffects
        threshold: 0.002
    }
    readonly property Translate shift: Translate {
        y: root.distance * (1 - root.rise.value)
    }
    readonly property Binding opacityBinding: Binding {
        target: root.target
        when: root.target !== null
        property: "opacity"
        value: Math.max(0, Math.min(1, root.fade.value))
    }
}
