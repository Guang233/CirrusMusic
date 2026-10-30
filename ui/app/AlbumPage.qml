pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components
import Cirrus.Presentation

// Album details: header (cover, title, artists, year and length, play) over the
// numbered tracks. Partial when the source says the track list is incomplete.
PageScaffold {
    id: page

    required property AppShell shell
    property alias albumKey: pageModel.albumKey

    title: pageModel.title
    canGoBack: shell.canGoBack
    onBackRequested: shell.back()

    AlbumPageModel {
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
        showCovers: false
        showAlbum: false
        showAlbumAction: false
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
                    glyph: Symbols.album
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
                    // Each artist opens their page.
                    Flow {
                        width: parent.width
                        spacing: 4
                        Repeater {
                            model: pageModel.artists
                            delegate: Button {
                                required property var modelData
                                size: Md.XSmall
                                variant: Md.Text
                                enabled: modelData.key !== ""
                                text: modelData.name
                                onClicked: page.shell.openArtist(modelData.key)
                            }
                        }
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
                text: qsTr("曲目列表可能不完整")
            }
            StateMessage {
                width: parent.width
                topPadding: 48
                visible: pageModel.state === LoadState.Empty
                glyph: Symbols.album
                message: qsTr("这张专辑没有可显示的曲目")
            }
        }
    }
}
