import QtQuick
import Cirrus.Window
import Cirrus.Presentation
import CirrusMusic

// The window and how the app lives on the desktop.
SettingsGroup {
    id: group

    title: qsTr("窗口与系统")

    ChoiceSetting {
        text: qsTr("窗口装饰")
        supportingText: qsTr("顶栏（导航按钮、搜索）始终都在。应用绘制：窗口按钮、阴影和圆角由应用绘制。无：不画窗口按钮和阴影，适合 niri、sway、Hyprland 等平铺窗口管理器。系统：使用桌面自己的标题栏，出问题时再用。也可以用启动参数 --titlebar=custom|hidden|system 临时指定。")
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
        value: (group.Window.window as AppWindow)?.titleBar ?? -1
        onChosen: value => {
            App.settings.titleBar = value;
            (group.Window.window as AppWindow)?.setTitleBar(value);
        }
    }
    ChoiceSetting {
        text: qsTr("关闭主窗口时")
        supportingText: Desktop.trayAvailable
            ? qsTr("最小化到托盘后继续播放。从托盘图标可以重新打开窗口或退出。")
            : qsTr("当前桌面没有系统托盘（例如没有装托盘扩展的 GNOME）。在后台播放时，再次打开 Cirrus Music 回到窗口，在窗口里按 Ctrl+Q 退出。")
        options: [{
                value: -1,
                text: qsTr("每次询问")
            }, {
                value: 1,
                text: Desktop.trayAvailable ? qsTr("最小化到托盘") : qsTr("后台播放")
            }, {
                value: 0,
                text: qsTr("退出")
            }]
        value: App.settings.closeAction
        onChosen: value => App.settings.closeAction = value
    }
    SwitchSetting {
        visible: Desktop.switchableGraphics
        text: qsTr("使用独立显卡")
        supportingText: qsTr("界面更流畅，但更耗电；窗口隐藏时不占用显卡。重启后生效。")
        value: App.settings.preferDiscreteGpu
        onSwitched: value => App.settings.preferDiscreteGpu = value
    }
}
