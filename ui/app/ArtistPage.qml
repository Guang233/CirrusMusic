pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components
import Cirrus.Presentation

// Artist page: round avatar and summary, popular songs, then the albums as cards.
PageScaffold {
    id: page

    required property AppShell shell
    property alias artistKey: pageModel.artistKey

    title: pageModel.name

    ArtistPageModel {
        id: pageModel
    }

    PageState {
        id: pageState
        anchors.fill: parent
        model: pageModel
        emptyInContent: true
        skeleton: ListSkeleton {
            roundCover: true
            rows: 5
        }
    }

    component SectionHeading: Text {
        topPadding: 8
        color: Theme.color.onSurface
        font: Theme.type.titleLarge.font
    }

    SongList {
        id: list
        anchors.fill: parent
        opacity: pageState.contentOpacity
        visible: opacity > 0
        model: pageModel.topSongs
        onAlbumRequested: key => page.shell.openAlbum(key)
        onArtistRequested: key => page.shell.openArtist(key)

        header: Column {
            x: list.sideMargin
            width: list.width - 2 * list.sideMargin
            bottomPadding: 12
            spacing: 16

            Row {
                width: parent.width
                spacing: 24

                CoverArt {
                    width: 160
                    height: 160
                    radius: width / 2
                    glyph: Symbols.person
                    coverKey: pageModel.avatarKey
                }
                Column {
                    width: parent.width - 184
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8
                    Text {
                        width: parent.width
                        text: pageModel.name
                        elide: Text.ElideRight
                        color: Theme.color.onSurface
                        font: Theme.type.headlineMedium.font
                    }
                    Text {
                        width: parent.width
                        visible: text !== ""
                        text: pageModel.description
                        wrapMode: Text.Wrap
                        maximumLineCount: 3
                        elide: Text.ElideRight
                        color: Theme.color.onSurfaceVariant
                        font: Theme.type.bodyMedium.font
                    }
                    Text {
                        text: pageModel.summary
                        color: Theme.color.onSurfaceVariant
                        font: Theme.type.bodySmall.font
                    }
                    Item {
                        width: 1
                        height: 8
                    }
                    Button {
                        visible: pageModel.topSongs.count > 0
                        variant: Md.Filled
                        glyph: Symbols.play_arrow
                        text: qsTr("播放热门歌曲")
                        onClicked: App.player.playSongs(pageModel.topSongs.keys(), 0)
                    }
                }
            }
            NoticeBar {
                width: parent.width
                visible: pageModel.state === LoadState.Partial
                text: qsTr("部分信息暂时无法获取")
            }
            StateMessage {
                width: parent.width
                topPadding: 48
                visible: pageModel.state === LoadState.Empty
                glyph: Symbols.person
                message: qsTr("还没有这位歌手的歌曲和专辑")
            }
            SectionHeading {
                visible: pageModel.topSongs.count > 0
                text: qsTr("热门歌曲")
            }
        }

        footer: Column {
            x: list.sideMargin
            width: list.width - 2 * list.sideMargin
            topPadding: 24
            spacing: 8
            visible: pageModel.albums.count > 0

            SectionHeading {
                text: qsTr("专辑")
            }
            CollectionGrid {
                // Cards carry their own padding; pull them out to line up the covers.
                x: -8
                width: parent.width + 16
                model: pageModel.albums
                onActivated: (kind, key) => page.shell.openCollection(kind, key)
            }
        }
    }
}
