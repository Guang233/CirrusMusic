pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components
import Cirrus.Presentation
import Cirrus.Window

// The player page (docs/ui/pages/player.md): over the whole window, title bar row
// included, in the colors of the playing song's cover. Three views (cover, lyrics,
// queue), the controls under the cover or at the bottom, and while playing they hide
// after a few seconds without input. Opening grows it out of the mini player with the
// cover flying across (docs/ui/motion-choreography.md §2).
//   AppWindow { overlay: PlayerPage { appWindow: window; miniBar: …; miniCover: … } }
FocusScope {
    id: page

    required property AppWindow appWindow
    // The mini player's card and cover: where opening starts and closing ends. Null:
    // the page slides up instead.
    property Item miniBar
    property Item miniCover

    property bool opened: false
    readonly property bool reduced: Theme.motion.reduced
    // 0 closed to 1 open, overshooting a little (geometry); and the fade that replaces
    // it with reduced motion.
    readonly property real progress: grow.value
    // The shell is completely hidden behind the page (it may stop drawing).
    readonly property bool covering: reduced ? fade.value >= 0.999 : grow.value >= 0.6
    // The cover is flying between the mini player and the page.
    readonly property bool flying: !reduced && miniCover !== null && (grow.running
                                                                       || (grow.value > 0.001
                                                                           && grow.value < 0.999))

    // 0 cover, 1 lyrics, 2 queue.
    property int view: App.settings.playerView
    readonly property bool wide: width >= 1000
    readonly property bool controlsAtBottom: wide && App.settings.playerControls === 1
    // Views switch with motion; window resizes follow at once.
    property bool animateLayout: false

    // Controls, top buttons and the pointer are hidden (docs/ui/pages/player.md §2.2).
    property bool chromeHidden: false
    readonly property bool canHide: opened && App.settings.playerAutoHide > 0
                                    && App.player.status === Playback.Playing
                                    && !controlsFocused
                                    && !overControls && !volume.barOpen && !header.menuOpen
    // Someone is working the hiding controls with the keyboard: one of them has the
    // focus, reached by keyboard (the play button focused on opening does not count).
    readonly property bool controlsFocused: {
        if (!InputMode.keyboard)
            return false;
        const focused = appWindow.activeFocusItem;
        const control = focused as T.Control;
        if (!focused || (control && !control.visualFocus))
            return false;
        for (let item = focused; item; item = item.parent) {
            if (item === hideable)
                return true;
        }
        return false;
    }
    // Where the pointer last was: Qt Quick re-sends hover events at the same place
    // whenever the scene changes (lyrics scrolling), which must not count as movement.
    property point lastPointer: Qt.point(-1, -1)
    readonly property bool overControls: progressHover.hovered || controlsHover.hovered
                                         || rowHover.hovered || topHover.hovered

    property bool fullScreenByPlayer: false
    property int restoreVisibility: Window.Windowed

    signal commentsRequested
    signal artistRequested(string key)
    signal albumRequested(string key)
    signal addToPlaylistRequested(string key, string title)
    signal loginRequested

    function open(): void {
        if (App.player.current.key === "" || opened)
            return;
        captureRects();
        opened = true;
        wake();
        controls.playButton.forceActiveFocus(Qt.OtherFocusReason);
    }
    function close(): void {
        if (!opened)
            return;
        if (fullScreenByPlayer)
            toggleFullScreen();
        captureRects();
        opened = false;
    }
    function setView(next: int): void {
        animateLayout = true;
        settle.restart();
        view = next;
        App.settings.playerView = next;
    }
    function toggleFullScreen(): void {
        if (appWindow.visibility === Window.FullScreen) {
            if (restoreVisibility === Window.Maximized)
                appWindow.showMaximized();
            else
                appWindow.showNormal();
            fullScreenByPlayer = false;
        } else {
            restoreVisibility = appWindow.visibility;
            appWindow.showFullScreen();
            fullScreenByPlayer = true;
        }
    }
    // Shows the hidden controls and starts counting again.
    function wake(): void {
        chromeHidden = false;
        hideTimer.restart();
    }

    // --- Shared element geometry -----------------------------------------------------

    property rect miniBarRect: Qt.rect(0, height, width, 0)
    property rect miniCoverRect
    property rect coverRect
    function captureRects(): void {
        if (miniBar)
            miniBarRect = miniBar.mapToItem(page, 0, 0, miniBar.width, miniBar.height);
        if (miniCover)
            miniCoverRect = miniCover.mapToItem(page, 0, 0, miniCover.width, miniCover.height);
        const art = cover.art;
        coverRect = art.mapToItem(page, 0, 0, art.width, art.height);
    }
    onWidthChanged: if (opened || grow.value > 0) Qt.callLater(captureRects)
    onHeightChanged: if (opened || grow.value > 0) Qt.callLater(captureRects)

    visible: grow.value > 0.001 || fade.value > 0.001
    opacity: reduced ? fade.value : 1
    enabled: opened
    focus: opened
    ColorScope.scheme: colors.scheme
    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("正在播放")

    Spring {
        id: grow
        target: page.opened ? 1 : 0
        spec: page.opened ? Theme.motion.slowSpatial : Theme.motion.defaultSpatial
        threshold: 0.001
    }
    Spring {
        id: fade
        target: page.opened ? 1 : 0
        spec: Theme.motion.defaultEffects
        threshold: 0.001
    }

    PlayerPalette {
        id: colors
        palette: Theme.coverPalette
        dark: App.settings.playerTheme === 0 ? Theme.dark : App.settings.playerTheme === 1
    }

    Timer {
        id: settle
        interval: 900
        onTriggered: page.animateLayout = false
    }
    Timer {
        id: hideTimer
        interval: Math.max(1, App.settings.playerAutoHide) * 1000
        onTriggered: {
            if (!page.canHide)
                return;
            page.chromeHidden = true;
            // Keys come to the page first while hidden: the first one only wakes.
            if (!InputMode.keyboard)
                page.forceActiveFocus();
        }
    }
    onCanHideChanged: if (canHide)
                          hideTimer.restart()
                      else
                          chromeHidden = false

    // --- Layout ----------------------------------------------------------------------

    readonly property real topHeight: 48
    readonly property real bottomRowHeight: 64
    readonly property real contentTop: topHeight + 8
    readonly property real contentBottom: height - bottomRowHeight - 8
    readonly property real sideMargin: wide ? 48 : 24
    // Heights under the cover: header, progress, controls and the gaps between.
    readonly property real headerHeight: 64
    readonly property real progressHeight: 44
    readonly property real controlsHeight: 76
    readonly property real stackExtra: 28 + headerHeight + (controlsAtBottom ? 0
                                                                              : 16 + progressHeight + 8 + controlsHeight)
    // The bottom band of the bottom layout: progress over the controls.
    readonly property real bandTop: height - bottomRowHeight - progressHeight - 16
    readonly property real stackBottom: controlsAtBottom ? bandTop - 16 : contentBottom
    // Two columns share a centered group that grows with the window, up to 1800; the
    // cover column takes 45% of it.
    readonly property real groupWidth: Math.min(width - 2 * sideMargin, 1800)
    readonly property real groupX: (width - groupWidth) / 2
    readonly property real leftWidth: groupWidth * 0.45
    // The largest cover (also its decode size).
    readonly property real maxCover: 720

    // Rects of the blocks for the current view and width.
    readonly property var layout: {
        const availH = stackBottom - contentTop;
        if (view === 0 || (wide && view > 0)) {
            const columnW = view === 0 ? width : leftWidth;
            const columnX = view === 0 ? 0 : groupX;
            const size = Math.max(120, Math.min(maxCover, height * (view === 0 ? 0.55 : 0.52),
                                                columnW - (view === 0 ? 2 * sideMargin : sideMargin),
                                                availH - stackExtra));
            const x = columnX + (columnW - size) / 2;
            const y = contentTop + Math.max(0, (availH - size - stackExtra) / 2);
            const below = y + size + 28;
            const panelX = groupX + leftWidth + sideMargin / 2;
            return {
                cover: Qt.rect(x, y, size, size),
                header: Qt.rect(x, below, size, headerHeight),
                progress: controlsAtBottom ? Qt.rect(sideMargin, bandTop, width - 2 * sideMargin,
                                                     progressHeight)
                                           : Qt.rect(x, below + headerHeight + 16, size,
                                                     progressHeight),
                controls: controlsAtBottom
                          ? Qt.rect((width - 400) / 2,
                                    height - (bottomRowHeight + controlsHeight) / 2, 400,
                                    controlsHeight)
                          : Qt.rect(x, below + headerHeight + 16 + progressHeight + 8, size,
                                    controlsHeight),
                panel: Qt.rect(panelX, contentTop, groupX + groupWidth - panelX,
                               stackBottom - contentTop)
            };
        }
        // Narrow lyrics or queue: a header row, the panel, the controls at the bottom.
        const controlsY = contentBottom - controlsHeight;
        const progressY = controlsY - progressHeight - 4;
        const controlsW = Math.min(width - 2 * sideMargin, 420);
        return {
            cover: Qt.rect(sideMargin, contentTop, 64, 64),
            header: Qt.rect(sideMargin + 64 + 16, contentTop, width - 2 * sideMargin - 80, 64),
            progress: Qt.rect(sideMargin, progressY, width - 2 * sideMargin, progressHeight),
            controls: Qt.rect((width - controlsW) / 2, controlsY, controlsW, controlsHeight),
            panel: Qt.rect(sideMargin, contentTop + 64 + 24, width - 2 * sideMargin,
                           progressY - contentTop - 64 - 40)
        };
    }

    // An item that glides to `to` (springs) while views switch, and follows at once
    // otherwise (resizing).
    component Glide: Item {
        id: glide
        property rect to
        property bool animate
        function follow(): void {
            if (animate) {
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

    // --- The growing panel -----------------------------------------------------------

    readonly property real p: reduced ? 1 : grow.value
    readonly property rect panelRect: miniBar && !reduced
                                      ? Qt.rect(Theme.motion.lerp(miniBarRect.x, 0, p),
                                                Theme.motion.lerp(miniBarRect.y, 0, p),
                                                Theme.motion.lerp(miniBarRect.width, width, p),
                                                Theme.motion.lerp(miniBarRect.height, height, p))
                                      : Qt.rect(0, 0, width, height)
    readonly property real panelRadius: Theme.motion.lerp(Theme.shape.large,
                                                          appWindow.cornerRadius,
                                                          Math.min(1, Math.max(0, p)))

    Item {
        id: panel
        x: page.panelRect.x
        y: page.panelRect.y
        width: Math.max(0, page.panelRect.width)
        height: Math.max(0, page.panelRect.height)
        clip: page.p < 0.999

        PlayerBackground {
            anchors.fill: parent
            radius: page.panelRadius
            colors: colors
        }
        // The mini player's frame color, fading into the page's background.
        Rectangle {
            anchors.fill: parent
            radius: page.panelRadius
            color: Theme.color.surfaceContainer
            opacity: page.miniBar && !page.reduced ? 1 - Theme.motion.segment(page.p, 0, 0.5) : 0
            visible: opacity > 0
        }

        // The page's content at its final size, revealed by the panel.
        Item {
            id: content
            x: -panel.x
            y: -panel.y + (page.miniBar || page.reduced ? 0 : 48 * (1 - page.p))
            width: page.width
            height: page.height

            // Pointer movement anywhere shows the controls again.
            HoverHandler {
                onPointChanged: {
                    const at = point.scenePosition;
                    if (Math.abs(at.x - page.lastPointer.x) + Math.abs(at.y - page.lastPointer.y) < 2)
                        return;
                    page.lastPointer = at;
                    page.wake();
                }
            }
            HoverHandler {
                enabled: page.chromeHidden
                cursorShape: Qt.BlankCursor
            }

            Glide {
                id: coverBox
                to: page.layout.cover
                animate: page.animateLayout
                PlayerCover {
                    id: cover
                    anchors.fill: parent
                    colors: colors
                    opacity: page.flying ? 0 : 1
                }
            }

            Glide {
                id: panelBox
                to: page.layout.panel
                animate: page.animateLayout
                opacity: Theme.motion.segment(page.p, 0.65, 1) * panelFade.value
                visible: opacity > 0

                Spring {
                    id: panelFade
                    target: page.view > 0 ? 1 : 0
                    spec: Theme.motion.defaultEffects
                }
                LyricsView {
                    anchors.fill: parent
                    colors: colors
                    visible: page.view === 1
                }
                Text {
                    anchors.left: parent.left
                    y: parent.height * 0.3
                    visible: page.view === 2
                    text: qsTr("播放队列稍后加入")
                    color: colors.contentVariant
                    font: Fonts.font(700, 28)
                }
            }

            // Header, progress and controls: they fade in late and rise a little.
            Item {
                id: chrome
                anchors.fill: parent
                opacity: Theme.motion.segment(page.p, 0.55, 1)
                transform: Translate {
                    y: 16 * (1 - Theme.motion.segment(page.p, 0.55, 1))
                }

                Glide {
                    to: page.layout.header
                    animate: page.animateLayout
                    SongHeader {
                        id: header
                        anchors.fill: parent
                        colors: colors
                        compact: !page.wide && page.view > 0
                        titleSize: Math.max(22, Math.min(32, page.layout.cover.width / 20))
                        onCommentsRequested: page.commentsRequested()
                        onArtistRequested: key => page.artistRequested(key)
                        onAlbumRequested: key => page.albumRequested(key)
                        onAddToPlaylistRequested: (key, title) => page.addToPlaylistRequested(key,
                                                                                              title)
                    }
                }

                // What hides while playing.
                Item {
                    id: hideable
                    anchors.fill: parent
                    opacity: hidden.value
                    visible: opacity > 0
                    enabled: !page.chromeHidden

                    Spring {
                        id: hidden
                        target: page.chromeHidden ? 0 : 1
                        spec: page.chromeHidden ? Theme.motion.slowEffects : Theme.motion.fastEffects
                        Component.onCompleted: snapTo(1)
                    }

                    Glide {
                        to: page.layout.progress
                        animate: page.animateLayout
                        PlayerProgress {
                            id: progress
                            anchors.fill: parent
                            colors: colors
                            positionMs: clock.positionMs
                        }
                        HoverHandler {
                            id: progressHover
                        }
                    }
                    Glide {
                        to: page.layout.controls
                        animate: page.animateLayout
                        PlayerControls {
                            id: controls
                            anchors.fill: parent
                            colors: colors
                            onLoginRequested: page.loginRequested()
                        }
                        HoverHandler {
                            id: controlsHover
                        }
                    }

                    // The bottom row: volume and the view switches.
                    Row {
                        anchors.right: parent.right
                        anchors.rightMargin: page.sideMargin - 10
                        y: page.height - page.bottomRowHeight + (page.bottomRowHeight - height) / 2
                        spacing: 4

                        HoverHandler {
                            id: rowHover
                        }

                        VolumeButton {
                            id: volume
                            colors: colors
                        }
                        PlayerButton {
                            checkable: true
                            checked: page.view === 1
                            color: colors.content
                            glyph: Symbols.lyrics
                            text: qsTr("歌词")
                            onClicked: page.setView(page.view === 1 ? 0 : 1)
                        }
                        PlayerButton {
                            checkable: true
                            checked: page.view === 2
                            color: colors.content
                            glyph: Symbols.queue_music
                            text: qsTr("播放队列")
                            onClicked: page.setView(page.view === 2 ? 0 : 2)
                        }
                    }

                    // The top row: close, full screen and the window buttons; moves the
                    // window like the shell's title bar row.
                    TitleBar {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        height: page.topHeight
                        frame: page.appWindow.frame
                        windowControls: page.appWindow.titleBar !== TitleBarMode.System
                        windowButtons: page.appWindow.customFrame
                                       && page.appWindow.visibility !== Window.FullScreen
                        buttonTint: colors.content
                        leadingInset: page.appWindow.titleBar === TitleBarMode.Custom
                                      && page.appWindow.macOS ? 72 : 8

                        HoverHandler {
                            id: topHover
                        }

                        PlayerButton {
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            color: colors.content
                            glyph: Symbols.expand_more
                            text: qsTr("关闭播放页")
                            onClicked: page.close()
                        }
                        PlayerButton {
                            anchors.right: parent.right
                            anchors.rightMargin: 4
                            anchors.verticalCenter: parent.verticalCenter
                            color: colors.content
                            iconSize: 22
                            glyph: page.appWindow.visibility === Window.FullScreen
                                   ? Symbols.fullscreen_exit : Symbols.fullscreen
                            text: page.appWindow.visibility === Window.FullScreen ? qsTr("退出全屏")
                                                                                  : qsTr("全屏")
                            onClicked: page.toggleFullScreen()
                        }
                    }
                }
            }

            // While hidden, the first click only shows the controls.
            MouseArea {
                anchors.fill: parent
                enabled: page.chromeHidden
                visible: enabled
                onPressed: page.wake()
            }
        }
    }

    // The flying cover.
    CoverArt {
        readonly property real t: page.p
        visible: page.flying
        x: Theme.motion.lerp(page.miniCoverRect.x, page.coverRect.x, t)
        y: Theme.motion.lerp(page.miniCoverRect.y, page.coverRect.y, t)
        width: Theme.motion.lerp(page.miniCoverRect.width, page.coverRect.width, t)
        height: Theme.motion.lerp(page.miniCoverRect.height, page.coverRect.height, t)
        radius: Theme.motion.lerp(Theme.shape.small, Theme.shape.medium, Math.min(1, Math.max(0, t)))
        coverKey: App.player.current.coverKey
        // The page cover's decode (cached), not a new one every frame of the flight.
        sourceSize: Qt.size(cover.decodeSize, cover.decodeSize)
    }

    PlaybackClock {
        id: clock
        active: page.visible
    }
    Binding {
        target: App.lyrics
        property: "positionMs"
        value: clock.positionMs
        when: page.visible
    }

    // Single keys (§7); while hidden the first key only shows the controls (Space
    // still plays or pauses).
    Keys.onPressed: event => {
        const hidden = page.chromeHidden;
        page.wake();
        switch (event.key) {
        case Qt.Key_Space:
            App.player.playPause();
            break;
        case Qt.Key_Escape:
            if (page.fullScreenByPlayer || page.appWindow.visibility === Window.FullScreen)
                page.toggleFullScreen();
            else
                page.close();
            break;
        default:
            if (hidden) {
                event.accepted = true;
                return;
            }
            switch (event.key) {
            case Qt.Key_Left:
                progress.seekBy(-5000);
                break;
            case Qt.Key_Right:
                progress.seekBy(5000);
                break;
            case Qt.Key_L:
                page.setView(page.view === 1 ? 0 : 1);
                break;
            case Qt.Key_Q:
                page.setView(page.view === 2 ? 0 : 2);
                break;
            case Qt.Key_F:
                page.toggleFullScreen();
                break;
            default:
                return;
            }
        }
        event.accepted = true;
    }
}
