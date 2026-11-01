pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Window
import Cirrus.Presentation

// Application window. `titleBar` comes from main.cpp (--titlebar, CIRRUS_TITLEBAR,
// App.settings.titleBar, else the desktop default); see docs/ui/README.md §6.1.
AppWindow {
    id: window

    width: 1200
    height: 800
    visible: true
    title: qsTr("Cirrus Music")

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
        property: "variant"
        value: App.settings.variant
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
    }

    AppShell {
        id: shell
        anchors.fill: parent
        searchField: topBar.searchField
    }

    // Closing hides the window to the tray (playback goes on) or quits, as the user
    // chose; without a tray it quits. See docs/ui/pages/shell.md §7.
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
        const action = Desktop.checkTray() ? App.settings.closeAction : 0;
        if (action === 1)
            hideToTray();
        else if (action === 0)
            Desktop.quit();
        else if (!closeDialog.opened)
            closeDialog.open();
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
