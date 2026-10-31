pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme

// Page history of the shell's content area with the MD3 transitions of
// docs/ui/motion-choreography.md §3:
// - push(): forward, shared X axis (old page 30 left and out, new one in from 30 right)
// - pop(): back, the same mirrored
// - replaceAll(): new top-level destination, history cleared; fade through with the new
//   page scaling up from 0.92
// One progress spring drives each transition; popping during a push reverses that same
// transition, and a new navigation mid-way starts from what is on screen. The new page
// takes input at once. The newest `cachedPages` pages stay alive (keeping scroll
// position); older ones are destroyed and recreated when navigated back to, with
// `saveState()` / `restoreState(state)` if the page defines them.
//   PageStack { id: stack; Component.onCompleted: replaceAll(homePage) }
//   stack.push(playlistPage, { playlistKey: key })
Item {
    id: root

    property int cachedPages: 4
    readonly property int depth: entries.length
    readonly property Item currentItem: entries.length > 0 ? entries[entries.length - 1].item : null

    // {component, properties, item, state}; the last one is the current page.
    property var entries: []

    // The transition on screen: `incoming` becomes current, `outgoing` leaves.
    property Item incoming: null
    property Item outgoing: null
    property int kind: PageStack.Kind.Forward
    // Where `outgoing` starts from when it was itself mid-transition.
    property real outgoingFromOpacity: 1
    property real outgoingFromX: 0
    property real outgoingFromScale: 1
    // Destroyed when the transition settles (a popped page), else hidden.
    property bool destroyOutgoing: false
    // Set while begin() rearranges pages, so stopping the spring does not finish().
    property bool starting: false

    enum Kind {
        Forward,
        Back,
        Fade
    }

    function push(component: Component, properties: var): Item {
        const previous = currentItem;
        const item = create(component, properties ?? {});
        entries = entries.concat([{
                component: component,
                properties: properties ?? {},
                item: item,
                state: undefined
            }]);
        trim();
        begin(item, PageStack.Kind.Forward, false, previous);
        return item;
    }

    function pop(): bool {
        if (entries.length < 2)
            return false;
        const leaving = entries[entries.length - 1];
        // Back during the push of this very page: run that transition in reverse.
        if (progress.target === 1 && incoming === leaving.item && kind === PageStack.Kind.Forward
                && outgoing === entries[entries.length - 2].item) {
            entries = entries.slice(0, -1);
            destroyOutgoing = false;
            progress.target = 0;
            return true;
        }
        entries = entries.slice(0, -1);
        const back = entries[entries.length - 1];
        if (!back.item) {
            back.item = create(back.component, back.properties);
            if (back.state !== undefined && typeof back.item.restoreState === "function")
                back.item.restoreState(back.state);
            entries = entries.slice(); // notify currentItem
        }
        begin(back.item, PageStack.Kind.Back, true, leaving.item);
        return true;
    }

    function replaceAll(component: Component, properties: var): Item {
        const item = create(component, properties ?? {});
        const previous = currentItem;
        for (const entry of entries) {
            if (entry.item && entry.item !== previous && entry.item !== incoming
                    && entry.item !== outgoing)
                entry.item.destroy();
        }
        entries = [{
                component: component,
                properties: properties ?? {},
                item: item,
                state: undefined
            }];
        begin(item, PageStack.Kind.Fade, true, previous);
        return item;
    }

    function create(component: Component, properties: var): Item {
        const item = component.createObject(root, Object.assign({
            visible: false,
            width: Qt.binding(() => root.width),
            height: Qt.binding(() => root.height)
        }, properties)) as Item;
        if (!item)
            console.warn("PageStack: cannot create page:", component.errorString());
        return item;
    }

    // Pages beyond the cache keep only their component, properties and saved state.
    function trim() {
        const keep = Math.max(1, cachedPages);
        for (let i = 0; i < entries.length - keep; ++i) {
            const entry = entries[i];
            if (!entry.item || entry.item === outgoing)
                continue;
            if (typeof entry.item.saveState === "function")
                entry.state = entry.item.saveState();
            entry.item.destroy();
            entry.item = null;
        }
    }

    // `from` leaves (null for the first page), `next` arrives.
    function begin(next: Item, transition: int, destroyLeaving: bool, from: Item) {
        starting = true;

        // A page still leaving from the previous transition goes at once; a page still
        // arriving leaves from where it is on screen.
        if (outgoing && outgoing !== from && outgoing !== next)
            settleOutgoing();
        if (from && from === incoming && progress.running) {
            outgoingFromOpacity = from.opacity;
            outgoingFromX = from.x;
            outgoingFromScale = from.scale;
        } else {
            outgoingFromOpacity = 1;
            outgoingFromX = 0;
            outgoingFromScale = 1;
        }

        kind = transition;
        outgoing = from;
        destroyOutgoing = destroyLeaving;
        incoming = next;
        if (outgoing)
            outgoing.enabled = false;
        if (incoming) {
            incoming.enabled = true;
            incoming.visible = true;
            if (focusIsOurs())
                incoming.forceActiveFocus();
        }
        progress.snapTo(0);
        progress.target = 1;
        starting = false;
    }

    // The keyboard focus is in the stack or nowhere, so a new page may take it. Focus
    // elsewhere (e.g. the title bar's search field, typing) stays where it is.
    function focusIsOurs(): bool {
        for (let item = root.Window.activeFocusItem; item; item = item.parent) {
            if (item === root)
                return true;
        }
        return !root.Window.activeFocusItem || root.Window.activeFocusItem === root.Window.contentItem;
    }

        function settleOutgoing() {
        if (!outgoing)
            return;
        if (destroyOutgoing)
            outgoing.destroy();
        else
            resetPage(outgoing, false);
        outgoing = null;
    }

    function resetPage(page: Item, shown: bool) {
        page.visible = shown;
        page.enabled = shown;
        page.opacity = 1;
        page.x = 0;
        page.scale = 1;
    }

    // Called when the spring comes to rest.
    function finish() {
        if (progress.target === 1) {
            settleOutgoing();
            if (incoming)
                resetPage(incoming, true);
        } else {
            // A reversed push: the pushed page goes, the page under it is current again.
            if (incoming)
                incoming.destroy();
            if (outgoing)
                resetPage(outgoing, true);
            outgoing = null;
        }
        incoming = null;
    }

    // Reduced motion: a quick cross-fade, no movement.
    readonly property bool reduced: Theme.motion.reduced

    Spring {
        id: progress
        spec: root.reduced ? Theme.motion.fastEffects : Theme.motion.standardDefaultSpatial
        threshold: 0.001
        onRunningChanged: if (!running && !root.starting && (root.incoming || root.outgoing))
                              root.finish()
    }

    // Opacity and geometry of both pages, derived from the one progress value.
    Binding {
        when: root.outgoing !== null
        target: root.outgoing
        property: "opacity"
        value: root.outgoingFromOpacity
               * (root.reduced ? 1 - progress.value
                  : 1 - Theme.motion.segment(progress.value, 0, 0.35))
        restoreMode: Binding.RestoreNone
    }
    Binding {
        when: root.outgoing !== null
        target: root.outgoing
        property: "x"
        value: root.reduced || root.kind === PageStack.Kind.Fade ? root.outgoingFromX
               : Theme.motion.lerp(root.outgoingFromX, root.kind === PageStack.Kind.Forward ? -30 : 30,
                                   progress.value)
        restoreMode: Binding.RestoreNone
    }
    Binding {
        when: root.outgoing !== null
        target: root.outgoing
        property: "scale"
        value: root.outgoingFromScale
        restoreMode: Binding.RestoreNone
    }
    Binding {
        when: root.incoming !== null
        target: root.incoming
        property: "opacity"
        value: root.reduced ? progress.value
               : Theme.motion.segment(progress.value, root.kind === PageStack.Kind.Fade ? 0.35 : 0.2, 1)
        restoreMode: Binding.RestoreNone
    }
    Binding {
        when: root.incoming !== null
        target: root.incoming
        property: "x"
        value: root.reduced || root.kind === PageStack.Kind.Fade ? 0
               : Theme.motion.lerp(root.kind === PageStack.Kind.Forward ? 30 : -30, 0, progress.value)
        restoreMode: Binding.RestoreNone
    }
    Binding {
        when: root.incoming !== null
        target: root.incoming
        property: "scale"
        value: root.reduced || root.kind !== PageStack.Kind.Fade ? 1
               : Theme.motion.lerp(0.92, 1, progress.value)
        restoreMode: Binding.RestoreNone
    }
}
