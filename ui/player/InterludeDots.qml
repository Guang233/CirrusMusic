pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme

// The interlude (docs/ui/pages/player.md §5.5): three dots that light up in turn over
// the gap, breathing slowly, and in its last half second swell once and shrink away.
Item {
    id: root

    required property PlayerPalette colors
    required property real fontSize
    property real timeMs
    // The gap: from the previous line's end to the next line's start.
    property real fromMs
    property real toMs
    property bool running

    readonly property real dot: fontSize * 0.3
    readonly property real progress: running ? Math.max(0, Math.min(1, (timeMs - fromMs)
                                                                      / Math.max(1, toMs - fromMs)))
                                             : 0
    readonly property real ending: running ? Math.max(0, Math.min(1, (timeMs - toMs + 500) / 500)) : 0
    readonly property real breath: running ? 1 + 0.03 * (1 - Math.cos(2 * Math.PI * (timeMs - fromMs)
                                                                       / 2400))
                                           : 1

    implicitWidth: dot * 3 + dot * 0.8 * 2
    implicitHeight: dot

    scale: breath * (ending < 0.4 ? 1 + 0.25 * ending / 0.4 : 1.25 * (1 - (ending - 0.4) / 0.6))
    opacity: ending < 0.6 ? 1 : 1 - (ending - 0.6) / 0.4

    Row {
        spacing: root.dot * 0.8
        Repeater {
            model: 3
            Rectangle {
                required property int index
                width: root.dot
                height: root.dot
                radius: root.dot / 2
                color: root.colors.content
                opacity: 0.4 + 0.6 * Math.max(0, Math.min(1, root.progress * 3 - index))
            }
        }
    }
}
