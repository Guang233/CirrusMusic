import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Presentation

// Asked the first time the main window is closed (docs/ui/pages/shell.md §7).
// `chosen` carries App.settings.closeAction's value: 0 quit, 1 keep playing in the
// background (in the tray when there is one). Esc, 取消 or the close button keep the
// window open.
Dialog {
    id: dialog

    signal chosen(int action)

    width: 440
    title: qsTr("关闭主窗口")
    acceptText: qsTr("确定")
    rejectText: qsTr("取消")
    onAboutToShow: {
        keepPlaying.checked = true;
        remember.checked = true;
    }
    onAccepted: {
        const action = keepPlaying.checked ? 1 : 0;
        if (remember.checked)
            App.settings.closeAction = action;
        chosen(action);
    }

    Column {
        width: parent.width
        spacing: 12

        RadioButton {
            id: keepPlaying
            text: Desktop.trayAvailable ? qsTr("最小化到托盘，继续播放") : qsTr("在后台继续播放")
        }
        Text {
            // Under the radio button's label.
            x: keepPlaying.leftPadding + keepPlaying.indicator.width + keepPlaying.spacing
            width: parent.width - x
            text: Desktop.trayAvailable
                  ? qsTr("从托盘图标可以重新打开窗口或退出。")
                  : qsTr("当前桌面没有系统托盘：再次打开 Cirrus 回到窗口，在窗口里按 Ctrl+Q 退出。")
            wrapMode: Text.Wrap
            color: Theme.color.onSurfaceVariant
            font: Theme.type.bodySmall.font
        }
        RadioButton {
            text: qsTr("退出 Cirrus")
        }
        CheckBox {
            id: remember
            text: qsTr("不再询问（可以在「设置」里更改）")
        }
    }
}
