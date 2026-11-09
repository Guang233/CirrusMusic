import QtQuick
import QtQuick.Templates as T
import Cirrus.Theme

// Read-only text that can be selected and copied (controls.md §8e): descriptions,
// comments, an artist's introduction. Plain text, never HTML; line breaks kept.
// - mouse: drag, double-click a word, triple-click a line; right-click (or the Menu
//   key, Shift+F10) opens 复制 / 全选;
// - keyboard: once focused (Tab when `activeFocusOnTab`), Shift+arrows select, Ctrl+A
//   selects all, Ctrl+C copies; a focus ring shows while focused from the keyboard.
//   SelectableText { width: parent.width; text: pageModel.description }
TextEdit {
    id: control

    // Where a keyboard selection moves the text into view (the dialog's scroller).
    signal cursorMoved(rect area)

    readOnly: true
    selectByMouse: true
    selectByKeyboard: true
    persistentSelection: false
    textFormat: TextEdit.PlainText
    wrapMode: TextEdit.Wrap
    color: ColorScope.scheme.onSurfaceVariant
    selectionColor: ColorScope.scheme.primaryContainer
    selectedTextColor: ColorScope.scheme.onPrimaryContainer
    font: Theme.type.bodyLarge.font
    Accessible.role: Accessible.StaticText
    Accessible.name: text

    onCursorRectangleChanged: if (activeFocus)
        cursorMoved(cursorRectangle)
    // Whether the focus came from the keyboard (Tab, or keys pressed since): the ring.
    property bool keyboardFocus: false
    property bool quiet: false
    // Takes the focus without the ring (a dialog opening on it).
    function focusQuietly(): void {
        quiet = true;
        forceActiveFocus(Qt.PopupFocusReason);
        quiet = false;
    }
    onActiveFocusChanged: {
        keyboardFocus = activeFocus && !quiet && !pointer.active;
        // A selection left behind would look like a stuck highlight.
        if (!activeFocus && !menu.visible)
            deselect();
    }
    PointHandler {
        id: pointer
        acceptedButtons: Qt.AllButtons
    }

    function openMenu(x: real, y: real): void {
        menu.popup(control, x, y);
    }

    Keys.onMenuPressed: openMenu(cursorRectangle.x, cursorRectangle.bottom)
    Keys.onPressed: event => {
        keyboardFocus = true;
        if (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier)) {
            openMenu(cursorRectangle.x, cursorRectangle.bottom);
            event.accepted = true;
        }
    }

    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: (point, button) => {
            control.forceActiveFocus(Qt.MouseFocusReason);
            control.openMenu(point.position.x, point.position.y);
        }
    }

    FocusRing {
        anchors.fill: parent
        anchors.margins: -4
        shown: control.activeFocus && control.keyboardFocus
        containerRadius: Theme.shape.extraSmall
    }

    Menu {
        id: menu
        MenuItem {
            glyph: Symbols.content_copy
            text: qsTr("复制")
            trailingText: "Ctrl+C"
            // All of it when nothing is selected.
            onTriggered: {
                if (control.selectedText === "") {
                    control.selectAll();
                    control.copy();
                    control.deselect();
                } else {
                    control.copy();
                }
            }
        }
        MenuItem {
            glyph: Symbols.select_all
            text: qsTr("全选")
            trailingText: "Ctrl+A"
            onTriggered: {
                control.forceActiveFocus(Qt.PopupFocusReason);
                control.selectAll();
            }
        }
    }
}
