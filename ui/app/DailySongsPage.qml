pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components
import Cirrus.Presentation

// Today's recommended songs, or a past day's (`date`, "yyyy-MM-dd"), in the shared
// song page layout (docs/ui/pages/album-daily.md §2): the day as the cover, a reason
// column, no sorting. "历史日推" lists the days with history; picking one from today
// opens it as a new page, from a past day switches the day in place.
SongCollectionPage {
    id: page
    origin: Playback.FromDaily

    property alias date: pageModel.date
    readonly property bool history: date !== ""
    readonly property date day: history ? Date.fromLocaleString(Qt.locale("C"), date, "yyyy-MM-dd")
                                        : new Date()

    model: pageModel
    songs: pageModel.songs
    title: history ? qsTr("历史日推") : qsTr("每日推荐")
    cover: DateTile {
        day: page.day
    }
    sortable: false
    reasonColumn: pageModel.hasReasons
    actionSkeletons: [116, 104]
    findPlaceholder: qsTr("在推荐中查找")
    noMatchText: qsTr("推荐里没有与「%1」相符的歌")
    emptyGlyph: Symbols.today
    signedOutText: qsTr("登录后，这里每天都有一组为你挑的歌")
    emptyText: history ? qsTr("这一天的推荐没有可显示的歌") : qsTr("今天的推荐还没有准备好")

    // "10月31日 星期五"
    function dayText(text: string): string {
        const day = Date.fromLocaleString(Qt.locale("C"), text, "yyyy-MM-dd");
        return qsTr("%1月%2日 %3").arg(day.getMonth() + 1).arg(day.getDate())
                                 .arg(Qt.locale().toString(day, "dddd"));
    }
    function openHistory(text: string): void {
        if (history)
            date = text;
        else
            shell.openDaily(text);
    }

    // Back on this page after 6:00: today's songs have changed.
    onVisibleChanged: {
        if (visible)
            pageModel.reloadIfStale();
    }

    DailySongsPageModel {
        id: pageModel
        onNotice: message => page.shell.notify(message)
    }

    infoContent: Column {
        spacing: 8

        Text {
            text: page.history ? qsTr("历史日推") : qsTr("推荐")
            color: Theme.color.primary
            font: Theme.type.labelLarge.font
        }
        Text {
            width: parent.width
            text: page.history ? qsTr("%1月%2日的每日推荐").arg(page.day.getMonth() + 1)
                                                         .arg(page.day.getDate())
                               : qsTr("每日推荐")
            elide: Text.ElideRight
            color: Theme.color.onSurface
            font: page.wide ? Theme.type.headlineLargeEmphasized.font : Theme.type.headlineSmallEmphasized.font
            Accessible.role: Accessible.Heading
        }
        Text {
            width: parent.width
            text: page.history ? qsTr("那一天根据你的口味生成的歌")
                               : qsTr("根据你的口味生成，每天 6:00 更新")
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

    actionsContent: Row {
        spacing: 8
        Button {
            id: historyButton
            variant: Md.Tonal
            glyph: Symbols.history
            text: qsTr("历史日推")
            onClicked: {
                pageModel.loadHistoryDates();
                historyMenu.popup(historyButton, 0, historyButton.height + 4);
            }

            Menu {
                id: historyMenu

                Connections {
                    target: pageModel
                    function onHistoryFailed() {
                        historyMenu.close();
                    }
                }

                MenuItem {
                    visible: pageModel.historyLoading || pageModel.historyDates.length === 0
                    height: visible ? implicitHeight : 0
                    enabled: false
                    text: pageModel.historyLoading ? qsTr("正在加载…") : qsTr("还没有历史日推")
                }
                Instantiator {
                    model: pageModel.historyLoading ? [] : pageModel.historyDates
                    delegate: MenuItem {
                        required property string modelData
                        checkable: true
                        checked: modelData === page.date
                        text: page.dayText(modelData)
                        onTriggered: page.openHistory(modelData)
                    }
                    onObjectAdded: (index, object) => historyMenu.insertItem(index + 1,
                                                                             object as MenuItem)
                    onObjectRemoved: (index, object) => historyMenu.removeItem(object as MenuItem)
                }
            }
        }
    }
}
