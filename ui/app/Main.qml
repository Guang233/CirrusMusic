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

    AppShell {
        anchors.fill: parent
    }
}
