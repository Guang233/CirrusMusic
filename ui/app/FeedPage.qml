pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components
import Cirrus.Presentation

// Home and discover: sections of cards (one row each), a few songs, and on the home
// page the daily songs and personal FM shortcuts.
PageScaffold {
    id: page

    required property AppShell shell
    property alias feed: pageModel.feed


    HomePageModel {
        id: pageModel
    }

    PageState {
        id: pageState
        anchors.fill: parent
        model: pageModel
        skeleton: ListSkeleton {
            rows: 4
        }
    }

    component SectionTitle: Text {
        color: Theme.color.onSurface
        font: Theme.type.titleLarge.font
    }

    // A large tappable tile for the shortcuts section.
    component Shortcut: Rectangle {
        id: shortcut
        property string glyph
        property string title
        property string subtitle
        property color contentColor
        signal clicked

        height: 112
        radius: Theme.shape.extraLarge

        Icon {
            x: 24
            anchors.verticalCenter: parent.verticalCenter
            glyph: shortcut.glyph
            size: 40
            fill: 1
            color: shortcut.contentColor
        }
        Column {
            x: 88
            width: parent.width - 112
            anchors.verticalCenter: parent.verticalCenter
            spacing: 4
            Text {
                width: parent.width
                text: shortcut.title
                elide: Text.ElideRight
                color: shortcut.contentColor
                font: Theme.type.titleLarge.font
            }
            Text {
                width: parent.width
                text: shortcut.subtitle
                elide: Text.ElideRight
                color: shortcut.contentColor
                opacity: 0.8
                font: Theme.type.bodyMedium.font
            }
        }
        StateLayer {
            anchors.fill: parent
            radius: shortcut.radius
            color: shortcut.contentColor
            hovered: area.containsMouse
            pressed: area.pressed
            pressX: area.mouseX
            pressY: area.mouseY
        }
        MouseArea {
            id: area
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: shortcut.clicked()
        }
        Accessible.role: Accessible.Button
        Accessible.name: title
    }

    Flickable {
        id: flick
        anchors.fill: parent
        opacity: pageState.contentOpacity
        visible: opacity > 0
        contentHeight: column.implicitHeight + 24
        clip: true
        // Overshoots and springs back at the ends.
        boundsBehavior: Flickable.DragAndOvershootBounds
        ScrollBar.vertical: ScrollBar {}

        Column {
            id: column
            x: 24
            width: flick.width - 48
            spacing: 32

            Repeater {
                model: pageModel.sections
                delegate: Column {
                    id: section
                    required property int kind
                    required property string title
                    required property CollectionListModel collections
                    required property SongListModel songs

                    width: column.width
                    spacing: 12

                    SectionTitle {
                        visible: section.title !== ""
                        text: section.title
                    }

                    // Daily songs and personal FM.
                    Row {
                        visible: section.kind === Feed.Shortcuts
                        width: parent.width
                        spacing: 12
                        Shortcut {
                            width: (parent.width - parent.spacing) / 2
                            color: Theme.color.primaryContainer
                            contentColor: Theme.color.onPrimaryContainer
                            glyph: Symbols.today
                            title: qsTr("每日推荐")
                            subtitle: App.session.signedIn ? qsTr("根据你的口味，每天更新")
                                                           : qsTr("登录后查看")
                            onClicked: App.session.signedIn ? page.shell.openDaily("")
                                                            : page.shell.openLogin()
                        }
                        Shortcut {
                            width: (parent.width - parent.spacing) / 2
                            color: Theme.color.tertiaryContainer
                            contentColor: Theme.color.onTertiaryContainer
                            glyph: Symbols.radio
                            title: qsTr("私人 FM")
                            subtitle: App.session.signedIn ? qsTr("一直播放你可能喜欢的歌")
                                                           : qsTr("登录后收听")
                            onClicked: App.session.signedIn ? App.player.startFm()
                                                            : page.shell.openLogin()
                        }
                    }

                    CollectionGrid {
                        visible: section.kind === Feed.Collections
                        x: -8
                        width: parent.width + 16
                        maximumRows: 1
                        model: section.collections
                        onActivated: (kind, key) => page.shell.openCollection(kind, key)
                    }

                    SongList {
                        visible: section.kind === Feed.Songs
                        width: parent.width
                        height: contentHeight
                        interactive: false
                        sideMargin: 0
                        bottomMargin: 0
                        model: section.songs
                        onAlbumRequested: key => page.shell.openAlbum(key)
                        onArtistRequested: key => page.shell.openArtist(key)
                    }
                }
            }
        }
    }
}
