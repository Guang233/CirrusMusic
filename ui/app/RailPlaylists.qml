pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components
import Cirrus.Presentation

// The expanded rail's own content (docs/ui/pages/shell.md §2): my music, then the
// playlists the account created and saved, each group collapsible. Guests get a
// sign-in prompt instead of the playlists.
Flickable {
    id: root

    required property LibraryPageModel library
    // The playlist the shell shows, highlighted in the list.
    property string selectedKey

    signal playlistRequested(string key)
    // The library page on a tab (0 liked songs … 4 recently played).
    signal libraryRequested(int tab)
    signal signInRequested

    contentHeight: column.height + 8
    // Overshoots and springs back at the ends; WheelScroller eases mouse wheels.
    boundsBehavior: Flickable.DragAndOvershootBounds
    WheelScroller {
        flickable: root
    }
    ScrollBar.vertical: ScrollBar {}

    component Divider: Item {
        width: column.width
        height: 17
        Rectangle {
            x: 36
            y: 8
            width: parent.width - 72
            height: 1
            color: Theme.color.outlineVariant
        }
    }

    component Label: Text {
        x: 36
        width: column.width - 72
        topPadding: 8
        bottomPadding: 8
        elide: Text.ElideRight
        color: Theme.color.onSurfaceVariant
        font: Theme.type.titleSmall.font
    }

    // Collapsible header and the playlists of one group.
    component PlaylistGroup: Column {
        id: group

        required property string title
        required property CollectionListModel playlists
        property bool open: true

        width: column.width
        visible: playlists.count > 0

        T.AbstractButton {
            id: header
            width: parent.width
            height: 40
            focusPolicy: Qt.TabFocus
            Accessible.name: group.title
            onClicked: group.open = !group.open

            background: Item {
                StateLayer {
                    x: 20
                    width: parent.width - 40
                    height: parent.height
                    radius: height / 2
                    color: Theme.color.onSurfaceVariant
                    hovered: header.hovered
                    pressed: header.down
                    focused: header.visualFocus
                }
            }
            contentItem: Item {
                Text {
                    x: 36
                    width: parent.width - x - 72
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("%1  %2").arg(group.title).arg(group.playlists.count)
                    elide: Text.ElideRight
                    color: Theme.color.onSurfaceVariant
                    font: Theme.type.titleSmall.font
                }
                // Turns over as the group opens.
                Icon {
                    x: parent.width - 36 - width
                    anchors.verticalCenter: parent.verticalCenter
                    glyph: Symbols.expand_more
                    rotation: 180 * openness.value
                    size: 20
                    color: Theme.color.onSurfaceVariant
                }
            }
        }

        // The rows unfold and fold with a spring; folded rows are hidden, so Tab skips them.
        Spring {
            id: openness
            target: group.open ? 1 : 0
            spec: Theme.motion.standardDefaultSpatial
            threshold: 0.002
        }
        Item {
            width: group.width
            height: Math.round(rows.implicitHeight * Math.max(0, openness.value))
            visible: openness.value > 0.002
            clip: true

            Column {
                id: rows
                width: parent.width
                opacity: Theme.motion.segment(openness.value, 0, 0.6)
                Repeater {
                    model: group.playlists
                    delegate: SidebarRow {
                        required property string key
                        required property string title
                        required coverKey
                        width: group.width
                        cover: true
                        text: title
                        selected: root.selectedKey === key
                        onClicked: root.playlistRequested(key)
                    }
                }
            }
        }
    }

    Column {
        id: column
        width: root.width

        Divider {}
        Label {
            text: qsTr("我的")
        }
        SidebarRow {
            width: column.width
            glyph: Symbols.favorite
            text: qsTr("我喜欢的音乐")
            onClicked: root.libraryRequested(0)
        }
        SidebarRow {
            width: column.width
            glyph: Symbols.history
            text: qsTr("最近播放")
            onClicked: root.libraryRequested(4)
        }
        Divider {}

        PlaylistGroup {
            visible: App.session.signedIn && playlists.count > 0
            title: qsTr("创建的歌单")
            playlists: root.library.createdPlaylists
        }
        PlaylistGroup {
            visible: App.session.signedIn && playlists.count > 0
            title: qsTr("收藏的歌单")
            playlists: root.library.savedPlaylists
        }

        // Guests.
        Column {
            x: 36
            width: column.width - 72
            visible: !App.session.signedIn
            topPadding: 8
            spacing: 12
            Text {
                width: parent.width
                text: qsTr("登录后，你创建和收藏的歌单会显示在这里。")
                wrapMode: Text.Wrap
                color: Theme.color.onSurfaceVariant
                font: Theme.type.bodyMedium.font
            }
            Button {
                variant: Md.Tonal
                size: Md.Small
                text: qsTr("扫码登录")
                onClicked: root.signInRequested()
            }
        }
    }
}
