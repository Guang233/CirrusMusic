pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components
import Cirrus.Presentation

// Playlist details (docs/ui/pages/playlist.md): a header that scrolls away (cover,
// title, creator, description, tags, counts; play all, save, comments; the
// in-playlist find) over the song table. Once the header has gone, a compact bar with
// the title, play all and the find stays on top, the column titles under it.
// Ctrl+F (AppShell) finds in the playlist; typing on the table does too.
Item {
    id: page

    required property AppShell shell
    property alias playlistKey: pageModel.playlistKey
    readonly property string title: pageModel.title
    // Below this the header stacks (§3.2).
    readonly property bool wide: width >= 840
    readonly property bool hasSongs: pageModel.songs.count > 0
    // The in-playlist find, shared by the header's field and the compact bar's.
    property string findText
    // Set by the header content (it lives inside the table's header).
    property Item headerContent: null
    property SearchBar headerFind: null
    // 0–1 as the header's action row scrolls under the top: the compact bar slides in.
    readonly property real compactProgress: headerContent
        ? Math.max(0, Math.min(1, (songTable.scrolled - headerContent.height + 112) / 56)) : 0

    function find(): void {
        const field = compactProgress > 0.5 ? compactFind : headerFind;
        if (!field)
            return;
        field.forceActiveFocus(Qt.ShortcutFocusReason);
        field.selectAll();
    }

    function playAll(): void {
        const keys = sorted.keys();
        if (keys.length > 0)
            App.player.playSongs(keys, 0);
    }

    // Asks first (menu, Delete).
    function removeSongs(keys: list<string>): void {
        removeDialog.songKeys = keys;
        removeDialog.open();
    }

    function toggleSubscribed(): void {
        if (!App.session.signedIn)
            page.shell.openLogin();
        else
            pageModel.subscribed = !pageModel.subscribed;
    }

    clip: true

    PlaylistPageModel {
        id: pageModel
        onLibraryChanged: page.shell.refreshLibrary()
        onNotice: message => page.shell.notify(message)
        onDeleted: page.shell.back()
    }

    SortedSongsModel {
        id: sorted
        source: pageModel.songs
        filterText: page.findText
    }

    PageState {
        id: pageState
        anchors.fill: parent
        model: pageModel
        emptyInContent: true
        // Laid out like the header and the table below (§6).
        skeleton: SongTableSkeleton {
            table: songTable
            headerSkeleton: Column {
                topPadding: 24
                bottomPadding: 16
                spacing: 24

                Item {
                    x: songTable.rowX
                    width: songTable.rowWidth
                    height: skeletonCover.height

                    Skeleton {
                        id: skeletonCover
                        width: page.wide ? 200 : 140
                        height: width
                        radius: Theme.shape.large
                    }
                    Column {
                        readonly property real available: parent.width - x
                        x: skeletonCover.width + 24
                        y: page.wide ? 4 : (skeletonCover.height - height) / 2
                        spacing: 12
                        Skeleton {
                            width: 32
                            height: 12
                        }
                        Skeleton {
                            width: Math.min(280, parent.available)
                            height: page.wide ? 32 : 24
                        }
                        Skeleton {
                            width: Math.min(200, parent.available)
                            height: 14
                        }
                        Skeleton {
                            visible: page.wide
                            width: Math.min(360, parent.available)
                            height: 14
                        }
                        Skeleton {
                            width: Math.min(220, parent.available)
                            height: 14
                        }
                    }
                }
                // Actions and the find field.
                Item {
                    x: songTable.rowX
                    width: songTable.rowWidth
                    height: 40
                    Row {
                        spacing: 8
                        Repeater {
                            model: [116, 88, 96]
                            Skeleton {
                                required property int modelData
                                width: modelData
                                height: 40
                                radius: height / 2
                            }
                        }
                    }
                    Skeleton {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.min(240, parent.width / 3)
                        height: 36
                        radius: height / 2
                    }
                }
            }
        }
    }

    // Find field behaviour, for both fields: Esc clears, then returns to the table;
    // ↓ and Enter go to the table.
    component FindField: SearchBar {
        id: field
        height: 36
        leftPadding: 44
        handlesEscape: false
        font: Theme.type.bodyMedium.font
        placeholderText: qsTr("在歌单中查找")
        Accessible.name: qsTr("在歌单中查找")
        text: page.findText
        onTextChanged: page.findText = text
        Keys.onEscapePressed: {
            if (length > 0)
                clear();
            else
                songTable.focusTable();
        }
        Keys.onDownPressed: songTable.focusTable()
        onAccepted: songTable.focusTable()
    }

    // Description and tags: beside the cover when wide, under it otherwise.
    component Extras: Column {
        spacing: 12

        Column {
            width: parent.width
            visible: pageModel.description !== ""
            Text {
                id: description
                width: parent.width
                text: pageModel.description
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
                color: Theme.color.onSurfaceVariant
                font: Theme.type.bodyMedium.font
            }
            Button {
                visible: description.truncated
                variant: Md.Text
                size: Md.XSmall
                text: qsTr("更多")
                onClicked: descriptionDialog.open()
            }
        }
        Flow {
            width: parent.width
            visible: pageModel.tags.length > 0
            spacing: 8
            Repeater {
                model: pageModel.tags
                Chip {
                    required property string modelData
                    text: modelData
                    // Until the discover page has categories, a tag searches.
                    onClicked: page.shell.search(modelData)
                }
            }
        }
    }

    SongTable {
        id: songTable
        anchors.fill: parent
        opacity: pageState.contentOpacity
        visible: opacity > 0
        songs: sorted
        pinnedTop: compactBar.y + compactBar.height
        // The account's own playlist; moving only in its own order, unfiltered.
        removable: pageModel.own
        reorderable: pageModel.own && sorted.sortField === SortedSongsModel.Original
                     && page.findText.trim() === ""
        onMoveRequested: (from, to) => pageModel.moveSong(from, to)
        onRemoveRequested: keys => page.removeSongs(keys)
        onAddToPlaylistRequested: keys => page.shell.addToPlaylist(keys)
        onSignInRequested: page.shell.openLogin()
        onAlbumRequested: key => page.shell.openAlbum(key)
        onArtistRequested: key => page.shell.openArtist(key)
        // Typing on the table goes on in the find field.
        onTyped: text => {
            const field = page.compactProgress > 0.5 ? compactFind : page.headerFind;
            if (!field)
                return;
            field.forceActiveFocus(Qt.ShortcutFocusReason);
            field.insert(field.length, text);
            field.cursorPosition = field.length;
        }

        headerContent: Column {
            id: header

            topPadding: 24
            bottomPadding: 16
            spacing: 24
            Component.onCompleted: page.headerContent = header

            Item {
                x: songTable.rowX
                width: songTable.rowWidth
                height: Math.max(cover.height, info.implicitHeight)

                CoverArt {
                    id: cover
                    width: page.wide ? 200 : 140
                    height: width
                    radius: Theme.shape.large
                    glyph: Symbols.queue_music
                    coverKey: pageModel.coverKey
                }
                Column {
                    id: info
                    x: cover.width + 24
                    width: parent.width - x
                    anchors.verticalCenter: page.wide ? undefined : cover.verticalCenter
                    spacing: 8

                    Text {
                        text: qsTr("歌单")
                        color: Theme.color.primary
                        font: Theme.type.labelLarge.font
                    }
                    // The title; the account's own playlist can be edited from here.
                    Item {
                        width: parent.width
                        height: Math.max(titleText.height, editButton.visible ? editButton.height : 0)
                        Text {
                            id: titleText
                            width: Math.min(implicitWidth, parent.width
                                            - (editButton.visible ? editButton.width + 8 : 0))
                            anchors.verticalCenter: parent.verticalCenter
                            text: pageModel.title
                            wrapMode: Text.Wrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                            color: Theme.color.onSurface
                            font: page.wide ? Theme.type.headlineLarge.font
                                            : Theme.type.headlineSmall.font
                            Accessible.role: Accessible.Heading
                        }
                        IconButton {
                            id: editButton
                            x: titleText.width + 8
                            anchors.verticalCenter: parent.verticalCenter
                            visible: pageModel.own
                            glyph: Symbols.edit
                            text: qsTr("编辑歌单信息")
                            onClicked: editDialog.open()
                        }
                    }
                    Text {
                        width: parent.width
                        text: [pageModel.creator, pageModel.updatedText].filter(s => s !== "")
                                                                        .join(" · ")
                        elide: Text.ElideRight
                        color: Theme.color.onSurfaceVariant
                        font: Theme.type.bodyMedium.font
                    }
                    Extras {
                        width: parent.width
                        visible: page.wide
                        topPadding: 4
                        bottomPadding: 4
                    }
                    // Counts the service gave; unknown ones are left out, not shown as 0.
                    Text {
                        width: parent.width
                        text: {
                            const parts = [qsTr("%1 首").arg(App.format.count(pageModel.songs.count))];
                            if (pageModel.playCount >= 0)
                                parts.push(qsTr("播放 %1 次").arg(App.format.count(pageModel.playCount)));
                            if (pageModel.subscriberCount >= 0)
                                parts.push(qsTr("收藏 %1").arg(App.format.count(pageModel.subscriberCount)));
                            return parts.join(" · ");
                        }
                        elide: Text.ElideRight
                        color: Theme.color.onSurfaceVariant
                        font: Theme.type.bodyMedium.font
                    }
                }
            }

            Extras {
                x: songTable.rowX
                width: songTable.rowWidth
                visible: !page.wide && (pageModel.description !== "" || pageModel.tags.length > 0)
            }

            // Actions, and the find on the right.
            Item {
                x: songTable.rowX
                width: songTable.rowWidth
                height: 40

                Row {
                    spacing: 8
                    Button {
                        enabled: page.hasSongs
                        variant: Md.Filled
                        glyph: Symbols.play_arrow
                        text: qsTr("播放全部")
                        onClicked: page.playAll()
                    }
                    // Someone else's playlist (P5).
                    Button {
                        visible: !pageModel.own
                        variant: Md.Tonal
                        glyph: pageModel.subscribed ? Symbols.bookmark_added : Symbols.bookmark_add
                        text: pageModel.subscribed ? qsTr("已收藏") : qsTr("收藏")
                        onClicked: page.toggleSubscribed()
                    }
                    Button {
                        variant: Md.Text
                        glyph: Symbols.comment
                        text: pageModel.commentCount > 0
                              ? qsTr("评论 %1").arg(App.format.count(pageModel.commentCount))
                              : qsTr("评论")
                        // The comments page is designed later (playlist.md §2).
                        onClicked: page.shell.notify(qsTr("评论页还在设计中"))
                    }
                    IconButton {
                        id: moreButton
                        visible: pageModel.own
                        glyph: Symbols.more_vert
                        text: qsTr("更多")
                        onClicked: moreMenu.popup(moreButton, 0, moreButton.height + 4)

                        Menu {
                            id: moreMenu
                            MenuItem {
                                glyph: Symbols.edit
                                text: qsTr("编辑歌单信息")
                                onTriggered: editDialog.open()
                            }
                            MenuItem {
                                glyph: Symbols.delete_
                                text: qsTr("删除歌单")
                                onTriggered: deleteDialog.open()
                            }
                        }
                    }
                }
                FindField {
                    id: headerFind
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(240, parent.width / 3)
                    visible: page.hasSongs
                    Component.onCompleted: page.headerFind = headerFind
                }
            }

            NoticeBar {
                x: songTable.rowX
                width: songTable.rowWidth
                visible: pageModel.state === LoadState.Partial
                text: qsTr("有些歌曲的信息暂时无法获取")
            }
            StateMessage {
                x: songTable.rowX
                width: songTable.rowWidth
                topPadding: 24
                // Also once the last song has been removed.
                visible: pageState.showsContent && !page.hasSongs
                glyph: Symbols.queue_music
                message: pageModel.own ? qsTr("这个歌单还没有歌。在歌曲的菜单里选择「收藏到歌单」就能加进来。")
                                       : qsTr("这个歌单还没有歌")
            }
            StateMessage {
                x: songTable.rowX
                width: songTable.rowWidth
                topPadding: 24
                visible: page.hasSongs && sorted.count === 0
                glyph: Symbols.search
                message: qsTr("歌单里没有与「%1」相符的歌").arg(page.findText.trim())
            }
        }
    }

    // Slides in, following the scroll, once the header's actions have gone.
    Rectangle {
        id: compactBar
        width: parent.width
        height: 56
        y: -height * (1 - page.compactProgress)
        visible: page.compactProgress > 0
        color: Theme.color.surface

        // The rows scrolling under it are not hovered or clicked through it.
        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.AllButtons
        }
        HoverHandler {
            blocking: true
        }

        Item {
            x: songTable.rowX
            width: songTable.rowWidth
            height: parent.height

            CoverArt {
                id: compactCover
                x: 8
                width: 32
                height: 32
                anchors.verticalCenter: parent.verticalCenter
                radius: Theme.shape.small
                glyph: Symbols.queue_music
                coverKey: pageModel.coverKey
            }
            Text {
                x: compactCover.x + compactCover.width + 12
                width: compactPlay.x - x - 12
                anchors.verticalCenter: parent.verticalCenter
                text: pageModel.title
                elide: Text.ElideRight
                color: Theme.color.onSurface
                font: Theme.type.titleMedium.font
            }
            IconButton {
                id: compactPlay
                x: compactFind.x - width - 12
                anchors.verticalCenter: parent.verticalCenter
                enabled: page.hasSongs
                variant: Md.Filled
                glyph: Symbols.play_arrow
                iconFill: 1
                text: qsTr("播放全部")
                onClicked: page.playAll()
            }
            FindField {
                id: compactFind
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(240, parent.width / 3)
            }
        }
    }

    Dialog {
        id: descriptionDialog
        title: pageModel.title
        supportingText: pageModel.description
    }

    Dialog {
        id: editDialog
        width: 480
        title: qsTr("编辑歌单信息")
        acceptText: qsTr("保存")
        rejectText: qsTr("取消")
        acceptEnabled: titleField.text.trim() !== ""
        initialFocusItem: titleField
        onAboutToShow: {
            titleField.text = pageModel.title;
            descriptionField.text = pageModel.description;
        }
        onAccepted: pageModel.updateInfo(titleField.text, descriptionField.text)

        Column {
            width: parent.width
            spacing: 16
            TextField {
                id: titleField
                width: parent.width
                variant: Md.Outlined
                gapColor: Theme.color.surfaceContainerHigh
                label: qsTr("标题")
                error: text.trim() === ""
                supportingText: error ? qsTr("标题不能为空") : ""
            }
            TextField {
                id: descriptionField
                width: parent.width
                variant: Md.Outlined
                gapColor: Theme.color.surfaceContainerHigh
                label: qsTr("简介")
            }
        }
    }

    Dialog {
        id: removeDialog
        property list<string> songKeys
        title: songKeys.length > 1 ? qsTr("从歌单移除 %1 首歌？").arg(songKeys.length)
                                   : qsTr("从歌单移除这首歌？")
        supportingText: qsTr("移除后可以在歌曲的菜单里重新收藏到「%1」。").arg(pageModel.title)
        acceptText: qsTr("移除")
        rejectText: qsTr("取消")
        onAccepted: pageModel.removeSongs(songKeys)
    }

    Dialog {
        id: deleteDialog
        title: qsTr("删除歌单？")
        supportingText: qsTr("「%1」会被删除，无法恢复。歌曲本身不受影响。").arg(pageModel.title)
        acceptText: qsTr("删除")
        rejectText: qsTr("取消")
        onAccepted: pageModel.deletePlaylist()
    }
}
