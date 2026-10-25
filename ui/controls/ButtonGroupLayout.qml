import QtQuick
import QtQuick.Templates as T

// Shared row behind StandardButtonGroup and ConnectedButtonGroup. Children are
// Buttons/IconButtons: the pressed one grows by `expandedRatio` of its width and
// its neighbours give up the same amount (via their widthDelta springs).
Row {
    id: root

    property bool connected: false
    property bool exclusive: false
    property real expandedRatio: 0.15

    property var trackedButtons: []

    function groupButtons() {
        const result = [];
        for (let i = 0; i < root.children.length; ++i) {
            const child = root.children[i];
            if (child instanceof T.AbstractButton && child.visible && child.widthDelta !== undefined)
                result.push(child);
        }
        return result;
    }

    function relayout() {
        const buttons = root.groupButtons();
        const deltas = buttons.map(() => 0);
        const pressed = buttons.findIndex(b => b.down);
        if (pressed >= 0) {
            const grow = buttons[pressed].naturalWidth * root.expandedRatio;
            const neighbours = [pressed - 1, pressed + 1].filter(i => i >= 0 && i < buttons.length);
            deltas[pressed] = grow;
            for (const i of neighbours)
                deltas[i] = -grow / neighbours.length;
        }
        buttons.forEach((b, i) => b.widthDelta = deltas[i]);
    }

    function scheduleAttach() {
        if (root) // null while the group is being torn down
            Qt.callLater(root.attach);
    }

    function attach() {
        const buttons = root.groupButtons();
        for (const b of buttons) {
            if (root.trackedButtons.indexOf(b) < 0) {
                b.downChanged.connect(root.relayout);
                b.visibleChanged.connect(root.scheduleAttach);
                root.trackedButtons.push(b);
            }
        }
        const last = buttons.length - 1;
        buttons.forEach((b, i) => {
            b.groupPosition = !root.connected || last === 0 ? Md.Alone
                            : i === 0 ? Md.First : i === last ? Md.Last : Md.Middle;
        });
        exclusiveGroup.buttons = root.exclusive ? buttons : [];
    }

    spacing: connected ? 2 : 12
    onChildrenChanged: scheduleAttach()
    onConnectedChanged: scheduleAttach()
    onExclusiveChanged: scheduleAttach()

    T.ButtonGroup {
        id: exclusiveGroup
        exclusive: true
    }
}
