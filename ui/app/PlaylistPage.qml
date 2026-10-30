pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components
import Cirrus.Presentation

// Playlist details. Prototype of the page pattern: the page model loads on
// `playlistKey`, the page shows each load state. The shared page state components
// and song list arrive in UI phase 3, step 4.
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

    ListView {
        id: list
        anchors.fill: parent
        visible: pageModel.state === LoadState.Ready || pageModel.state === LoadState.Partial
                 || pageModel.state === LoadState.Empty
        clip: true
        bottomMargin: 24
        boundsBehavior: Flickable.StopAtBounds
        model: pageModel.songs
        ScrollBar.vertical: ScrollBar {}

        header: Row {
            x: 24
            width: list.width - 48
            height: 184
            spacing: 24

            Rectangle {
                width: 160
                height: 160
                radius: Theme.shape.large
                color: Theme.color.surfaceContainerHighest
                clip: true
                Icon {
                    anchors.centerIn: parent
                    visible: cover.status !== Image.Ready
                    glyph: Symbols.queue_music
                    size: 48
                    color: Theme.color.onSurfaceVariant
                }
                Image {
                    id: cover
                    anchors.fill: parent
                    source: pageModel.coverKey !== "" ? "image://cover/" + pageModel.coverKey : ""
                    sourceSize: Qt.size(320, 320)
                    fillMode: Image.PreserveAspectCrop
                }
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
                Text {
                    visible: pageModel.state === LoadState.Partial
                    text: qsTr("部分歌曲信息暂时无法获取")
                    color: Theme.color.tertiary
                    font: Theme.type.bodySmall.font
                }
                Text {
                    visible: pageModel.state === LoadState.Empty
                    text: qsTr("这个歌单还没有歌曲")
                    color: Theme.color.onSurfaceVariant
                    font: Theme.type.bodyMedium.font
                }
            }
        }

        delegate: ListItem {
            id: song
            required property string title
            required property string artistText
            required property string albumTitle
            required property string durationText
            required property string coverKey
            x: 8
            width: list.width - 16
            segmented: true
            text: title
            supportingText: artistText + " · " + albumTitle
            trailingText: durationText
            leading: Component {
                Image {
                    width: 48
                    height: 48
                    source: song.coverKey !== "" ? "image://cover/" + song.coverKey : ""
                    sourceSize: Qt.size(96, 96)
                    fillMode: Image.PreserveAspectCrop
                }
            }
        }
    }

    LoadingIndicator {
        anchors.centerIn: parent
        visible: pageModel.state === LoadState.Loading
    }

    Column {
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 360)
        spacing: 16
        visible: pageModel.state === LoadState.Error || pageModel.state === LoadState.Offline
                 || pageModel.state === LoadState.SignedOut

        Icon {
            anchors.horizontalCenter: parent.horizontalCenter
            glyph: pageModel.state === LoadState.Offline ? Symbols.wifi_off : Symbols.error
            size: 48
            color: Theme.color.onSurfaceVariant
        }
        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            text: pageModel.error.message
            color: Theme.color.onSurface
            font: Theme.type.bodyLarge.font
        }
        Button {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: pageModel.error.retryable && pageModel.state !== LoadState.Offline
            variant: Md.Tonal
            glyph: Symbols.refresh
            text: qsTr("重试")
            onClicked: pageModel.retry()
        }
    }
}
