pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme

// The card → detail page cover flight (expressive.md §5, motion-choreography.md §3.1).
// Lies over the page stack. A copy of the card's cover flies from the card to the detail
// page's header cover (position, size and corners, the expressive default spatial spring:
// the flying cover pushes no layout) while the pages do their usual shared-axis
// transition; it waits there until the page shows its content and its own cover, then
// fades out over it. Back flies it home when both covers are on screen. Both real covers
// are hidden meanwhile. Nothing flies with reduced motion.
//
// A detail page offers its header cover with `flightCover` (Item), `flightRound`,
// `flightRadius` and `flightReady` (content and cover shown).
//   CoverFlight { anchors.fill: stack; current: stack.currentItem }
//   coverFlight.forward(CoverHandoff.take(), stack.push(…))
Item {
    id: root

    // The page on top of the stack; another one ends a flight at once.
    property Item current: null

    enum Phase {
        Idle,
        Flying,
        Holding,
        HandingOver
    }
    property int phase: CoverFlight.Phase.Idle
    readonly property bool active: phase !== CoverFlight.Phase.Idle

    // The way to the page (the card's cover and the page), kept for the way back.
    property Item source: null
    property bool sourceRound: false
    property var page: null

    // This flight: the proxy leaves `from` (fixed rectangle) and lands on `to` (followed).
    property Item from: null
    property Item to: null
    property rect fromRect
    property real fromRadius
    property bool toRound
    property real toRadius
    property bool backwards: false
    property Item expected: null
    property double landedAt: 0

    function rectOf(item: Item): rect {
        const p = item.mapToItem(root, 0, 0);
        return Qt.rect(p.x, p.y, item.width, item.height);
    }
    // Shown and inside this area (e.g. not scrolled away).
    function onScreen(item: Item): bool {
        if (!item || !item.visible || item.width <= 0)
            return false;
        const r = rectOf(item);
        return r.x + r.width > 0 && r.y + r.height > 0 && r.x < width && r.y < height;
    }
    function radiusOf(item: Item, round: bool, radius: real): real {
        return round ? Math.min(item.width, item.height) / 2 : radius;
    }

    // `handoff`: CoverHandoff.take(); `detail`: the page just pushed.
    function forward(handoff: var, detail: var): void {
        stop();
        if (!handoff || !detail || Theme.motion.reduced || detail.flightCover === undefined
                || !detail.flightCover || !onScreen(handoff.cover))
            return;
        source = handoff.cover;
        sourceRound = handoff.round;
        page = detail;
        start(source, sourceRound, (source as CoverArt)?.radius ?? 0, detail.flightCover,
              detail.flightRound, detail.flightRadius, false, detail);
    }

    // On going back from `leaving` to `arriving`: flies the cover home if it came from
    // there. Returns whether it flies.
    function backward(leaving: var, arriving: Item): bool {
        const was = page;
        const card = source;
        stop();
        if (Theme.motion.reduced || !was || was !== leaving || !card || !was.flightCover
                || !onScreen(was.flightCover))
            return false;
        // The arriving page keeps the card where it was (cached pages keep their scroll);
        // scrolled away, the pages just slide.
        const r = rectOf(card);
        if (!card.visible || r.y + r.height <= 0 || r.y >= height || r.x + r.width <= -40
                || r.x >= width)
            return false;
        source = card;
        page = was;
        start(was.flightCover, was.flightRound, was.flightRadius, card, sourceRound,
              (card as CoverArt)?.radius ?? 0, true, arriving);
        return true;
    }

    function start(a: Item, aRound: bool, aRadius: real, b: Item, bRound: bool, bRadius: real,
                   back: bool, next: Item): void {
        from = a;
        to = b;
        fromRect = rectOf(a);
        fromRadius = radiusOf(a, aRound, aRadius);
        toRound = bRound;
        toRadius = bRadius;
        backwards = back;
        expected = next;
        const cover = (back ? b : a) as CoverArt;
        proxy.sourceSize = cover ? cover.sourceSize : Qt.size(fromRect.width, fromRect.height);
        proxy.glyph = cover ? cover.glyph : proxy.glyph;
        proxy.coverKey = cover ? cover.coverKey : "";
        fade.snapTo(1);
        progress.snapTo(0);
        a.opacity = 0;
        b.opacity = 0;
        phase = CoverFlight.Phase.Flying;
        place();
        progress.target = 1;
    }

    // Puts both covers back and hides the copy.
    function stop(): void {
        if (from)
            from.opacity = 1;
        if (to)
            to.opacity = 1;
        from = null;
        to = null;
        expected = null;
        phase = CoverFlight.Phase.Idle;
        proxy.coverKey = "";
    }

    function place(): void {
        if (!to) {
            stop();
            return;
        }
        const target = rectOf(to);
        const p = progress.value;
        proxy.x = Theme.motion.lerp(fromRect.x, target.x, p);
        proxy.y = Theme.motion.lerp(fromRect.y, target.y, p);
        proxy.width = Math.max(1, Theme.motion.lerp(fromRect.width, target.width, p));
        proxy.height = Math.max(1, Theme.motion.lerp(fromRect.height, target.height, p));
        proxy.radius = Math.max(0, Theme.motion.lerp(fromRadius, radiusOf(to, toRound, toRadius), p));
    }

    function landed(): void {
        if (backwards) {
            stop();
            return;
        }
        phase = CoverFlight.Phase.Holding;
        landedAt = Date.now();
    }

    function handOver(): void {
        phase = CoverFlight.Phase.HandingOver;
        if (to)
            to.opacity = 1;
        fade.target = 0;
    }

    onCurrentChanged: {
        if (active && current !== expected)
            stop();
    }

    Spring {
        id: progress
        spec: Theme.motion.defaultSpatial
        threshold: 0.001
        onRunningChanged: if (!running && root.phase === CoverFlight.Phase.Flying)
                              root.landed()
    }
    Spring {
        id: fade
        spec: Theme.motion.fastEffects
        threshold: 0.01
        onRunningChanged: if (!running && root.phase === CoverFlight.Phase.HandingOver)
                              root.stop()
    }

    // Follows the target (its page slides in, or scrolls while loading) every frame,
    // and hands over once the page shows its content and cover (or after 3 s).
    FrameAnimation {
        running: root.active
        onTriggered: {
            root.place();
            if (root.phase === CoverFlight.Phase.Holding
                    && (root.page?.flightReady || Date.now() - root.landedAt > 3000))
                root.handOver();
        }
    }

    CoverArt {
        id: proxy
        visible: root.active
        opacity: Math.max(0, Math.min(1, fade.value))
        immediate: true
    }
}
