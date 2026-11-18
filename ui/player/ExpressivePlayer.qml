pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components
import Cirrus.Presentation
import Cirrus.Window

// The player page in the Material style (docs/ui/pages/player-expressive.md): on the
// cover scheme's Surface, a top app bar, the now-playing card (PrimaryContainer) and,
// with lyrics, a lyrics card (SurfaceContainer) beside it, and a floating toolbar for
// the play mode, volume, lyrics, the queue and the display settings. Narrow windows
// with lyrics stack the lyrics card over a compact now-playing strip. The cards glide
// when lyrics are shown or hidden. PlayerPage owns the opening transition, the side
// panels and the keys; this item only lays out and asks.
Item {
    id: root

    required property PlayerPalette colors
    required property AppWindow appWindow
    property int view: 0
    property int sheet: 0
    property real positionMs: 0
    property bool flying: false
    property bool hasSettings: true
    // Layout changes glide (off while the page opens or the window resizes).
    property bool animate: false

    readonly property bool wide: width >= 1000
    readonly property bool lyrics: view === 1
    readonly property bool compactCard: !wide && lyrics
    readonly property Item coverItem: card.coverItem
    readonly property int coverShape: card.coverShape
    readonly property real coverRotation: card.coverRotation
    readonly property real coverRadius: card.coverRadius
    readonly property Item playButton: card.playButton
    readonly property Item queueButton: queueToggle
    readonly property Item settingsButton: settingsToggle

    // Where the cards and the toolbar go.
    readonly property real contentTop: 72
    readonly property real toolbarHeight: 64
    readonly property rect cardRect: {
        if (compactCard) {
            const y = height - 16 - toolbarHeight - 16 - 96;
            return Qt.rect(24, y, width - 48, 96);
        }
        if (wide && lyrics) {
            const w = Math.round(Math.max(480, Math.min(600, width * 0.42)));
            return Qt.rect(24, contentTop, w, card.coverSizeFor(w, height - contentTop - 36, w - 200));
        }
        const w = Math.min(640, width - 48);
        return Qt.rect((width - w) / 2, contentTop, w,
                       card.coverSizeFor(w, height - contentTop - 16 - toolbarHeight - 32, w - 296));
    }
    readonly property rect lyricsRect: compactCard
                                       ? Qt.rect(24, contentTop, width - 48, cardRect.y - 16 - contentTop)
                                       : Qt.rect(cardRect.x + cardRect.width + 16, contentTop,
                                                 width - cardRect.x - cardRect.width - 16 - 24,
                                                 cardRect.height)
    readonly property real toolbarCenter: wide && lyrics ? lyricsRect.x + lyricsRect.width / 2
                                                         : width / 2
    readonly property real toolbarY: wide && lyrics ? lyricsRect.y + lyricsRect.height - 16 - toolbarHeight
                                                    : height - 16 - toolbarHeight

    signal closeRequested
    signal fullScreenRequested
    signal viewRequested(int view)
    signal sheetRequested(int sheet)
    signal artistRequested(string key)
    signal albumRequested(string key)
    signal songRequested
    signal commentsRequested
    signal addToPlaylistRequested(string key, string title)
    signal copyRequested
    signal loginRequested

    // An item placed on `to`, gliding there on a spring while `animate`.
    component Glide: Item {
        id: glide
        property rect to
        function follow(): void {
            if (root.animate) {
                gx.target = to.x;
                gy.target = to.y;
                gw.target = to.width;
                gh.target = to.height;
            } else {
                gx.snapTo(to.x);
                gy.snapTo(to.y);
                gw.snapTo(to.width);
                gh.snapTo(to.height);
            }
        }
        x: gx.value
        y: gy.value
        width: gw.value
        height: gh.value
        onToChanged: follow()
        Component.onCompleted: follow()
        Spring {
            id: gx
            spec: Theme.motion.defaultSpatial
        }
        Spring {
            id: gy
            spec: Theme.motion.defaultSpatial
        }
        Spring {
            id: gw
            spec: Theme.motion.defaultSpatial
        }
        Spring {
            id: gh
            spec: Theme.motion.defaultSpatial
        }
    }

    // The top app bar; moves the window like the shell's title bar row.
    TitleBar {
        anchors.left: parent.left
        anchors.right: parent.right
        height: root.contentTop
        contentHeight: root.contentTop
        frame: root.appWindow.frame
        windowControls: root.appWindow.titleBar !== TitleBarMode.System
        windowButtons: root.appWindow.customFrame && root.appWindow.visibility !== Window.FullScreen
        buttonTint: ColorScope.scheme.onSurface
        leadingInset: root.appWindow.titleBar === TitleBarMode.Custom && root.appWindow.macOS ? 72 : 12

        IconButton {
            id: closeButton
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            glyph: Symbols.expand_more
            text: qsTr("关闭播放页")
            onClicked: root.closeRequested()
        }
        Text {
            anchors.left: closeButton.right
            anchors.leftMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("正在播放")
            color: ColorScope.scheme.onSurface
            font: Theme.type.titleMedium.font
        }
        IconButton {
            anchors.right: parent.right
            anchors.rightMargin: 4
            anchors.verticalCenter: parent.verticalCenter
            glyph: root.appWindow.visibility === Window.FullScreen ? Symbols.fullscreen_exit
                                                                   : Symbols.fullscreen
            text: root.appWindow.visibility === Window.FullScreen ? qsTr("退出全屏") : qsTr("全屏")
            onClicked: root.fullScreenRequested()
        }
    }

    // The lyrics card: the page's lyrics view (the same settings as the default style).
    Glide {
        id: lyricsBox
        to: root.lyricsRect
        opacity: lyricsFade.value
        visible: opacity > 0.01

        Spring {
            id: lyricsFade
            target: root.lyrics ? 1 : 0
            spec: Theme.motion.defaultEffects
            Component.onCompleted: snapTo(target)
        }
        Rectangle {
            anchors.fill: parent
            radius: 48
            color: ColorScope.scheme.surfaceContainer
            clip: true

            LyricsView {
                anchors.fill: parent
                anchors.leftMargin: 32
                anchors.rightMargin: 32
                // Clear of the toolbar floating over the bottom.
                anchors.bottomMargin: root.wide ? root.toolbarHeight + 16 : 0
                colors: root.colors
                positionMs: root.positionMs
                visible: root.lyrics || lyricsFade.value > 0.01
                onCopyRequested: root.copyRequested()
            }
        }
    }

    Glide {
        to: root.cardRect

        ExpressiveNowPlaying {
            id: card
            // The cover's edge for a card `w` wide with `room` in height: what is
            // left after the rest, within 160 and `widest`; the card's height.
            function coverSizeFor(w: real, room: real, widest: real): real {
                return Math.round(Math.max(160, Math.min(widest, room - restHeight))) + restHeight;
            }
            anchors.fill: parent
            coverSize: Math.max(0, parent.height - restHeight)
            compact: root.compactCard
            positionMs: root.positionMs
            flying: root.flying
            onArtistRequested: key => root.artistRequested(key)
            onAlbumRequested: key => root.albumRequested(key)
            onSongRequested: root.songRequested()
            onCommentsRequested: root.commentsRequested()
            onAddToPlaylistRequested: (key, title) => root.addToPlaylistRequested(key, title)
            onCopyRequested: root.copyRequested()
            onLoginRequested: root.loginRequested()
        }
    }

    // The floating toolbar (MD3E, standard colors): the play mode and volume, then the
    // views. Floating, so it has a shadow.
    Item {
        id: toolbar
        readonly property int mode: App.player.repeat === Playback.RepeatOne ? 1
                                  : App.player.order === Playback.Shuffle ? 2
                                  : App.player.repeat === Playback.RepeatAll ? 0 : 3
        function setMode(next: int): void {
            switch (next) {
            case 0:
                App.player.setOrder(Playback.Sequential);
                App.player.setRepeat(Playback.RepeatAll);
                break;
            case 1:
                App.player.setRepeat(Playback.RepeatOne);
                break;
            case 2:
                App.player.setOrder(Playback.Shuffle);
                App.player.setRepeat(Playback.RepeatAll);
                break;
            default:
                App.player.setOrder(Playback.Sequential);
                App.player.setRepeat(Playback.RepeatOff);
            }
        }
        width: tools.width + 16
        height: root.toolbarHeight
        x: Math.round(tbx.value - width / 2)
        y: tby.value

        Spring {
            id: tbx
            spec: Theme.motion.defaultSpatial
            readonly property real to: root.toolbarCenter
            onToChanged: root.animate ? target = to : snapTo(to)
            Component.onCompleted: snapTo(to)
        }
        Spring {
            id: tby
            spec: Theme.motion.defaultSpatial
            readonly property real to: root.toolbarY
            onToChanged: root.animate ? target = to : snapTo(to)
            Component.onCompleted: snapTo(to)
        }

        Elevation {
            anchors.fill: bar
            level: Theme.elevation.level3
            radius: bar.radius
        }
        Rectangle {
            id: bar
            anchors.fill: parent
            radius: height / 2
            color: ColorScope.scheme.surfaceContainerHighest
        }

        // A view the toolbar toggles: selected, it takes a square tonal container.
        component Toggle: IconButton {
            anchors.verticalCenter: parent.verticalCenter
            variant: checked ? Md.Tonal : Md.Standard
            checkable: true
        }

        Row {
            id: tools
            x: 8
            anchors.verticalCenter: parent.verticalCenter
            spacing: 4

            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                glyph: [Symbols.repeat, Symbols.repeat_one, Symbols.shuffle,
                    Symbols.playlist_play][toolbar.mode]
                text: [qsTr("列表循环"), qsTr("单曲循环"), qsTr("随机播放"),
                    qsTr("顺序播放")][toolbar.mode]
                onClicked: toolbar.setMode((toolbar.mode + 1) % 4)
            }
            MiniVolume {
                anchors.verticalCenter: parent.verticalCenter
            }
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 1
                height: 24
                color: ColorScope.scheme.outlineVariant
            }
            Toggle {
                checked: root.lyrics
                glyph: Symbols.lyrics
                text: qsTr("歌词")
                onClicked: {
                    checked = Qt.binding(() => root.lyrics);
                    root.viewRequested(root.lyrics ? 0 : 1);
                }
            }
            Toggle {
                id: queueToggle
                checked: root.sheet === 1
                glyph: Symbols.queue_music
                text: qsTr("播放队列")
                onClicked: {
                    checked = Qt.binding(() => root.sheet === 1);
                    root.sheetRequested(root.sheet === 1 ? 0 : 1);
                }
            }
            Toggle {
                id: settingsToggle
                visible: root.hasSettings
                checked: root.sheet === 2
                glyph: Symbols.tune
                text: qsTr("显示设置")
                onClicked: {
                    checked = Qt.binding(() => root.sheet === 2);
                    root.sheetRequested(root.sheet === 2 ? 0 : 2);
                }
            }
        }
    }
}
