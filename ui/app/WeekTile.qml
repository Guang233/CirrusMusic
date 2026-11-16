pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Presentation

// This week's listening on the home page (docs/ui/redesign-md3e.md §6.2), from the
// local statistics (pages/stats.md): the time as a large number and one bar a day,
// today's in the primary color. Opens the statistics page.
Rectangle {
    id: tile

    required property AppShell shell
    readonly property var bars: week.bars
    // Today is the last day that is not in the future.
    readonly property int todayIndex: {
        let last = -1;
        for (let i = 0; i < bars.length; ++i)
            if (!bars[i].future)
                last = i;
        return last;
    }
    readonly property real scaleTop: Math.max(1, week.chartTop)

    radius: 40
    color: Theme.color.surfaceContainerHigh
    clip: true
    Accessible.role: Accessible.Button
    Accessible.name: qsTr("本周听了 %1").arg(week.durationText)

    // Follows new plays by itself.
    StatsPageModel {
        id: week
        songLimit: 1
    }

    HoverHandler {
        id: hover
        cursorShape: Qt.PointingHandCursor
    }
    TapHandler {
        onTapped: tile.shell.openStats()
    }
    StateLayer {
        anchors.fill: parent
        radius: tile.radius
        color: Theme.color.onSurface
        hovered: hover.hovered
    }

    Text {
        id: caption
        x: 24
        y: 22
        text: qsTr("本周听了")
        color: Theme.color.onSurfaceVariant
        font: Theme.type.labelLarge.font
    }
    // "6 小时 12 分": the first number large, the rest beside it.
    Row {
        x: 24
        anchors.top: caption.bottom
        anchors.topMargin: 2
        spacing: 6
        Repeater {
            model: week.durationParts
            delegate: Row {
                id: part
                required property int index
                required property var modelData
                anchors.bottom: parent.bottom
                spacing: 3
                Text {
                    id: number
                    text: part.modelData.value
                    color: Theme.color.onSurface
                    font: part.index === 0 ? Fonts.font(800, 52) : Theme.type.titleLargeEmphasized.font
                    lineHeightMode: Text.FixedHeight
                    lineHeight: part.index === 0 ? 56 : 30
                    verticalAlignment: Text.AlignBottom
                }
                Text {
                    anchors.baseline: number.baseline
                    text: part.modelData.unit
                    color: Theme.color.onSurfaceVariant
                    font: Theme.type.titleSmall.font
                }
            }
        }
    }

    Row {
        id: plot
        readonly property real barHeight: Math.max(16, Math.min(48, tile.height - 140))
        x: 24
        y: tile.height - barHeight - 24
        width: tile.width - 48
        height: barHeight
        spacing: Math.max(4, (width - 7 * 16) / 6)
        Repeater {
            model: tile.bars
            delegate: Item {
                id: day
                required property int index
                required property var modelData
                width: 16
                height: plot.height
                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    // An empty day keeps a dot.
                    height: Math.max(width, plot.height * day.modelData.ms / tile.scaleTop)
                    radius: width / 2
                    color: day.index === tile.todayIndex ? Theme.color.primary
                         : day.modelData.future ? Qt.alpha(Theme.color.outlineVariant, 0.5)
                                                : Theme.color.outlineVariant
                }
            }
        }
    }
}
