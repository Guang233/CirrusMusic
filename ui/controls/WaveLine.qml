pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme

// The MD3E wave of ProgressBar and Slider (`wavy`): a sine stroke with round caps
// across the item, centered vertically. While `waving` it flows forward; otherwise its
// height springs down to a straight line. Under the software renderer it is a line.
// Make it at least `thickness + 2 * amplitude` high.
Item {
    id: root

    property color color
    property real thickness: 4
    property bool waving: true
    // MD3E: 3 px amplitude, 32 px wavelength, one wavelength a second.
    property real amplitude: 3
    property real wavelength: 32
    property real speed: wavelength
    readonly property bool shaders: GraphicsInfo.api !== GraphicsInfo.Software
                                    && GraphicsInfo.api !== GraphicsInfo.Unknown

    Spring {
        id: waveSize
        target: root.waving ? root.amplitude : 0
        spec: Theme.motion.slowEffects
        threshold: 0.01
    }

    FrameAnimation {
        running: root.shaders && waveSize.value > 0.01 && root.visible && root.width > 0
                 && !Theme.motion.reduced
        onTriggered: wave.phase = (wave.phase + frameTime * root.speed) % root.wavelength
    }

    ShaderEffect {
        id: wave

        property color strokeColor: root.color
        property size boxSize: Qt.size(width, height)
        property real thickness: root.thickness
        property real amplitude: waveSize.value
        property real wavelength: root.wavelength
        property real phase: 0
        property real pixel: 1 / Screen.devicePixelRatio

        anchors.fill: parent
        visible: root.shaders
        fragmentShader: Qt.resolvedUrl("shaders/wave.frag.qsb")
    }
    Rectangle {
        visible: !root.shaders
        y: (parent.height - height) / 2
        width: parent.width
        height: root.thickness
        radius: height / 2
        color: root.color
    }
}
