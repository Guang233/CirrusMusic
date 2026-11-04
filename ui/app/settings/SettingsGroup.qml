pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme

// One group of the settings page (docs/ui/pages/settings.md): a title, which is also
// its entry in the page's index, over its rows in an MD3E segmented list (2 px gaps,
// the outer corners round on the first and last shown rows). Rows are SettingRows
// or any Item; rows with `first` / `last` get them set, hidden rows leave no gap.
//   SettingsGroup { title: qsTr("歌词"); SwitchSetting { ... } SwitchSetting { ... } }
Column {
    id: group

    property string title
    default property alias rows: body.data

    // Which rows are shown changed: the ends move.
    function markEnds(): void {
        const shown = [];
        for (let i = 0; i < body.children.length; ++i) {
            const row = body.children[i];
            if (tracked.indexOf(row) < 0) {
                tracked.push(row);
                row.visibleChanged.connect(group.scheduleMarkEnds);
            }
            if (row.visible)
                shown.push(row);
        }
        shown.forEach((row, i) => {
            if (row.first !== undefined)
                row.first = i === 0;
            if (row.last !== undefined)
                row.last = i === shown.length - 1;
        });
    }
    function scheduleMarkEnds(): void {
        if (group) // null while the group is being torn down
            Qt.callLater(group.markEnds);
    }
    property var tracked: []

    width: parent ? parent.width : 0
    spacing: 8
    Component.onCompleted: scheduleMarkEnds()

    Text {
        leftPadding: 16
        text: group.title
        color: Theme.color.primary
        font: Theme.type.titleSmall.font
        Accessible.role: Accessible.Heading
    }
    Column {
        id: body
        width: parent.width
        spacing: 2
        onChildrenChanged: group.scheduleMarkEnds()
    }
}
