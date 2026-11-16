pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Presentation

// The listening statistics' full song ranking (docs/ui/pages/stats.md §2.4): at most
// 100 songs of the period the dashboard showed, in the shared song page layout, with
// a plays or time column. The order follows the dashboard's 按次数/按时长 (they share
// the setting); no sorting or find.
SongCollectionPage {
    id: page

    // The dashboard's range and period when it was opened.
    property int range: StatsPageModel.Week
    property int offset: 0

    model: pageModel
    songs: pageModel.songs
    title: qsTr("听得最多的歌")
    cover: GlyphTile {
        glyph: Symbols.bar_chart
        tone: Md.Primary
    }
    sortable: false
    findable: false
    ranked: true
    reasonColumn: true
    reasonTitle: pageModel.byTime ? qsTr("时长") : qsTr("次数")
    actionSkeletons: [116, 200]
    emptyGlyph: Symbols.bar_chart
    emptyText: qsTr("这段时间没有听歌")

    StatsPageModel {
        id: pageModel
        range: page.range
        offset: page.offset
        byTime: App.settings.statsByTime
        songLimit: 100
    }

    infoContent: Column {
        spacing: 8

        Text {
            text: qsTr("听歌统计")
            color: page.infoAccentColor
            font: Theme.type.labelLarge.font
        }
        Text {
            width: parent.width
            text: page.title
            elide: Text.ElideRight
            color: page.infoColor
            font: page.wide ? Theme.type.headlineLargeEmphasized.font : Theme.type.headlineSmallEmphasized.font
            Accessible.role: Accessible.Heading
        }
        Text {
            width: parent.width
            text: pageModel.range === StatsPageModel.AllTime ? qsTr("所有时间")
                                                             : pageModel.periodTitle
            elide: Text.ElideRight
            color: page.infoVariantColor
            font: Theme.type.bodyMedium.font
        }
    }

    actionsContent: ConnectedButtonGroup {
        exclusive: true
        Button {
            variant: Md.Tonal
            checkable: true
            checked: !pageModel.byTime
            text: qsTr("按次数")
            onClicked: App.settings.statsByTime = false
        }
        Button {
            variant: Md.Tonal
            checkable: true
            checked: pageModel.byTime
            text: qsTr("按时长")
            onClicked: App.settings.statsByTime = true
        }
    }
}
