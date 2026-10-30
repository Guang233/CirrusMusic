pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components

// Home. Prototype: entry points to the sample playlists until the home sections
// exist (UI phase 3, step 4).
PageScaffold {
    id: page

    required property AppShell shell

    title: qsTr("首页")
    canGoBack: shell.canGoBack
    onBackRequested: shell.back()

    Column {
        x: 24
        width: parent.width - 48
        spacing: 16

        Text {
            width: parent.width
            text: qsTr("首页分区在阶段 3 第 4 步实现。先打开一个示例歌单看页面切换：")
            wrapMode: Text.Wrap
            color: Theme.color.onSurfaceVariant
            font: Theme.type.bodyLarge.font
        }
        Flow {
            width: parent.width
            spacing: 8
            Repeater {
                // Sample catalog playlists (ui/samples/catalog.json).
                model: [["pl-01", "深夜通勤"], ["pl-02", "周末慢下来"], ["pl-03", "华语独立精选"],
                    ["pl-04", "Workout Mix"], ["pl-05", "空歌单"]]
                delegate: Button {
                    required property var modelData
                    variant: Md.Tonal
                    glyph: Symbols.queue_music
                    text: modelData[1]
                    onClicked: page.shell.openPlaylist(modelData[0])
                }
            }
        }
    }
}
