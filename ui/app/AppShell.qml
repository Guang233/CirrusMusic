pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components
import Cirrus.Presentation

// The shell (docs/ui/README.md §6.2): navigation rail, page stack and mini player. Large windows
// (≥ 1200) keep the rail expanded beside the content and push it when toggled;
// narrower ones keep it collapsed and expand it over the content with a scrim.
// Crossing the breakpoint switches without animation (motion-choreography.md §5).
// Pages get `shell` and navigate through it.
Item {
    id: shell

    readonly property bool large: width >= 1200
    readonly property bool canGoBack: stack.depth > 1
    readonly property var destinations: [{
            glyph: Symbols.home,
            text: qsTr("首页"),
            page: homePage
        }, {
            glyph: Symbols.explore,
            text: qsTr("发现"),
            page: discoverPage
        }, {
            glyph: Symbols.search,
            text: qsTr("搜索"),
            page: searchPage
        }, {
            glyph: Symbols.library_music,
            text: qsTr("音乐库"),
            page: libraryPage
        }, {
            glyph: Symbols.settings,
            text: qsTr("设置"),
            page: settingsPage
        }]

    // Top-level destination: clears the history.
    function select(index: int) {
        rail.currentIndex = index;
        stack.replaceAll(destinations[index].page, {
            shell: shell
        });
        if (!large)
            rail.expanded = false;
    }

    function openPlaylist(key: string) {
        stack.push(playlistPage, {
            shell: shell,
            playlistKey: key
        });
    }

    function openAlbum(key: string) {
        stack.push(albumPage, {
            shell: shell,
            albumKey: key
        });
    }

    function openArtist(key: string) {
        stack.push(artistPage, {
            shell: shell,
            artistKey: key
        });
    }

    // Collection.Kind and key, as collection cards report them.
    function openCollection(kind: int, key: string) {
        if (kind === Collection.Album)
            openAlbum(key);
        else if (kind === Collection.Artist)
            openArtist(key);
        else
            openPlaylist(key);
    }

    function back() {
        stack.pop();
    }

    Component.onCompleted: {
        rail.snap(large);
        select(0);
    }
    onLargeChanged: rail.snap(large)

    Component {
        id: homePage
        HomePage {}
    }
    Component {
        id: discoverPage
        PlaceholderPage {
            title: qsTr("发现")
            description: qsTr("推荐歌单、榜单与新歌，阶段 3 第 4 步实现。")
        }
    }
    Component {
        id: searchPage
        SearchPage {}
    }
    Component {
        id: libraryPage
        PlaceholderPage {
            title: qsTr("音乐库")
            description: qsTr("喜欢、歌单、收藏与最近播放，阶段 3 第 4 步实现。")
        }
    }
    Component {
        id: settingsPage
        SettingsPage {}
    }
    Component {
        id: playlistPage
        PlaylistPage {}
    }
    Component {
        id: albumPage
        AlbumPage {}
    }
    Component {
        id: artistPage
        ArtistPage {}
    }

    PageStack {
        id: stack
        // Pushed aside by a large window's rail; under the floating rail otherwise.
        x: shell.large ? rail.width : rail.collapsedWidth
        width: shell.width - x
        height: shell.height - miniPlayer.reservedHeight
    }

    MiniPlayer {
        id: miniPlayer
        x: stack.x + 12
        width: stack.width - 24
        y: shell.height - implicitHeight - bottomMargin
    }

    // Blocks the content while the rail floats over it.
    MouseArea {
        anchors.fill: parent
        visible: !shell.large && rail.progress > 0
        hoverEnabled: true
        onClicked: rail.expanded = false
        onWheel: wheel => wheel.accepted = true

        Rectangle {
            anchors.fill: parent
            color: Theme.color.scrim
            opacity: 0.32 * rail.progress
        }
    }

    NavigationRail {
        id: rail
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        modal: !shell.large
        header: Component {
            IconButton {
                x: 8
                glyph: rail.expanded ? Symbols.menu_open : Symbols.menu
                text: rail.expanded ? qsTr("收起导航栏") : qsTr("展开导航栏")
                onClicked: rail.expanded = !rail.expanded
            }
        }
        onActivated: index => shell.select(index)

        Repeater {
            model: shell.destinations
            delegate: NavigationRailItem {
                required property var modelData
                glyph: modelData.glyph
                text: modelData.text
            }
        }
    }

    Shortcut {
        sequence: "Esc"
        enabled: !shell.large && rail.expanded
        onActivated: rail.expanded = false
    }
    Shortcut {
        sequences: [StandardKey.Back]
        enabled: shell.canGoBack
        onActivated: shell.back()
    }
    TapHandler {
        acceptedButtons: Qt.BackButton
        onTapped: shell.back()
    }
}
