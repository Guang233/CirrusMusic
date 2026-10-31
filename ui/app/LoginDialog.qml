pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Presentation

// QR sign-in in a dialog over whatever page is open (docs/ui/pages/shell.md §4).
// Opening it starts the sign-in, closing it cancels one still in progress; it closes
// itself once signed in.
Dialog {
    id: dialog

    readonly property int phase: App.session.qrLogin.phase

    width: 360
    title: qsTr("扫码登录")
    supportingText: qsTr("用手机上的音乐 App 扫描二维码。")

    onOpened: App.session.beginQrLogin()
    onClosed: {
        if (App.session.status === Account.SigningIn)
            App.session.cancelLogin();
    }

    Connections {
        target: App.session
        function onAccountChanged() {
            if (App.session.signedIn)
                dialog.close();
        }
    }

    // The code on white whatever the theme, so phones can read it.
    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        width: 216
        height: 216
        radius: Theme.shape.large
        color: "white"

        Image {
            anchors.centerIn: parent
            width: 184
            height: 184
            source: dialog.visible ? App.session.qrLogin.qrImage : ""
            sourceSize: Qt.size(184, 184)
            smooth: false
            cache: false
            opacity: dialog.phase === Account.WaitingForScan ? 1 : 0.15
        }
        LoadingIndicator {
            anchors.centerIn: parent
            visible: dialog.phase === Account.NoQr || dialog.phase === Account.Confirming
        }
        Icon {
            anchors.centerIn: parent
            visible: dialog.phase === Account.WaitingForConfirmation
            glyph: Symbols.check
            size: 64
            color: Theme.color.primary
        }
        Button {
            anchors.centerIn: parent
            visible: dialog.phase === Account.QrExpired || dialog.phase === Account.QrFailed
            variant: Md.Filled
            glyph: Symbols.refresh
            text: qsTr("刷新二维码")
            onClicked: App.session.refreshQr()
        }
    }

    Item {
        width: parent.width
        height: 12
    }

    Text {
        width: parent.width
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        text: {
            switch (dialog.phase) {
            case Account.WaitingForScan:
                return qsTr("等待扫码");
            case Account.WaitingForConfirmation:
                return qsTr("已扫码，请在手机上确认登录");
            case Account.Confirming:
                return qsTr("正在登录……");
            case Account.QrExpired:
                return qsTr("二维码已过期");
            case Account.QrFailed:
                return qsTr("登录失败，请刷新二维码重试");
            default:
                return App.session.signedIn ? qsTr("已登录") : qsTr("正在获取二维码……");
            }
        }
        color: dialog.phase === Account.QrFailed ? Theme.color.error : Theme.color.onSurface
        font: Theme.type.bodyLarge.font
    }

    CheckBox {
        anchors.horizontalCenter: parent.horizontalCenter
        text: qsTr("记住登录状态")
        checked: App.session.qrLogin.remember
        onToggled: App.session.qrLogin.remember = checked
    }

    buttons: [
        Button {
            variant: Md.Text
            text: qsTr("取消")
            onClicked: dialog.close()
        }
    ]
}
