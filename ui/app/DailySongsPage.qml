pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components
import Cirrus.Presentation

// Today's recommended songs, for signed-in accounts.
PageScaffold {
    id: page

    required property AppShell shell

    title: qsTr("每日推荐")

    DailySongsPageModel {
        id: pageModel
    }

    PageState {
        id: pageState
        anchors.fill: parent
        model: pageModel
        emptyGlyph: Symbols.today
        emptyText: qsTr("今天的推荐还没有准备好")
        onSignInRequested: page.shell.openLogin()
    }

    SongList {
        id: list
        anchors.fill: parent
        opacity: pageState.contentOpacity
        visible: opacity > 0
        model: pageModel.songs
        onAlbumRequested: key => page.shell.openAlbum(key)
        onArtistRequested: key => page.shell.openArtist(key)

        header: Row {
            x: list.sideMargin
            width: list.width - 2 * list.sideMargin
            height: 152
            spacing: 24

            // Today's date as the cover.
            Rectangle {
                width: 128
                height: 128
                radius: Theme.shape.large
                color: Theme.color.primaryContainer
                Column {
                    anchors.centerIn: parent
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: Qt.formatDate(new Date(), "d")
                        color: Theme.color.onPrimaryContainer
                        font: Theme.type.displayMedium.font
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: Qt.locale().monthName(new Date().getMonth(), Locale.ShortFormat)
                        color: Theme.color.onPrimaryContainer
                        font: Theme.type.titleSmall.font
                    }
                }
            }
            Column {
                width: parent.width - 152
                spacing: 8
                topPadding: 8
                Text {
                    text: qsTr("每日推荐")
                    color: Theme.color.onSurface
                    font: Theme.type.headlineMedium.font
                }
                Text {
                    width: parent.width
                    text: qsTr("根据你的口味生成，每天更新")
                    wrapMode: Text.Wrap
                    color: Theme.color.onSurfaceVariant
                    font: Theme.type.bodyMedium.font
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
    }
}
