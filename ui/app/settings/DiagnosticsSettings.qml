pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Presentation
import CirrusMusic

// The app's log (docs/tasks/logging.md §7, settings.md S11): noting a problem's moment
// (logging-v2 §4.4), detailed logging for a day, the folder, a package to send with a
// problem, and clearing earlier sessions' files.
SettingsGroup {
    id: group

    required property AppShell shell

    title: qsTr("诊断")
    glyph: Symbols.bug_report
    hue: "grey"

    // The page shows the files as of opening it.
    Component.onCompleted: App.diagnostics.refresh()

    ActionSetting {
        text: qsTr("记下刚才的问题")
        supportingText: qsTr("出问题后马上记下，任何地方都可以按 Ctrl+Alt+Shift+D")
        actionText: qsTr("记下")
        onTriggered: App.diagnostics.mark()
    }
    SwitchSetting {
        text: qsTr("详细日志")
        supportingText: App.diagnostics.verboseEnds !== ""
                        ? qsTr("%1 自动关闭").arg(App.diagnostics.verboseEnds)
                        : qsTr("问题要很久才出现时打开，24 小时后自动关闭")
        value: App.diagnostics.verbose
        onSwitched: value => App.diagnostics.verbose = value
    }
    ActionSetting {
        text: qsTr("日志")
        supportingText: App.diagnostics.available
                        ? qsTr("已占用 %1").arg(App.format.bytes(App.diagnostics.bytes))
                        : qsTr("只输出到终端")
        actionText: qsTr("打开文件夹")
        actionEnabled: App.diagnostics.available
        onTriggered: App.diagnostics.openFolder()
    }
    ActionSetting {
        text: qsTr("导出诊断包")
        supportingText: qsTr("把日志打包，反馈问题时附上")
        actionText: App.diagnostics.exporting ? qsTr("正在收集…") : qsTr("导出")
        actionEnabled: App.diagnostics.available && !App.diagnostics.exporting
        onTriggered: exportDialog.start()
    }
    ActionSetting {
        text: qsTr("清除旧日志")
        supportingText: App.diagnostics.oldBytes > 0
                        ? qsTr("之前运行的日志，%1").arg(App.format.bytes(App.diagnostics.oldBytes))
                        : qsTr("之前运行的日志")
        actionText: qsTr("清除")
        actionEnabled: App.diagnostics.oldBytes > 0
        onTriggered: {
            App.diagnostics.clear();
            App.diagnostics.notice("log.cleared");
            group.shell.notify(qsTr("已清除旧日志"));
        }
    }

    ExportLogsDialog {
        id: exportDialog
    }
    Connections {
        target: App.diagnostics
        function onExportingChanged() {
            if (App.diagnostics.exporting)
                group.shell.notify(qsTr("正在收集诊断信息，检查网络连接要几秒"));
        }
        function onExportFinished(ok: bool) {
            App.diagnostics.notice(ok ? "log.exported" : "log.export_failed");
            group.shell.notify(ok ? qsTr("已导出诊断包") : qsTr("没能导出诊断包"));
        }
    }
}
