pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Presentation

// Songs as a desktop table (docs/ui/pages/playlist.md §3–§5): number, cover with
// title and artists, album, liked, duration. The column titles sort. `headerContent`
// scrolls above the column titles, which then stay pinned `pinnedTop` below the top.
// Mouse: a click selects (Ctrl toggles, Shift extends), a double click or the play
// button that replaces the number on hover plays the shown order from that row.
// Keyboard: the table is one Tab stop; arrows, PageUp/PageDown and Home/End move
// the current row (Shift extends the selection, Ctrl moves without selecting),
// Enter plays it, Ctrl+A selects all, the menu key or Shift+F10 opens its menu.
// Typed characters go to `typed` (the page's in-page find). `removable` adds
// removing (menu, Delete) through `removeRequested`. Reordering has its own page
// (playlist.md §13), away from accidental drags.
//   SongTable { songs: SortedSongsModel { source: pageModel.songs } }
Item {
    id: table

    required property SortedSongsModel songs
    property Component headerContent
    property real pinnedTop: 0
    property real maxContentWidth: 1200
    property bool showAlbumAction: true
    property bool showLiked: true
    // The page's own playlist: songs can be removed.
    property bool removable: false
    readonly property ListView view: list
    readonly property real rowPitch: 56 + list.spacing
    // The rows on screen (rows start at content y 0, under the header), for loading
    // their details.
    readonly property int firstVisibleRow: Math.max(0, Math.floor(list.contentY / rowPitch))
    readonly property int lastVisibleRow: Math.max(0, Math.floor((list.contentY + list.height)
                                                                 / rowPitch))
    // Bars pinned over the top of the table leave the scroll bar this much room.
    readonly property real scrollBarWidth: 12
    // How far the content has scrolled from its top.
    readonly property real scrolled: list.contentY - list.originY

    // Rows: x and width inside the panel; columns: x and width inside a row.
    readonly property real rowX: Math.max(24, (width - maxContentWidth) / 2)
    readonly property real rowWidth: Math.min(width - 48, maxContentWidth)
    readonly property bool showAlbum: rowWidth >= 640
    readonly property real numberWidth: 40
    readonly property real titleX: 16 + numberWidth + 16
    readonly property real durationWidth: 56
    readonly property real moreWidth: 40
    readonly property real likedWidth: showLiked ? 40 : 0
    readonly property real flexWidth: rowWidth - titleX - 16 - durationWidth - moreWidth - 16
                                      - (showAlbum ? 16 : 0) - (showLiked ? likedWidth + 16 : 0)
    readonly property real albumWidth: showAlbum ? Math.round(flexWidth * 0.4) : 0
    readonly property real titleWidth: flexWidth - albumWidth
    readonly property real albumX: titleX + titleWidth + 16
    readonly property real durationX: rowWidth - 16 - moreWidth - durationWidth
    readonly property real likedX: durationX - 16 - likedWidth

    // Selected keys (key -> true), replaced as a whole so bindings see each change.
    property var selectedKeys: ({})
    readonly property int selectionCount: Object.keys(selectedKeys).length
    // Where Shift+click and Shift+arrows extend the selection from.
    property int anchorRow: -1
    // The keyboard's row. Not ListView.currentIndex: the list would move the focus
    // to the current delegate, taking it from the header's controls (the find field).
    property int currentRow: -1
    // The current row shows its focus ring only after keyboard use.
    property bool keyboardUsed: false

    signal albumRequested(string key)
    signal artistRequested(string key)
    signal typed(string text)
    signal signInRequested
    signal addToPlaylistRequested(list<string> keys)
    signal removeRequested(list<string> keys)



    // The songs an action on `row` is for: the selection when the row is in it.
    function actionKeys(row: int): list<string> {
        const key = songs.keyAt(row);
        return selectedKeys[key] ? selectedInOrder() : [key];
    }
    function setLiked(key: string, liked: bool): void {
        if (!App.session.signedIn)
            signInRequested();
        else
            App.library.setLiked(key, liked);
    }

    // Plays the shown order from `row`, leaving out songs the service has no details for.
    function play(row: int): void {
        if (row < 0 || row >= songs.count || isMissing(row))
            return;
        const key = songs.keyAt(row);
        const keys = songs.keys().filter((k, i) => !isMissing(i));
        App.player.playSongs(keys, keys.indexOf(key));
    }
    function isMissing(row: int): bool {
        return songs.data(songs.index(row, 0), SongListModel.MissingRole) === true;
    }
    function selectOnly(row: int): void {
        const keys = {};
        if (row >= 0)
            keys[songs.keyAt(row)] = true;
        selectedKeys = keys;
        anchorRow = row;
    }
    function toggle(row: int): void {
        const keys = Object.assign({}, selectedKeys);
        const key = songs.keyAt(row);
        if (keys[key])
            delete keys[key];
        else
            keys[key] = true;
        selectedKeys = keys;
        anchorRow = row;
    }
    function selectRange(from: int, to: int): void {
        const keys = {};
        const first = Math.max(0, Math.min(from, to));
        for (let row = first; row <= Math.max(from, to); ++row)
            keys[songs.keyAt(row)] = true;
        selectedKeys = keys;
    }
    function selectAll(): void {
        const keys = {};
        for (const key of songs.keys())
            keys[key] = true;
        selectedKeys = keys;
    }
    // Selected keys in the shown order.
    function selectedInOrder(): list<string> {
        return songs.keys().filter(key => table.selectedKeys[key] === true);
    }
    // Focus on the table itself. The list is a focus scope that hands the focus back
    // to the header control it last held (the find field), so that one lets go first.
    function takeFocus(reason: int): void {
        list.forceActiveFocus(reason);
        // The header's nested scopes (Loader) each hold on to their child.
        for (let i = 0; i < 8; ++i) {
            const held = list.Window.activeFocusItem;
            if (!held || held === list)
                break;
            held.focus = false;
            list.forceActiveFocus(reason);
        }
    }
    function focusTable(): void {
        if (currentRow < 0 && list.count > 0)
            currentRow = 0;
        takeFocus(Qt.TabFocusReason);
    }

    // Keyboard move: Shift extends from the anchor, Ctrl only moves.
    function moveTo(row: int, modifiers: int): void {
        if (list.count === 0)
            return;
        row = Math.max(0, Math.min(list.count - 1, row));
        currentRow = row;
        list.positionViewAtIndex(row, ListView.Contain);
        if (modifiers & Qt.ShiftModifier) {
            if (anchorRow < 0)
                anchorRow = row;
            selectRange(anchorRow, row);
        } else if (!(modifiers & Qt.ControlModifier)) {
            selectOnly(row);
        }
    }
    function openMenu(row: int, anchor: Item, x: real, y: real): void {
        if (!selectedKeys[songs.keyAt(row)])
            selectOnly(row);
        currentRow = row;
        songMenu.openFor(row, anchor, x, y);
    }

    // Selected rows that left the list (reloaded, filtered out) are no longer selected.
    Connections {
        target: table.songs
        function onModelReset() {
            table.selectedKeys = {};
            table.anchorRow = -1;
        }
        // Removed songs leave the selection.
        function onRowsRemoved() {
            const keys = {};
            for (const key of table.songs.keys()) {
                if (table.selectedKeys[key])
                    keys[key] = true;
            }
            table.selectedKeys = keys;
            table.currentRow = Math.min(table.currentRow, table.songs.count - 1);
            table.anchorRow = -1;
        }
        function onFilterTextChanged() {
            table.selectedKeys = {};
            table.anchorRow = -1;
            table.currentRow = table.songs.count > 0 ? 0 : -1;
        }
    }

    // A sortable column title.
    component SortTitle: T.AbstractButton {
        id: sortTitle

        required property int field
        property bool alignRight: false
        // The pinned copy is not a Tab stop (the header's titles are).
        property bool pinned: false
        readonly property bool active: table.songs.sortField === field

        height: 40
        focusPolicy: pinned ? Qt.NoFocus : Qt.TabFocus
        Accessible.name: text
        onClicked: table.songs.cycleSort(field)

        contentItem: Item {
            Row {
                x: sortTitle.alignRight ? parent.width - width : 0
                anchors.verticalCenter: parent.verticalCenter
                spacing: 4
                layoutDirection: sortTitle.alignRight ? Qt.RightToLeft : Qt.LeftToRight
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: sortTitle.text
                    color: sortTitle.active || sortTitle.hovered ? Theme.color.onSurface
                                                                : Theme.color.onSurfaceVariant
                    font: Theme.type.labelMedium.font
                }
                Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: sortTitle.active
                    glyph: table.songs.descending ? Symbols.arrow_downward : Symbols.arrow_upward
                    size: 16
                    color: Theme.color.onSurface
                }
            }
        }
        background: FocusRing {
            shown: sortTitle.visualFocus
            containerRadius: Theme.shape.small
            offset: -2
        }
    }



    // The column titles, in the header and pinned while the header is scrolled away.
    component ColumnTitles: Item {
        id: titles

        property bool pinned: false

        width: table.rowWidth
        height: 40

        Text {
            x: 16
            width: table.numberWidth
            anchors.verticalCenter: parent.verticalCenter
            horizontalAlignment: Text.AlignRight
            text: "#"
            color: Theme.color.onSurfaceVariant
            font: Theme.type.labelMedium.font
        }
        SortTitle {
            x: table.titleX
            width: table.titleWidth
            pinned: titles.pinned
            field: SortedSongsModel.Title
            text: qsTr("标题")
        }
        SortTitle {
            x: table.albumX
            width: table.albumWidth
            visible: table.showAlbum
            pinned: titles.pinned
            field: SortedSongsModel.Album
            text: qsTr("专辑")
        }
        Text {
            x: table.likedX
            width: table.likedWidth
            anchors.verticalCenter: parent.verticalCenter
            visible: table.showLiked
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("喜欢")
            color: Theme.color.onSurfaceVariant
            font: Theme.type.labelMedium.font
        }
        SortTitle {
            x: table.durationX
            width: table.durationWidth
            alignRight: true
            pinned: titles.pinned
            field: SortedSongsModel.Duration
            text: qsTr("时长")
        }
    }

    // A name that opens something: underlined while hovered.
    component LinkText: Text {
        id: link

        signal activated

        elide: Text.ElideRight
        font.underline: linkHover.hovered
        HoverHandler {
            id: linkHover
            cursorShape: Qt.PointingHandCursor
        }
        TapHandler {
            onTapped: link.activated()
        }
    }

    ListView {
        id: list

        anchors.fill: parent
        model: table.songs
        clip: true
        spacing: 2
        bottomMargin: 24
        // Overshoots and springs back at the ends (touchpad, touch, and WheelScroller).
        boundsBehavior: Flickable.DragAndOvershootBounds
        // Wheel and touch scroll; mouse drags don't (desktop).
        acceptedButtons: Qt.NoButton
        keyNavigationEnabled: false
        highlightFollowsCurrentItem: false
        activeFocusOnTab: true
        Accessible.role: Accessible.Table
        ScrollBar.vertical: ScrollBar {}
        WheelScroller {
            flickable: list
        }

        // At the top until the user scrolls: a header that grows once its data arrives
        // moves originY, and the view would otherwise keep showing the old top.
        property bool pinnedToTop: true
        onContentYChanged: pinnedToTop = contentY <= originY + 1
        onOriginYChanged: {
            if (pinnedToTop)
                contentY = originY;
        }

        // Kept at -1 (see currentRow); the list sets 0 itself when rows arrive.
        currentIndex: -1
        onCurrentIndexChanged: {
            if (currentIndex !== -1)
                currentIndex = -1;
        }
        onActiveFocusChanged: {
            if (activeFocus && table.currentRow < 0 && count > 0)
                table.currentRow = 0;
        }

        header: Column {
            width: list.width
            Loader {
                width: parent.width
                sourceComponent: table.headerContent
            }
            ColumnTitles {
                x: table.rowX
            }
        }

        Keys.onPressed: event => {
            // Keys from the header's controls (find field, sort titles) bubble up here.
            if (list.Window.activeFocusItem !== list) {
                event.accepted = false;
                return;
            }
            table.keyboardUsed = true;
            const rows = Math.max(1, Math.floor(list.height / 58) - 1);
            const ctrl = event.modifiers & Qt.ControlModifier;
            switch (event.key) {
            case Qt.Key_Up:
                table.moveTo(table.currentRow - 1, event.modifiers);
                break;
            case Qt.Key_Down:
                table.moveTo(table.currentRow + 1, event.modifiers);
                break;
            case Qt.Key_Delete:
                if (table.removable && table.currentRow >= 0)
                    table.removeRequested(table.actionKeys(table.currentRow));
                else
                    event.accepted = false;
                break;
            case Qt.Key_PageUp:
                table.moveTo(table.currentRow - rows, event.modifiers);
                break;
            case Qt.Key_PageDown:
                table.moveTo(table.currentRow + rows, event.modifiers);
                break;
            case Qt.Key_Home:
                table.moveTo(0, event.modifiers);
                list.positionViewAtBeginning();
                break;
            case Qt.Key_End:
                table.moveTo(list.count - 1, event.modifiers);
                break;
            case Qt.Key_Return:
            case Qt.Key_Enter:
                table.play(table.currentRow);
                break;
            case Qt.Key_Space:
                if (ctrl && table.currentRow >= 0)
                    table.toggle(table.currentRow);
                else
                    event.accepted = false;
                break;
            case Qt.Key_A:
                if (ctrl)
                    table.selectAll();
                else
                    event.accepted = false;
                break;
            case Qt.Key_Menu:
            case Qt.Key_F10:
                if (table.currentRow >= 0 && (event.key === Qt.Key_Menu
                                              || event.modifiers & Qt.ShiftModifier)) {
                    const current = list.itemAtIndex(table.currentRow);
                    if (current)
                        table.openMenu(table.currentRow, current, table.rowX + 48,
                                       current.height);
                } else {
                    event.accepted = false;
                }
                break;
            case Qt.Key_Escape:
                if (table.selectionCount > 0)
                    table.selectedKeys = {};
                else
                    event.accepted = false;
                break;
            default:
                // Printable text without shortcuts goes to the in-page find.
                if (event.text.length > 0 && event.text.trim().length > 0
                        && !(event.modifiers & (Qt.ControlModifier | Qt.AltModifier
                                                | Qt.MetaModifier))) {
                    table.typed(event.text);
                } else {
                    event.accepted = false;
                }
            }
        }

        delegate: Item {
            id: row

            required property int index
            required property string key
            required property string title
            required property string secondaryTitle
            required property var artists
            required property string artistText
            required property string albumKey
            required property string albumTitle
            required property string coverKey
            required property string durationText
            required property bool loaded
            required property bool unavailable
            required property bool current
            required property bool liked
            required property bool missing

            readonly property bool selected: table.selectedKeys[key] === true
            readonly property bool hovered: hover.hovered
            readonly property bool playing: current && App.player.status === Playback.Playing
            readonly property bool first: index === 0
            readonly property bool last: index === list.count - 1
            readonly property bool showsFocus: list.activeFocus && table.keyboardUsed
                                               && index === table.currentRow
            readonly property color strongColor: current ? Theme.color.primary
                                               : Theme.mix(Theme.color.onSurface,
                                                           Theme.color.onSecondaryContainer,
                                                           selection.value)
            readonly property color mutedColor: Theme.mix(Theme.color.onSurfaceVariant,
                                                          Theme.color.onSecondaryContainer,
                                                          selection.value)

            // ListView puts delegates at x 0: the row's body is inset inside.
            width: list.width
            height: 56
            Accessible.role: Accessible.Row
            Accessible.name: loaded ? qsTr("%1，%2").arg(title).arg(artistText)
                           : missing ? qsTr("歌曲信息暂时无法获取") : qsTr("正在加载")
            Accessible.selected: selected

            Item {
                id: rowBody
                x: table.rowX
                width: table.rowWidth
                height: parent.height

                Spring {
                    id: selection
                    target: row.selected ? 1 : 0
                    spec: Theme.motion.defaultEffects
                }
                Spring {
                    id: corner
                    target: row.selected ? Theme.shape.large
                          : row.hovered || row.showsFocus ? Theme.shape.medium
                          : Theme.shape.extraSmall
                    spec: Theme.motion.fastSpatial
                    threshold: 0.1
                }

                Rectangle {
                    id: container
                    readonly property real inner: Math.max(0, corner.value)
                    readonly property real topRadius: row.first ? Math.max(Theme.shape.large, inner)
                                                                : inner
                    readonly property real bottomRadius: row.last ? Math.max(Theme.shape.large, inner)
                                                                  : inner
                    anchors.fill: parent
                    color: Theme.mix(Theme.color.surfaceContainer, Theme.color.secondaryContainer,
                                     selection.value)
                    topLeftRadius: topRadius
                    topRightRadius: topRadius
                    bottomLeftRadius: bottomRadius
                    bottomRightRadius: bottomRadius

                    StateLayer {
                        anchors.fill: parent
                        topLeftRadius: container.topRadius
                        topRightRadius: container.topRadius
                        bottomLeftRadius: container.bottomRadius
                        bottomRightRadius: container.bottomRadius
                        hovered: row.hovered
                        pressed: tap.pressed
                        pressX: tap.point.position.x
                        pressY: tap.point.position.y
                    }
                    FocusRing {
                        shown: row.showsFocus
                        offset: -5
                        containerTopLeftRadius: container.topRadius
                        containerTopRightRadius: container.topRadius
                        containerBottomLeftRadius: container.bottomRadius
                        containerBottomRightRadius: container.bottomRadius
                    }
                }

                HoverHandler {
                    id: hover
                }
                TapHandler {
                    id: tap
                    onTapped: (eventPoint, button) => {
                        table.keyboardUsed = false;
                        table.currentRow = row.index;
                        table.takeFocus(Qt.MouseFocusReason);
                        if (point.modifiers & Qt.ShiftModifier && table.anchorRow >= 0)
                            table.selectRange(table.anchorRow, row.index);
                        else if (point.modifiers & Qt.ControlModifier)
                            table.toggle(row.index);
                        else
                            table.selectOnly(row.index);
                    }
                    onDoubleTapped: table.play(row.index)
                }
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: eventPoint => {
                        table.keyboardUsed = false;
                        table.takeFocus(Qt.MouseFocusReason);
                        table.openMenu(row.index, rowBody, eventPoint.position.x,
                                       eventPoint.position.y);
                    }
                }

                Item {
                    anchors.fill: parent
                    opacity: row.unavailable || row.missing ? Theme.state.disabledContent : 1

                    // Number; the play (or pause) button on hover; the equalizer while
                    // this song plays.
                    Item {
                        x: 16
                        width: table.numberWidth
                        height: parent.height
                        Text {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            visible: !row.hovered && !row.current
                            text: row.index + 1
                            color: row.mutedColor
                            font: Fonts.tabular(Theme.type.labelLarge.font)
                        }
                        Icon {
                            anchors.right: parent.right
                            anchors.rightMargin: 2
                            anchors.verticalCenter: parent.verticalCenter
                            visible: !row.hovered && row.current
                            glyph: Symbols.graphic_eq
                            size: 20
                            color: Theme.color.primary
                        }
                        IconButton {
                            anchors.right: parent.right
                            anchors.rightMargin: -6
                            anchors.verticalCenter: parent.verticalCenter
                            visible: row.hovered && row.loaded
                            size: Md.XSmall
                            focusPolicy: Qt.NoFocus
                            showToolTip: false
                            glyph: row.playing ? Symbols.pause : Symbols.play_arrow
                            iconFill: 1
                            text: row.playing ? qsTr("暂停") : qsTr("播放")
                            onClicked: {
                                if (row.current)
                                    App.player.playPause();
                                else
                                    table.play(row.index);
                            }
                        }
                    }

                    CoverArt {
                        x: table.titleX
                        width: 40
                        height: 40
                        anchors.verticalCenter: parent.verticalCenter
                        coverKey: row.coverKey
                    }

                    Column {
                        x: table.titleX + 52
                        width: table.titleWidth - 52
                        anchors.verticalCenter: parent.verticalCenter
                        visible: row.loaded
                        spacing: 2

                        Text {
                            width: parent.width
                            elide: Text.ElideRight
                            textFormat: Text.StyledText
                            text: {
                                const escape = s => s.replace(/&/g, "&amp;").replace(/</g, "&lt;")
                                                     .replace(/>/g, "&gt;");
                                const alias = row.secondaryTitle === "" ? ""
                                            : " <font color=\"" + row.mutedColor + "\">"
                                              + escape(qsTr("（%1）").arg(row.secondaryTitle))
                                              + "</font>";
                                return escape(row.title) + alias;
                            }
                            color: row.strongColor
                            font: Theme.type.bodyLarge.font
                        }
                        // Each artist opens their page.
                        Row {
                            width: parent.width
                            clip: true
                            Repeater {
                                model: row.artists
                                delegate: Row {
                                    id: artist
                                    required property int index
                                    required property var modelData
                                    LinkText {
                                        text: artist.modelData.name
                                        color: row.mutedColor
                                        font: Theme.type.bodyMedium.font
                                        enabled: artist.modelData.key !== ""
                                        onActivated: table.artistRequested(artist.modelData.key)
                                    }
                                    Text {
                                        visible: artist.index < row.artists.length - 1
                                        text: qsTr(" / ")
                                        color: row.mutedColor
                                        font: Theme.type.bodyMedium.font
                                    }
                                }
                            }
                        }
                    }

                    // The service has no details for this song.
                    Text {
                        x: table.titleX + 52
                        width: table.titleWidth - 52
                        anchors.verticalCenter: parent.verticalCenter
                        visible: row.missing
                        text: qsTr("歌曲信息暂时无法获取")
                        elide: Text.ElideRight
                        color: Theme.color.onSurfaceVariant
                        font: Theme.type.bodyMedium.font
                    }

                    // Details still on their way: placeholder bars.
                    Column {
                        x: table.titleX + 52
                        anchors.verticalCenter: parent.verticalCenter
                        visible: !row.loaded && !row.missing
                        spacing: 8
                        Skeleton {
                            width: Math.min(200, table.titleWidth - 52)
                            height: 14
                        }
                        Skeleton {
                            width: Math.min(120, table.titleWidth - 52)
                            height: 12
                        }
                    }

                    LinkText {
                        x: table.albumX
                        width: Math.min(implicitWidth, table.albumWidth)
                        anchors.verticalCenter: parent.verticalCenter
                        visible: table.showAlbum && row.loaded
                        enabled: row.albumKey !== "" && table.showAlbumAction
                        text: row.albumTitle
                        color: row.mutedColor
                        font: Theme.type.bodyMedium.font
                        onActivated: table.albumRequested(row.albumKey)
                    }

                    IconButton {
                        x: table.likedX + (table.likedWidth - width) / 2
                        anchors.verticalCenter: parent.verticalCenter
                        visible: table.showLiked && row.loaded
                        size: Md.XSmall
                        focusPolicy: Qt.NoFocus
                        checkable: true
                        checked: row.liked
                        iconFill: row.liked ? 1 : 0
                        glyph: Symbols.favorite
                        text: row.liked ? qsTr("取消喜欢") : qsTr("喜欢")
                        onToggled: {
                            table.setLiked(row.key, checked);
                            checked = Qt.binding(() => row.liked);
                        }
                    }

                    Text {
                        x: table.durationX
                        width: table.durationWidth
                        anchors.verticalCenter: parent.verticalCenter
                        visible: row.loaded
                        horizontalAlignment: Text.AlignRight
                        text: row.durationText
                        color: row.mutedColor
                        font: Fonts.tabular(Theme.type.labelLarge.font)
                    }
                }

                IconButton {
                    id: more
                    x: parent.width - 16 - table.moreWidth + (table.moreWidth - width) / 2
                    anchors.verticalCenter: parent.verticalCenter
                    size: Md.XSmall
                    focusPolicy: Qt.NoFocus
                    opacity: row.hovered || row.showsFocus || songMenu.row === row.index ? 1 : 0
                    enabled: row.loaded
                    glyph: Symbols.more_vert
                    text: qsTr("更多")
                    onClicked: table.openMenu(row.index, more, 0, more.height)
                }
            }
        }
    }

    ColumnTitles {
        x: table.rowX
        y: table.pinnedTop
        pinned: true
        // Once the header's own titles scroll under the pinned top.
        visible: list.headerItem !== null
                 && list.headerItem.y + list.headerItem.height - 40 - list.contentY < table.pinnedTop

        // Covers the rows scrolling under it, for the pointer too; not the scroll bar.
        Rectangle {
            z: -1
            x: -table.rowX
            width: table.width - table.scrollBarWidth
            height: parent.height
            color: Theme.color.surface
            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.AllButtons
            }
            HoverHandler {
                blocking: true
            }
        }
    }

    Menu {
        id: songMenu

        // The row the menu is open for; -1 when closed.
        property int row: -1
        property string key
        property string albumKey
        property var artists: []
        property bool liked: false
        // The songs the actions are for (the selection when the row is in it).
        property list<string> keys
        readonly property int songCount: keys.length

        function openFor(target: int, anchor: Item, x: real, y: real): void {
            row = target;
            const index = table.songs.index(target, 0);
            key = table.songs.keyAt(target);
            keys = table.actionKeys(target);
            albumKey = table.songs.data(index, SongListModel.AlbumKeyRole) ?? "";
            artists = table.songs.data(index, SongListModel.ArtistsRole) ?? [];
            liked = table.songs.data(index, SongListModel.LikedRole) ?? false;
            popup(anchor, x, y);
        }

        onClosed: row = -1

        MenuItem {
            glyph: Symbols.play_arrow
            text: songMenu.songCount > 1 ? qsTr("播放选中的 %1 首").arg(songMenu.songCount)
                                         : qsTr("播放")
            onTriggered: {
                if (songMenu.songCount > 1)
                    App.player.playSongs(songMenu.keys, 0);
                else
                    table.play(songMenu.row);
            }
        }
        MenuSeparator {}
        MenuItem {
            visible: songMenu.songCount === 1
            height: visible ? implicitHeight : 0
            glyph: Symbols.favorite
            text: songMenu.liked ? qsTr("取消喜欢") : qsTr("喜欢")
            onTriggered: table.setLiked(songMenu.key, !songMenu.liked)
        }
        MenuItem {
            glyph: Symbols.playlist_add
            text: songMenu.songCount > 1 ? qsTr("收藏选中的 %1 首到歌单…").arg(songMenu.songCount)
                                         : qsTr("收藏到歌单…")
            onTriggered: table.addToPlaylistRequested(songMenu.keys)
        }
        MenuItem {
            visible: table.removable
            height: visible ? implicitHeight : 0
            glyph: Symbols.playlist_remove
            text: songMenu.songCount > 1 ? qsTr("从歌单移除选中的 %1 首").arg(songMenu.songCount)
                                         : qsTr("从歌单移除")
            trailingText: "Delete"
            onTriggered: table.removeRequested(songMenu.keys)
        }
        MenuSeparator {}
        MenuItem {
            id: albumItem
            visible: table.showAlbumAction
            height: visible ? implicitHeight : 0
            enabled: songMenu.albumKey !== ""
            glyph: Symbols.album
            text: qsTr("查看专辑")
            onTriggered: table.albumRequested(songMenu.albumKey)
        }
        Instantiator {
            model: songMenu.artists
            delegate: MenuItem {
                required property var modelData
                enabled: modelData.key !== ""
                glyph: Symbols.person
                text: qsTr("查看歌手：%1").arg(modelData.name)
                onTriggered: table.artistRequested(modelData.key)
            }
            // After "查看专辑".
            onObjectAdded: (index, object) => {
                let base = songMenu.count;
                for (let i = 0; i < songMenu.count; ++i) {
                    if (songMenu.itemAt(i) === albumItem)
                        base = i + 1;
                }
                songMenu.insertItem(base + index, object as MenuItem);
            }
            onObjectRemoved: (index, object) => songMenu.removeItem(object as MenuItem)
        }
    }
}
