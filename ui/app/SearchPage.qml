pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components
import Cirrus.Presentation

// Search: the bar with suggestions under it while typing, trending keywords before
// the first search, then results in four tabs, each with its own state and paging.
PageScaffold {
    id: page

    required property AppShell shell

    function run(text: string): void {
        bar.text = text;
        search.submit(text);
        bar.focus = false;
    }

    title: qsTr("搜索")
    canGoBack: shell.canGoBack
    onBackRequested: shell.back()

    SearchModel {
        id: search
        text: bar.text
        tab: tabs.currentIndex
    }

    SearchBar {
        id: bar
        x: 24
        width: Math.min(parent.width - 48, 720)
        focus: true
        placeholderText: qsTr("搜索歌曲、歌手、专辑、歌单")
        onAccepted: page.run(text)
        Keys.onDownPressed: suggestionList.forceActiveFocus()
    }

    // Before the first search.
    Column {
        anchors.top: bar.bottom
        anchors.topMargin: 24
        x: 24
        width: parent.width - 48
        spacing: 12
        visible: search.query === ""

        Text {
            visible: search.hotKeywords.length > 0
            text: qsTr("热门搜索")
            color: Theme.color.onSurface
            font: Theme.type.titleMedium.font
        }
        Flow {
            width: parent.width
            spacing: 8
            Repeater {
                model: search.hotKeywords
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
        anchors.top: bar.bottom
        anchors.topMargin: 8
        anchors.bottom: parent.bottom
        width: parent.width
        visible: search.query !== ""

        TabBar {
            id: tabs
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
                model: search.songs
                emptyGlyph: Symbols.search
                emptyText: qsTr("没有找到“%1”相关的歌曲").arg(search.query)
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
                model: search.songs.songs
                pager: search.songs
                onAlbumRequested: key => page.shell.openAlbum(key)
                onArtistRequested: key => page.shell.openArtist(key)
                footer: LoadMoreFooter {
                    width: songList.width
                    model: search.songs
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
                emptyText: qsTr("没有找到“%1”相关的%2").arg(search.query).arg(collectionTab.noun)
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
            results: search.albums
            tab: SearchModel.Albums
            noun: qsTr("专辑")
        }
        CollectionTab {
            results: search.artists
            tab: SearchModel.Artists
            noun: qsTr("歌手")
        }
        CollectionTab {
            results: search.playlists
            tab: SearchModel.Playlists
            noun: qsTr("歌单")
        }
    }

    // Suggestions while typing, over the content below the bar.
    Rectangle {
        id: suggestions
        anchors.top: bar.bottom
        anchors.topMargin: 4
        x: bar.x
        z: 2
        width: bar.width
        height: suggestionList.contentHeight + 16
        visible: (bar.activeFocus || suggestionList.activeFocus) && search.suggestions.length > 0
                 && bar.text.trim() !== search.query
        radius: Theme.shape.large
        color: Theme.color.surfaceContainerHigh

        Elevation {
            anchors.fill: parent
            z: -1
            level: Theme.elevation.level2
            radius: suggestions.radius
        }

        ListView {
            id: suggestionList
            y: 8
            width: parent.width
            height: contentHeight
            interactive: false
            keyNavigationEnabled: true
            model: search.suggestions
            Keys.onUpPressed: event => {
                if (currentIndex <= 0)
                    bar.forceActiveFocus();
                else
                    event.accepted = false;
            }
            delegate: ListItem {
                required property string modelData
                width: suggestionList.width
                leadingGlyph: Symbols.search
                text: modelData
                highlighted: suggestionList.activeFocus && ListView.isCurrentItem
                onClicked: page.run(modelData)
                Keys.onReturnPressed: page.run(modelData)
            }
        }
    }
}
