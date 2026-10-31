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
// Pages get `shell` and navigate through it. The expanded rail also lists the
// account's playlists; search lives in the title bar row (docs/ui/pages/shell.md).
Item {
    id: shell

    readonly property bool large: width >= 1200
    readonly property bool canGoBack: stack.depth > 1
    // Left edge of the page area, for content lined up with it (the title bar search).
    readonly property real contentX: stack.x
    readonly property bool searching: (stack.currentItem as SearchPage) !== null
    // The playlist the current page shows, for the rail's playlist rows.
    readonly property string openPlaylistKey: (stack.currentItem as PlaylistPage)?.playlistKey ?? ""
    readonly property var destinations: [{
            glyph: Symbols.home,
            text: qsTr("首页"),
            page: homePage
        }, {
            glyph: Symbols.explore,
            text: qsTr("发现"),
            page: discoverPage
        }, {
            glyph: Symbols.library_music,
            text: qsTr("音乐库"),
            page: libraryPage
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

    function openSearch() {
        if (!searching)
            stack.push(searchPage, {
                shell: shell
            });
    }

    function openSettings() {
        if ((stack.currentItem as SettingsPage) === null)
            stack.push(settingsPage, {
                shell: shell
            });
    }

    // The library page on one of its tabs (0 liked songs … 4 recently played).
    function openLibrary(tab: int) {
        stack.push(libraryPage, {
            shell: shell,
            initialTab: tab
        });
    }

    function openDaily() {
        stack.push(dailySongsPage, {
            shell: shell
        });
    }

    // QR sign-in in a dialog, over the current page.
    function openLogin() {
        if (!App.session.signedIn)
            loginDialog.open();
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
        FeedPage {
            title: qsTr("发现")
            feed: Feed.Discover
        }
    }
    Component {
        id: searchPage
        SearchPage {}
    }
    Component {
        id: libraryPage
        LibraryPage {}
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
    Component {
        id: dailySongsPage
        DailySongsPage {}
    }

    PageStack {
        id: stack
        // Scrolled content must not draw over the title bar row above.
        clip: true
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

    // The account's playlists for the rail.
    LibraryPageModel {
        id: myLibrary
    }

    NavigationRail {
        id: rail
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        modal: !shell.large
        minimumExpandedWidth: 272
        header: Component {
            IconButton {
                x: 8
                glyph: rail.expanded ? Symbols.menu_open : Symbols.menu
                text: rail.expanded ? qsTr("收起导航栏") : qsTr("展开导航栏")
                onClicked: rail.expanded = !rail.expanded
            }
        }
        expandedContent: Component {
            RailPlaylists {
                library: myLibrary
                selectedKey: shell.openPlaylistKey
                onPlaylistRequested: key => shell.openPlaylist(key)
                onLibraryRequested: tab => shell.openLibrary(tab)
                onSignInRequested: shell.openLogin()
            }
        }
        footer: Component {
            RailAccount {
                progress: rail.progress
                onSignInRequested: shell.openLogin()
                onSettingsRequested: shell.openSettings()
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
        sequences: ["Ctrl+K", StandardKey.Find]
        onActivated: shell.openSearch()
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

    LoginDialog {
        id: loginDialog
    }
}
