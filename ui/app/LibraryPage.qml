pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components
import Cirrus.Presentation

// The account's library in tabs: liked songs, playlists (created and saved), albums,
// artists, recently played. Guests get the sign-in prompt.
PageScaffold {
    id: page

    required property AppShell shell
    // The tab to open on (0 liked songs … 4 recently played).
    property int initialTab: 0

    title: qsTr("音乐库")
    canGoBack: shell.canGoBack
    onBackRequested: shell.back()

    LibraryPageModel {
        id: pageModel
    }

    PageState {
        id: pageState
        anchors.fill: parent
        model: pageModel
        emptyGlyph: Symbols.library_music
        emptyText: qsTr("音乐库还是空的。喜欢的歌曲、收藏的歌单都会出现在这里")
        skeleton: ListSkeleton {
            header: false
        }
        onSignInRequested: page.shell.openLogin()
    }

    component Tab: Item {
        required property int index
        anchors.top: tabs.bottom
        anchors.topMargin: 8
        anchors.bottom: parent.bottom
        width: parent.width
        visible: tabs.currentIndex === index
    }
    component Heading: Text {
        color: Theme.color.onSurface
        font: Theme.type.titleMedium.font
    }

    Item {
        anchors.fill: parent
        opacity: pageState.contentOpacity
        visible: opacity > 0

        TabBar {
            id: tabs
            Component.onCompleted: currentIndex = page.initialTab
            x: 24
            width: Math.min(parent.width - 48, 720)
            TabButton {
                text: qsTr("喜欢")
            }
            TabButton {
                text: qsTr("歌单")
            }
            TabButton {
                text: qsTr("专辑")
            }
            TabButton {
                text: qsTr("歌手")
            }
            TabButton {
                text: qsTr("最近播放")
            }
        }

        Tab {
            index: 0
            SongList {
                anchors.fill: parent
                topMargin: 8
                model: pageModel.likedSongs
                onAlbumRequested: key => page.shell.openAlbum(key)
                onArtistRequested: key => page.shell.openArtist(key)
            }
        }
        Tab {
            index: 1
            Flickable {
                id: playlists
                anchors.fill: parent
                contentHeight: playlistColumn.implicitHeight + 24
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}

                Column {
                    id: playlistColumn
                    x: 24
                    y: 8
                    width: playlists.width - 48
                    spacing: 12
                    Heading {
                        visible: pageModel.createdPlaylists.count > 0
                        text: qsTr("创建的歌单")
                    }
                    CollectionGrid {
                        x: -8
                        width: parent.width + 16
                        model: pageModel.createdPlaylists
                        onActivated: (kind, key) => page.shell.openCollection(kind, key)
                    }
                    Heading {
                        visible: pageModel.savedPlaylists.count > 0
                        text: qsTr("收藏的歌单")
                    }
                    CollectionGrid {
                        x: -8
                        width: parent.width + 16
                        model: pageModel.savedPlaylists
                        onActivated: (kind, key) => page.shell.openCollection(kind, key)
                    }
                }
            }
        }
        Tab {
            index: 2
            CollectionView {
                anchors.fill: parent
                topMargin: 8
                model: pageModel.albums
                onActivated: (kind, key) => page.shell.openCollection(kind, key)
            }
        }
        Tab {
            index: 3
            CollectionView {
                anchors.fill: parent
                topMargin: 8
                model: pageModel.artists
                onActivated: (kind, key) => page.shell.openCollection(kind, key)
            }
        }
        Tab {
            index: 4
            SongList {
                anchors.fill: parent
                topMargin: 8
                model: pageModel.recentSongs
                onAlbumRequested: key => page.shell.openAlbum(key)
                onArtistRequested: key => page.shell.openArtist(key)
            }
        }
    }
}
