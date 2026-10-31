pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components
import Cirrus.Presentation

// "Today" at the top of the home page (docs/ui/pages/home.md §3.1): the date as a
// large numeral beside today's recommended songs. The numeral's weight shows whether
// today's songs are playing (MiSans's continuous weight axis). Guests get the sign-in
// invitation in the same place.
Rectangle {
    id: card

    required property AppShell shell
    required property DailySongsPageModel daily
    // Narrow windows: smaller numeral, no covers.
    property bool compact: false

    readonly property date today: new Date()
    readonly property bool signedIn: App.session.signedIn
    readonly property int loadState: daily.state
    readonly property bool ready: loadState === LoadState.Ready || loadState === LoadState.Partial
    readonly property int songCount: daily.songs.count
    // Today's songs are what is playing (not personal FM picking one of them).
    readonly property bool active: daily.songs.containsCurrent && App.player.mode === Playback.Queue
    readonly property bool playing: active && (App.player.status === Playback.Playing
                                               || App.player.status === Playback.Loading)

    radius: Theme.shape.extraLarge
    color: Theme.color.primaryContainer
    clip: true

    Spring {
        id: weight
        target: card.active ? 630 : 200
        spec: Theme.motion.slowEffects
    }
    // Covers spread a little while hovered.
    Spring {
        id: spread
        target: hover.hovered && card.ready ? 1 : 0
        spec: Theme.motion.defaultSpatial
    }

    HoverHandler {
        id: hover
    }
    TapHandler {
        enabled: card.signedIn && card.ready
        onTapped: card.shell.openDaily()
    }
    StateLayer {
        anchors.fill: parent
        radius: card.radius
        color: Theme.color.onPrimaryContainer
        hovered: hover.hovered && card.signedIn && card.ready
    }

    // The date: numeral, month and weekday.
    Text {
        id: numeral
        x: 32
        y: card.compact ? 12 : 16
        text: Qt.formatDate(card.today, "d")
        color: Theme.color.onPrimaryContainer
        font: Fonts.font(weight.value, size)
        // The digits only: MiSans's line box is much taller than its figures.
        readonly property real size: card.compact ? 72 : 112
        lineHeightMode: Text.FixedHeight
        lineHeight: Math.round(size * 0.9)
        Accessible.ignored: true
    }
    // The date lines keep their place while the numeral's weight changes.
    TextMetrics {
        id: boldNumeral
        text: numeral.text
        font: Fonts.font(630, numeral.size)
    }
    Column {
        anchors.left: numeral.left
        anchors.leftMargin: Math.ceil(boldNumeral.advanceWidth) + 20
        anchors.right: covers.visible ? covers.left : parent.right
        anchors.rightMargin: 24
        anchors.verticalCenter: numeral.verticalCenter
        spacing: 2
        Text {
            width: parent.width
            text: qsTr("%1月 · %2").arg(card.today.getMonth() + 1)
                                   .arg(Qt.locale().toString(card.today, "dddd"))
            elide: Text.ElideRight
            color: Theme.color.onPrimaryContainer
            font: Theme.type.titleMedium.font
            Accessible.name: Qt.locale().toString(card.today, Locale.LongFormat)
        }
        Text {
            width: parent.width
            visible: card.signedIn
            text: card.ready ? qsTr("今日推荐 · %n 首", "", card.songCount) : qsTr("今日推荐")
            elide: Text.ElideRight
            color: Theme.color.onPrimaryContainer
            opacity: 0.8
            font: Theme.type.titleMedium.font
        }
    }

    // What today holds, and the actions. Bottom-left.
    Column {
        id: lower
        x: 32
        width: (covers.visible ? covers.x - 24 : card.width - 32) - x
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 24
        spacing: 12

        // Signed in, loaded: the first song and why it was picked.
        Column {
            width: parent.width
            visible: !card.compact && card.signedIn && card.ready && card.songCount > 0
            spacing: 2
            Text {
                width: parent.width
                text: qsTr("第一首《%1》").arg(card.daily.lead.title)
                elide: Text.ElideRight
                color: Theme.color.onPrimaryContainer
                font: Theme.type.bodyLarge.font
            }
            Text {
                width: parent.width
                text: card.daily.lead.reason !== "" ? card.daily.lead.reason
                                                    : card.daily.lead.artistText
                elide: Text.ElideRight
                color: Theme.color.onPrimaryContainer
                opacity: 0.8
                font: Theme.type.bodyMedium.font
            }
        }

        // Loading: placeholders for the two lines.
        Column {
            visible: card.signedIn && !card.ready && card.loadState === LoadState.Loading
            spacing: 8
            Skeleton {
                width: Math.min(220, lower.width)
                height: 16
                color: Qt.alpha(Theme.color.onPrimaryContainer, 0.12)
            }
            Skeleton {
                width: Math.min(160, lower.width)
                height: 14
                color: Qt.alpha(Theme.color.onPrimaryContainer, 0.12)
            }
        }

        // Anything else worth a sentence: empty, failed, signed out.
        Text {
            width: parent.width
            visible: text !== ""
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
            color: Theme.color.onPrimaryContainer
            font: Theme.type.bodyLarge.font
            text: {
                if (App.session.status === Account.Expired)
                    return qsTr("登录已过期，重新扫码登录后恢复今日推荐和私人 FM。");
                if (!card.signedIn)
                    return qsTr("登录后，这里每天都有一组为你挑的歌。私人 FM 也需要登录。");
                if (card.loadState === LoadState.Empty)
                    return qsTr("今天的推荐还没生成，稍后刷新试试。");
                if (card.loadState === LoadState.Offline)
                    return qsTr("网络不可用，今日推荐没有加载出来。");
                if (card.loadState === LoadState.Error)
                    return qsTr("今日推荐没有加载出来。");
                return "";
            }
        }

        Row {
            spacing: 8
            Button {
                visible: card.signedIn && card.ready && card.songCount > 0
                variant: Md.Filled
                size: Md.Medium
                glyph: card.playing ? Symbols.pause : Symbols.play_arrow
                text: card.playing ? qsTr("暂停") : qsTr("播放")
                onClicked: {
                    if (card.active)
                        App.player.playPause();
                    else
                        App.player.playSongs(card.daily.songs.keys(), 0);
                }
            }
            Button {
                visible: card.signedIn && card.ready && card.songCount > 0
                variant: Md.Text
                size: Md.Medium
                text: qsTr("全部 %n 首", "", card.songCount)
                trailingGlyph: Symbols.chevron_right
                onClicked: card.shell.openDaily()
            }
            Button {
                visible: card.signedIn && (card.loadState === LoadState.Error
                                           || card.loadState === LoadState.Offline)
                variant: Md.Tonal
                size: Md.Medium
                text: qsTr("重试")
                onClicked: card.daily.retry()
            }
            Button {
                visible: !card.signedIn
                variant: Md.Filled
                size: Md.Medium
                glyph: Symbols.qr_code_2
                text: qsTr("扫码登录")
                onClicked: card.shell.openLogin()
            }
        }
    }

    // The first few covers, stacked with the first song in front.
    Item {
        id: covers
        readonly property real size: Math.min(152, card.height - 64)
        x: card.width - width - 40
        y: (card.height - height) / 2 - 8
        width: size + 3 * 24
        height: size + 24
        visible: !card.compact && card.signedIn && card.ready && card.songCount > 0
                 && card.width >= 560

        Repeater {
            model: card.daily.coverKeys
            delegate: CoverArt {
                required property int index
                required property string modelData
                readonly property real step: 24 + 8 * spread.value
                x: index * step
                y: index * 6
                z: -index
                width: covers.size
                height: covers.size
                rotation: index * (4 + 2 * spread.value)
                transformOrigin: Item.BottomLeft
                opacity: 1 - index * 0.12
                coverKey: modelData
                radius: Theme.shape.large
            }
        }
    }
}
