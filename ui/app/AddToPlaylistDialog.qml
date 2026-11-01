pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components
import Cirrus.Presentation

// "收藏到歌单": picks one of the account's own playlists for `songKeys`
// (docs/ui/pages/playlist.md §5). The shell owns one; `AppShell.addToPlaylist(keys)`.
// The list is one Tab stop: ↑/↓ pick, Enter adds; or click a playlist.
Dialog {
    id: dialog

    required property LibraryPageModel library
    property list<string> songKeys

    function pick(key: string, title: string): void {
        App.library.addToPlaylist(key, title, songKeys);
        close();
    }

    onAboutToShow: list.currentIndex = 0

    width: 400
    title: songKeys.length > 1 ? qsTr("收藏 %1 首到歌单").arg(songKeys.length)
                               : qsTr("收藏到歌单")
    rejectText: qsTr("取消")

    ListView {
        id: list
        width: parent.width
        height: Math.min(contentHeight, 360)
        visible: count > 0
        clip: true
        model: dialog.library.createdPlaylists
        boundsBehavior: Flickable.StopAtBounds
        activeFocusOnTab: true
        highlightFollowsCurrentItem: false
        Accessible.role: Accessible.List
        Accessible.name: dialog.title
        ScrollBar.vertical: ScrollBar {}
        Keys.onReturnPressed: (currentItem as ListItem)?.clicked()
        Keys.onEnterPressed: (currentItem as ListItem)?.clicked()

        delegate: ListItem {
            id: item
            required property string key
            required property string title
            required property string coverKey
            required property int count
            width: list.width
            // The keyboard's pick, while the list has the focus.
            highlighted: list.activeFocus && ListView.isCurrentItem
            text: title
            supportingText: count >= 0 ? qsTr("%1 首").arg(App.format.count(count)) : ""
            leading: Component {
                CoverArt {
                    width: 40
                    height: 40
                    glyph: Symbols.queue_music
                    coverKey: item.coverKey
                }
            }
            onClicked: dialog.pick(key, title)
        }
    }
    Text {
        width: parent.width
        visible: list.count === 0
        wrapMode: Text.Wrap
        text: qsTr("你还没有自己创建的歌单。")
        color: Theme.color.onSurfaceVariant
        font: Theme.type.bodyMedium.font
    }
}
