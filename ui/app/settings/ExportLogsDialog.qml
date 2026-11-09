pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Dialogs as D
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Presentation

// Picks the log files for a diagnostic package (docs/tasks/logging.md §7): every file
// listed with its size and time, all ticked; 导出 asks where to save the zip. Only logs
// go in: no account, settings or cache.
//   ExportLogsDialog { id: exportDialog; onExported: ok => … }   exportDialog.start()
Dialog {
    id: dialog

    // name -> ticked
    property var chosen: ({})
    readonly property var names: App.diagnostics.files.map(f => f.name)
                                                     .filter(name => dialog.chosen[name])

    signal exported(bool ok)

    function start(): void {
        App.diagnostics.refresh();
        const all = {};
        for (const f of App.diagnostics.files)
            all[f.name] = true;
        chosen = all;
        open();
    }

    function toggle(name: string): void {
        const next = Object.assign({}, chosen);
        next[name] = !next[name];
        chosen = next;
    }

    title: qsTr("导出诊断包")
    supportingText: qsTr("只含日志，不含账号、设置和缓存。")
    acceptText: qsTr("导出")
    acceptEnabled: names.length > 0
    rejectText: qsTr("取消")
    width: Math.min(520, (parent ? parent.width : 520) - 48)

    onAccepted: saveDialog.open()

    // Up to a week of sessions: a list that scrolls past 360 px.
    ListView {
        id: list
        width: parent.width
        height: Math.min(contentHeight, 360)
        clip: true
        model: App.diagnostics.files
        boundsBehavior: Flickable.StopAtBounds
        activeFocusOnTab: true
        highlightFollowsCurrentItem: false
        Accessible.role: Accessible.List
        Accessible.name: dialog.title
        ScrollBar.vertical: ScrollBar {}
        Keys.onSpacePressed: (currentItem as ListItem)?.clicked()
        Keys.onReturnPressed: (currentItem as ListItem)?.clicked()
        Keys.onEnterPressed: (currentItem as ListItem)?.clicked()

        delegate: ListItem {
            id: row

            required property var modelData
            required property int index

            width: list.width
            highlighted: list.activeFocus && ListView.isCurrentItem
            text: modelData.name
            supportingText: modelData.current
                            ? qsTr("%1 · 本次运行").arg(App.format.bytes(modelData.bytes))
                            : qsTr("%1 · %2").arg(App.format.bytes(modelData.bytes))
                              .arg(App.format.relativeTime(modelData.modified))
            first: index === 0
            last: index === list.count - 1
            leading: Component {
                CheckBox {
                    checked: dialog.chosen[row.modelData.name] === true
                    focusPolicy: Qt.NoFocus
                    Accessible.name: row.modelData.name
                    onToggled: dialog.toggle(row.modelData.name)
                }
            }
            onClicked: {
                list.currentIndex = index;
                dialog.toggle(modelData.name);
            }
        }
    }

    D.FileDialog {
        id: saveDialog
        title: qsTr("保存诊断包")
        fileMode: D.FileDialog.SaveFile
        defaultSuffix: "zip"
        nameFilters: [qsTr("Zip 文件 (*.zip)")]
        selectedFile: App.diagnostics.packageName()
        onAccepted: dialog.exported(App.diagnostics.exportTo(selectedFile, dialog.names))
    }
}
