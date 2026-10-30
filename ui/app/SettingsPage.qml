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
    canGoBack: shell.canGoBack
    onBackRequested: shell.back()

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
                label: qsTr("标题栏")
                hint: qsTr("自绘：窗口按钮与阴影由应用绘制。系统：使用桌面的标题栏。隐藏：不显示标题栏，适合平铺窗口管理器。也可以用启动参数 --titlebar 临时指定。")
                options: [{
                        value: TitleBarMode.Custom,
                        text: qsTr("自绘")
                    }, {
                        value: TitleBarMode.System,
                        text: qsTr("系统")
                    }, {
                        value: TitleBarMode.Hidden,
                        text: qsTr("隐藏")
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
