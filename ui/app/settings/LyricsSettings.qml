import QtQuick
import Cirrus.Presentation

// What the lyrics show besides the original lines.
SettingsGroup {
    title: qsTr("歌词")

    SwitchSetting {
        text: qsTr("显示翻译")
        supportingText: qsTr("外文歌词下方显示中文翻译（有翻译时）")
        value: App.settings.showTranslation
        onSwitched: value => App.settings.showTranslation = value
    }
    SwitchSetting {
        text: qsTr("显示音译")
        supportingText: qsTr("日语、粤语等歌词下方显示罗马音（有音译时）")
        value: App.settings.showRomanization
        onSwitched: value => App.settings.showRomanization = value
    }
}
