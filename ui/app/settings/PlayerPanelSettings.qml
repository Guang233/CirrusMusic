import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Presentation

// The player's display settings panel (docs/ui/pages/player.md §9.1): the lyrics and
// player page groups of the settings page, and this song's lyric offset, which only
// the panel has. Main.qml hands it to the player page.
Column {
    spacing: 24

    LyricsSettings {
        width: parent.width
    }
    PlayerSettings {
        width: parent.width
    }
    SettingsGroup {
        width: parent.width
        title: qsTr("本首歌词")
        glyph: Symbols.lyrics
        hue: "pink"

        SettingRow {
            id: offsetRow
            readonly property real seconds: App.lyrics.offsetMs / 1000
            text: qsTr("歌词偏移")
            supportingText: seconds === 0 ? qsTr("与歌曲同步")
                                          : seconds > 0 ? qsTr("延后 %1 秒").arg(seconds.toFixed(1))
                                                        : qsTr("提前 %1 秒").arg((-seconds).toFixed(1))
            focusPolicy: Qt.NoFocus
            hoverEnabled: false
            enabled: App.lyrics.status === Lyrics.Ready
            trailing: Component {
                Row {
                    spacing: 4
                    IconButton {
                        glyph: Symbols.remove
                        text: qsTr("提前 0.1 秒")
                        onClicked: App.lyrics.setOffset(App.lyrics.offsetMs - 100)
                    }
                    IconButton {
                        glyph: Symbols.history
                        text: qsTr("重置")
                        enabled: App.lyrics.offsetMs !== 0
                        onClicked: App.lyrics.setOffset(0)
                    }
                    IconButton {
                        glyph: Symbols.add
                        text: qsTr("延后 0.1 秒")
                        onClicked: App.lyrics.setOffset(App.lyrics.offsetMs + 100)
                    }
                }
            }
        }
    }
}
