pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls

// The app, its version, and what it is built with.
SettingsGroup {
    id: group

    // The open source parts and fonts, with their licenses (third_party/, ui/theme/fonts/).
    readonly property var components: [{
            name: "Qt",
            license: "LGPL-3.0",
            use: qsTr("应用框架")
        }, {
            name: qsTr("小米 MiSans"),
            license: qsTr("MiSans 字体知识产权许可协议"),
            use: qsTr("界面文字字体")
        }, {
            name: "Material Symbols",
            license: "Apache-2.0",
            use: qsTr("图标")
        }, {
            name: "material-color-utilities",
            license: "Apache-2.0",
            use: qsTr("配色生成")
        }, {
            name: "m3shapes",
            license: "Apache-2.0",
            use: qsTr("形状与变形")
        }, {
            name: "NeteaseCloudMusicApi Enhanced",
            license: "MIT",
            use: qsTr("接口参考")
        }]

    title: qsTr("关于")
    glyph: Symbols.info
    hue: "grey"

    SettingRow {
        text: qsTr("Cirrus Music %1").arg(Qt.application.version)
        supportingText: qsTr("非官方客户端，与网易云音乐没有关联。界面文字使用小米 MiSans 字体。")
        focusPolicy: Qt.NoFocus
        hoverEnabled: false
        leading: Component {
            Image {
                width: 40
                height: 40
                sourceSize: Qt.size(80, 80)
                source: "qrc:/qt/qml/CirrusMusic/icons/cirrus-music.svg"
            }
        }
    }
    ActionSetting {
        text: qsTr("第三方许可")
        supportingText: ""
        actionText: qsTr("查看")
        onTriggered: licensesDialog.open()
    }

    Dialog {
        id: licensesDialog
        title: qsTr("第三方许可")
        acceptText: qsTr("关闭")
        Column {
            width: 400
            spacing: 12
            Repeater {
                model: group.components
                delegate: Column {
                    required property var modelData
                    width: parent.width
                    Text {
                        width: parent.width
                        text: parent.modelData.name
                        color: ColorScope.scheme.onSurface
                        font: Theme.type.titleSmall.font
                    }
                    Text {
                        width: parent.width
                        text: qsTr("%1 · %2").arg(parent.modelData.use).arg(parent.modelData.license)
                        wrapMode: Text.Wrap
                        color: ColorScope.scheme.onSurfaceVariant
                        font: Theme.type.bodyMedium.font
                    }
                }
            }
        }
    }
}
