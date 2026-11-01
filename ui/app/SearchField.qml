pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Cirrus.Theme
import Cirrus.Controls

// The one search input of the app, in the title bar row (docs/ui/pages/shell.md §3).
// Works like a browser's address bar: the keyboard focus stays in the field while
// the list under it shows trending keywords (empty field) or suggestions (typing).
// ↑/↓ pick a line, Enter searches the picked line or the text, Esc closes the list,
// then clears the text, then hands the focus back to the page.
SearchBar {
    id: field

    // Shown under the field: suggestions for the text, trending keywords when empty.
    property list<string> suggestions
    property list<string> trending
    // The list is wanted: the field was focused or edited since the last search.
    property bool listWanted: false
    property int picked: -1

    readonly property list<string> lines: text.trim() === "" ? trending : suggestions
    readonly property bool listShown: activeFocus && listWanted && lines.length > 0

    signal submitted(string text)
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

    height: 40
    handlesEscape: false
    placeholderText: qsTr("搜索歌曲、歌手、专辑、歌单")
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
    onLinesChanged: picked = -1
    onAccepted: submit(picked >= 0 ? lines[picked] : text)

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

        contentItem: Column {
            // Rows past the springing height stay hidden.
            clip: true
            Text {
                x: 16
                visible: field.text.trim() === ""
                topPadding: 4
                bottomPadding: 8
                text: qsTr("热门搜索")
                color: Theme.color.onSurfaceVariant
                font: Theme.type.labelLarge.font
            }
            Repeater {
                model: field.lines
                ListItem {
                    required property int index
                    required property string modelData
                    width: popup.width
                    focusPolicy: Qt.NoFocus
                    leadingGlyph: field.text.trim() === "" ? Symbols.trending_up : Symbols.search
                    text: modelData
                    highlighted: index === field.picked
                    onClicked: field.submit(modelData)
                }
            }
        }
    }
}
