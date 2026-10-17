pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Cirrus.Theme

// Development gallery. The chrome uses QtQuick.Controls.Basic and fixed neutral
// colors only until Cirrus.Theme provides real tokens (UI phase 1).
ApplicationWindow {
    id: window

    required property string initialPage
    property bool dark: Application.styleHints.colorScheme === Qt.Dark
    readonly property color surface: dark ? "#141218" : "#fdf8fd"
    readonly property color surfaceVariant: dark ? "#211f26" : "#f1ecf4"
    readonly property color foreground: dark ? "#e6e0e9" : "#1d1b20"
    readonly property color foregroundMuted: dark ? "#cac4d0" : "#49454f"
    readonly property color accent: dark ? "#d0bcff" : "#6750a4"

    component PageEntry: QtObject {
        required property string key
        required property string title
        required property Component page
    }

    readonly property list<PageEntry> pages: [
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
    color: surface
    font.family: Fonts.sans

    Component {
        id: resourcesPage
        ResourcesPage {
            foreground: window.foreground
            foregroundMuted: window.foregroundMuted
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 200
            color: window.surfaceVariant

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 4

                Label {
                    text: qsTr("Cirrus Gallery")
                    color: window.foreground
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                    Layout.bottomMargin: 8
                }

                ListView {
                    id: nav
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: window.pages
                    currentIndex: Math.max(0, window.pages.findIndex(p => p.key === window.initialPage))
                    delegate: ItemDelegate {
                        required property int index
                        required property PageEntry modelData
                        width: ListView.view.width
                        text: modelData.title
                        highlighted: ListView.isCurrentItem
                        onClicked: nav.currentIndex = index
                    }
                }

                Switch {
                    text: qsTr("深色")
                    checked: window.dark
                    onToggled: window.dark = checked
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
