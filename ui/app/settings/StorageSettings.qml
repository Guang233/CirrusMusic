import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Presentation
import CirrusMusic

// What the app keeps on this machine and clearing it. Reporting the listening history
// comes with the backend (docs/ui/pages/settings.md §1).
SettingsGroup {
    id: group

    required property AppShell shell

    title: qsTr("存储与隐私")
    glyph: Symbols.database

    // The page shows the usage as of opening it.
    Component.onCompleted: App.cache.refresh()

    ActionSetting {
        text: qsTr("缓存")
        supportingText: App.cache.bytes < 0 ? qsTr("封面、歌曲信息和歌词")
                                            : qsTr("封面、歌曲信息和歌词，已占用 %1")
                                              .arg(App.format.bytes(App.cache.bytes))
        actionText: App.cache.clearing ? qsTr("正在清除…") : qsTr("清除")
        actionEnabled: !App.cache.clearing && App.cache.bytes !== 0
        onTriggered: clearCacheDialog.open()
    }
    ActionSetting {
        text: qsTr("搜索历史")
        supportingText: qsTr("%1 条").arg(App.settings.searchHistory.length)
        actionText: qsTr("清除")
        actionEnabled: App.settings.searchHistory.length > 0
        onTriggered: clearHistoryDialog.open()
    }

    Connections {
        target: App.cache
        function onCleared(ok: bool) {
            group.shell.notify(ok ? qsTr("已清除缓存") : qsTr("没能清除缓存，请稍后再试"));
        }
    }

    Dialog {
        id: clearCacheDialog
        title: qsTr("清除缓存？")
        supportingText: qsTr("封面、歌曲信息和歌词会在用到时重新下载。不影响登录、音乐库和听歌记录。")
        acceptText: qsTr("清除")
        rejectText: qsTr("取消")
        onAccepted: App.cache.clear()
    }
    Dialog {
        id: clearHistoryDialog
        title: qsTr("清除搜索历史？")
        supportingText: qsTr("搜索框下拉里的最近搜索会全部删除。")
        acceptText: qsTr("清除")
        rejectText: qsTr("取消")
        onAccepted: {
            App.settings.searchHistory = [];
            group.shell.notify(qsTr("已清除搜索历史"));
        }
    }
}
