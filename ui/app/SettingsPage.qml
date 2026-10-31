pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components
import Cirrus.Window
import Cirrus.Presentation

// Settings, bound to App.settings (docs/ui/presentation.md §5.1).
PageScaffold {
    id: page

    required property AppShell shell

    title: qsTr("设置")

    // A label above a single-choice connected button group.
    component Choice: Column {
        id: choice
        property string label
        property string hint
        // [{value, text}]
        property var options: []
        property int value
        signal chosen(int value)

        spacing: 8
        Text {
            text: choice.label
            color: Theme.color.onSurface
            font: Theme.type.bodyLarge.font
        }
        ConnectedButtonGroup {
            exclusive: true
            Repeater {
                model: choice.options
                delegate: Button {
                    required property var modelData
                    size: Md.Small
                    variant: Md.Tonal
                    checkable: true
                    checked: choice.value === modelData.value
                    text: modelData.text
                    onClicked: choice.chosen(modelData.value)
                }
            }
        }
        Text {
            width: parent.width
            visible: text !== ""
            text: choice.hint
            wrapMode: Text.WordWrap
            color: Theme.color.onSurfaceVariant
            font: Theme.type.bodySmall.font
        }
    }

    component Section: Text {
        topPadding: 16
        color: Theme.color.primary
        font: Theme.type.titleSmall.font
    }

    component Toggle: ListItem {
        id: toggle
        property bool value
        signal switched(bool value)
        // Text aligned with the other labels; the item's padding extends to the left.
        x: -leftPadding
        width: column.width + leftPadding + rightPadding
        onClicked: switched(!value)
        trailing: Component {
            Switch {
                focusPolicy: Qt.NoFocus
                checked: toggle.value
                onToggled: toggle.switched(checked)
            }
        }
    }

    Flickable {
        anchors.fill: parent
        contentHeight: column.implicitHeight + 32
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}

        Column {
            id: column
            x: 24
            width: Math.min(parent.width - 48, 720)
            spacing: 16

            Section {
                text: qsTr("账号")
            }
            Row {
                spacing: 16
                CoverArt {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 48
                    height: 48
                    radius: width / 2
                    glyph: Symbols.person
                    coverKey: App.session.avatarKey
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: column.width - 64 - 16 - accountButton.width
                    text: App.session.signedIn ? App.session.nickname : qsTr("未登录")
                    elide: Text.ElideRight
                    color: Theme.color.onSurface
                    font: Theme.type.bodyLarge.font
                }
                Button {
                    id: accountButton
                    anchors.verticalCenter: parent.verticalCenter
                    variant: App.session.signedIn ? Md.Outlined : Md.Filled
                    glyph: App.session.signedIn ? Symbols.logout : Symbols.login
                    text: App.session.signedIn ? qsTr("退出登录") : qsTr("登录")
                    onClicked: App.session.signedIn ? App.session.signOut() : page.shell.openLogin()
                }
            }

            Section {
                text: qsTr("外观")
            }
            Choice {
                width: column.width
                label: qsTr("明暗")
                options: [{
                        value: 0,
                        text: qsTr("跟随系统")
                    }, {
                        value: 1,
                        text: qsTr("亮色")
                    }, {
                        value: 2,
                        text: qsTr("暗色")
                    }]
                value: App.settings.themeMode
                onChosen: value => App.settings.themeMode = value
            }
            Choice {
                width: column.width
                label: qsTr("主题色来源")
                hint: qsTr("跟随封面时，界面配色随正在播放的歌曲变化。")
                options: [{
                        value: 2,
                        text: qsTr("系统强调色")
                    }, {
                        value: 0,
                        text: qsTr("跟随封面")
                    }, {
                        value: 1,
                        text: qsTr("固定")
                    }]
                value: App.settings.seedSource
                onChosen: value => App.settings.seedSource = value
            }
            Choice {
                width: column.width
                label: qsTr("字体")
                hint: Fonts.bundledAvailable
                      ? qsTr("内置字体为小米 MiSans。字体缺少的字符（如韩文、emoji）由系统字体补全。")
                      : qsTr("这个版本没有内置字体，使用系统字体。")
                options: [{
                        value: Fonts.Bundled,
                        text: qsTr("内置（MiSans）")
                    }, {
                        value: Fonts.System,
                        text: qsTr("系统字体")
                    }, {
                        value: Fonts.Custom,
                        text: qsTr("自定义")
                    }]
                value: App.settings.fontSource
                onChosen: value => App.settings.fontSource = value
            }
            // The custom family, from the installed fonts.
            Row {
                visible: App.settings.fontSource === Fonts.Custom
                spacing: 12
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("字体族")
                    color: Theme.color.onSurfaceVariant
                    font: Theme.type.bodyMedium.font
                }
                Button {
                    id: familyButton
                    variant: Md.Outlined
                    trailingGlyph: Symbols.keyboard_arrow_down
                    text: Fonts.customFamily !== "" && Fonts.sans === Fonts.customFamily
                          ? Fonts.customFamily : qsTr("选择字体")
                    onClicked: familyMenu.popup(familyButton, 0, familyButton.height)
                }
                Menu {
                    id: familyMenu
                    Instantiator {
                        // Built when first needed: there can be hundreds.
                        active: App.settings.fontSource === Fonts.Custom
                        model: active ? Fonts.families() : []
                        delegate: MenuItem {
                            required property string modelData
                            checkable: true
                            checked: modelData === App.settings.customFontFamily
                            text: modelData
                            onTriggered: App.settings.customFontFamily = modelData
                        }
                        onObjectAdded: (index, object) => familyMenu.insertItem(index, object as MenuItem)
                        onObjectRemoved: (index, object) => familyMenu.removeItem(object as MenuItem)
                    }
                }
            }
            Toggle {
                text: qsTr("减少动态效果")
                supportingText: qsTr("页面切换改为淡入淡出，不做位移和缩放")
                value: App.settings.reducedMotion
                onSwitched: value => App.settings.reducedMotion = value
            }

            Section {
                text: qsTr("窗口")
            }
            Choice {
                width: column.width
                label: qsTr("窗口装饰")
                hint: qsTr("顶栏（导航按钮、搜索）始终都在。应用绘制：窗口按钮、阴影和圆角由应用绘制。无：不画窗口按钮和阴影，适合 niri、sway、Hyprland 等平铺窗口管理器。系统：使用桌面自己的标题栏，出问题时再用。也可以用启动参数 --titlebar=custom|hidden|system 临时指定。")
                options: [{
                        value: TitleBarMode.Custom,
                        text: qsTr("应用绘制")
                    }, {
                        value: TitleBarMode.Hidden,
                        text: qsTr("无")
                    }, {
                        value: TitleBarMode.System,
                        text: qsTr("系统")
                    }]
                value: (page.Window.window as AppWindow)?.titleBar ?? -1
                onChosen: value => {
                    App.settings.titleBar = value;
                    (page.Window.window as AppWindow)?.setTitleBar(value);
                }
            }

            Section {
                text: qsTr("歌词")
            }
            Toggle {
                text: qsTr("显示翻译")
                value: App.settings.showTranslation
                onSwitched: value => App.settings.showTranslation = value
            }
            Toggle {
                text: qsTr("显示音译")
                value: App.settings.showRomanization
                onSwitched: value => App.settings.showRomanization = value
            }
        }
    }
}
