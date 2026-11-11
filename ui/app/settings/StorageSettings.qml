pragma ComponentBehavior: Bound
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
    hue: "yellow"

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
        text: qsTr("歌曲缓存")
        supportingText: App.cache.audioBytes < 0 ? qsTr("听过的歌")
                                                 : qsTr("听过的歌，已占用 %1")
                                                   .arg(App.format.bytes(App.cache.audioBytes))
        actionText: App.cache.audioClearing ? qsTr("正在清除…") : qsTr("清除")
        actionEnabled: !App.cache.audioClearing && App.cache.audioBytes !== 0
        onTriggered: clearAudioDialog.open()
    }
    // The limit: a slider and a field in GB (S9), 0.5–50, the same value.
    SettingRow {
        id: limitRow

        readonly property real gigabytes: App.settings.audioCacheLimit / (1 << 30)

        function apply(value: real): void {
            const clamped = Math.max(0.5, Math.min(50, Math.round(value * 10) / 10));
            App.settings.audioCacheLimit = Math.round(clamped * (1 << 30));
        }

        text: qsTr("歌曲缓存上限")
        supportingText: qsTr("满了先删最久没听的")
        focusPolicy: Qt.NoFocus
        hoverEnabled: false
        trailing: Component {
            Row {
                spacing: 12
                // Logarithmic: 0.5 GB at the left, 50 at the right, 5 in the middle.
                Slider {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 200
                    from: 0
                    to: 1
                    stepSize: 0.02
                    value: Math.log(limitRow.gigabytes / 0.5) / Math.log(100)
                    Accessible.name: limitRow.text
                    onMoved: limitRow.apply(Math.round(0.5 * Math.pow(100, value) * 2) / 2)
                }
                TextField {
                    id: limitField
                    anchors.verticalCenter: parent.verticalCenter
                    width: 72
                    fieldHeight: 40
                    variant: Md.Outlined
                    gapColor: ColorScope.scheme.surfaceContainerLow
                    horizontalAlignment: TextInput.AlignRight
                    text: limitRow.gigabytes.toLocaleString(Qt.locale(), "f", 1)
                    Accessible.name: qsTr("歌曲缓存上限（GB）")
                    validator: DoubleValidator {
                        bottom: 0
                        top: 50
                        decimals: 1
                        notation: DoubleValidator.StandardNotation
                    }
                    onEditingFinished: {
                        const value = Number.fromLocaleString(Qt.locale(), text);
                        if (!isNaN(value))
                            limitRow.apply(value);
                        text = Qt.binding(() => limitRow.gigabytes.toLocaleString(Qt.locale(), "f",
                                                                                  1));
                    }
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "GB"
                    font: Theme.type.labelLarge.font
                    color: ColorScope.scheme.onSurfaceVariant
                }
            }
        }
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
        function onAudioCleared(ok: bool) {
            group.shell.notify(ok ? qsTr("已清除歌曲缓存") : qsTr("没能清除歌曲缓存，请稍后再试"));
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
        id: clearAudioDialog
        title: qsTr("清除歌曲缓存？")
        supportingText: qsTr("听过的歌会在再听时重新下载。不影响登录、音乐库和听歌记录。")
        acceptText: qsTr("清除")
        rejectText: qsTr("取消")
        onAccepted: App.cache.clearAudio()
    }
    Dialog {
        id: clearHistoryDialog
        title: qsTr("清除搜索历史？")
        supportingText: qsTr("搜索框下拉里的最近搜索会全部删除。")
        acceptText: qsTr("清除")
        rejectText: qsTr("取消")
        onAccepted: {
            App.settings.searchHistory = [];
            App.diagnostics.notice("search.history_cleared");
            group.shell.notify(qsTr("已清除搜索历史"));
        }
    }
}
