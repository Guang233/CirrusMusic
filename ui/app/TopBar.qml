pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Presentation

// The app's top bar, in the window's title bar row (docs/ui/pages/shell.md §1): over
// the rail the rail toggle and the app's name (the logo goes before it once there is
// one), then back and forward, the search field, and on the right the app's own
// actions. Window buttons, when the window has them, come after the actions (the
// title bar row adds them); the actions are never hidden. Empty space moves the
// window (the title bar row handles that under this item).
Item {
    id: bar

    required property AppShell shell
    readonly property alias searchField: field
    // App actions at the right end, e.g. full screen later.
    property alias actions: actionRow.data

    // Over the rail: as wide as the rail beside the panel.
    Item {
        id: lead
        width: bar.shell.navWidth - bar.x
        height: bar.height

        IconButton {
            x: 28
            anchors.verticalCenter: parent.verticalCenter
            glyph: bar.shell.railExpanded ? Symbols.menu_open : Symbols.menu
            text: bar.shell.railExpanded ? qsTr("收起导航栏") : qsTr("展开导航栏")
            onClicked: bar.shell.toggleRail()
        }
        // The app's name; the logo will go in front of it.
        Text {
            x: 80
            width: Math.max(0, lead.width - x - 16)
            anchors.verticalCenter: parent.verticalCenter
            opacity: Theme.motion.segment((lead.width - 96) / 176, 0.6, 1)
            visible: opacity > 0
            text: qsTr("Cirrus")
            elide: Text.ElideRight
            color: Theme.color.onSurface
            font: Theme.type.titleMedium.font
        }
    }

    Row {
        id: history
        x: lead.width + 4
        anchors.verticalCenter: parent.verticalCenter
        spacing: 4
        IconButton {
            enabled: bar.shell.canGoBack
            glyph: Symbols.arrow_back
            text: qsTr("后退（Alt+←）")
            onClicked: bar.shell.back()
        }
        IconButton {
            enabled: bar.shell.canGoForward
            glyph: Symbols.arrow_forward
            text: qsTr("前进（Alt+→）")
            onClicked: bar.shell.forward()
        }
    }

    SearchField {
        id: field
        x: history.x + history.width + 12
        anchors.verticalCenter: parent.verticalCenter
        width: Math.max(0, Math.min(400, actionRow.x - 16 - x))
        history: bar.shell.searchModel.history
        hotKeywords: bar.shell.searchModel.hotKeywords
        suggestions: bar.shell.searchModel.suggestions
        onTextEdited: {
            if (!bar.shell.searching && text.trim() !== "")
                bar.shell.openSearch();
        }
        onSubmitted: value => bar.shell.search(value)
        onOpened: (kind, key) => bar.shell.openCollection(kind, key)
        onHistoryRemoved: value => bar.shell.searchModel.removeHistory(value)
        onHistoryCleared: bar.shell.searchModel.clearHistory()
        onDismissed: bar.shell.focusContent()
    }

    Row {
        id: actionRow
        x: bar.width - width - 8
        anchors.verticalCenter: parent.verticalCenter
        spacing: 4
    }
}
