import QtQuick
import Cirrus.Presentation

// Playback. Output device and restoring the queue at startup come with the backend
// (docs/ui/pages/settings.md §1).
SettingsGroup {
    title: qsTr("播放")

    MenuSetting {
        text: qsTr("音质")
        supportingText: qsTr("拿不到这一档时，自动用能拿到的最高音质；无损和 Hi-Res 通常需要会员。从下一首开始生效。")
        // The backend's AudioQuality.
        options: [{
                value: 0,
                text: qsTr("标准"),
                description: qsTr("128 kbps，省流量")
            }, {
                value: 1,
                text: qsTr("极高"),
                description: qsTr("320 kbps")
            }, {
                value: 2,
                text: qsTr("无损"),
                description: qsTr("FLAC，最高 48 kHz／16 bit")
            }, {
                value: 3,
                text: qsTr("Hi-Res"),
                description: qsTr("FLAC，最高 192 kHz／24 bit")
            }]
        value: App.settings.audioQuality
        onChosen: value => App.settings.audioQuality = value
    }
}
