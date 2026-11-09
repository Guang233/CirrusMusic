pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Effects
import Cirrus.Theme
import Cirrus.Controls

// One line of the lyrics view (docs/ui/pages/player.md §5.1): the interlude dots when
// a long gap leads to it, then the original (with ruby, §6), the romanization when it
// isn't ruby, and the translation. Placed by LyricsView: it glides to its place with a
// delay after the current line (the waterfall, §5.2) and dims, shrinks and blurs with
// its distance from it.
Item {
    id: line

    // The LyricsView (untyped: the two files refer to each other, which qmllint can't resolve).
    required property var view
    required property int index
    required property double startMs
    required property double endMs
    required property string text
    required property string translation
    required property string romanization
    required property var words
    required property var ruby
    required property double gapMs

    readonly property PlayerPalette colors: line.view.colors
    readonly property real fontSize: line.view.fontSize
    readonly property bool highlighted: line.view.allLit
                                        || line.view.active.indexOf(index) >= 0
                                        || (line.view.active.length === 0 && index === line.view.current
                                            && line.view.interludeIndex < 0)
    readonly property bool wordsMode: line.view.wordFill && words.length > 0
    readonly property bool showRuby: line.view.rubyOn && ruby.length > 0
    // The romanization on its own row under the original (not ruby, or not aligned).
    readonly property bool romanBelow: line.view.showRomanization && romanization !== "" && !showRuby
    readonly property bool interlude: line.view.interludeIndex === index
    // Pieces laid out one by one (words or ruby units); empty: one wrapping text.
    readonly property var pieces: buildPieces()
    property real heldMs: -1

    // Where the view wants it, relative to the track.
    readonly property real targetY: (line.view.tops[index] ?? 0) - (line.view.tops[line.view.anchorIndex] ?? 0)
                                    + line.view.anchorY
    // On screen (with a margin), in view coordinates.
    readonly property real screenY: y + line.view.scrollY
    readonly property bool onScreen: screenY + height > -fontSize * 2
                                     && screenY < line.view.height + fontSize * 2
    property int seenAnchor: -1

    width: line.view.width
    height: column.implicitHeight
    implicitHeight: column.implicitHeight
    y: glide.value
    opacity: Theme.motion.lerp(line.view.dimLevel, 1, emphasis.value)
    scale: 0.96 + 0.04 * emphasis.value
    transformOrigin: [Item.TopLeft, Item.Top, Item.TopRight][line.view.align]
    readonly property int textAlign: [Text.AlignLeft, Text.AlignHCenter, Text.AlignRight][line.view.align]

    onImplicitHeightChanged: line.view.scheduleLayout()
    onHighlightedChanged: if (!highlighted)
                              heldMs = line.view.timeMs
    onTargetYChanged: {
        if (line.view.instant || !onScreen && !(targetY + line.view.scrollY + height > 0
                                           && targetY + line.view.scrollY < line.view.height)) {
            delay.stop();
            glide.snapTo(targetY);
        } else if (seenAnchor >= 0 && Math.abs(line.view.anchorIndex - seenAnchor) > 2) {
            // A jump over many lines (a seek): every line moves by the same distance,
            // so the whole track slides as one block, at most 60% of the height.
            delay.stop();
            const from = glide.value - targetY;
            const limit = line.view.height * 0.6;
            glide.snapTo(targetY + Math.max(-limit, Math.min(limit, from)));
            glide.target = targetY;
        } else if (seenAnchor !== line.view.anchorIndex) {
            // The waterfall: from the current line down, each starts 35 ms later.
            delay.interval = Math.min(8, Math.max(0, index - line.view.anchorIndex)) * 35;
            delay.restart();
        } else if (glide.running || delay.running) {
            if (!delay.running)
                glide.target = targetY;
        } else {
            glide.snapTo(targetY);
        }
        seenAnchor = line.view.anchorIndex;
    }
    Component.onCompleted: {
        seenAnchor = line.view.anchorIndex;
        glide.snapTo(targetY);
    }

    Timer {
        id: delay
        onTriggered: glide.target = line.targetY
    }
    Spring {
        id: glide
        spec: Theme.motion.defaultSpatial
    }
    Spring {
        id: emphasis
        target: line.highlighted ? 1 : 0
        spec: Theme.motion.defaultEffects
        Component.onCompleted: snapTo(target)
    }
    Spring {
        id: blurAmount
        target: !line.view.blurOn || line.view.browsing || line.highlighted ? 0
                                                                   : Math.min(5, Math.abs(line.index
                                                                                          - line.view.anchorIndex) * 1.2)
        spec: Theme.motion.defaultEffects
    }
    // The sung words' rise; they all fall back together once the line is done.
    Spring {
        id: riseSpring
        target: line.highlighted && line.wordsMode ? 1 : 0
        spec: Theme.motion.defaultSpatial
    }


    function buildPieces(): var {
        if (!showRuby && !wordsMode)
            return [];
        const raw = [];
        const add = (from, to, text, rubyText) => {
            if (to > from)
                raw.push({ from: from, to: to, text: text, ruby: rubyText });
        };
        let at = 0;
        for (const unit of (showRuby ? ruby : words)) {
            add(at, unit.from, text.slice(at, unit.from), "");
            add(unit.from, unit.to, showRuby ? unit.text : text.slice(unit.from, unit.to),
                showRuby ? unit.ruby : "");
            at = Math.max(at, unit.to);
        }
        add(at, text.length, text.slice(at), "");

        let lastEnd = startMs;
        return raw.map(p => {
            let s = -1, e = -1;
            if (wordsMode) {
                for (const w of words) {
                    if (w.from < p.to && w.to > p.from) {
                        s = s < 0 ? w.startMs : Math.min(s, w.startMs);
                        e = Math.max(e, w.endMs);
                    }
                }
            }
            if (s < 0) {
                s = wordsMode ? lastEnd : startMs;
                e = wordsMode ? lastEnd : endMs;
            }
            lastEnd = e;
            const trimmed = p.text.trim();
            const lead = trimmed === "" ? p.text.length : p.text.match(/^\s*/)[0].length;
            const trail = trimmed === "" ? 0 : p.text.match(/\s*$/)[0].length;
            return {
                text: trimmed,
                ruby: p.ruby,
                startMs: s,
                endMs: e,
                lead: lead,
                trail: trail,
                longNote: wordsMode && e - s >= 1000 && trimmed.length > 0 && trimmed.length <= 3
            };
        });
    }

    // Wraps the pieces into rows (centered when the lyrics are).
    function layoutPieces(): void {
        const avail = width;
        const gap = fontSize * 0.1;
        let x = 0, y = 0, rowHeight = 0;
        let row = [];
        const finish = () => {
            const shift = Math.max(0, avail - x) * [0, 0.5, 1][line.view.align];
            for (const item of row) {
                item.x += shift;
                item.y = y;
            }
            y += rowHeight + gap;
            x = 0;
            rowHeight = 0;
            row = [];
        };
        for (let i = 0; i < pieceRepeater.count; ++i) {
            const item = pieceRepeater.itemAt(i);
            if (!item)
                continue;
            if (x > 0 && x + item.implicitWidth > avail + 0.5)
                finish();
            item.x = x;
            x += item.implicitWidth;
            rowHeight = Math.max(rowHeight, item.implicitHeight);
            row.push(item);
        }
        if (row.length > 0)
            finish();
        pieceBox.implicitHeight = Math.max(0, y - gap);
    }
    function scheduleLayout(): void {
        Qt.callLater(layoutPieces);
    }
    onWidthChanged: scheduleLayout()
    onPiecesChanged: scheduleLayout()
    onTextAlignChanged: scheduleLayout()

    // Hover and click: jump to the line.
    HoverHandler {
        id: hover
        enabled: line.view.seekable
        cursorShape: Qt.PointingHandCursor
    }
    // Right-click: copy this line, or open the copy dialog.
    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: eventPoint => line.view.openLineMenu(line.index, line.text, line,
                                                       eventPoint.position)
    }
    TapHandler {
        id: tap
        enabled: line.view.seekable
        onPressedChanged: if (pressed) {
                              const at = point.position;
                              stateLayer.pressX = at.x - stateLayer.x;
                              stateLayer.pressY = at.y - stateLayer.y;
                          }
        onTapped: line.view.seekToLine(line.index)
    }
    // The hover and press layer reaches past the text on both sides (LyricsView keeps
    // room for it).
    StateLayer {
        id: stateLayer
        x: -line.view.hoverInset
        y: -line.fontSize * 0.25
        width: parent.width + 2 * line.view.hoverInset
        height: parent.height + line.fontSize * 0.5
        radius: Theme.shape.large
        color: line.colors.content
        hovered: hover.hovered
        pressed: tap.pressed
        focused: line.view.keyboardBrowsing && line.view.keyIndex === line.index

        FocusRing {
            shown: stateLayer.focused
            offset: -2
            containerRadius: stateLayer.radius
            border.color: line.colors.content
        }
    }

    Column {
        id: column
        width: parent.width
        spacing: line.fontSize * 0.15
        // Only the text blurs: the hover layer reaches past the line, and a layer on the
        // whole line would cut its rounded corners square.
        layer.enabled: line.view.shaders && line.onScreen && blurAmount.value > 0.1
        layer.smooth: true
        layer.effect: MultiEffect {
            blurEnabled: true
            blurMax: 8
            blur: blurAmount.value / 8
        }

        // The interlude dots open a room above the line.
        Item {
            width: parent.width
            height: dotsRoom.value
            visible: height > 0.5
            clip: true

            Spring {
                id: dotsRoom
                target: line.interlude ? dots.implicitHeight + line.fontSize * 0.5 : 0
                spec: Theme.motion.defaultSpatial
            }
            InterludeDots {
                id: dots
                x: (parent.width - width) * [0, 0.5, 1][line.view.align]
                anchors.bottom: parent.bottom
                anchors.bottomMargin: line.fontSize * 0.25
                transformOrigin: [Item.Left, Item.Center, Item.Right][line.view.align]
                colors: line.colors
                fontSize: line.fontSize
                running: line.interlude
                timeMs: running ? line.view.timeMs : 0
                fromMs: line.startMs - line.gapMs
                toMs: line.startMs
            }
        }

        // Only lines with ruby have its room above (§6.1).
        Item {
            id: pieceBox
            width: parent.width
            visible: line.pieces.length > 0

            Repeater {
                id: pieceRepeater
                model: line.pieces
                onItemAdded: line.scheduleLayout()

                LyricSegment {
                    required property var modelData
                    piece: modelData
                    colors: line.colors
                    baseFont: line.view.lineFont
                    rubyFont: line.view.rubyFont
                    fontSize: line.fontSize
                    spaceWidth: line.view.spaceWidth
                    rubyRoom: line.showRuby ? line.view.rubyRoom : 0
                    timed: line.highlighted && line.wordsMode && !line.view.allLit
                    timeMs: timed ? line.view.timeMs : 0
                    heldMs: line.heldMs
                    lift: riseSpring.value
                    shaders: line.view.shaders
                    glowEnabled: line.view.glow
                    onImplicitWidthChanged: line.scheduleLayout()
                    onImplicitHeightChanged: line.scheduleLayout()
                }
            }
        }

        Text {
            width: parent.width
            visible: line.pieces.length === 0
            text: line.text
            wrapMode: Text.Wrap
            horizontalAlignment: line.textAlign
            color: line.colors.content
            font: line.view.lineFont
        }
        Text {
            width: parent.width
            visible: line.romanBelow
            text: line.romanization
            wrapMode: Text.Wrap
            horizontalAlignment: line.textAlign
            color: line.colors.contentVariant
            font: line.view.smallFont
        }
        Text {
            width: parent.width
            visible: line.view.showTranslation && line.translation !== ""
            text: line.translation
            wrapMode: Text.Wrap
            horizontalAlignment: line.textAlign
            color: line.colors.contentVariant
            font: line.view.smallFont
        }
    }
}
