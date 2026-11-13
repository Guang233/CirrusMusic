pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme

// A quick grow and spring back, for an icon turning on (a like, expressive.md §5):
// `scale` goes to 1.2 and returns with the expressive fast spatial spring's overshoot.
// `arm()` on the press, then `filled()` when the icon fills: it plays only for a fill
// that follows the user's click (not one arriving with data), and not with reduced motion.
//   Pop { id: pop }  Icon { scale: pop.scale }  onClicked: pop.arm()  onIconFillChanged: …
QtObject {
    id: root

    readonly property real scale: 1 + 0.2 * spring.value
    property double armedAt: 0

    function arm(): void {
        armedAt = Date.now();
    }
    function filled(): void {
        if (Date.now() - armedAt > 2000 || Theme.motion.reduced)
            return;
        armedAt = 0;
        spring.target = 1;
        back.restart();
    }

    readonly property Spring spring: Spring {
        target: 0
        spec: Theme.motion.fastSpatial
        threshold: 0.002
    }
    readonly property Timer back: Timer {
        interval: 90
        onTriggered: root.spring.target = 0
    }
}
