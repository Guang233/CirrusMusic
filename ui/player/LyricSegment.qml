pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Effects
import Cirrus.Theme

// One piece of a lyric line laid out on its own (docs/ui/pages/player.md §5.3, §6.1):
// a word, or a unit of base text with its romaji ruby centered above. On the line
// being sung it fills from left to right (shaders/fill.frag), floats up along an
// ease-in-out curve over its own time, starting a little early so that each word
// lifts the next ones with it (a wave); a long note slowly grows with a soft glow.
Item {
    id: root

    // {text, ruby, startMs, endMs, lead, trail, longNote}: the text without the spaces
    // around it, which only add width (lead, trail: how many).
    required property var piece
    required property PlayerPalette colors
    required property font baseFont
    required property font rubyFont
    required property real fontSize
    required property real spaceWidth
    // Height kept above the text for ruby; 0 without.
    property real rubyRoom
    // Follows the clock: the highlighted line, timed by word.
    property bool timed
    property real timeMs
    // When the line stopped being highlighted: what had been reached stays up while
    // the line settles.
    property real heldMs: -1
    // The line's rise, 0 to 1 (a spring): everything falls back together.
    property real lift
    property bool shaders
    property bool glowEnabled

    readonly property real dim: 0.4
    readonly property real startMs: piece.startMs
    readonly property real endMs: piece.endMs
    // 0 unsung to 1 sung; lines not being followed show fully (their opacity dims).
    readonly property real progress: !timed ? 1
                                            : endMs <= startMs ? (timeMs >= startMs ? 1 : 0)
                                                               : Math.max(0, Math.min(1, (timeMs - startMs)
                                                                                      / (endMs - startMs)))
    // 0 to 1 over the piece's time, from 300 ms before it starts.
    readonly property real rise: {
        const at = timed ? timeMs : heldMs;
        const lead = 300;
        const span = Math.max(endMs - startMs, 250) + lead;
        const t = Math.max(0, Math.min(1, (at - startMs + lead) / span));
        return 0.5 - 0.5 * Math.cos(Math.PI * t);
    }
    // A long note: grows over the note, settles in 600 ms after it.
    readonly property real grow: {
        if (!timed || !piece.longNote)
            return 0;
        if (timeMs < endMs)
            return Math.sin(progress * Math.PI / 2);
        return Math.max(0, 1 - (timeMs - endMs) / 600);
    }

    readonly property real innerWidth: Math.max(base.implicitWidth,
                                                rubyText.visible ? rubyText.implicitWidth : 0)
    implicitWidth: piece.lead * spaceWidth + innerWidth + piece.trail * spaceWidth
    implicitHeight: rubyRoom + base.implicitHeight

    transform: Translate {
        y: -0.05 * root.fontSize * root.rise * root.lift
    }

    // The glow: a blurred copy behind, in the content color.
    Loader {
        x: base.x
        y: base.y
        active: root.piece.longNote && root.glowEnabled && root.shaders && root.timed
        sourceComponent: Text {
            text: root.piece.text
            font: root.baseFont
            color: root.colors.content
            scale: content.scale
            transformOrigin: Item.Bottom
            opacity: root.grow * (root.colors.dark ? 0.7 : 0.35)
            layer.enabled: true
            layer.smooth: true
            layer.effect: MultiEffect {
                blurEnabled: true
                blur: 1
                blurMax: 24
            }
        }
    }

    Item {
        id: content
        width: root.implicitWidth
        height: root.implicitHeight
        scale: 1 + 0.08 * root.grow
        transformOrigin: Item.Bottom
        // Partly sung: the soft edge needs the shader; otherwise plain opacity.
        layer.enabled: root.shaders && root.progress > 0 && root.progress < 1
        layer.smooth: true
        layer.effect: ShaderEffect {
            property var source
            readonly property real progress: root.progress
            readonly property real edge: Math.min(1, root.fontSize * 0.5 / Math.max(1, root.width))
            readonly property real dim: root.dim
            fragmentShader: Qt.resolvedUrl("shaders/fill.frag.qsb")
        }
        opacity: layer.enabled || root.progress >= 1 ? 1 : root.dim + (1 - root.dim) * root.progress

        Text {
            id: rubyText
            visible: root.rubyRoom > 0 && root.piece.ruby !== ""
            x: root.piece.lead * root.spaceWidth + (root.innerWidth - implicitWidth) / 2
            y: (root.rubyRoom - implicitHeight) / 2
            text: root.piece.ruby
            font: root.rubyFont
            color: root.colors.contentVariant
        }
        Text {
            id: base
            x: root.piece.lead * root.spaceWidth + (root.innerWidth - implicitWidth) / 2
            y: root.rubyRoom
            text: root.piece.text
            font: root.baseFont
            color: root.colors.content
        }
    }
}
