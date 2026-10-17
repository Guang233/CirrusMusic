import QtQuick
import Cirrus.Theme

// Material Symbols glyph. Use names from Symbols, e.g. `Icon { glyph: Symbols.play_arrow }`.
// Decorative by default; give the parent control the accessible name.
Item {
    id: root

    property string glyph
    property real size: 24
    property real fill: 0
    property real weight: 400
    property real grade: 0
    property alias color: label.color

    implicitWidth: size
    implicitHeight: size
    Accessible.ignored: true

    Text {
        id: label

        anchors.centerIn: parent
        text: root.glyph
        renderType: Text.QtRendering
        font.family: Fonts.symbols
        font.pixelSize: root.size
        font.variableAxes: ({
                "FILL": root.fill,
                "wght": root.weight,
                "GRAD": root.grade,
                "opsz": Math.max(20, Math.min(48, root.size))
            })
    }
}
