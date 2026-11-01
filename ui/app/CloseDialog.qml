import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Presentation

// Asked the first time the main window is closed while a tray icon is shown
// (docs/ui/pages/shell.md §7). `chosen` carries App.settings.closeAction's value:
// 0 quit, 1 hide to the tray. Esc or the close button keeps the window open.
Dialog {
    id: dialog

    signal chosen(int action)

    function choose(action: int): void {
        if (remember.checked)
            App.settings.closeAction = action;
        chosen(action);
    }

    width: 440
    title: qsTr("关闭主窗口")
    supportingText: qsTr("最小化到托盘后继续播放。从托盘图标可以重新打开窗口或退出。")
    acceptText: qsTr("最小化到托盘")
    onAboutToShow: remember.checked = true
    onAccepted: choose(1)

    buttons: Button {
        variant: Md.Text
        text: qsTr("退出")
        onClicked: {
            dialog.close();
            dialog.choose(0);
        }
    }

    CheckBox {
        id: remember
        text: qsTr("记住我的选择（可以在「设置」里更改）")
    }
}
