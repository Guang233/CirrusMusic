pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Cirrus.Theme
import Cirrus.Controls

// The one search input of the app, in the title bar row (docs/ui/pages/shell.md §3,
// search.md §2). Works like a browser's address bar: the keyboard focus stays in the
// field while the list under it shows recent searches and trending keywords (empty
// field), or "搜索「…」" and grouped suggestions (typing). ↑/↓ pick a line, Enter
// runs the picked line or searches the text, Delete removes a picked recent search,
// Esc closes the list, then clears the text, then hands the focus back to the page.
// A suggested artist, album or playlist opens (`opened`); a song searches its title.
SearchBar {
    id: field

    // Recent searches, newest first; [{keyword, description}]; and for the text,
    // [{song, kind, key, title}] (SearchModel).
    property list<string> history
    property var hotKeywords: []
    property var suggestions: []
    // The list is wanted: the field was focused or edited since the last search.
    property bool listWanted: false
    property int picked: -1
    // Kept while a recent search is removed, so the pick stays in place.
    property bool keepPicked: false

    readonly property bool typing: text.trim() !== ""
    // The list's lines: {type: "history" | "hot" | "query" | "suggestion", text,
    // glyph, group (a title above the line, or ""), rank, note, song, kind, key}.
    readonly property var lines: {
        const lines = [];
        if (typing) {
            lines.push({
                type: "query",
                text: text.trim(),
                glyph: Symbols.search,
                group: ""
            });
            const groups = [qsTr("歌单"), qsTr("专辑"), qsTr("歌手"), qsTr("单曲")];
            const glyphs = [Symbols.queue_music, Symbols.album, Symbols.person];
            let last = "";
            for (const suggestion of suggestions) {
                // Collection.Kind: Playlist 0, Album 1, Artist 2.
                const group = suggestion.song ? groups[3] : groups[suggestion.kind];
                lines.push({
                    type: "suggestion",
                    text: suggestion.title,
                    glyph: suggestion.song ? Symbols.music_note : glyphs[suggestion.kind],
                    group: group === last ? "" : group,
                    song: suggestion.song,
                    kind: suggestion.kind,
                    key: suggestion.key
                });
                last = group;
            }
            return lines;
        }
        history.slice(0, 10).forEach((query, i) => lines.push({
                type: "history",
                text: query,
                glyph: Symbols.history,
                group: i === 0 ? qsTr("搜索历史") : ""
            }));
        hotKeywords.slice(0, 10).forEach((hot, i) => lines.push({
                type: "hot",
                text: hot.keyword,
                note: hot.description,
                rank: i + 1,
                group: i === 0 ? qsTr("热门搜索") : ""
            }));
        return lines;
    }
    readonly property bool listShown: activeFocus && listWanted && lines.length > 0

    signal submitted(string text)
    // A suggested artist, album or playlist (Collection.Kind and key).
    signal opened(int kind, string key)
    signal historyRemoved(string text)
    signal historyCleared
    // Esc on an empty field.
    signal dismissed

    function submit(value: string): void {
        value = value.trim();
        if (value === "")
            return;
        listWanted = false;
        picked = -1;
        text = value;
        submitted(value);
    }

    function run(line: var): void {
        if (line.type !== "suggestion" || line.song) {
            submit(line.text);
            return;
        }
        listWanted = false;
        picked = -1;
        opened(line.kind, line.key);
    }

    function removeHistory(index: int): void {
        keepPicked = true;
        historyRemoved(lines[index].text);
        keepPicked = false;
        picked = Math.min(picked, lines.length - 1);
    }

    height: 40
    handlesEscape: false
    placeholderText: qsTr("搜索歌曲、歌手、专辑、歌单、播客")
    Accessible.name: qsTr("搜索")

    onActiveFocusChanged: {
        listWanted = activeFocus;
        picked = -1;
    }
    // A click on the field opens the list again, also when it kept the focus.
    onPressed: listWanted = true
    onTextEdited: {
        listWanted = true;
        picked = -1;
    }
    onLinesChanged: {
        if (!keepPicked)
            picked = -1;
    }
    onPickedChanged: revealLine(picked)
    onAccepted: {
        if (picked >= 0 && listShown)
            run(lines[picked]);
        else
            submit(text);
    }

    Keys.onDownPressed: {
        if (!listShown)
            listWanted = true;
        else
            picked = Math.min(picked + 1, lines.length - 1);
    }
    Keys.onUpPressed: {
        if (listShown)
            picked = Math.max(picked - 1, -1);
    }
    Keys.onDeletePressed: event => {
        if (listShown && picked >= 0 && lines[picked].type === "history")
            removeHistory(picked);
        else
            event.accepted = false;
    }
    // SearchBar leaves Esc alone (handlesEscape: false); this handler runs after its.
    Keys.onEscapePressed: event => {
        event.accepted = true;
        if (listShown)
            listWanted = false;
        else if (length > 0)
            clear();
        else
            dismissed();
    }

    // The shortcut, while the field waits.
    Text {
        x: field.width - width - 16
        anchors.verticalCenter: parent.verticalCenter
        visible: !field.activeFocus && field.length === 0
        text: "Ctrl+K"
        color: Theme.color.outline
        font: Theme.type.labelMedium.font
    }

    // The list unfolds from the field and follows its line count with a spring, so
    // suggestions arriving while typing don't make it jump; it fades out on close.
    // Opened and closed from `listShown`; a press outside the field and the list
    // closes it too (clicking the field, typing or ↓ opens it again).
    onListShownChanged: listShown ? popup.open() : popup.close()

    T.Popup {
        id: popup

        property bool unfolded: false

        y: field.height + 4
        width: field.width
        height: Math.max(0, sizeSpring.value)
        padding: 0
        topPadding: 8
        bottomPadding: 8
        // Never takes the focus: typing goes on in the field.
        focus: false
        closePolicy: T.Popup.CloseOnPressOutsideParent
        onAboutToShow: unfolded = true
        onClosed: {
            unfolded = false;
            sizeSpring.snapTo(0);
            field.listWanted = false;
        }

        Spring {
            id: sizeSpring
            target: popup.unfolded ? popup.implicitContentHeight + popup.topPadding
                                     + popup.bottomPadding : 0
            spec: Theme.motion.standardFastSpatial
            threshold: 0.5
        }

        enter: Transition {
            NumberAnimation {
                property: "opacity"
                from: 0
                to: 1
                duration: Theme.motion.fastEffects.duration
                easing: Theme.motion.fastEffects.easing
            }
        }
        exit: Transition {
            NumberAnimation {
                property: "opacity"
                from: 1
                to: 0
                duration: Theme.motion.fastEffects.duration
                easing: Theme.motion.fastEffects.easing
            }
        }

        background: Item {
            Elevation {
                anchors.fill: parent
                level: Theme.elevation.level2
                radius: Theme.shape.large
            }
            Rectangle {
                anchors.fill: parent
                radius: Theme.shape.large
                color: Theme.color.surfaceContainerHigh
            }
        }

        // Long lists (recent and trending: up to 20 lines) scroll inside the window.
        contentItem: Flickable {
            id: flick
            implicitHeight: Math.min(lineColumn.implicitHeight,
                                     Math.max(160, (field.Window.height || 800) - 120))
            contentHeight: lineColumn.implicitHeight
            // Rows past the springing height stay hidden.
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            interactive: contentHeight > height
            ScrollBar.vertical: ScrollBar {}

            Column {
                id: lineColumn
                width: flick.width

                Repeater {
                    id: lineRepeater
                    model: field.lines
                    delegate: Column {
                        id: entry

                        required property int index
                        required property var modelData
                        readonly property bool isHistory: modelData.type === "history"

                        width: lineColumn.width

                        // The group's title; recent searches can be cleared from it.
                        Item {
                            width: parent.width
                            height: 36
                            visible: entry.modelData.group !== ""
                            Text {
                                x: 16
                                anchors.verticalCenter: parent.verticalCenter
                                text: entry.modelData.group
                                color: Theme.color.onSurfaceVariant
                                font: Theme.type.labelLarge.font
                            }
                            Button {
                                anchors.right: parent.right
                                anchors.rightMargin: 8
                                anchors.verticalCenter: parent.verticalCenter
                                visible: entry.isHistory
                                variant: Md.Text
                                size: Md.XSmall
                                focusPolicy: Qt.NoFocus
                                text: qsTr("清除")
                                onClicked: field.historyCleared()
                            }
                        }
                        ListItem {
                            id: line
                            width: parent.width
                            implicitHeight: 44
                            topPadding: 0
                            bottomPadding: 0
                            focusPolicy: Qt.NoFocus
                            highlighted: entry.index === field.picked
                            leadingGlyph: entry.modelData.glyph ?? ""
                            leading: entry.modelData.type === "hot" ? rank : null
                            trailing: entry.isHistory ? remove
                                    : (entry.modelData.note ?? "") !== "" ? note : null
                            text: entry.modelData.type === "query"
                                  ? qsTr("搜索「%1」").arg(entry.modelData.text)
                                  : entry.modelData.text
                            Accessible.name: text
                            onClicked: field.run(entry.modelData)

                            // Trending rank: the first three in primary.
                            Component {
                                id: rank
                                Text {
                                    width: 24
                                    horizontalAlignment: Text.AlignHCenter
                                    text: entry.modelData.rank
                                    color: entry.modelData.rank <= 3 ? Theme.color.primary
                                                                     : Theme.color.onSurfaceVariant
                                    font: Fonts.tabular(Theme.type.titleSmall.font)
                                }
                            }
                            Component {
                                id: note
                                Text {
                                    width: Math.min(implicitWidth, line.width * 0.4)
                                    text: entry.modelData.note
                                    elide: Text.ElideRight
                                    color: Theme.color.onSurfaceVariant
                                    font: Theme.type.bodySmall.font
                                }
                            }
                            Component {
                                id: remove
                                IconButton {
                                    size: Md.XSmall
                                    focusPolicy: Qt.NoFocus
                                    glyph: Symbols.close
                                    text: qsTr("删除这条搜索历史（Delete）")
                                    onClicked: field.removeHistory(entry.index)
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Keeps the picked line in view inside the scrolling list.
    function revealLine(index: int): void {
        const item = lineRepeater.itemAt(index);
        if (!item)
            return;
        if (index === 0)
            flick.contentY = 0;
        else if (item.y < flick.contentY)
            flick.contentY = item.y;
        else if (item.y + item.height > flick.contentY + flick.height)
            flick.contentY = item.y + item.height - flick.height;
    }
}
