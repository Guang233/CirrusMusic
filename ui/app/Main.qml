pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Window
import Cirrus.Presentation
import Cirrus.Player

// Application window. `titleBar` comes from main.cpp (--titlebar, CIRRUS_TITLEBAR,
// App.settings.titleBar, else the desktop default); see docs/ui/README.md §6.1.
AppWindow {
    id: window

    // As the user left it (App.settings), within the screen.
    width: Math.max(minimumWidth, Math.min(App.settings.windowWidth, Screen.desktopAvailableWidth))
    height: Math.max(minimumHeight,
                     Math.min(App.settings.windowHeight, Screen.desktopAvailableHeight))
    visible: true
    title: qsTr("Cirrus")

    Component.onCompleted: {
        if (App.settings.windowMaximized)
            window.showMaximized();
    }
    onVisibilityChanged: {
        if (window.visibility === Window.Maximized)
            App.settings.windowMaximized = true;
        else if (window.visibility === Window.Windowed)
            App.settings.windowMaximized = false;
    }
    onWidthChanged: saveSize.restart()
    onHeightChanged: saveSize.restart()
    Timer {
        id: saveSize
        interval: 500
        onTriggered: {
            // The size to come back to, not a maximized or full-screen one.
            if (window.visibility !== Window.Windowed)
                return;
            App.settings.windowWidth = window.width;
            App.settings.windowHeight = window.height;
        }
    }

    // Appearance follows the user's settings.
    Binding {
        target: Theme
        property: "mode"
        value: App.settings.themeMode
    }
    Binding {
        target: Theme
        property: "seedSource"
        value: App.settings.seedSource
    }
    Binding {
        target: Theme
        property: "fixedSeed"
        value: App.settings.fixedSeed
    }
    Binding {
        target: Theme
        property: "variant"
        value: App.settings.variant
    }
    // The playing song's cover: the "follow the cover" seed and the player's colors.
    Binding {
        target: Theme
        property: "cover"
        value: App.player.current.coverKey !== "" ? "image://cover/" + encodeURIComponent(App.player.current.coverKey)
                                                  : ""
    }
    Binding {
        target: Theme.motion
        property: "reduced"
        value: App.settings.reducedMotion
    }
    Binding {
        target: Fonts
        property: "source"
        value: App.settings.fontSource
    }
    Binding {
        target: Fonts
        property: "fontFiles"
        value: App.settings.customFontFiles
    }
    Binding {
        target: Fonts
        property: "customFamily"
        value: App.settings.customFontFamily
    }

    // The frame (top bar and rail) is SurfaceContainer; pages sit on a Surface panel.
    showTitle: false
    surfaceColor: Theme.color.surfaceContainer
    titleContent: TopBar {
        id: topBar
        anchors.fill: parent
        shell: shell
        visible: !player.covering
    }

    AppShell {
        id: shell
        anchors.fill: parent
        searchField: topBar.searchField
        covered: player.opened
        miniFlying: player.flying
        // Hidden once the player page covers it (docs/ui/motion-choreography.md §2.2).
        opacity: 1 - Theme.motion.segment(player.progress, 0.5, 0.6)
        visible: !player.covering
        onPlayerRequested: player.open()
        onUncoverRequested: player.close()
        onCoveredNotice: text => player.notify(text)
    }

    // The player page, over everything including the top bar (docs/ui/pages/player.md).
    overlay: PlayerPage {
        id: player
        anchors.fill: parent
        appWindow: window
        miniBar: shell.miniBar
        miniCover: shell.miniCover
        miniTitle: shell.miniTitle
        miniControls: shell.miniControls
        miniSeek: shell.miniSeek
        settingsContent: Component {
            PlayerPanelSettings {}
        }
        onCommentsRequested: {
            close();
            const song = App.player.current;
            shell.openSongComments(song.key, song.title, song.artistText, song.coverKey);
        }
        onArtistRequested: key => {
            close();
            shell.openArtist(key);
        }
        onAlbumRequested: key => {
            close();
            shell.openAlbum(key);
        }
        onAddToPlaylistRequested: key => shell.addToPlaylist([key])
        onLoginRequested: shell.openLogin()
        // The focus goes back to the mini player (motion-choreography §2.2).
        onOpenedChanged: {
            App.diagnostics.playerShown(opened);
            if (!opened)
                shell.focusMiniPlayer();
        }
    }
    // "Something just went wrong": noted in the log with the UI's state (logging-v2 §4.4).
    Shortcut {
        sequence: "Ctrl+Alt+Shift+D"
        context: Qt.ApplicationShortcut
        onActivated: App.diagnostics.mark()
    }
    Connections {
        target: App.diagnostics
        function onMarked() {
            const text = qsTr("已记下这一刻，导出诊断包时会一起带上");
            if (player.opened)
                player.notify(text);
            else
                shell.notify(text);
        }
    }
    Shortcut {
        sequence: "Ctrl+Shift+F"
        enabled: App.player.current.key !== ""
        onActivated: player.opened ? player.close() : player.open()
    }

    // Closing hides the window (playback goes on, in the tray when there is one) or
    // quits, as the user chose. See docs/ui/pages/shell.md §7.
    function hideToTray(): void {
        window.hide();
    }
    function showFromTray(): void {
        if (window.visibility === Window.Minimized)
            window.showNormal();
        else
            window.show();
        window.raise();
        window.requestActivate();
    }

    onClosing: close => {
        if (Desktop.quitting)
            return;
        close.accepted = false;
        // A panel may have started since: show the tray icon, update the wording.
        Desktop.checkTray();
        const action = App.settings.closeAction;
        if (action === 1)
            hideToTray();
        else if (action === 0)
            Desktop.quit();
        else if (!closeDialog.opened)
            closeDialog.open();
    }

    // The way out when the window only hides and there is no tray menu.
    Shortcut {
        sequences: [StandardKey.Quit] // every binding of it (Ctrl+Q and the like)
        context: Qt.ApplicationShortcut
        onActivated: Desktop.quit()
    }

    Connections {
        target: Desktop
        function onShowRequested() {
            window.showFromTray();
        }
    }

    CloseDialog {
        id: closeDialog
        onChosen: action => action === 1 ? window.hideToTray() : Desktop.quit()
    }
}
