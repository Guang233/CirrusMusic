pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Presentation

// The lyrics column (docs/ui/pages/player.md §5). A first version: the current line
// held at 30% of the height, the others dimmed, translation under each line, and the
// states without lyrics. Waterfall scrolling, word-by-word fill, blur, browsing,
// interludes and ruby come in step 4.
Item {
    id: root

    required property PlayerPalette colors

    readonly property int status: App.lyrics.status
    readonly property real fontSize: Math.max(26, Math.min(56, width / 17))

    Text {
        anchors.left: parent.left
        y: parent.height * 0.3
        width: parent.width
        visible: root.status !== Lyrics.Ready
        wrapMode: Text.Wrap
        color: root.colors.contentVariant
        font: Fonts.font(700, root.fontSize)
        text: root.status === Lyrics.Loading ? qsTr("正在加载歌词")
            : root.status === Lyrics.Instrumental ? qsTr("纯音乐，请欣赏")
            : root.status === Lyrics.Failed ? qsTr("歌词加载失败")
            : root.status === Lyrics.NoLyrics ? qsTr("暂无歌词") : ""
    }

    ListView {
        id: list
        anchors.fill: parent
        visible: root.status === Lyrics.Ready
        clip: true
        interactive: false
        model: App.lyrics.lines
        currentIndex: Math.max(0, App.lyrics.current)
        highlightRangeMode: ListView.StrictlyEnforceRange
        preferredHighlightBegin: height * 0.3
        preferredHighlightEnd: height * 0.3 + 1
        highlightMoveDuration: Theme.motion.reduced ? 0 : 500
        spacing: root.fontSize * 0.9

        delegate: Column {
            id: line
            required property int index
            required property string text
            required property string translation
            readonly property bool active: App.lyrics.activeLines.indexOf(index) >= 0
            width: ListView.view.width
            spacing: 6
            opacity: active ? 1 : 0.4
            Behavior on opacity {
                NumberAnimation {
                    duration: Theme.motion.defaultEffects.duration
                }
            }

            Text {
                width: parent.width
                text: line.text
                wrapMode: Text.Wrap
                color: root.colors.content
                font: Fonts.font(700, root.fontSize)
            }
            Text {
                width: parent.width
                visible: App.settings.showTranslation && line.translation !== ""
                text: line.translation
                wrapMode: Text.Wrap
                color: root.colors.contentVariant
                font: Fonts.font(600, root.fontSize * 0.6)
            }
        }
    }
}
