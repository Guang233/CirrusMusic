pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls

// Seven rows (Monday to Sunday) of 24 hours (docs/ui/pages/stats.md §2.6): one hue in
// five steps, from the empty cell's color to `fullColor`. Hovering a cell, or moving
// through them with the arrow keys (one Tab stop), shows its tooltip. A legend
// (少 … 多) sits under the grid on the right.
//   HeatGrid { levels: model.heat; tips: model.heatTips }
FocusScope {
    id: grid

    // 168 levels 0–4, Monday 0:00 first, and their tooltips.
    property var levels: []
    property var tips: []
    property real cell: 20
    property real gap: 2
    property color emptyColor: ColorScope.scheme.surfaceContainerHighest
    property color fullColor: ColorScope.scheme.primary
    property color textColor: ColorScope.scheme.onSurfaceVariant

    property int hoveredIndex: -1
    property int currentIndex: -1
    readonly property int shownIndex: hoveredIndex >= 0 ? hoveredIndex
                                                        : activeFocus ? currentIndex : -1

    readonly property real labelWidth: 24
    readonly property real hourBand: 18
    readonly property real gridWidth: 24 * cell + 23 * gap
    readonly property real gridHeight: 7 * cell + 6 * gap
    readonly property var dayLetters: [qsTr("一"), qsTr("二"), qsTr("三"), qsTr("四"),
                                       qsTr("五"), qsTr("六"), qsTr("日")]

    function colorOf(level: int): color {
        return level <= 0 ? emptyColor : Theme.mix(emptyColor, fullColor, level / 4);
    }
    function move(dx: int, dy: int): void {
        const i = Math.max(0, currentIndex);
        const day = Math.max(0, Math.min(6, Math.floor(i / 24) + dy));
        const hour = Math.max(0, Math.min(23, i % 24 + dx));
        currentIndex = day * 24 + hour;
    }
    function indexAt(x: real, y: real): int {
        const cx = x - labelWidth, cy = y - hourBand;
        if (cx < 0 || cy < 0)
            return -1;
        const hour = Math.floor(cx / (cell + gap)), day = Math.floor(cy / (cell + gap));
        return hour >= 0 && hour < 24 && day >= 0 && day < 7 ? day * 24 + hour : -1;
    }

    implicitWidth: labelWidth + gridWidth
    implicitHeight: hourBand + gridHeight + 8 + legend.height
    activeFocusOnTab: levels.length === 168
    Accessible.role: Accessible.Table
    Accessible.name: shownIndex >= 0 ? (tips[shownIndex] ?? "") : ""

    onActiveFocusChanged: {
        if (activeFocus && currentIndex < 0) {
            let best = 0;
            for (let i = 1; i < levels.length; ++i)
                if (levels[i] > levels[best])
                    best = i;
            currentIndex = best;
        }
    }
    Keys.onLeftPressed: move(-1, 0)
    Keys.onRightPressed: move(1, 0)
    Keys.onUpPressed: move(0, -1)
    Keys.onDownPressed: move(0, 1)
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Home) {
            currentIndex = Math.floor(Math.max(0, currentIndex) / 24) * 24;
            event.accepted = true;
        } else if (event.key === Qt.Key_End) {
            currentIndex = Math.floor(Math.max(0, currentIndex) / 24) * 24 + 23;
            event.accepted = true;
        }
    }

    Repeater {
        model: [0, 6, 12, 18, 23]
        delegate: Text {
            required property int modelData
            x: grid.labelWidth + modelData * (grid.cell + grid.gap)
            width: grid.cell
            horizontalAlignment: Text.AlignHCenter
            text: modelData
            color: grid.textColor
            font: Theme.type.labelSmall.font
        }
    }
    Repeater {
        model: 7
        delegate: Text {
            required property int index
            y: grid.hourBand + index * (grid.cell + grid.gap)
            height: grid.cell
            verticalAlignment: Text.AlignVCenter
            text: grid.dayLetters[index]
            color: grid.textColor
            font: Theme.type.labelSmall.font
        }
    }

    Repeater {
        model: 168
        delegate: Rectangle {
            id: cellItem
            required property int index
            readonly property bool shown: grid.shownIndex === index
            x: grid.labelWidth + (index % 24) * (grid.cell + grid.gap)
            y: grid.hourBand + Math.floor(index / 24) * (grid.cell + grid.gap)
            width: grid.cell
            height: grid.cell
            radius: Theme.shape.extraSmall
            color: grid.colorOf(grid.levels[index] ?? 0)

            StateLayer {
                anchors.fill: parent
                radius: parent.radius
                color: ColorScope.scheme.onSurface
                hovered: cellItem.shown && grid.hoveredIndex === cellItem.index
                focused: false
                pressed: false
            }
            FocusRing {
                shown: grid.activeFocus && grid.currentIndex === cellItem.index
                       && InputMode.keyboard
                containerTopLeftRadius: cellItem.radius
                containerTopRightRadius: cellItem.radius
                containerBottomRightRadius: cellItem.radius
                containerBottomLeftRadius: cellItem.radius
            }
            ToolTip {
                parent: cellItem
                delay: 0
                visible: cellItem.shown && (grid.tips[cellItem.index] ?? "") !== ""
                text: grid.tips[cellItem.index] ?? ""
            }
        }
    }

    Row {
        id: legend
        anchors.right: parent.right
        y: grid.hourBand + grid.gridHeight + 8
        spacing: 4
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("少")
            color: grid.textColor
            font: Theme.type.labelSmall.font
        }
        Repeater {
            model: 5
            delegate: Rectangle {
                required property int index
                anchors.verticalCenter: parent.verticalCenter
                width: 12
                height: 12
                radius: 3
                color: grid.colorOf(index)
            }
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("多")
            color: grid.textColor
            font: Theme.type.labelSmall.font
        }
    }

    HoverHandler {
        onPointChanged: grid.hoveredIndex = hovered ? grid.indexAt(point.position.x,
                                                                   point.position.y) : -1
        onHoveredChanged: {
            if (!hovered)
                grid.hoveredIndex = -1;
        }
    }
}
