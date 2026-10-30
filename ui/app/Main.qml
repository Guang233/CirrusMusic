pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Window

// Application window. `titleBar` comes from main.cpp (--titlebar, CIRRUS_TITLEBAR,
// else the desktop default); see docs/ui/README.md §6.1.
AppWindow {
    id: window

    width: 1200
    height: 800
    visible: true
    title: qsTr("Cirrus Music")

    // Prototype content until the app shell exists (UI phase 3, step 3).
    Column {
        anchors.centerIn: parent
        spacing: 16

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("窗口原型")
            color: Theme.color.onSurface
            font: Theme.type.headlineSmall.font
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            horizontalAlignment: Text.AlignHCenter
            lineHeight: Theme.type.bodyMedium.lineHeight
            lineHeightMode: Text.FixedHeight
            color: Theme.color.onSurfaceVariant
            font: Theme.type.bodyMedium.font
            text: [qsTr("平台：%1").arg(Qt.platform.pluginName),
                qsTr("窗口：%1").arg(window.maximized ? qsTr("最大化")
                                                     : window.frame.tiled ? qsTr("平铺") : qsTr("普通")),
                qsTr("阴影边距：%1 px").arg(window.frame.margin)].join("\n")
        }
        // Switches without a restart (see AppWindow.setTitleBar).
        ConnectedButtonGroup {
            anchors.horizontalCenter: parent.horizontalCenter
            exclusive: true

            Repeater {
                model: [{
                        mode: TitleBarMode.Custom,
                        label: qsTr("自绘标题栏")
                    }, {
                        mode: TitleBarMode.System,
                        label: qsTr("系统标题栏")
                    }, {
                        mode: TitleBarMode.Hidden,
                        label: qsTr("隐藏标题栏")
                    }]
                delegate: Button {
                    required property var modelData
                    variant: Md.Tonal
                    checkable: true
                    checked: window.titleBar === modelData.mode
                    text: modelData.label
                    onClicked: window.setTitleBar(modelData.mode)
                }
            }
        }
    }
}
