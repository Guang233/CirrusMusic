import QtQuick
import Cirrus.Theme
import Cirrus.Components

// Stand-in for a destination that is built later (UI phase 3, step 4).
PageScaffold {
    id: page

    required property AppShell shell
    property string description

    canGoBack: shell.canGoBack
    onBackRequested: shell.back()

    Text {
        x: 24
        width: parent.width - 48
        text: page.description
        wrapMode: Text.Wrap
        color: Theme.color.onSurfaceVariant
        font: Theme.type.bodyLarge.font
    }
}
