pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components
import Cirrus.Presentation

// Playlist details: header (cover, title, description, creator, play all) over the
// songs. An empty playlist keeps its header and says so below it.
PageScaffold {
    id: page

    required property AppShell shell
    property alias playlistKey: pageModel.playlistKey

    title: pageModel.title
    canGoBack: shell.canGoBack
    onBackRequested: shell.back()

    PlaylistPageModel {
        id: pageModel
    }

    PageState {
        id: pageState
        anchors.fill: parent
        model: pageModel
        emptyInContent: true
    }

    SongList {
        id: list
        anchors.fill: parent
        opacity: pageState.contentOpacity
        visible: opacity > 0
        model: pageModel.songs
        onAlbumRequested: key => page.shell.openAlbum(key)
        onArtistRequested: key => page.shell.openArtist(key)

        header: Column {
            x: list.sideMargin
            width: list.width - 2 * list.sideMargin
            bottomPadding: 16
            spacing: 16

            Row {
                width: parent.width
                spacing: 24

                CoverArt {
                    width: 160
                    height: 160
                    radius: Theme.shape.large
                    glyph: Symbols.queue_music
                    coverKey: pageModel.coverKey
                }
                Column {
                    width: parent.width - 184
                    spacing: 8
                    Text {
                        width: parent.width
                        text: pageModel.title
                        elide: Text.ElideRight
                        color: Theme.color.onSurface
                        font: Theme.type.headlineMedium.font
                    }
                    Text {
                        width: parent.width
                        visible: text !== ""
                        text: pageModel.description
                        wrapMode: Text.Wrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                        color: Theme.color.onSurfaceVariant
                        font: Theme.type.bodyMedium.font
                    }
                    Text {
                        text: qsTr("%1 · %2 首").arg(pageModel.creator).arg(App.format.count(pageModel.songs.count))
                        color: Theme.color.onSurfaceVariant
                        font: Theme.type.bodySmall.font
                    }
                    Item {
                        width: 1
                        height: 8
                    }
                    Button {
                        visible: pageModel.songs.count > 0
                        variant: Md.Filled
                        glyph: Symbols.play_arrow
                        text: qsTr("播放全部")
                        onClicked: App.player.playSongs(pageModel.songs.keys(), 0)
                    }
                }
            }
            NoticeBar {
                width: parent.width
                visible: pageModel.state === LoadState.Partial
                text: qsTr("部分歌曲信息暂时无法获取")
            }
            StateMessage {
                width: parent.width
                topPadding: 48
                visible: pageModel.state === LoadState.Empty
                glyph: Symbols.queue_music
                message: qsTr("这个歌单还没有歌曲")
            }
        }
    }
}
