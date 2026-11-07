pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Effects
import Cirrus.Theme
import Cirrus.Presentation

// The player's background (docs/ui/pages/player.md §3.2), in the style the user picked
// (App.settings.playerBackground):
// - 0 flowing light: the cover's 5 × 5 colors as soft points drifting and slowly
//   turning (shaders/flow.frag, a quarter of the size), scaled up with a dither.
// - 1 blurred cover: the cover enlarged twice, the copies turning slowly against each
//   other, heavily blurred at a quarter of the size and toned with the solid color;
//   it flows and rests like the light.
// - 2 solid: the solid color.
// Without shaders (software renderer) it is a still gradient of the same colors. The
// flow slows to a stop while paused and doesn't run with reduced motion or unseen.
Item {
    id: root

    required property PlayerPalette colors
    property real radius
    // Playing: the light flows; otherwise it comes to rest.
    property bool flowing

    readonly property int style: App.settings.playerBackground
    readonly property bool shaders: GraphicsInfo.api !== GraphicsInfo.Software
                                    && GraphicsInfo.api !== GraphicsInfo.Unknown
    readonly property bool still: !shaders || style === 2
    readonly property real speedFactor: [0.5, 1, 1.8][Math.max(0, Math.min(2,
                                                                           App.settings.playerFlowSpeed))]

    // Seconds of flow; a different start for every run.
    property real flowTime: Math.random() * 600
    property real speed: 0

    // Row `r` of the grid, averaged: the still gradient.
    function rowColor(r: int): color {
        const grid = colors.grid;
        if (grid.length < 25)
            return colors.solid;
        let c = grid[r * 5];
        for (let i = 1; i < 5; ++i)
            c = Theme.mix(c, grid[r * 5 + i], 1 / (i + 1));
        return c;
    }

    FrameAnimation {
        readonly property real target: root.flowing ? root.speedFactor : 0
        running: root.visible && root.shaders && root.style !== 2 && !Theme.motion.reduced
                 && (target > 0 || root.speed > 0.001)
        onTriggered: {
            // Eases to the target speed over about two seconds.
            root.speed += (target - root.speed) * Math.min(1, frameTime * 1.5);
            if (target === 0 && root.speed < 0.001)
                root.speed = 0;
            root.flowTime += frameTime * root.speed;
        }
    }

    // --- Still: gradient or solid ---------------------------------------------------

    Rectangle {
        anchors.fill: parent
        visible: root.still
        radius: root.radius
        color: root.colors.solid
        gradient: root.style === 2 ? null : stillGradient

        Gradient {
            id: stillGradient
            GradientStop {
                position: 0
                color: root.rowColor(0)
            }
            GradientStop {
                position: 0.25
                color: root.rowColor(1)
            }
            GradientStop {
                position: 0.5
                color: root.rowColor(2)
            }
            GradientStop {
                position: 0.75
                color: root.rowColor(3)
            }
            GradientStop {
                position: 1
                color: root.rowColor(4)
            }
        }
    }

    // --- Flowing light ---------------------------------------------------------------

    // The 5 × 5 colors as a texture, one texel each.
    Grid {
        id: cells
        columns: 5
        visible: false
        Repeater {
            model: 25
            Rectangle {
                required property int index
                width: 1
                height: 1
                color: root.colors.grid[index] ?? root.colors.solid
            }
        }
    }
    ShaderEffectSource {
        id: cellTexture
        sourceItem: root.shaders && root.style === 0 ? cells : null
        hideSource: true
        textureSize: Qt.size(5, 5)
        smooth: false
        visible: false
    }

    ShaderEffect {
        id: flow
        readonly property var colors: cellTexture
        readonly property real time: root.flowTime
        readonly property real aspect: root.width / Math.max(1, root.height)
        width: Math.max(16, Math.ceil(root.width / 4))
        height: Math.max(16, Math.ceil(root.height / 4))
        visible: false
        fragmentShader: Qt.resolvedUrl("shaders/flow.frag.qsb")
    }
    ShaderEffectSource {
        id: flowTexture
        sourceItem: root.shaders && root.style === 0 ? flow : null
        hideSource: true
        smooth: true
        visible: false
    }

    // --- Blurred cover ---------------------------------------------------------------

    Item {
        id: blurred
        width: Math.max(16, Math.ceil(root.width / 4))
        height: Math.max(16, Math.ceil(root.height / 4))
        visible: false

        Item {
            id: covers
            anchors.fill: parent
            visible: false
            readonly property real side: Math.hypot(width, height) * 1.1
            readonly property string source: root.shaders && root.style === 1
                                             && App.player.current.coverKey !== ""
                                             ? "image://cover/" + App.player.current.coverKey : ""

            // Two copies, the second smaller and offset, turning the other way.
            Image {
                anchors.centerIn: parent
                width: covers.side
                height: covers.side
                source: covers.source
                sourceSize: Qt.size(64, 64)
                smooth: true
                asynchronous: true
                retainWhileLoading: true
                rotation: root.flowTime * 360 / 80
            }
            Image {
                x: parent.width * 0.5 - width * 0.5 + parent.width * 0.18 * Math.cos(root.flowTime / 13)
                y: parent.height * 0.5 - height * 0.5 + parent.height * 0.18 * Math.sin(root.flowTime / 17)
                width: covers.side * 0.7
                height: covers.side * 0.7
                source: covers.source
                sourceSize: Qt.size(64, 64)
                smooth: true
                asynchronous: true
                retainWhileLoading: true
                opacity: 0.8
                rotation: -root.flowTime * 360 / 60
            }
        }
        MultiEffect {
            anchors.fill: parent
            source: covers
            blurEnabled: true
            blurMax: 64
            blur: 1
            autoPaddingEnabled: false
        }
        // Tones it into the background range (§3.1).
        Rectangle {
            anchors.fill: parent
            color: Qt.alpha(root.colors.solid, 0.55)
        }
    }
    ShaderEffectSource {
        id: blurredTexture
        sourceItem: root.shaders && root.style === 1 ? blurred : null
        hideSource: true
        smooth: true
        visible: false
    }

    // Full size: scaled up, dithered, the corners rounded.
    ShaderEffect {
        readonly property var source: root.style === 1 ? blurredTexture : flowTexture
        readonly property size boxSize: Qt.size(width, height)
        readonly property real radius: root.radius
        anchors.fill: parent
        visible: !root.still
        fragmentShader: Qt.resolvedUrl("shaders/present.frag.qsb")
    }
}
