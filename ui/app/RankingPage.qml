pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components
import Cirrus.Presentation

// The listening ranking (docs/ui/pages/library.md §3.3): the songs played most, the
// last week or all time, in the shared song page layout. Numbers are ranks (the
// first three in primary); no sorting or find.
SongCollectionPage {
    id: page

    model: pageModel
    songs: pageModel.songs
    title: qsTr("听歌排行")
    cover: GlyphTile {
        glyph: Symbols.leaderboard
        color: Theme.color.tertiaryContainer
        contentColor: Theme.color.onTertiaryContainer
    }
    sortable: false
    findable: false
    ranked: true
    actionSkeletons: [116, 200]
    emptyGlyph: Symbols.leaderboard
    emptyText: pageModel.allTime ? qsTr("还没有听歌记录") : qsTr("这一周还没有听歌记录")
    signedOutText: qsTr("登录后可以看到你的听歌排行")

    RankingPageModel {
        id: pageModel
    }

    infoContent: Column {
        spacing: 8

        Text {
            text: qsTr("音乐库")
            color: Theme.color.primary
            font: Theme.type.labelLarge.font
        }
        Text {
            width: parent.width
            text: qsTr("听歌排行")
            elide: Text.ElideRight
            color: Theme.color.onSurface
            font: page.wide ? Theme.type.headlineLarge.font : Theme.type.headlineSmall.font
            Accessible.role: Accessible.Heading
        }
        Text {
            width: parent.width
            text: pageModel.allTime ? qsTr("所有时间里听得最多的歌") : qsTr("最近一周听得最多的歌")
            elide: Text.ElideRight
            color: Theme.color.onSurfaceVariant
            font: Theme.type.bodyMedium.font
        }
        Text {
            width: parent.width
            visible: text !== ""
            text: pageModel.countText
            elide: Text.ElideRight
            color: Theme.color.onSurfaceVariant
            font: Theme.type.bodyMedium.font
        }
    }

    // The last week or all time; as tall as the other buttons of the row.
    actionsContent: ConnectedButtonGroup {
        exclusive: true
        Button {
            variant: Md.Tonal
            checkable: true
            checked: !pageModel.allTime
            text: qsTr("最近一周")
            onClicked: pageModel.allTime = false
        }
        Button {
            variant: Md.Tonal
            checkable: true
            checked: pageModel.allTime
            text: qsTr("所有时间")
            onClicked: pageModel.allTime = true
        }
    }
}
