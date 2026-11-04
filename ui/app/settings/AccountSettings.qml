pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components
import Cirrus.Presentation
import CirrusMusic

// The account: who is signed in, and signing in or out.
SettingsGroup {
    id: group

    required property AppShell shell

    title: qsTr("账号")
    glyph: Symbols.account_circle
    hue: "cyan"

    SettingRow {
        text: App.session.signedIn ? App.session.nickname : qsTr("未登录")
        supportingText: App.session.signedIn ? qsTr("网易云音乐账号") : ""
        focusPolicy: Qt.NoFocus
        hoverEnabled: false
        leading: Component {
            CoverArt {
                width: 40
                height: 40
                radius: width / 2
                glyph: Symbols.person
                coverKey: App.session.avatarKey
            }
        }
        trailing: Component {
            Button {
                variant: App.session.signedIn ? Md.Outlined : Md.Filled
                glyph: App.session.signedIn ? Symbols.logout : Symbols.login
                text: App.session.signedIn ? qsTr("退出登录") : qsTr("登录")
                onClicked: App.session.signedIn ? App.session.signOut() : group.shell.openLogin()
            }
        }
    }
}
