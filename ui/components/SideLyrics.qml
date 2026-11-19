pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Presentation

// The side player's lyrics (docs/ui/redesign-md3e.md §11): the current song's lines
// in a list on the pane's color, the current line lit and kept a third of the way
// down, the others dim; a click on a timed line seeks there. Scrolling by hand stops
// the following for three seconds. Plain-text lyrics are all lit and scroll freely.
// Whoever shows it drives App.lyrics.positionMs.
//   SideLyrics { tones: root.tones; anchors.fill: parent }
Item {
    id: root

    required property PlayerTones tones

    readonly property int status: App.lyrics.status
    readonly property bool ready: status === Lyrics.Ready
    readonly property bool plain: App.lyrics.timing === Lyrics.PlainText
    readonly property bool showTranslation: App.settings.showTranslation

    ListView {
        id: list
        property bool browsing: false
        anchors.fill: parent
        visible: root.ready
        clip: true
        model: App.lyrics.lines
        currentIndex: root.plain ? -1 : App.lyrics.current
        highlightRangeMode: browsing || root.plain ? ListView.NoHighlightRange : ListView.ApplyRange
        preferredHighlightBegin: height / 3
        preferredHighlightEnd: height / 3 + 1
        highlightFollowsCurrentItem: true
        highlightMoveDuration: Theme.motion.reduced ? 0 : 450
        highlightMoveVelocity: -1
        boundsBehavior: Flickable.StopAtBounds
        // Room to bring the first and last lines to the third.
        header: Item {
            width: 1
            height: root.plain ? 8 : list.height / 3
        }
        footer: Item {
            width: 1
            height: root.plain ? 8 : list.height * 2 / 3
        }
        ScrollBar.vertical: ScrollBar {}
        onMovementStarted: browsing = true
        onMovementEnded: back.restart()
        Accessible.name: qsTr("歌词")

        Timer {
            id: back
            interval: 3000
            onTriggered: list.browsing = false
        }

        delegate: Item {
            id: line
            required property int index
            required property real startMs
            required property string text
            required property string translation
            readonly property bool lit: root.plain || index === App.lyrics.current
            width: list.width
            height: words.implicitHeight + (extra.visible ? 2 + extra.implicitHeight : 0) + 14

            Text {
                id: words
                y: 7
                width: parent.width
                text: line.text
                wrapMode: Text.Wrap
                color: Theme.mix(Qt.alpha(root.tones.ink, 0.42), root.tones.ink, light.value)
                font: Fonts.font(700, 18)
            }
            Spring {
                id: light
                target: line.lit ? 1 : 0
                spec: Theme.motion.defaultEffects
                Component.onCompleted: snapTo(target)
            }
            Text {
                id: extra
                anchors.top: words.bottom
                anchors.topMargin: 2
                width: parent.width
                visible: root.showTranslation && line.translation !== ""
                text: line.translation
                wrapMode: Text.Wrap
                color: Theme.mix(Qt.alpha(root.tones.ink, 0.32), root.tones.inkVariant, light.value)
                font: Theme.type.bodyMedium.font
            }
            MouseArea {
                anchors.fill: parent
                enabled: !root.plain && line.startMs >= 0
                cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                onClicked: {
                    list.browsing = false;
                    App.player.seek(Math.max(0, line.startMs + App.lyrics.shiftMs));
                }
            }
        }
    }

    // No lyrics, instrumental, or loading failed (with a retry).
    Column {
        anchors.centerIn: parent
        width: parent.width
        spacing: 12
        visible: !root.ready && root.status !== Lyrics.Loading && root.status !== Lyrics.None

        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: root.status === Lyrics.Instrumental ? qsTr("纯音乐，请欣赏")
                : root.status === Lyrics.Failed ? qsTr("歌词加载失败") : qsTr("暂无歌词")
            color: root.tones.inkVariant
            font: Theme.type.titleMedium.font
        }
        Button {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: root.status === Lyrics.Failed
            containerTint: root.tones.raised
            tint: root.tones.ink
            text: qsTr("重试")
            onClicked: App.lyrics.retry()
        }
    }
}
