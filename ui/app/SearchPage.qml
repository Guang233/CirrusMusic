pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components
import Cirrus.Presentation

// Search results: trending keywords before the first search, then results in four
// tabs, each with its own state and paging. The input and its suggestions are the
// title bar's search field (AppShell.searchField); `search` is the shell's model.
PageScaffold {
    id: page

    required property AppShell shell
    required property SearchModel search

    function run(text: string): void {
        shell.searchField.text = text;
        shell.search(text);
    }

    title: qsTr("搜索")
    canGoBack: shell.canGoBack
    onBackRequested: shell.back()

    // Before the first search.
    Column {
        anchors.top: parent.top
        anchors.topMargin: 8
        x: 24
        width: parent.width - 48
        spacing: 12
        visible: page.search.query === ""

        Text {
            visible: page.search.hotKeywords.length > 0
            text: qsTr("热门搜索")
            color: Theme.color.onSurface
            font: Theme.type.titleMedium.font
        }
        Flow {
            width: parent.width
            spacing: 8
            Repeater {
                model: page.search.hotKeywords
                delegate: Chip {
                    required property string modelData
                    required property int index
                    glyph: index < 3 ? Symbols.trending_up : ""
                    text: modelData
                    onClicked: page.run(modelData)
                }
            }
        }
    }

    // Results.
    Item {
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: parent.width
        visible: page.search.query !== ""

        TabBar {
            id: tabs
            currentIndex: page.search.tab
            onCurrentIndexChanged: page.search.tab = currentIndex
            x: 24
            width: Math.min(parent.width - 48, 720)
            TabButton {
                text: qsTr("歌曲")
            }
            TabButton {
                text: qsTr("专辑")
            }
            TabButton {
                text: qsTr("歌手")
            }
            TabButton {
                text: qsTr("歌单")
            }
        }

        Item {
            anchors.top: tabs.bottom
            anchors.topMargin: 8
            anchors.bottom: parent.bottom
            width: parent.width
            visible: tabs.currentIndex === SearchModel.Songs

            PageState {
                id: songState
                anchors.fill: parent
                model: page.search.songs
                emptyGlyph: Symbols.search
                emptyText: qsTr("没有找到“%1”相关的歌曲").arg(page.search.query)
                skeleton: ListSkeleton {
                    header: false
                }
            }
            SongList {
                id: songList
                anchors.fill: parent
                topMargin: 8
                opacity: songState.contentOpacity
                visible: opacity > 0
                model: page.search.songs.songs
                pager: page.search.songs
                onAlbumRequested: key => page.shell.openAlbum(key)
                onArtistRequested: key => page.shell.openArtist(key)
                footer: LoadMoreFooter {
                    width: songList.width
                    model: page.search.songs
                }
            }
        }

        component CollectionTab: Item {
            id: collectionTab
            required property CollectionResults results
            required property int tab
            required property string noun

            anchors.top: tabs.bottom
            anchors.topMargin: 8
            anchors.bottom: parent.bottom
            width: parent.width
            visible: tabs.currentIndex === tab

            PageState {
                id: collectionState
                anchors.fill: parent
                model: collectionTab.results
                emptyGlyph: Symbols.search
                emptyText: qsTr("没有找到“%1”相关的%2").arg(page.search.query).arg(collectionTab.noun)
                skeleton: ListSkeleton {
                    header: false
                }
            }
            CollectionView {
                anchors.fill: parent
                topMargin: 8
                opacity: collectionState.contentOpacity
                visible: opacity > 0
                model: collectionTab.results.items
                pager: collectionTab.results
                onActivated: (kind, key) => page.shell.openCollection(kind, key)
            }
        }

        CollectionTab {
            results: page.search.albums
            tab: SearchModel.Albums
            noun: qsTr("专辑")
        }
        CollectionTab {
            results: page.search.artists
            tab: SearchModel.Artists
            noun: qsTr("歌手")
        }
        CollectionTab {
            results: page.search.playlists
            tab: SearchModel.Playlists
            noun: qsTr("歌单")
        }
    }
}
