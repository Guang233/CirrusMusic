pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Presentation

// Sign-in in a dialog over whatever page is open (docs/ui/pages/shell.md §4): the QR
// code on the left, phone or email on the right, both usable at once. Opening it
// starts the QR sign-in, closing it cancels one still in progress; it closes itself
// once signed in.
Dialog {
    id: dialog

    readonly property int phase: App.session.qrLogin.phase
    readonly property FormLogin form: App.session.formLogin
    readonly property bool byEmail: tabs.currentIndex === 1
    property bool byCode: true
    property string countryCode: "86"
    // For the resend countdown.
    property real now: Date.now()
    readonly property int resendSeconds: Math.max(0, Math.ceil((form.resendAt - now) / 1000))
    readonly property bool canSubmit: !form.busy && (byEmail
        ? emailField.length > 0 && emailPasswordField.length > 0
        : phoneField.length > 0 && (byCode ? codeField.length > 0 : passwordField.length > 0))

    // Common regions first; the rest by pinyin.
    readonly property var regions: [
        { code: "86", name: qsTr("中国大陆") },
        { code: "852", name: qsTr("中国香港") },
        { code: "853", name: qsTr("中国澳门") },
        { code: "886", name: qsTr("中国台湾") },
        { code: "1", name: qsTr("美国／加拿大") },
        { code: "81", name: qsTr("日本") },
        { code: "82", name: qsTr("韩国") },
        { code: "65", name: qsTr("新加坡") },
        { code: "60", name: qsTr("马来西亚") },
        { code: "44", name: qsTr("英国") },
        { code: "61", name: qsTr("澳大利亚") },
        { code: "49", name: qsTr("德国") },
        { code: "33", name: qsTr("法国") },
        { code: "971", name: qsTr("阿联酋") },
        { code: "353", name: qsTr("爱尔兰") },
        { code: "55", name: qsTr("巴西") },
        { code: "7", name: qsTr("俄罗斯") },
        { code: "63", name: qsTr("菲律宾") },
        { code: "31", name: qsTr("荷兰") },
        { code: "855", name: qsTr("柬埔寨") },
        { code: "95", name: qsTr("缅甸") },
        { code: "46", name: qsTr("瑞典") },
        { code: "41", name: qsTr("瑞士") },
        { code: "66", name: qsTr("泰国") },
        { code: "34", name: qsTr("西班牙") },
        { code: "64", name: qsTr("新西兰") },
        { code: "39", name: qsTr("意大利") },
        { code: "91", name: qsTr("印度") },
        { code: "62", name: qsTr("印度尼西亚") },
        { code: "84", name: qsTr("越南") }
    ]

    function submit(): void {
        if (!canSubmit)
            return;
        if (byEmail)
            App.session.beginFormLogin(Account.EmailPassword, "", emailField.text,
                                       emailPasswordField.text);
        else if (byCode)
            App.session.beginFormLogin(Account.PhoneCode, countryCode, phoneField.text,
                                       codeField.text);
        else
            App.session.beginFormLogin(Account.PhonePassword, countryCode, phoneField.text,
                                       passwordField.text);
    }

    width: 760
    title: qsTr("登录")

    onOpened: {
        App.session.clearFormError();
        App.session.beginQrLogin();
    }
    onClosed: {
        if (App.session.status === Account.SigningIn)
            App.session.cancelLogin();
        // Don't keep what was typed, the password least of all.
        passwordField.clear();
        emailPasswordField.clear();
        codeField.clear();
    }

    Connections {
        target: App.session
        function onAccountChanged() {
            if (App.session.signedIn)
                dialog.close();
        }
    }
    // A phone/email sign-in ends the QR one; after it failed, show a new code.
    Connections {
        target: dialog.form
        function onChanged() {
            dialog.now = Date.now();
            if (dialog.visible && !dialog.form.busy && dialog.form.error !== ""
                    && dialog.phase === Account.NoQr && App.session.status !== Account.SignedIn)
                App.session.beginQrLogin();
        }
    }
    Timer {
        interval: 1000
        repeat: true
        running: dialog.visible && dialog.resendSeconds > 0
        onTriggered: dialog.now = Date.now()
    }

    Row {
        width: parent.width

        // QR code.
        Column {
            id: qrSide
            width: 264
            spacing: 16

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
                    visible: dialog.form.busy || dialog.phase === Account.NoQr
                             || dialog.phase === Account.WaitingForConfirmation
                             || dialog.phase === Account.Confirming
                    color: Theme.color.primary
                }
                Button {
                    anchors.centerIn: parent
                    visible: !dialog.form.busy && (dialog.phase === Account.QrExpired
                                                   || dialog.phase === Account.QrFailed)
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
                    if (dialog.form.busy)
                        return qsTr("正在登录……");
                    switch (dialog.phase) {
                    case Account.WaitingForScan:
                        return qsTr("用手机上的音乐 App 扫码");
                    case Account.WaitingForConfirmation:
                        return qsTr("已扫码，请在手机上确认");
                    case Account.Confirming:
                        return qsTr("正在登录……");
                    case Account.QrExpired:
                        return qsTr("二维码已过期");
                    case Account.QrFailed:
                        return qsTr("登录失败，请刷新二维码");
                    default:
                        return App.session.signedIn ? qsTr("已登录") : qsTr("正在获取二维码……");
                    }
                }
                color: dialog.phase === Account.QrFailed && !dialog.form.busy
                       ? Theme.color.error : Theme.color.onSurface
                font: Theme.type.bodyLarge.font
            }
        }

        // "或" between the two ways.
        Column {
            width: 56
            height: formSide.height
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 1
                height: (parent.height - orText.height - 16) / 2
                color: Theme.color.outlineVariant
            }
            Text {
                id: orText
                anchors.horizontalCenter: parent.horizontalCenter
                topPadding: 8
                bottomPadding: 8
                text: qsTr("或")
                color: Theme.color.onSurfaceVariant
                font: Theme.type.labelLarge.font
            }
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 1
                height: (parent.height - orText.height - 16) / 2
                color: Theme.color.outlineVariant
            }
        }

        // Phone or email.
        Column {
            id: formSide
            width: parent.width - qrSide.width - 56
            spacing: 16

            TabBar {
                id: tabs
                width: parent.width
                onCurrentIndexChanged: App.session.clearFormError()
                TabButton {
                    text: qsTr("手机号")
                }
                TabButton {
                    text: qsTr("邮箱")
                }
            }

            ConnectedButtonGroup {
                visible: !dialog.byEmail
                exclusive: true
                Button {
                    size: Md.XSmall
                    variant: Md.Tonal
                    checkable: true
                    checked: dialog.byCode
                    text: qsTr("验证码")
                    onClicked: {
                        dialog.byCode = true;
                        App.session.clearFormError();
                    }
                }
                Button {
                    size: Md.XSmall
                    variant: Md.Tonal
                    checkable: true
                    checked: !dialog.byCode
                    text: qsTr("密码")
                    onClicked: {
                        dialog.byCode = false;
                        App.session.clearFormError();
                    }
                }
            }

            Row {
                visible: !dialog.byEmail
                width: parent.width
                spacing: 8
                Button {
                    id: regionButton
                    size: Md.Medium
                    variant: Md.Outlined
                    shape: Md.Square
                    text: "+" + dialog.countryCode
                    trailingGlyph: Symbols.arrow_drop_down
                    Accessible.name: qsTr("国家或地区区号 +%1").arg(dialog.countryCode)
                    onClicked: regionMenu.popup(regionButton, 0, regionButton.height + 4)
                }
                TextField {
                    id: phoneField
                    width: parent.width - regionButton.width - 8
                    variant: Md.Outlined
                    gapColor: Theme.color.surfaceContainerHigh
                    label: qsTr("手机号")
                    inputMethodHints: Qt.ImhDialableCharactersOnly
                    validator: RegularExpressionValidator {
                        regularExpression: /[0-9]{0,15}/
                    }
                    onTextEdited: App.session.clearFormError()
                    onAccepted: dialog.submit()
                }
            }

            Row {
                visible: !dialog.byEmail && dialog.byCode
                width: parent.width
                spacing: 8
                TextField {
                    id: codeField
                    width: parent.width - sendButton.width - 8
                    variant: Md.Outlined
                    gapColor: Theme.color.surfaceContainerHigh
                    label: qsTr("验证码")
                    inputMethodHints: Qt.ImhDigitsOnly
                    validator: RegularExpressionValidator {
                        regularExpression: /[0-9]{0,8}/
                    }
                    onTextEdited: App.session.clearFormError()
                    onAccepted: dialog.submit()
                }
                Button {
                    id: sendButton
                    size: Md.Medium
                    variant: Md.Tonal
                    enabled: dialog.form.smsState !== Account.SmsSending
                             && dialog.resendSeconds === 0 && phoneField.length > 0
                    text: dialog.form.smsState === Account.SmsSending ? qsTr("正在发送")
                          : dialog.resendSeconds > 0 ? qsTr("%1 秒").arg(dialog.resendSeconds)
                          : dialog.form.smsState === Account.SmsSent ? qsTr("重新发送")
                          : qsTr("获取验证码")
                    onClicked: {
                        App.session.sendSmsCode(dialog.countryCode, phoneField.text);
                        codeField.forceActiveFocus();
                    }
                }
            }

            TextField {
                id: passwordField
                visible: !dialog.byEmail && !dialog.byCode
                width: parent.width
                variant: Md.Outlined
                gapColor: Theme.color.surfaceContainerHigh
                label: qsTr("密码")
                echoMode: TextInput.Password
                onTextEdited: App.session.clearFormError()
                onAccepted: dialog.submit()
            }

            TextField {
                id: emailField
                visible: dialog.byEmail
                width: parent.width
                variant: Md.Outlined
                gapColor: Theme.color.surfaceContainerHigh
                label: qsTr("邮箱")
                inputMethodHints: Qt.ImhEmailCharactersOnly | Qt.ImhNoAutoUppercase
                onTextEdited: App.session.clearFormError()
                onAccepted: dialog.submit()
            }
            TextField {
                id: emailPasswordField
                visible: dialog.byEmail
                width: parent.width
                variant: Md.Outlined
                gapColor: Theme.color.surfaceContainerHigh
                label: qsTr("密码")
                echoMode: TextInput.Password
                onTextEdited: App.session.clearFormError()
                onAccepted: dialog.submit()
            }

            Text {
                width: parent.width
                visible: text !== ""
                wrapMode: Text.Wrap
                text: {
                    if (dialog.form.error !== "")
                        return dialog.form.error;
                    if (dialog.form.smsError !== "")
                        return dialog.form.smsError;
                    if (!dialog.byEmail && dialog.byCode
                            && dialog.form.smsState === Account.SmsSent)
                        return qsTr("验证码已发送");
                    return "";
                }
                color: dialog.form.error !== "" || dialog.form.smsError !== ""
                       ? Theme.color.error : Theme.color.onSurfaceVariant
                font: Theme.type.bodyMedium.font
            }
        }
    }

    Item {
        width: parent.width
        height: 8
    }

    CheckBox {
        text: qsTr("记住登录状态")
        checked: App.session.qrLogin.remember
        onToggled: App.session.qrLogin.remember = checked
    }

    buttons: [
        Button {
            variant: Md.Text
            text: qsTr("取消")
            onClicked: dialog.reject()
        },
        Button {
            variant: Md.Filled
            enabled: dialog.canSubmit
            text: dialog.form.busy ? qsTr("正在登录") : qsTr("登录")
            onClicked: dialog.submit()
        }
    ]

    Menu {
        id: regionMenu
        Instantiator {
            model: regionMenu.visible ? dialog.regions : []
            delegate: MenuItem {
                required property var modelData
                checkable: true
                checked: modelData.code === dialog.countryCode
                text: qsTr("%1 +%2").arg(modelData.name).arg(modelData.code)
                onTriggered: dialog.countryCode = modelData.code
            }
            onObjectAdded: (index, object) => regionMenu.insertItem(index, object as MenuItem)
            onObjectRemoved: (index, object) => regionMenu.removeItem(object as MenuItem)
        }
    }
}
