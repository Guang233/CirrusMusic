import QtQuick
import Cirrus.Theme
import Cirrus.Presentation

// Playback. Output device and restoring the queue at startup come with the backend
// (docs/ui/pages/settings.md §1).
SettingsGroup {
    title: qsTr("播放")
    glyph: Symbols.graphic_eq
    hue: "pink"

    MenuSetting {
        text: qsTr("音质")
        supportingText: qsTr("在线播放的音质；没有这一档时用最接近的")
        // The backend's AudioQuality.
        options: [{
                value: 0,
                text: qsTr("标准"),
                description: qsTr("128 kbps")
            }, {
                value: 1,
                text: qsTr("极高"),
                description: qsTr("320 kbps")
            }, {
                value: 2,
                text: qsTr("无损"),
                description: qsTr("FLAC，通常需要会员")
            }, {
                value: 3,
                text: qsTr("Hi-Res"),
                description: qsTr("高解析度 FLAC，通常需要会员")
            }]
        value: App.settings.audioQuality
        onChosen: value => App.settings.audioQuality = value
    }
}
