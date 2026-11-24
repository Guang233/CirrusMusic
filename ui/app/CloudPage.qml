pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Dialogs as D
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components
import Cirrus.Presentation

// The cloud drive (docs/ui/pages/cloud.md §3): the account's uploaded songs in the shared
// song page layout, how much space they use, uploading (the button, or files dropped on the
// page), and the cloud actions on the songs (delete, 纠错, 取消关联).
SongCollectionPage {
    id: page

    model: pageModel
    songs: pageModel.songs
    title: qsTr("云盘")
    cover: GlyphTile {
        glyph: Symbols.cloud
        tone: Md.Secondary
    }
    cloud: true
    reasonColumn: true
    reasonTitle: qsTr("添加时间")
    findPlaceholder: qsTr("在云盘中查找")
    noMatchText: qsTr("云盘里没有与「%1」相符的歌")
    emptyGlyph: Symbols.cloud
    emptyText: qsTr("云盘里还没有歌")
    signedOutText: qsTr("登录后可以使用云盘")
    actionSkeletons: [116, 96]

    onCloudDeleteRequested: keys => {
        deleteDialog.keys = keys;
        deleteDialog.open();
    }
    onMatchRequested: key => {
        matchDialog.songKey = key;
        matchDialog.open();
    }
    onUnmatchRequested: key => pageModel.unmatch(key)

    function upload(): void {
        fileDialog.open();
    }

    CloudPageModel {
        id: pageModel
        onNotice: message => page.shell.notify(message)
    }

    infoContent: Column {
        spacing: 8

        KindLabel {
            text: qsTr("音乐库")
            color: page.infoAccentColor
            chip: page.twoPane
        }
        Text {
            width: parent.width
            text: qsTr("云盘")
            elide: Text.ElideRight
            color: page.infoColor
            font: page.titleFont
            Accessible.role: Accessible.Heading
        }
        Text {
            width: parent.width
            visible: text !== ""
            text: [pageModel.countText, pageModel.usageText].filter(t => t !== "").join(" · ")
            elide: Text.ElideRight
            color: page.infoVariantColor
            font: Theme.type.bodyMedium.font
        }
        // How full it is; error-colored from 90%.
        ProgressBar {
            width: Math.min(parent.width, 280)
            visible: pageModel.usage >= 0
            value: Math.max(0, pageModel.usage)
            alert: pageModel.usage >= 0.9
            Accessible.name: qsTr("云盘空间")
        }
    }

    actionsContent: Button {
        variant: Md.Tonal
        glyph: Symbols.cloud_upload
        text: qsTr("上传")
        onClicked: page.upload()
    }

    // The uploads, collapsed to a line; open for one row per file (cloud.md §5).
    noticeContent: CloudUploadPanel {
        visible: App.cloudUploads.count > 0
    }

    // Files dropped on the page upload.
    DropArea {
        id: drop
        anchors.fill: parent
        keys: ["text/uri-list"]
        onEntered: drag => drag.accept(Qt.CopyAction)
        onDropped: drop => {
            App.cloudUploads.add(drop.urls);
            drop.accept(Qt.CopyAction);
        }
        Rectangle {
            anchors.fill: parent
            anchors.margins: 12
            visible: drop.containsDrag
            radius: Theme.shape.extraLarge
            color: Qt.alpha(ColorScope.scheme.primaryContainer, 0.92)
            border.width: 2
            border.color: ColorScope.scheme.primary
            Column {
                anchors.centerIn: parent
                spacing: 12
                Icon {
                    anchors.horizontalCenter: parent.horizontalCenter
                    glyph: Symbols.cloud_upload
                    size: 48
                    color: ColorScope.scheme.onPrimaryContainer
                }
                Text {
                    text: qsTr("松开即可上传到云盘")
                    color: ColorScope.scheme.onPrimaryContainer
                    font: Theme.type.titleLarge.font
                }
            }
        }
    }

    D.FileDialog {
        id: fileDialog
        title: qsTr("上传到云盘")
        fileMode: D.FileDialog.OpenFiles
        currentFolder: App.cloudUploads.folder
        nameFilters: [qsTr("音频文件 (*.mp3 *.flac *.ape *.wav *.m4a *.aac *.ogg *.opus *.wma)"),
                      qsTr("所有文件 (*)")]
        onAccepted: App.cloudUploads.add(selectedFiles)
    }

    Dialog {
        id: deleteDialog
        property list<string> keys
        title: keys.length > 1 ? qsTr("从云盘删除 %1 首歌？").arg(keys.length)
                               : qsTr("从云盘删除这首歌？")
        supportingText: qsTr("文件会一起删掉，不能恢复。")
        acceptText: qsTr("删除")
        rejectText: qsTr("取消")
        onAccepted: pageModel.deleteSongs(keys)
    }

    CloudMatchDialog {
        id: matchDialog
        onPicked: (songKey, targetKey) => pageModel.match(songKey, targetKey)
        searchText: songKey !== "" ? pageModel.searchTextFor(songKey) : ""
    }
}
