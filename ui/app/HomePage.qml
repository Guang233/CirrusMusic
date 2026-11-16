pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components
import Cirrus.Presentation

// The home page (docs/ui/pages/home.md): "today" and personal FM on top, the songs
// played lately, today's and recommended playlists, charts, new albums and popular
// artists; guests get the sign-in invitation instead of the private parts. Every
// section loads and fails on its own.
Item {
    id: page

    required property AppShell shell

    readonly property bool signedIn: App.session.signedIn
    // Content column: at most 1200 wide, centered (§4).
    readonly property real columnWidth: Math.min(width - 48, 1200)
    readonly property bool wide: columnWidth >= 792
    readonly property bool narrow: width < 600

    // Everything the account can see failed: one page-level message instead.
    readonly property bool allFailed: {
        const failed = m => m.state === LoadState.Error || m.state === LoadState.Offline;
        const common = failed(recommended) && failed(charts) && failed(newAlbums)
                       && failed(artists);
        return signedIn ? common && failed(daily) && failed(recent) && failed(dailyPlaylists)
                        : common;
    }

    function refresh() {
        for (const m of [daily, recent, dailyPlaylists, recommended, charts, newAlbums, artists])
            m.retry();
    }

    DailySongsPageModel {
        id: daily
    }
    RecentSongsModel {
        id: recent
        limit: 6
    }
    ShelfModel {
        id: dailyPlaylists
        shelf: Feed.DailyPlaylists
    }
    ShelfModel {
        id: recommended
        shelf: Feed.RecommendedPlaylists
    }
    ChartsModel {
        id: charts
    }
    ShelfModel {
        id: newAlbums
        shelf: Feed.NewAlbums
    }
    ShelfModel {
        id: artists
        shelf: Feed.PopularArtists
    }

    Shortcut {
        sequences: [StandardKey.Refresh]
        enabled: page.visible
        onActivated: page.refresh()
    }

    component Placeholder: Column {
        spacing: 8
        Repeater {
            model: 3
            Skeleton {
                width: page.columnWidth
                height: 64
                radius: Theme.shape.large
            }
        }
    }
    component CardsPlaceholder: Row {
        spacing: 16
        Repeater {
            model: Math.max(1, Math.floor(page.columnWidth / 176))
            Skeleton {
                width: 160
                height: 200
                radius: Theme.shape.large
            }
        }
    }

    // One row of cards, as many as fit.
    component Cards: CollectionGrid {
        width: page.columnWidth + 16
        x: -8
        minimumCardWidth: 176
        maximumRows: 1
        onActivated: (kind, key) => page.shell.openCollection(kind, key)
    }

    // A few songs in one or two segmented columns; a row plays the section's songs.
    component SongColumns: Row {
        id: songColumns
        required property SongListModel songs
        readonly property int columns: page.wide ? 2 : 1
        readonly property int perColumn: Math.ceil(songs.count / columns)
        width: page.columnWidth
        spacing: 16
        Repeater {
            model: songColumns.columns
            Column {
                id: songColumn
                required property int index
                readonly property int firstRow: index * songColumns.perColumn
                width: (songColumns.width - (songColumns.columns - 1) * songColumns.spacing)
                       / songColumns.columns
                spacing: 2
                Repeater {
                    model: songColumns.songs
                    ListItem {
                        id: row
                        required property int index
                        required property string title
                        required property string artistText
                        required property string coverKey
                        required property string durationText
                        required property bool current
                        readonly property int lastRow: Math.min(songColumns.songs.count,
                                                             songColumn.firstRow
                                                             + songColumns.perColumn) - 1
                        width: songColumn.width
                        visible: index >= songColumn.firstRow && index <= lastRow
                        segmented: true
                        first: index === songColumn.firstRow
                        last: index === row.lastRow
                        containerColor: Theme.color.surfaceContainer
                        highlighted: current
                        text: title
                        supportingText: artistText
                        trailingText: durationText
                        onClicked: App.player.playSongs(songColumns.songs.keys(), index)
                        leading: Component {
                            CoverArt {
                                width: 48
                                height: 48
                                coverKey: row.coverKey
                            }
                        }
                    }
                }
            }
        }
    }

    Flickable {
        id: flick
        anchors.fill: parent
        visible: !page.allFailed
        contentHeight: column.height + 48
        bottomMargin: PageInsets.bottom
        // Overshoots and springs back at the ends.
        boundsBehavior: Flickable.DragAndOvershootBounds
        ScrollBar.vertical: ScrollBar {}

        Column {
            id: column
            x: (page.width - width) / 2
            y: 24
            width: page.columnWidth
            spacing: 40

            // Today and personal FM: side by side when wide, FM as a strip below otherwise.
            Item {
                width: column.width
                height: today.height + (fm.visible && !page.wide ? fm.height + 12 : 0)

                TodayCard {
                    id: today
                    shell: page.shell
                    daily: daily
                    compact: page.narrow
                    width: fm.visible && page.wide ? parent.width - 280 - 16 : parent.width
                    height: page.narrow ? 200 : 280
                }
                FmCard {
                    id: fm
                    visible: page.signedIn
                    strip: !page.wide
                    x: page.wide ? today.width + 16 : 0
                    y: page.wide ? 0 : today.height + 12
                    width: page.wide ? 280 : parent.width
                    height: page.wide ? 280 : 72
                }
            }

            HomeSection {
                width: column.width
                visible: page.signedIn && (hasContent || failed || loading)
                title: qsTr("最近听过")
                showsAll: true
                model: recent
                skeleton: Placeholder {}
                onAllRequested: page.shell.openRecent()

                SongColumns {
                    songs: recent.songs
                }
            }

            HomeSection {
                width: column.width
                visible: page.signedIn && (hasContent || failed || loading)
                title: qsTr("每日推荐歌单")
                model: dailyPlaylists
                skeleton: CardsPlaceholder {}

                Cards {
                    model: dailyPlaylists.items
                }
            }

            HomeSection {
                width: column.width
                title: qsTr("推荐歌单")
                showsAll: true
                model: recommended
                skeleton: CardsPlaceholder {}
                onAllRequested: page.shell.openDiscover(0)

                Cards {
                    model: recommended.items
                }
            }

            HomeSection {
                width: column.width
                title: qsTr("榜单")
                showsAll: true
                model: charts
                skeleton: CardsPlaceholder {}
                onAllRequested: page.shell.openDiscover(1)

                Flow {
                    id: chartFlow
                    readonly property int columns: Math.max(1, Math.floor((width + spacing) / 276))
                    width: column.width
                    spacing: 16
                    Repeater {
                        model: charts.charts
                        ChartCard {
                            required property string key
                            required property string title
                            required updateLabel
                            required coverKey
                            required previews
                            width: (chartFlow.width - (chartFlow.columns - 1) * chartFlow.spacing)
                                   / chartFlow.columns
                            text: title
                            onClicked: page.shell.openPlaylist(key)
                        }
                    }
                }
            }

            HomeSection {
                width: column.width
                title: qsTr("新碟上架")
                showsAll: true
                model: newAlbums
                skeleton: CardsPlaceholder {}
                onAllRequested: page.shell.openDiscover(3)

                Cards {
                    model: newAlbums.items
                }
            }

            HomeSection {
                width: column.width
                title: qsTr("热门歌手")
                showsAll: true
                model: artists
                skeleton: CardsPlaceholder {}
                onAllRequested: page.shell.openDiscover(2)

                Cards {
                    minimumCardWidth: 144
                    model: artists.items
                }
            }
        }
    }

    StateMessage {
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 480)
        visible: page.allFailed
        glyph: Symbols.wifi_off
        tone: Md.Neutral
        title: qsTr("连不上网易云音乐")
        message: qsTr("检查网络后重试。")
        actionText: qsTr("重试")
        actionGlyph: Symbols.refresh
        onActionTriggered: page.refresh()
    }
}
