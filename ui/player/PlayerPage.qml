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
    // The display settings panel's content (the app's PlayerPanelSettings); without
    // it there is no settings button.
    property Component settingsContent
    // The side panel open (§8, §9.1): 0 none, 1 the queue, 2 the display settings.
    property int sheet: 0
    readonly property bool settingsOpen: sheet === 2

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

    // 0 cover, 1 lyrics (the queue is a side panel now).
    property int view: Math.min(1, App.settings.playerView)
    readonly property bool wide: width >= 1000
    // Where the controls and the title go, as set; changes glide (see the Connections).
    // Not bindings: a binding would move them before the glide is switched on.
    property int controlsPlace: -1
    property int headerPlace: -1
    Component.onCompleted: {
        controlsPlace = App.settings.playerControls;
        headerPlace = App.settings.playerHeader;
    }
    readonly property bool controlsAtBottom: wide && controlsPlace === 1
    readonly property bool headerAtBottom: wide && headerPlace === 1
    // Views switch with motion; window resizes follow at once.
    property bool animateLayout: false

    // Controls, top buttons and the pointer are hidden (docs/ui/pages/player.md §2.2).
    property bool chromeHidden: false
    readonly property bool canHide: opened && App.settings.playerAutoHide > 0
                                    && App.player.status === Playback.Playing
                                    && !controlsFocused
                                    && !overControls && !volume.barOpen && !header.menuOpen
                                    && !lyricsView.handBrowsing && sheet === 0 && !songInfo.opened
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
        sheet = 0;
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
    // A short message (Snackbar) above the bottom row, in the cover's colors: the
    // shell's notices while the page covers it.
    function notify(text: string): void {
        snackbar.show(text);
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

    // While the spring runs too: on its first frame and when closing bounces below 0,
    // the mini cover is already hidden and the flying one must still show.
    visible: grow.running || grow.value > 0.001 || fade.value > 0.001
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
    // The focus goes into the panel that opens, and back to its button when it closes.
    property int lastSheet: 0
    onSheetChanged: {
        const before = lastSheet === 1 ? queueSheet : lastSheet === 2 ? settingsSheet : null;
        const opener = lastSheet === 1 ? queueButton : settingsButton;
        lastSheet = sheet;
        if (sheet === 1) {
            queuePanel.list.forceActiveFocus(Qt.OtherFocusReason);
        } else if (sheet === 2) {
            settingsSheet.used = true;
            settingsSheet.closeButton.forceActiveFocus(Qt.OtherFocusReason);
        } else if (before) {
            for (let it = appWindow.activeFocusItem; it; it = it.parent) {
                if (it === before) {
                    opener.forceActiveFocus(Qt.OtherFocusReason);
                    break;
                }
            }
        }
    }
    // A new place for the controls or the title glides there.
    Connections {
        target: App.settings
        function onPlayerControlsChanged() {
            page.animateLayout = true;
            settle.restart();
            page.controlsPlace = App.settings.playerControls;
        }
        function onPlayerHeaderChanged() {
            page.animateLayout = true;
            settle.restart();
            page.headerPlace = App.settings.playerHeader;
        }
    }
    onCanHideChanged: if (canHide)
                          hideTimer.restart()
                      else
                          chromeHidden = false

    // --- Layout ----------------------------------------------------------------------

    readonly property real topHeight: 48
    // The bottom row: the view buttons on the right, the title on the left when it is
    // there. With the controls at the bottom it is a bar about the mini player's
    // height: the controls in the middle over the progress bar (§1.2).
    readonly property real barHeight: 88
    readonly property real bottomRowHeight: controlsAtBottom ? barHeight : 64
    readonly property real rowCenter: height - bottomRowHeight / 2
    readonly property real contentTop: topHeight + 8
    readonly property real contentBottom: height - bottomRowHeight - 8
    readonly property real sideMargin: wide ? 48 : 24
    // Heights under the cover: header, progress, controls and the gaps between.
    readonly property real headerHeight: 64
    readonly property real progressHeight: 44
    readonly property real controlsHeight: 76
    readonly property real stackExtra: {
        const parts = [];
        if (!headerAtBottom)
            parts.push(headerHeight);
        if (!controlsAtBottom)
            parts.push(progressHeight + 8 + controlsHeight);
        return parts.length === 0 ? 0 : 28 + parts.reduce((a, b) => a + b, 0) + 16 * (parts.length - 1);
    }
    readonly property real stackBottom: contentBottom
    // The bottom bar's middle: controls over the progress bar.
    readonly property real barCenterWidth: Math.min(560, width * 0.42)
    readonly property real barCenterX: (width - barCenterWidth) / 2
    // Two columns share a centered group that grows with the window, up to 1800; the
    // cover column takes 45% of it.
    readonly property real groupWidth: Math.min(width - 2 * sideMargin, 1800)
    readonly property real groupX: (width - groupWidth) / 2
    readonly property real leftWidth: groupWidth * 0.45
    // The largest cover (also its decode size).
    readonly property real maxCover: 720
    readonly property real minStackWidth: 360

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
            const progressY = headerAtBottom ? below : below + headerHeight + 16;
            // The title in the bottom row: up to the bar's middle or the view buttons.
            const headerRight = controlsAtBottom ? barCenterX - 24
                                                 : width - sideMargin - viewButtons.width - 24;
            // The header, progress and controls under a small cover keep a usable width.
            const stackW = Math.max(size, Math.min(minStackWidth, columnW - 2 * sideMargin));
            const stackX = columnX + (columnW - stackW) / 2;
            const panelX = groupX + leftWidth + sideMargin / 2;
            return {
                cover: Qt.rect(x, y, size, size),
                header: headerAtBottom ? Qt.rect(sideMargin, rowCenter - 28,
                                                 Math.max(120, headerRight - sideMargin), 56)
                                       : Qt.rect(stackX, below, stackW, headerHeight),
                progress: controlsAtBottom ? Qt.rect(barCenterX, height - 30, barCenterWidth, 20)
                                           : Qt.rect(stackX, progressY, stackW, progressHeight),
                controls: controlsAtBottom ? Qt.rect((width - Math.min(360, barCenterWidth)) / 2,
                                                     height - barHeight + 6,
                                                     Math.min(360, barCenterWidth), 52)
                                           : Qt.rect(stackX, progressY + progressHeight + 8,
                                                     stackW, controlsHeight),
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

    // A side panel's place: the right of the page, between the top row and the bottom
    // row.
    component Sheet: PlayerSheet {
        width: Math.min(440, page.width - 2 * page.sideMargin)
        x: page.width - page.sideMargin / 2 - width
        y: page.contentTop
        height: page.contentBottom - y
        onCloseRequested: page.sheet = 0
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
        // Over the last stretch near the mini player it fades, so the mini player shows
        // through instead of appearing at once when the page is gone.
        opacity: page.miniBar && !page.reduced ? Theme.motion.segment(page.p, 0, 0.3) : 1

        PlayerBackground {
            anchors.fill: parent
            radius: page.panelRadius
            colors: colors
            flowing: App.player.status === Playback.Playing
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
                    id: lyricsView
                    anchors.fill: parent
                    colors: colors
                    positionMs: clock.positionMs
                    visible: page.view === 1
                    onCopyRequested: songInfo.open()
                }
            }

            // Clicking the empty bottom row, where the mini player was, closes the page
            // (§2.3); hovering it shows a very faint band with a chevron as the hint.
            Item {
                id: closeBand
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: page.bottomRowHeight
                opacity: Theme.motion.segment(page.p, 0.55, 1)

                readonly property bool hinted: bandHover.hovered && !page.overControls
                                               && !page.chromeHidden

                HoverHandler {
                    id: bandHover
                    cursorShape: page.overControls ? Qt.ArrowCursor : Qt.PointingHandCursor
                }
                TapHandler {
                    onTapped: if (!page.overControls)
                                  page.close()
                }
                Spring {
                    id: bandHint
                    target: closeBand.hinted ? 1 : 0
                    spec: Theme.motion.fastEffects
                }
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: parent.height
                    bottomLeftRadius: page.panelRadius
                    bottomRightRadius: page.panelRadius
                    opacity: bandHint.value
                    visible: opacity > 0.01
                    gradient: Gradient {
                        GradientStop {
                            position: 0
                            color: Qt.alpha(colors.content, 0)
                        }
                        GradientStop {
                            position: 1
                            color: Qt.alpha(colors.content, 0.035)
                        }
                    }
                }
                Icon {
                    anchors.centerIn: parent
                    visible: !page.controlsAtBottom && opacity > 0.01
                    opacity: 0.6 * bandHint.value
                    glyph: Symbols.expand_more
                    color: colors.contentVariant
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
                        compact: (!page.wide && page.view > 0) || page.headerAtBottom
                        moreAfterText: page.headerAtBottom
                        titleSize: Math.max(22, Math.min(32, page.layout.cover.width / 20))
                        onCommentsRequested: page.commentsRequested()
                        onArtistRequested: key => page.artistRequested(key)
                        onAlbumRequested: key => page.albumRequested(key)
                        onAddToPlaylistRequested: (key, title) => page.addToPlaylistRequested(key,
                                                                                              title)
                        onCopyRequested: songInfo.open()
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
                            timesBeside: page.controlsAtBottom
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
                            compact: page.controlsAtBottom
                            onLoginRequested: page.loginRequested()
                        }
                        HoverHandler {
                            id: controlsHover
                        }
                    }

                    // The bottom row: volume, lyrics, the queue and the display settings.
                    Row {
                        id: viewButtons
                        anchors.right: parent.right
                        anchors.rightMargin: page.sideMargin - 10
                        y: rowY.value - height / 2
                        spacing: 4

                        Spring {
                            id: rowY
                            spec: Theme.motion.defaultSpatial
                            readonly property real to: page.rowCenter
                            onToChanged: page.animateLayout ? target = to : snapTo(to)
                            Component.onCompleted: snapTo(to)
                        }

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
                            id: queueButton
                            checkable: true
                            checked: page.sheet === 1
                            color: colors.content
                            glyph: Symbols.queue_music
                            text: qsTr("播放队列")
                            onClicked: page.sheet = page.sheet === 1 ? 0 : 1
                        }
                        PlayerButton {
                            id: settingsButton
                            visible: page.settingsContent !== null
                            checkable: true
                            checked: page.sheet === 2
                            color: colors.content
                            glyph: Symbols.tune
                            text: qsTr("显示设置")
                            onClicked: page.sheet = page.sheet === 2 ? 0 : 2
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

            // The side panels (§8, §9.1): from the right, over the lyrics; see-through,
            // so display changes show behind. A click beside them or Esc closes them.
            MouseArea {
                anchors.fill: parent
                visible: page.sheet !== 0
                // The lyrics under it don't react while a panel is open.
                hoverEnabled: true
                onWheel: wheel => wheel.accepted = true
                onPressed: page.sheet = 0
            }
            Sheet {
                id: queueSheet
                title: qsTr("播放队列")
                shown: page.sheet === 1
                PlayerQueue {
                    id: queuePanel
                    anchors.fill: parent
                    onArtistRequested: key => page.artistRequested(key)
                    onAlbumRequested: key => page.albumRequested(key)
                    onAddToPlaylistRequested: (key, title) => page.addToPlaylistRequested(key, title)
                }
            }
            Sheet {
                id: settingsSheet
                // Loaded on first opening, then kept.
                property bool used: false
                title: qsTr("显示设置")
                shown: page.sheet === 2

                Flickable {
                    id: settingsFlick
                    anchors.fill: parent
                    clip: true
                    contentHeight: settingsLoader.height + 12
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}

                    // Tabbing to a row out of view scrolls it in.
                    Connections {
                        target: page.appWindow
                        enabled: page.sheet === 2
                        function onActiveFocusItemChanged() {
                            const item = page.appWindow.activeFocusItem;
                            let inside = false;
                            for (let it = item; it && !inside; it = it.parent)
                                inside = it === settingsScope;
                            if (!inside)
                                return;
                            const r = item.mapToItem(settingsFlick.contentItem, 0, 0, item.width,
                                                     item.height);
                            const flick = settingsFlick;
                            if (r.y < flick.contentY)
                                flick.contentY = Math.max(0, r.y - 16);
                            else if (r.y + r.height > flick.contentY + flick.height)
                                flick.contentY = Math.min(flick.contentHeight - flick.height,
                                                          r.y + r.height + 16 - flick.height);
                        }
                    }

                    FocusScope {
                        id: settingsScope
                        x: 16
                        width: settingsFlick.width - 32
                        height: settingsLoader.height
                        Loader {
                            id: settingsLoader
                            width: parent.width
                            active: settingsSheet.used
                            sourceComponent: page.settingsContent
                            onLoaded: item.width = Qt.binding(() => width)
                        }
                    }
                }
            }

            // While hidden, the first click only shows the controls, and the pointer is
            // hidden. Setting and resetting an item's cursor takes effect at once, even
            // with the pointer still; a handler's cursor would wait for the next move.
            MouseArea {
                anchors.fill: parent
                acceptedButtons: page.chromeHidden ? Qt.AllButtons : Qt.NoButton
                cursorShape: page.chromeHidden ? Qt.BlankCursor : undefined
                onPressed: page.wake()
            }
        }
    }

    Item {
        anchors.fill: parent
        anchors.bottomMargin: page.bottomRowHeight
        Snackbar {
            id: snackbar
            anchors.bottom: parent.bottom
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

    SongInfoDialog {
        id: songInfo
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
            if (page.sheet !== 0)
                page.sheet = 0;
            else if (lyricsView.handBrowsing)
                lyricsView.stopBrowsing(true);
            else if (page.fullScreenByPlayer || page.appWindow.visibility === Window.FullScreen)
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
                page.sheet = page.sheet === 1 ? 0 : 1;
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
