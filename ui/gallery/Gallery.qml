pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Cirrus.Theme
import Cirrus.Samples

// Development gallery. The chrome still uses QtQuick.Controls.Basic until the
// MD3E controls exist (UI phase 2); colors and type come from Cirrus.Theme.
ApplicationWindow {
    id: window

    required property string initialPage
    required property string initialCover
    required property string initialMode

    component PageEntry: QtObject {
        required property string key
        required property string title
        required property Component page
    }

    readonly property list<PageEntry> pages: [
        PageEntry {
            key: "theme"
            title: qsTr("主题")
            page: themePage
        },
        PageEntry {
            key: "resources"
            title: qsTr("资源")
            page: resourcesPage
        }
    ]

    visible: true
    title: qsTr("Cirrus Gallery")
    minimumWidth: 720
    minimumHeight: 480
    color: Theme.color.surface
    font: Theme.type.bodyMedium.font

    Component.onCompleted: {
        if (initialCover !== "")
            Theme.cover = Samples.cover(initialCover);
        if (initialMode === "light")
            Theme.mode = Theme.Mode.Light;
        else if (initialMode === "dark")
            Theme.mode = Theme.Mode.Dark;
    }

    Component {
        id: themePage
        ThemePage {}
    }

    Component {
        id: resourcesPage
        ResourcesPage {
            foreground: Theme.color.onSurface
            foregroundMuted: Theme.color.onSurfaceVariant
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 200
            color: Theme.color.surfaceContainer

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 4

                Text {
                    text: qsTr("Cirrus Gallery")
                    color: Theme.color.onSurface
                    font: Theme.type.titleLarge.font
                    Layout.bottomMargin: 8
                }

                ListView {
                    id: nav
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 2
                    model: window.pages
                    currentIndex: Math.max(0, window.pages.findIndex(p => p.key === window.initialPage))
                    delegate: Rectangle {
                        id: navItem
                        required property int index
                        required property PageEntry modelData
                        readonly property bool current: ListView.isCurrentItem
                        width: ListView.view.width
                        height: 40
                        radius: Theme.shape.full
                        color: current ? Theme.color.secondaryContainer : "transparent"
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            x: 16
                            text: navItem.modelData.title
                            color: navItem.current ? Theme.color.onSecondaryContainer : Theme.color.onSurfaceVariant
                            font: Theme.type.labelLarge.font
                        }
                        TapHandler {
                            onTapped: nav.currentIndex = navItem.index
                        }
                    }
                }

                Switch {
                    text: qsTr("深色")
                    checked: Theme.dark
                    onToggled: Theme.mode = checked ? Theme.Mode.Dark : Theme.Mode.Light
                }
            }
        }

        Loader {
            Layout.fillWidth: true
            Layout.fillHeight: true
            sourceComponent: window.pages[nav.currentIndex].page
        }
    }
}
