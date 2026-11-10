import QtQuick
import QtQuick.Templates as T
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components
import Cirrus.Presentation

// Footer of the navigation rail: the account (avatar and name; sign in for guests)
// and settings. Collapsed they stack in the 96 px column, expanded they share a row.
// A click on the account opens its user page; the context menu (right click, the Menu
// key) has it and signing out.
Item {
    id: root

    // The rail's expansion, 0–1.
    required property real progress

    signal signInRequested
    signal settingsRequested
    signal profileRequested

    implicitHeight: Theme.motion.lerp(104, 56, progress)

    T.AbstractButton {
        id: account
        x: 20
        y: Theme.motion.lerp(56, 0, root.progress)
        width: Theme.motion.lerp(56, root.width - 40 - 48, root.progress)
        height: 48
        focusPolicy: Qt.TabFocus
        Accessible.role: Accessible.Button
        Accessible.name: App.session.signedIn ? App.session.nickname : qsTr("登录")
        onClicked: {
            if (App.session.signedIn)
                root.profileRequested();
            else
                root.signInRequested();
        }
        Keys.onMenuPressed: openMenu()

        function openMenu(): void {
            if (App.session.signedIn)
                accountMenu.popup(account, 0, -accountMenu.implicitHeight - 4);
        }

        TapHandler {
            acceptedButtons: Qt.RightButton
            onTapped: account.openMenu()
        }

        HoverHandler {
            enabled: account.hoverEnabled
            cursorShape: Qt.PointingHandCursor
        }
        background: StateLayer {
            radius: height / 2
            color: Theme.color.onSurfaceVariant
            hovered: account.hovered
            pressed: account.down
            focused: account.visualFocus
        }

        contentItem: Item {
            CoverArt {
                id: avatar
                x: Theme.motion.lerp(12, 8, root.progress)
                width: 32
                height: 32
                anchors.verticalCenter: parent.verticalCenter
                visible: App.session.signedIn
                coverKey: App.session.avatarKey
                radius: width / 2
                glyph: Symbols.person
            }
            Icon {
                x: Theme.motion.lerp(12, 8, root.progress)
                anchors.verticalCenter: parent.verticalCenter
                visible: !App.session.signedIn
                glyph: Symbols.account_circle
                size: 32
                color: Theme.color.onSurfaceVariant
            }
            Text {
                x: 52
                width: parent.width - x - 12
                anchors.verticalCenter: parent.verticalCenter
                opacity: Theme.motion.segment(root.progress, 0.6, 1)
                visible: opacity > 0
                text: App.session.signedIn ? App.session.nickname : qsTr("登录")
                elide: Text.ElideRight
                color: Theme.color.onSurface
                font: Theme.type.labelLarge.font
            }
        }

        Menu {
            id: accountMenu
            MenuItem {
                glyph: Symbols.person
                text: qsTr("我的主页")
                onTriggered: root.profileRequested()
            }
            MenuItem {
                glyph: Symbols.logout
                text: qsTr("退出登录")
                onTriggered: App.session.signOut()
            }
        }
    }

    IconButton {
        x: Theme.motion.lerp(28, root.width - 20 - width, root.progress)
        y: 4
        glyph: Symbols.settings
        text: qsTr("设置")
        onClicked: root.settingsRequested()
    }
}
