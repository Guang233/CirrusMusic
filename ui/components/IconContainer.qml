pragma ComponentBehavior: Bound
import QtQuick
import M3Shapes
import Cirrus.Theme
import Cirrus.Controls

// A symbol on a tonal container (expressive.md §3.1), for display only (clickable icons
// are a Filled or Tonal IconButton). Containers of 40 and below stay circles; a shape
// (MaterialShape.*) is for large decorative ones, falling back to a circle without shaders.
//   IconContainer { glyph: Symbols.favorite; size: 56; tone: Md.Primary }
Item {
    id: root

    property string glyph
    property real size: 40
    property int shape: MaterialShape.Circle
    property int tone: Md.Secondary
    // Swaps container and content: e.g. on a container of the same role.
    property bool inverted: false
    property bool filled: true

    readonly property var colors: {
        const s = ColorScope.scheme;
        switch (tone) {
        case Md.Primary:
            return [s.primaryContainer, s.onPrimaryContainer, s.primary, s.onPrimary];
        case Md.Tertiary:
            return [s.tertiaryContainer, s.onTertiaryContainer, s.tertiary, s.onTertiary];
        case Md.Error:
            return [s.errorContainer, s.onErrorContainer, s.error, s.onError];
        case Md.Neutral:
            return [s.surfaceContainerHighest, s.onSurfaceVariant, s.inverseSurface,
                    s.inverseOnSurface];
        default:
            return [s.secondaryContainer, s.onSecondaryContainer, s.secondary, s.onSecondary];
        }
    }
    readonly property color containerColor: colors[inverted ? 2 : 0]
    readonly property color contentColor: colors[inverted ? 3 : 1]

    implicitWidth: size
    implicitHeight: size
    Accessible.ignored: true

    MorphShape {
        anchors.fill: parent
        shape: root.shape
        color: root.containerColor
        fallbackRadius: width / 2
    }
    Icon {
        anchors.centerIn: parent
        glyph: root.glyph
        size: Math.round(root.size <= 32 ? 20 : root.size <= 40 ? 24 : root.size * 0.5)
        fill: root.filled ? 1 : 0
        color: root.contentColor
    }
}
