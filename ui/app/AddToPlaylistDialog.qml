pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components
import Cirrus.Presentation

// "收藏到歌单": picks one of the account's own playlists for `songKeys`
// (docs/ui/pages/playlist.md §5), or makes a new one for them. The shell owns one;
// `AppShell.addToPlaylist(keys)`.
// Keyboard: 新建歌单 and the list are a Tab stop each (↓ / ↑ move between them);
// in the list ↑/↓ pick, Enter adds. 新建歌单 turns the dialog into a title field.
Dialog {
    id: dialog

    required property LibraryPageModel library
    property list<string> songKeys
    // Naming a new playlist instead of picking one.
    property bool creating: false

    function pick(key: string, title: string): void {
        App.library.addToPlaylist(key, title, songKeys);
        close();
    }

    function startCreating(): void {
        titleField.text = "";
        creating = true;
        Qt.callLater(() => titleField.forceActiveFocus(Qt.TabFocusReason));
    }

    onAboutToShow: {
        creating = false;
        list.currentIndex = 0;
    }
    onAccepted: if (creating)
        App.library.createPlaylistWith(titleField.text, songKeys)

    width: 400
    title: creating ? qsTr("新建歌单") : songKeys.length > 1 ? qsTr("收藏 %1 首到歌单").arg(songKeys.length) : qsTr("收藏到歌单")
    acceptText: creating ? qsTr("新建并收藏") : ""
    acceptEnabled: titleField.text.trim() !== ""
    rejectText: qsTr("取消")

    ListItem {
        id: newItem
        width: parent.width
        visible: !dialog.creating
        segmented: true
        text: qsTr("新建歌单")
        leading: Component {
            Rectangle {
                width: 40
                height: 40
                radius: Theme.shape.small
                color: ColorScope.scheme.secondaryContainer
                Icon {
                    anchors.centerIn: parent
                    glyph: Symbols.add
                    size: 24
                    color: ColorScope.scheme.onSecondaryContainer
                }
            }
        }
        Keys.onDownPressed: if (list.count > 0)
            list.forceActiveFocus(Qt.TabFocusReason)
        onClicked: dialog.startCreating()
    }

    ListView {
        id: list
        width: parent.width
        height: Math.min(contentHeight + topMargin, 360)
        visible: count > 0 && !dialog.creating
        clip: true
        topMargin: 2 // the gap below 新建歌单, as between the items
        spacing: 2
        model: dialog.library.createdPlaylists
        boundsBehavior: Flickable.StopAtBounds
        activeFocusOnTab: visible
        highlightFollowsCurrentItem: false
        Accessible.role: Accessible.List
        Accessible.name: dialog.title
        ScrollBar.vertical: ScrollBar {}
        Keys.onReturnPressed: (currentItem as ListItem)?.clicked()
        Keys.onEnterPressed: (currentItem as ListItem)?.clicked()
        Keys.onUpPressed: event => {
            if (currentIndex > 0)
                decrementCurrentIndex();
            else
                newItem.forceActiveFocus(Qt.TabFocusReason);
        }

        delegate: ListItem {
            id: item
            required property string key
            required property string title
            required property string coverKey
            required property int count
            width: list.width
            segmented: true
            focusPolicy: Qt.NoFocus
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

    TextField {
        id: titleField
        width: parent.width
        visible: dialog.creating
        variant: Md.Outlined
        gapColor: Theme.color.surfaceContainerHigh
        label: qsTr("歌单标题")
        maximumLength: 40
        supportingText: qsTr("最多 40 个字")
    }
}
