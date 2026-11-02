pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components
import Cirrus.Presentation

// The layout playlist, album and daily songs pages share (docs/ui/pages/playlist.md,
// album-daily.md): a header that scrolls away (cover, the page's lines, play all and
// its actions, the in-page find) over the song table. Once the header has gone, a
// compact bar with the title, play all and the find stays on top, the column titles
// under it. Ctrl+F (AppShell) calls find(); typing on the table finds too.
// Pages with tabs (artist.md) add a tab row under the actions, pinned under the
// compact bar once scrolled there, and show other content than songs in `bodyContent`.
// Podcasts (search.md §3.4) list their voices in the same table (`voices`).
// The header's parts are components, since they live inside the table's header:
//   SongCollectionPage { model: pageModel; songs: pageModel.songs; title: …
//       infoContent: Column { … }; actionsContent: Row { … } }
Item {
    id: page

    required property AppShell shell
    required property LoadableModel model
    required property SongListModel songs
    property string title
    property string coverKey
    property string coverGlyph: Symbols.queue_music
    // Drawn instead of the cover art (fills the size it gets: 200, 140 or 32).
    property Component cover: null
    // A round cover (artists): the skeleton matches.
    property bool roundCover: false
    // Right of the cover: the type label, title and the page's lines.
    property Component infoContent: null
    // Under the cover, full width (e.g. a description in narrow windows), while
    // `showExtra`.
    property Component extraContent: null
    property bool showExtra: true
    // After "播放全部" (a Row of buttons).
    property Component actionsContent: null
    // Under the actions (notices), while `showNotice`.
    property Component noticeContent: null
    property bool showNotice: true
    // Tabs under the actions, `tabsHeight` tall; instantiated twice (in the header and
    // pinned under the compact bar), so bind them to a page property.
    property Component tabsContent: null
    readonly property real tabsHeight: 48
    // Under the tabs, while `showBody`: a tab's content that is not the song table.
    property Component bodyContent: null
    property bool showBody: false
    // Whether the song table (column titles and rows) shows.
    property bool showSongs: true
    // Under the rows (e.g. the next page loading).
    property Component footerContent: null
    // Whether there is an in-page find (AppShell's Ctrl+F falls back to the search).
    property bool findable: true
    // The play button: its text and what it plays (the shown order when null).
    property string playText: qsTr("播放全部")
    property SongListModel playSource: null
    // The widths of the action skeletons, play all first.
    property list<int> actionSkeletons: [116]
    property string findPlaceholder: qsTr("在歌单中查找")
    // %1 is the text found.
    property string noMatchText: qsTr("歌单里没有与「%1」相符的歌")
    property string emptyGlyph: Symbols.queue_music
    property string emptyText: qsTr("这里还没有歌")
    // What a guest is told (pages that need signing in).
    property string signedOutText

    // Passed to the table.
    property bool sortable: true
    property bool removable: false
    property bool showCover: true
    property bool albumColumn: true
    property bool reasonColumn: false
    property bool numberByPosition: false
    property bool showAlbumAction: true
    property bool showLiked: true
    // Podcast voices instead of songs (search.md §3.4): they play with playVoices.
    property bool voices: false
    // What the play button plays is voices (off when it plays a list of songs while
    // the table shows voices, as on the recent page).
    property bool playsVoices: voices
    // Ranks (the listening ranking): the first three numbers in primary.
    property bool ranked: false

    readonly property alias table: songTable
    readonly property alias sorted: sortedModel
    readonly property alias pageState: state_
    // Below this the header stacks (playlist.md §3.2).
    readonly property bool wide: width >= 840
    readonly property bool hasSongs: songs.count > 0
    // The in-page find, shared by the header's field and the compact bar's.
    property string findText
    // Set by the header (it lives inside the table's header).
    property Item headerContent: null
    property Item headerActions: null
    property Item headerTabs: null
    property SearchBar headerFind: null
    readonly property bool canPlay: (playSource ? playSource.count > 0 : hasSongs)
                                    && (!playsVoices || App.player.canPlayVoices)
    // 0–1 as the header's action row scrolls under the top: the compact bar slides in.
    readonly property real compactProgress: headerActions
        ? Math.max(0, Math.min(1, (songTable.scrolled - headerActions.y - headerActions.height
                                   + 96) / 56)) : 0
    // The header's tabs have scrolled under the compact bar: the pinned copy shows.
    readonly property bool tabsPinned: tabsContent !== null && headerTabs !== null
                                       && headerTabs.y - songTable.scrolled
                                          < compactBar.y + compactBar.height

    signal removeRequested(list<string> keys)

    function find(): void {
        const field = compactProgress > 0.5 ? compactFind : headerFind;
        if (!field)
            return;
        field.forceActiveFocus(Qt.ShortcutFocusReason);
        field.selectAll();
    }

    function playAll(): void {
        const keys = playSource ? playSource.keys() : sortedModel.keys();
        if (keys.length === 0)
            return;
        if (playsVoices)
            App.player.playVoices(keys, 0);
        else
            App.player.playSongs(keys, 0);
    }

    clip: true

    SortedSongsModel {
        id: sortedModel
        source: page.showSongs ? page.songs : null
        filterText: page.findText
    }

    PageState {
        id: state_
        anchors.fill: parent
        model: page.model
        emptyInContent: true
        signedOutText: page.signedOutText
        onSignInRequested: page.shell.openLogin()
        // Laid out like the header and the table below (playlist.md §6).
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
                        radius: page.roundCover ? width / 2 : Theme.shape.large
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
                            model: page.actionSkeletons
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
        placeholderText: page.findPlaceholder
        Accessible.name: page.findPlaceholder
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

    // The cover, or what the page draws instead.
    component Cover: Item {
        id: coverItem
        property real radius: Theme.shape.large
        CoverArt {
            anchors.fill: parent
            visible: !page.cover
            radius: coverItem.radius
            glyph: page.coverGlyph
            coverKey: page.coverKey
        }
        Loader {
            anchors.fill: parent
            active: page.cover !== null
            sourceComponent: page.cover
        }
    }

    SongTable {
        id: songTable
        anchors.fill: parent
        opacity: state_.contentOpacity
        visible: opacity > 0
        songs: sortedModel
        pinnedTop: compactBar.y + compactBar.height + (page.tabsContent ? page.tabsHeight : 0)
        // No titles over an empty list.
        showColumnTitles: page.showSongs && page.hasSongs
        footerContent: page.footerContent
        sortable: page.sortable
        removable: page.removable
        showCover: page.showCover
        albumColumn: page.albumColumn
        reasonColumn: page.reasonColumn
        numberByPosition: page.numberByPosition
        showAlbumAction: page.showAlbumAction
        showLiked: page.showLiked
        voices: page.voices
        ranked: page.ranked
        onPodcastRequested: key => page.shell.openPodcast(key)
        onRemoveRequested: keys => page.removeRequested(keys)
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
                height: Math.max(cover.height, info.height)

                Cover {
                    id: cover
                    width: page.wide ? 200 : 140
                    height: width
                }
                Loader {
                    id: info
                    x: cover.width + 24
                    width: parent.width - x
                    anchors.verticalCenter: page.wide ? undefined : cover.verticalCenter
                    sourceComponent: page.infoContent
                }
            }

            Loader {
                x: songTable.rowX
                width: songTable.rowWidth
                visible: page.extraContent !== null && page.showExtra
                sourceComponent: page.extraContent
            }

            // Actions, and the find on the right.
            Item {
                id: actions
                x: songTable.rowX
                width: songTable.rowWidth
                height: 40
                Component.onCompleted: page.headerActions = actions

                Row {
                    spacing: 8
                    Button {
                        enabled: page.canPlay
                        variant: Md.Filled
                        glyph: Symbols.play_arrow
                        text: page.playText
                        onClicked: page.playAll()
                    }
                    Loader {
                        sourceComponent: page.actionsContent
                    }
                }
                FindField {
                    id: headerFind
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(240, parent.width / 3)
                    visible: page.findable && page.hasSongs
                    Component.onCompleted: page.headerFind = headerFind
                }
            }

            Loader {
                id: tabs
                x: songTable.rowX
                width: songTable.rowWidth
                height: page.tabsHeight
                visible: page.tabsContent !== null
                // Hidden while the pinned copy shows, so they don't both show.
                opacity: page.tabsPinned ? 0 : 1
                sourceComponent: page.tabsContent
                Component.onCompleted: page.headerTabs = tabs
            }
            Loader {
                x: songTable.rowX
                width: songTable.rowWidth
                visible: page.noticeContent !== null && page.showNotice
                sourceComponent: page.noticeContent
            }
            StateMessage {
                x: songTable.rowX
                width: songTable.rowWidth
                topPadding: 24
                // Also once the last song has been removed.
                visible: page.showSongs && state_.showsContent && !page.hasSongs
                glyph: page.emptyGlyph
                message: page.emptyText
            }
            StateMessage {
                x: songTable.rowX
                width: songTable.rowWidth
                topPadding: 24
                visible: page.showSongs && page.hasSongs && sortedModel.count === 0
                glyph: Symbols.search
                message: page.noMatchText.arg(page.findText.trim())
            }
            Loader {
                x: songTable.rowX
                width: songTable.rowWidth
                visible: page.bodyContent !== null && page.showBody
                active: visible
                sourceComponent: page.bodyContent
            }
        }
    }

    // Slides in, following the scroll, once the header's actions have gone.
    Rectangle {
        id: compactBar
        // Leaves the table's scroll bar uncovered.
        width: parent.width - songTable.scrollBarWidth
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

            Cover {
                id: compactCover
                x: 8
                width: 32
                height: 32
                anchors.verticalCenter: parent.verticalCenter
                radius: Theme.shape.small
            }
            Text {
                x: compactCover.x + compactCover.width + 12
                width: compactPlay.x - x - 12
                anchors.verticalCenter: parent.verticalCenter
                text: page.title
                elide: Text.ElideRight
                color: Theme.color.onSurface
                font: Theme.type.titleMedium.font
            }
            IconButton {
                id: compactPlay
                x: (compactFind.visible ? compactFind.x : parent.width) - width - 12
                anchors.verticalCenter: parent.verticalCenter
                enabled: page.canPlay
                variant: Md.Filled
                glyph: Symbols.play_arrow
                iconFill: 1
                text: page.playText
                onClicked: page.playAll()
            }
            FindField {
                id: compactFind
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(240, parent.width / 3)
                visible: page.findable
            }
        }
    }

    // The tabs, pinned under the compact bar once the header's have scrolled there.
    Rectangle {
        y: compactBar.y + compactBar.height
        width: parent.width - songTable.scrollBarWidth
        height: page.tabsHeight
        visible: page.tabsPinned
        color: Theme.color.surface

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.AllButtons
        }
        HoverHandler {
            blocking: true
        }
        Loader {
            x: songTable.rowX
            width: songTable.rowWidth
            height: parent.height
            active: page.tabsContent !== null
            sourceComponent: page.tabsContent
        }
    }
}
