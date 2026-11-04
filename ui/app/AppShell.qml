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
    // The library page's collection tab (library.md §2), kept while the app runs.
    property int libraryTab: 0
    // The discover page's tab (discover.md §2), likewise.
    property int discoverTab: 0
    // The account's playlists (the rail's, and the add-to-playlist dialog's).
    readonly property LibraryPageModel library: myLibrary
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

    // The song order of the account's own playlist (playlist.md §13).
    function openPlaylistOrder(key: string) {
        shell.go(playlistOrderPage, {
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

    // The comments of a song, playlist or album (CommentsPageModel.Target); the
    // header shows `title`, `subtitle` and the cover the opening page knows.
    function openComments(target: int, key: string, title: string, subtitle: string,
                          coverKey: string) {
        shell.go(commentsPage, {
            shell: shell,
            target: target,
            targetKey: key,
            title: title,
            subtitle: subtitle,
            coverKey: coverKey
        });
    }

    // The song's comments, from a song list or the mini player.
    function openSongComments(key: string, title: string, artistText: string,
                              coverKey: string) {
        openComments(CommentsPageModel.Song, key, title, artistText, coverKey);
    }

    function openPodcast(key: string) {
        shell.go(podcastPage, {
            shell: shell,
            podcastKey: key
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

    // "收藏到歌单" for `keys`: picks one of the account's playlists (sign-in first).
    function addToPlaylist(keys: list<string>) {
        if (!App.session.signedIn) {
            loginDialog.open();
            return;
        }
        addToPlaylistDialog.songKeys = keys;
        addToPlaylistDialog.open();
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

    // The account's "我喜欢的音乐" playlist (the library page until it is known).
    function openLikedPlaylist() {
        if (App.library.likedPlaylistKey !== "")
            openPlaylist(App.library.likedPlaylistKey);
        else
            select(2);
    }

    // A discover tab (0 playlists, 1 charts, 2 artists, 3 new albums): the home page's
    // "查看全部".
    function openDiscover(tab: int) {
        discoverTab = tab;
        select(1);
    }

    function openRecent() {
        shell.go(recentPage, {
            shell: shell
        });
    }

    function openRanking() {
        shell.go(rankingPage, {
            shell: shell
        });
    }

    // Today's recommended songs, or a past day's ("yyyy-MM-dd", album-daily.md §2.1).
    function openDaily(date: string) {
        shell.go(dailySongsPage, {
            shell: shell,
            date: date ?? ""
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
        else if (kind === Collection.Podcast)
            openPodcast(key);
        else
            openPlaylist(key);
    }

    function back() {
        if (stack.depth < 2)
            return;
        // Unsaved changes to a song order: the page asks first, then comes back here.
        const order = stack.currentItem as PlaylistOrderPage;
        if (order && order.hasChanges && !order.leaving) {
            order.leave();
            return;
        }
        const leaving = stack.entries[stack.entries.length - 1];
        if (!stack.pop())
            return;
        // An editing page left is done with, not something to go forward to.
        if (leaving.component !== playlistOrderPage)
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
        DiscoverPage {}
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
        id: playlistOrderPage
        PlaylistOrderPage {}
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
        id: podcastPage
        PodcastPage {}
    }
    Component {
        id: commentsPage
        CommentsPage {}
    }
    Component {
        id: recentPage
        RecentPage {}
    }
    Component {
        id: rankingPage
        RankingPage {}
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
        onCommentsRequested: {
            const song = App.player.current;
            shell.openSongComments(song.key, song.title, song.artistText, song.coverKey);
        }
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
                onLikedRequested: shell.openLikedPlaylist()
                onRecentRequested: shell.openRecent()
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
            const songPage = stack.currentItem as SongCollectionPage;
            if (songPage && songPage.findable) {
                songPage.find();
            } else {
                shell.searchField.forceActiveFocus();
                shell.searchField.selectAll();
            }
        }
    }
    Shortcut {
        sequences: [StandardKey.Preferences, "Ctrl+,"]
        onActivated: shell.openSettings()
    }

    // Playback, from anywhere (docs/ui/pages/settings.md §4). Text fields and lists
    // keep the keys they use themselves (Ctrl+←/→ in a field, Ctrl+↑/↓ in a list).
    Shortcut {
        sequence: "Ctrl+P"
        enabled: App.player.current.key !== ""
        onActivated: App.player.playPause()
    }
    Shortcut {
        sequence: "Ctrl+Left"
        enabled: App.player.canPrevious
        onActivated: App.player.previous()
    }
    Shortcut {
        sequence: "Ctrl+Right"
        enabled: App.player.canNext
        onActivated: App.player.next()
    }
    Shortcut {
        sequence: "Ctrl+Up"
        onActivated: shell.stepVolume(0.1)
    }
    Shortcut {
        sequence: "Ctrl+Down"
        onActivated: shell.stepVolume(-0.1)
    }
    // The player reports a new volume a moment later: quick presses step from the
    // last one asked for, not from the volume shown.
    property real pendingVolume: -1
    function stepVolume(delta: real): void {
        const from = pendingVolume >= 0 ? pendingVolume : App.player.volume;
        pendingVolume = Math.max(0, Math.min(1, Math.round((from + delta) * 10) / 10));
        App.player.setVolume(pendingVolume);
        volumeSettled.restart();
    }
    Timer {
        id: volumeSettled
        interval: 1000
        onTriggered: shell.pendingVolume = -1
    }
    Shortcut {
        sequence: "Ctrl+L"
        enabled: App.player.current.key !== "" && !App.player.current.voice
        onActivated: {
            if (!App.session.signedIn) {
                shell.openLogin();
                return;
            }
            const key = App.player.current.key;
            App.library.setLiked(key, !App.library.isLiked(key));
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

    AddToPlaylistDialog {
        id: addToPlaylistDialog
        library: myLibrary
    }

    // What App.library has to say (liked, added to a playlist, failures).
    Connections {
        target: App.library
        function onNotice(message: string) {
            shell.notify(message);
        }
        function onPlaylistChanged() {
            shell.refreshLibrary();
        }
    }

    LoginDialog {
        id: loginDialog
    }
}
