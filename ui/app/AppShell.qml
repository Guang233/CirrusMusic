pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components
import Cirrus.Presentation

// The shell (docs/ui/pages/shell.md): navigation rail and a rounded content panel
// with the page stack, in a frame that the top bar (TopBar, in the window's title
// bar row) continues; the mini player sits under the panel. Large windows (≥ 1200)
// keep the rail expanded beside the panel and push it when toggled; narrower ones
// keep it collapsed and expand it over the panel with a scrim. Crossing the
// breakpoint switches without animation (motion-choreography.md §5). Pages get
// `shell` and navigate through it; back and forward keep a browser-like history.
Item {
    id: shell

    readonly property bool large: width >= 1200
    readonly property bool canGoBack: stack.depth > 1
    readonly property bool canGoForward: forwardEntries.length > 0
    // Width the rail takes beside the panel (the floating rail does not push).
    readonly property real navWidth: large ? rail.width : rail.collapsedWidth
    readonly property real railProgress: rail.progress
    readonly property bool railExpanded: rail.expanded
    readonly property bool searching: (stack.currentItem as SearchPage) !== null
    readonly property SearchModel searchModel: searchModelObject
    // The search input, in the top bar; Main connects it.
    property SearchField searchField
    // History like a browser's: every page visited is an entry of the page stack, and
    // `sections` holds the rail destination of each entry (for the rail's selection).
    // Back returns to the page visited before; forward revisits pages left with back
    // ({component, properties, section}) until something new is opened.
    property var sections: []
    property var forwardEntries: []
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

    function toggleRail() {
        rail.expanded = !rail.expanded;
    }

    // A new page on top, in the current section; forward history ends here.
    function go(component: Component, properties: var) {
        visit(component, properties, rail.currentIndex, PageStack.Kind.Forward);
    }

    // A rail destination, as a new history entry (fading in). Nothing happens when it
    // is the page already shown.
    function select(index: int) {
        if (!large)
            rail.expanded = false;
        const top = stack.entries[stack.entries.length - 1];
        if (top && top.component === destinations[index].page)
            return;
        visit(destinations[index].page, {
            shell: shell
        }, index, PageStack.Kind.Fade);
    }

    function visit(component: Component, properties: var, section: int, transition: int) {
        forwardEntries = [];
        sections = sections.concat([section]);
        rail.currentIndex = section;
        stack.push(component, properties, transition);
    }

    function openPlaylist(key: string) {
        shell.go(playlistPage, {
            shell: shell,
            playlistKey: key
        });
    }

    function openAlbum(key: string) {
        shell.go(albumPage, {
            shell: shell,
            albumKey: key
        });
    }

    function openArtist(key: string) {
        shell.go(artistPage, {
            shell: shell,
            artistKey: key
        });
    }

    function openSearch() {
        if (!searching)
            shell.go(searchPage, {
                shell: shell,
                search: shell.searchModel
            });
    }

    // Searches from anywhere: the search page shows the results.
    function search(text: string) {
        openSearch();
        searchModelObject.submit(text);
    }

    // A short message at the bottom of the content panel (Snackbar).
    function notify(text: string) {
        snackbar.show(text);
    }

    // The account's playlists changed (saved, created, deleted): the rail reloads.
    function refreshLibrary() {
        myLibrary.refresh();
    }

    // Keyboard focus back to the page, e.g. after Esc in the search field.
    function focusContent() {
        stack.currentItem?.forceActiveFocus();
    }

    // The field shows what the search page shows, and nothing elsewhere.
    onSearchingChanged: {
        if (!searchField)
            return;
        if (!searching)
            searchField.clear();
        else if (searchField.text === "")
            searchField.text = searchModel.query;
    }

    SearchModel {
        id: searchModelObject
        text: shell.searchField ? shell.searchField.text : ""
    }

    function openSettings() {
        if ((stack.currentItem as SettingsPage) === null)
            shell.go(settingsPage, {
                shell: shell
            });
    }

    // The library page on one of its tabs (0 liked songs … 4 recently played).
    function openLibrary(tab: int) {
        shell.go(libraryPage, {
            shell: shell,
            initialTab: tab
        });
    }

    function openDaily() {
        shell.go(dailySongsPage, {
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
        if (stack.depth < 2)
            return;
        const leaving = stack.entries[stack.entries.length - 1];
        if (!stack.pop())
            return;
        forwardEntries = forwardEntries.concat([{
                component: leaving.component,
                properties: leaving.properties,
                section: sections[sections.length - 1]
            }]);
        sections = sections.slice(0, -1);
        rail.currentIndex = sections[sections.length - 1];
    }

    function forward() {
        if (!canGoForward)
            return;
        const next = forwardEntries[forwardEntries.length - 1];
        forwardEntries = forwardEntries.slice(0, -1);
        sections = sections.concat([next.section]);
        rail.currentIndex = next.section;
        stack.push(next.component, next.properties,
                   next.component === destinations[next.section].page ? PageStack.Kind.Fade
                                                                      : PageStack.Kind.Forward);
    }

    Component.onCompleted: {
        rail.snap(large);
        sections = [0];
        stack.replaceAll(homePage, {
            shell: shell
        });
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

    // The content panel: rounded, on the frame color (Main sets the window's surface
    // to SurfaceContainer); the corners are masked so scrolled pages stay inside.
    Rectangle {
        id: panel
        x: shell.navWidth
        width: shell.width - x - 8
        height: shell.height - 8 - miniPlayer.reservedHeight
        radius: Theme.shape.largeIncreased
        color: Theme.color.surface

        PageStack {
            id: stack
            anchors.fill: parent
            clip: true
        }

        Repeater {
            model: 4
            PanelCorner {
                required property int index
                radius: panel.radius
                color: Theme.color.surfaceContainer
                x: index === 1 || index === 2 ? panel.width - radius : 0
                y: index >= 2 ? panel.height - radius : 0
                rotation: index * 90
            }
        }

        Snackbar {
            id: snackbar
            anchors.bottom: parent.bottom
        }
    }

    MiniPlayer {
        id: miniPlayer
        x: panel.x
        width: panel.width
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
        topInset: 8
        restColor: Theme.color.surfaceContainer
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
        sequence: "Ctrl+K"
        onActivated: {
            shell.searchField.forceActiveFocus();
            shell.searchField.selectAll();
        }
    }
    // Pages with an in-page find take Ctrl+F; elsewhere it is the search too.
    Shortcut {
        sequences: [StandardKey.Find]
        onActivated: {
            const playlist = stack.currentItem as PlaylistPage;
            if (playlist) {
                playlist.find();
            } else {
                shell.searchField.forceActiveFocus();
                shell.searchField.selectAll();
            }
        }
    }
    Shortcut {
        sequences: [StandardKey.Back]
        enabled: shell.canGoBack
        onActivated: shell.back()
    }
    Shortcut {
        sequences: [StandardKey.Forward]
        enabled: shell.canGoForward
        onActivated: shell.forward()
    }
    TapHandler {
        acceptedButtons: Qt.BackButton | Qt.ForwardButton
        onTapped: (eventPoint, button) => {
            if (button === Qt.BackButton)
                shell.back();
            else
                shell.forward();
        }
    }

    LoginDialog {
        id: loginDialog
    }
}
