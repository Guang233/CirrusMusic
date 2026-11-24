pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Presentation

// 纠错 (docs/ui/pages/cloud.md §4): links a cloud song to the catalog song picked from a song
// search, first of the song's own title and artist. "关联" applies the selection.
//   CloudMatchDialog { songKey: key; onPicked: (songKey, targetKey) => model.match(…) }
Dialog {
    id: dialog

    // The cloud song (its row's key) and what the search starts with.
    property string songKey
    property string searchText
    // The catalog song selected in the list.
    property string selected

    signal picked(string songKey, string targetKey)

    function choose(key: string): void {
        dialog.picked(dialog.songKey, key);
        dialog.close();
    }

    title: qsTr("关联到曲库里的歌")
    width: Math.min(640, (parent ? parent.width : 640) - 48)
    acceptText: qsTr("关联")
    rejectText: qsTr("取消")
    acceptEnabled: selected !== ""
    initialFocusItem: search
    onAboutToShow: {
        selected = "";
        search.text = searchText;
        results.query = searchText;
    }
    onAccepted: picked(songKey, selected)

    CloudMatchSearch {
        id: results
    }

    Column {
        width: dialog.availableWidth
        spacing: 12

        TextField {
            id: search
            width: parent.width
            leadingGlyph: Symbols.search
            placeholderText: qsTr("搜索歌名、歌手")
            onAccepted: results.query = text
            // ↓ goes on to the list.
            Keys.onDownPressed: {
                list.currentIndex = Math.max(0, list.currentIndex);
                list.forceActiveFocus();
            }
        }

        ListView {
            id: list
            width: parent.width
            height: 360
            clip: true
            model: results.songs
            keyNavigationEnabled: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            onCurrentIndexChanged: {
                if (activeFocus && currentIndex >= 0)
                    dialog.selected = results.songs.keys()[currentIndex] ?? "";
            }
            Keys.onReturnPressed: dialog.accept()
            Keys.onEnterPressed: dialog.accept()
            Accessible.role: Accessible.List
            Accessible.name: qsTr("搜索结果")

            delegate: ListItem {
                required property string key
                required property string title
                required property string artistText
                required property string albumTitle
                required property string durationText
                width: ListView.view.width
                lines: 2
                segmented: true
                highlighted: key === dialog.selected
                text: title
                supportingText: [artistText, albumTitle].filter(t => t !== "").join(" · ")
                trailingText: durationText
                focusPolicy: Qt.NoFocus
                onClicked: dialog.selected = key
                onDoubleClicked: dialog.choose(key)
            }

            Text {
                anchors.centerIn: parent
                visible: list.count === 0 && results.state !== LoadState.Loading
                text: results.query === "" ? qsTr("输入歌名或歌手搜索") : qsTr("没有找到相符的歌")
                color: ColorScope.scheme.onSurfaceVariant
                font: Theme.type.bodyMedium.font
            }
            LoadingIndicator {
                anchors.centerIn: parent
                visible: results.state === LoadState.Loading
            }
        }
    }
}
