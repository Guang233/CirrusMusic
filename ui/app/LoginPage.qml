pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components
import Cirrus.Presentation

// QR sign-in. Opening the page starts it, leaving cancels it; once signed in the page
// goes back to where it was opened from.
PageScaffold {
    id: page

    required property AppShell shell

    readonly property int phase: App.session.qrLogin.phase

    title: qsTr("登录")
    canGoBack: shell.canGoBack
    onBackRequested: shell.back()

    Component.onCompleted: App.session.beginQrLogin()
    Component.onDestruction: {
        if (App.session.status === Account.SigningIn)
            App.session.cancelLogin();
    }

    Connections {
        target: App.session
        function onAccountChanged() {
            if (App.session.signedIn && page.shell.canGoBack)
                page.shell.back();
        }
    }

    Column {
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 360)
        spacing: 16

        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("扫码登录")
            color: Theme.color.onSurface
            font: Theme.type.headlineSmall.font
        }
        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            text: qsTr("用手机上的音乐 App 扫描二维码")
            color: Theme.color.onSurfaceVariant
            font: Theme.type.bodyMedium.font
        }

        // The code on white whatever the theme, so phones can read it.
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 232
            height: 232
            radius: Theme.shape.large
            color: "white"

            Image {
                id: code
                anchors.centerIn: parent
                width: 200
                height: 200
                source: App.session.qrLogin.qrImage
                sourceSize: Qt.size(200, 200)
                smooth: false
                cache: false
                opacity: page.phase === Account.WaitingForScan ? 1 : 0.15
            }
            LoadingIndicator {
                anchors.centerIn: parent
                visible: page.phase === Account.NoQr || page.phase === Account.Confirming
            }
            Icon {
                anchors.centerIn: parent
                visible: page.phase === Account.WaitingForConfirmation
                glyph: Symbols.check
                size: 64
                color: Theme.color.primary
            }
            Button {
                anchors.centerIn: parent
                visible: page.phase === Account.QrExpired || page.phase === Account.QrFailed
                variant: Md.Filled
                glyph: Symbols.refresh
                text: qsTr("刷新二维码")
                onClicked: App.session.refreshQr()
            }
        }

        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            text: {
                switch (page.phase) {
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
            color: page.phase === Account.QrFailed ? Theme.color.error : Theme.color.onSurface
            font: Theme.type.bodyLarge.font
        }
        CheckBox {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("记住登录状态")
            checked: App.session.qrLogin.remember
            onToggled: App.session.qrLogin.remember = checked
        }
    }
}
