pragma Singleton
import QtQuick

// The cover a card is being activated from (expressive.md §5): CollectionCard notes it
// as the press is released, just before the page opens, and the shell takes it to fly
// it to the detail page (CoverFlight). Stale after a second, so a press that opened
// nothing does not fly later.
QtObject {
    property Item cover: null
    property bool round: false
    property double at: 0

    function note(item: Item, isRound: bool): void {
        cover = item;
        round = isRound;
        at = Date.now();
    }
    // {cover, round}, or null when nothing was noted just now.
    function take(): var {
        const fresh = cover !== null && Date.now() - at < 1000;
        const taken = fresh ? {
                                  cover: cover,
                                  round: round
                              } : null;
        cover = null;
        return taken;
    }
}
