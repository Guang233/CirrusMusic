pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components
import Cirrus.Presentation

// Personal FM beside "today" on the home page (docs/ui/pages/home.md §5): art and
// buttons on top, the words at the bottom. Shows what FM is playing once it plays,
// otherwise what it is. `strip`: the 72 px row of
// medium and narrow windows.
Rectangle {
    id: card

    property bool strip: false

    readonly property bool fm: App.player.mode === Playback.PersonalFm
    readonly property bool loading: fm && (App.player.status === Playback.Loading
                                           || App.player.fmWaiting)
    readonly property bool playing: fm && (App.player.status === Playback.Playing || loading)
    readonly property bool hasSong: fm && App.player.current.key !== ""

    radius: strip ? Theme.shape.large : Theme.shape.extraLarge
    color: Theme.color.surfaceContainerHigh
    clip: true

    // Play or pause FM; starts it when something else is playing.
    function toggle() {
        if (fm)
            App.player.playPause();
        else
            App.player.startFm();
    }

    // Cover of the FM song, or the radio glyph.
    Item {
        id: art
        x: card.strip ? 16 : 24
        y: card.strip ? (card.height - height) / 2 : 24
        width: card.strip ? 40 : (card.hasSong ? 96 : 48)
        height: width

        CoverArt {
            anchors.fill: parent
            visible: card.hasSong
            coverKey: App.player.current.coverKey
            radius: card.strip ? Theme.shape.small : Theme.shape.large
        }
        Icon {
            anchors.centerIn: parent
            visible: !card.hasSong
            glyph: Symbols.radio
            size: card.strip ? 28 : 40
            fill: 1
            color: Theme.color.tertiary
        }
    }

    Column {
        id: texts
        x: card.strip ? art.x + art.width + 16 : 24
        y: card.strip ? (card.height - height) / 2 : card.height - height - 24
        width: (card.strip ? buttons.x - 16 : card.width - 24) - x
        spacing: 2
        Text {
            width: parent.width
            text: card.hasSong ? App.player.current.title : qsTr("私人 FM")
            elide: Text.ElideRight
            color: Theme.color.onSurface
            font: card.strip ? Theme.type.titleMedium.font : Theme.type.titleLarge.font
        }
        Text {
            width: parent.width
            text: card.hasSong ? qsTr("私人 FM · %1").arg(App.player.current.artistText)
                               : qsTr("按你的口味一直放下去")
            elide: Text.ElideRight
            color: Theme.color.onSurfaceVariant
            font: Theme.type.bodyMedium.font
        }
    }

    Row {
        id: buttons
        x: card.width - width - (card.strip ? 16 : 24)
        y: card.strip ? (card.height - height) / 2 : 24
        spacing: 8

        IconButton {
            id: playButton
            anchors.verticalCenter: parent.verticalCenter
            variant: Md.Filled
            size: card.strip ? Md.Small : Md.Medium
            widthMode: Md.Wide
            shape: card.playing ? Md.Square : Md.Round
            glyph: card.loading ? "" : card.playing ? Symbols.pause : Symbols.play_arrow
            iconFill: 1
            text: card.loading ? qsTr("正在加载") : card.playing ? qsTr("暂停私人 FM")
                                                                : qsTr("播放私人 FM")
            onClicked: card.toggle()

            LoadingIndicator {
                anchors.centerIn: parent
                implicitWidth: 28
                implicitHeight: 28
                visible: card.loading
                color: Theme.color.onPrimary
            }
        }
        IconButton {
            anchors.verticalCenter: parent.verticalCenter
            visible: card.fm
            enabled: !App.player.fmWaiting
            variant: Md.Tonal
            size: card.strip ? Md.Small : Md.Medium
            glyph: Symbols.skip_next
            iconFill: 1
            text: qsTr("下一首")
            onClicked: App.player.next()
        }
    }
}
