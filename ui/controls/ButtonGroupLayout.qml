import QtQuick
import QtQuick.Templates as T

// Shared row behind StandardButtonGroup and ConnectedButtonGroup. Children are
// Buttons/IconButtons: the pressed one grows by up to `expandedRatio` of its width and
// its neighbours give up the same amount (via their widthDelta springs). Neighbours
// only give up padding (`shrinkLimit`), so their labels never elide; the pressed
// button grows by what they give.
// An `exclusive` group is one Tab stop (the checked button, or the first): ←/→ check
// the neighbouring button, as a click would (radio-group keys).
Row {
    id: root

    property bool connected: false
    property bool exclusive: false
    property real expandedRatio: 0.15

    property var trackedButtons: []

    // Every button child, shown or not.
    function allButtons() {
        const result = [];
        for (let i = 0; i < root.children.length; ++i) {
            const child = root.children[i];
            if (child instanceof T.AbstractButton && child.widthDelta !== undefined)
                result.push(child);
        }
        return result;
    }
    // The buttons shown: they get the group positions.
    function groupButtons() {
        return root.allButtons().filter(b => b.visible);
    }

    function relayout() {
        const buttons = root.groupButtons();
        const deltas = buttons.map(() => 0);
        const pressed = buttons.findIndex(b => b.down);
        if (pressed >= 0) {
            const grow = buttons[pressed].naturalWidth * root.expandedRatio;
            const neighbours = [pressed - 1, pressed + 1].filter(i => i >= 0 && i < buttons.length);
            let given = 0;
            for (const i of neighbours) {
                deltas[i] = -Math.min(grow / neighbours.length, buttons[i].shrinkLimit);
                given -= deltas[i];
            }
            deltas[pressed] = neighbours.length > 0 ? given : grow;
        }
        buttons.forEach((b, i) => b.widthDelta = deltas[i]);
    }

    function scheduleAttach() {
        if (root) // null while the group is being torn down
            Qt.callLater(root.attach);
    }

    // Tracks every button, also those not shown yet (a group created hidden, e.g. on a
    // folded row or a page not shown yet): they join the exclusive group now and get
    // their positions once shown.
    function attach() {
        const all = root.allButtons();
        for (const b of all) {
            if (root.trackedButtons.indexOf(b) < 0) {
                b.downChanged.connect(root.relayout);
                b.visibleChanged.connect(root.scheduleAttach);
                b.checkedChanged.connect(root.updateTabStop);
                root.trackedButtons.push(b);
            }
        }
        const buttons = root.groupButtons();
        const last = buttons.length - 1;
        buttons.forEach((b, i) => {
            b.groupPosition = !root.connected || last === 0 ? Md.Alone
                            : i === 0 ? Md.First : i === last ? Md.Last : Md.Middle;
        });
        exclusiveGroup.buttons = root.exclusive ? all : [];
        root.updateTabStop();
    }

    // Exclusive: only the checked button (or the first) takes the focus from Tab.
    function updateTabStop() {
        if (!root || !root.exclusive)
            return;
        const buttons = root.groupButtons();
        const stop = Math.max(0, buttons.findIndex(b => b.checked));
        buttons.forEach((b, i) => b.focusPolicy = i === stop ? Qt.StrongFocus : Qt.ClickFocus);
    }

    // Checks the button `step` away from the focused one; the focus moves first, so
    // the old one can stop being the Tab stop.
    function moveChecked(step: int): bool {
        const buttons = root.groupButtons();
        const from = buttons.findIndex(b => b.activeFocus);
        if (!root.exclusive || from < 0)
            return false;
        const to = Math.max(0, Math.min(buttons.length - 1, from + step));
        if (to !== from) {
            buttons[to].forceActiveFocus(Qt.TabFocusReason);
            if (!buttons[to].checked)
                buttons[to].click();
        }
        return true;
    }

    spacing: connected ? 2 : 12
    onChildrenChanged: scheduleAttach()
    onConnectedChanged: scheduleAttach()
    onExclusiveChanged: scheduleAttach()
    Keys.onLeftPressed: event => event.accepted = moveChecked(-1)
    Keys.onRightPressed: event => event.accepted = moveChecked(1)

    T.ButtonGroup {
        id: exclusiveGroup
        exclusive: true
    }
}
