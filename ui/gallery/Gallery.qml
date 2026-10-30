pragma ComponentBehavior: Bound
import QtCore
import QtQuick
import QtQuick.Layouts
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Samples
import Cirrus.Window
import Cirrus.Presentation

// Development gallery, in the same AppWindow frame as the app (--titlebar picks
// the title bar mode). A few pages still use QtQuick.Controls.Basic as B.
AppWindow {
    id: window

    required property string initialPage
    required property string initialCover
    required property string initialMode
    required property bool capturing

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
            key: "buttons"
            title: qsTr("按钮")
            page: buttonsPage
        },
        PageEntry {
            key: "inputs"
            title: qsTr("选择与输入")
            page: inputsPage
        },
        PageEntry {
            key: "progress"
            title: qsTr("进度")
            page: progressPage
        },
        PageEntry {
            key: "lists"
            title: qsTr("列表与菜单")
            page: listsPage
        },
        PageEntry {
            key: "navigation"
            title: qsTr("导航")
            page: navigationPage
        },
        PageEntry {
            key: "data"
            title: qsTr("数据与状态")
            page: dataPage
        },
        PageEntry {
            key: "states"
            title: qsTr("页面状态")
            page: statesPage
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

    // Last page, cover and mode of interactive runs, so a rebuild-and-restart (see
    // tools/ui/gallery-watch.sh) comes back where it was. Unused by --grab runs.
    Settings {
        id: memory
        category: "Gallery"
        property string page: "theme"
        property string mode: "system"
        property string cover: Samples.cover("sunset").toString()
    }

    readonly property list<string> modeNames: ["system", "light", "dark"]
    property bool restored: false

    Component.onCompleted: {
        GalleryState.capturing = capturing;
        const page = initialPage !== "" ? initialPage : capturing ? "theme" : memory.page;
        nav.currentIndex = Math.max(0, pages.findIndex(p => p.key === page));
        if (initialCover !== "")
            Theme.cover = Samples.cover(initialCover);
        else
            Theme.cover = capturing ? Samples.cover("sunset") : memory.cover;
        const mode = initialMode !== "" ? initialMode : capturing ? "system" : memory.mode;
        Theme.mode = [Theme.Mode.System, Theme.Mode.Light, Theme.Mode.Dark][Math.max(0, modeNames.indexOf(mode))];
        restored = !capturing;
    }

    Connections {
        target: Theme
        enabled: window.restored
        function onModeChanged() {
            memory.mode = window.modeNames[Theme.mode];
        }
        function onCoverChanged() {
            memory.cover = Theme.cover.toString();
        }
    }

    Component {
        id: themePage
        ThemePage {}
    }

    Component {
        id: buttonsPage
        ButtonsPage {}
    }

    Component {
        id: inputsPage
        InputsPage {}
    }

    Component {
        id: progressPage
        ProgressPage {}
    }

    Component {
        id: listsPage
        ListsPage {}
    }

    Component {
        id: navigationPage
        NavigationPage {}
    }

    Component {
        id: dataPage
        DataPage {}
    }

    Component {
        id: statesPage
        StatesPage {}
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
            bottomLeftRadius: window.cornerRadius

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 4

                ListView {
                    id: nav
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 2
                    model: window.pages
                    onCurrentIndexChanged: if (window.restored)
                                               memory.page = window.pages[currentIndex].key
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

                Text {
                    text: qsTr("标题栏")
                    color: Theme.color.onSurfaceVariant
                    font: Theme.type.labelMedium.font
                }
                // Switches without a restart (AppWindow.setTitleBar).
                ConnectedButtonGroup {
                    exclusive: true
                    Layout.bottomMargin: 8

                    Repeater {
                        model: [{
                                mode: TitleBarMode.Custom,
                                label: qsTr("自绘")
                            }, {
                                mode: TitleBarMode.System,
                                label: qsTr("系统")
                            }, {
                                mode: TitleBarMode.Hidden,
                                label: qsTr("隐藏")
                            }]
                        delegate: Button {
                            required property var modelData
                            size: Md.XSmall
                            variant: Md.Tonal
                            checkable: true
                            checked: window.titleBar === modelData.mode
                            text: modelData.label
                            onClicked: {
                                // Reopened by main.cpp on the next interactive run.
                                if (window.restored)
                                    App.settings.titleBar = modelData.mode;
                                window.setTitleBar(modelData.mode);
                            }
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
