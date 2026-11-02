pragma ComponentBehavior: Bound
import QtQuick

// A single-select row of filter chips (MD3 chip set). Shows as many chips as fit the
// width, in order. One Tab stop: the focus starts on the selected chip, ←/→ move it,
// Space/Enter select; `activated` reports the chip picked (the owner sets
// `currentIndex`, which may also change from elsewhere).
//   ChipGroup { model: [qsTr("全部"), qsTr("华语")]; currentIndex: area; onActivated: i => area = i }
FocusScope {
    id: control

    property list<string> model
    property int currentIndex: -1
    property real spacing: 8
    // Set by the layout: how many chips are shown (the first ones of `model`), the
    // width they take, and the width all of them would take.
    property int shown: 0
    property real shownWidth: 0
    property real natural: 0
    // The chip the keyboard is on while the row has focus.
    property int focusIndex: currentIndex

    signal activated(int index)

    implicitWidth: natural
    implicitHeight: 32
    activeFocusOnTab: false
    onActiveFocusChanged: if (!activeFocus) focusIndex = currentIndex
    onCurrentIndexChanged: if (!activeFocus) focusIndex = currentIndex
    onWidthChanged: Qt.callLater(layout)

    // Lays the chips out left to right; those past the width are hidden.
    function layout(): void {
        let x = 0;
        let count = 0;
        let fits = true;
        for (let i = 0; i < chips.count; ++i) {
            const chip = chips.itemAt(i) as Chip;
            if (!chip)
                continue;
            chip.x = x;
            fits = fits && x + chip.implicitWidth <= width;
            chip.visible = fits;
            if (fits) {
                ++count;
                shownWidth = x + chip.implicitWidth;
            }
            x += chip.implicitWidth + spacing;
        }
        if (count === 0)
            shownWidth = 0;
        natural = Math.max(0, x - spacing);
        shown = count;
    }

    // The focus moves first: a chip can't stop being a Tab stop while it has focus.
    function focusChip(index: int, reason: int): void {
        (chips.itemAt(index) as Chip)?.forceActiveFocus(reason);
        focusIndex = index;
    }

    function moveFocus(step: int): void {
        focusChip(Math.max(0, Math.min(shown - 1, focusIndex + step)), Qt.TabFocusReason);
    }

    Repeater {
        id: chips
        model: control.model
        onItemAdded: Qt.callLater(control.layout)
        onItemRemoved: Qt.callLater(control.layout)

        delegate: Chip {
            required property int index
            required property string modelData

            text: modelData
            filter: true
            checked: index === control.currentIndex
            focusPolicy: index === Math.min(control.focusIndex, control.shown - 1)
                         ? Qt.TabFocus : Qt.NoFocus
            onImplicitWidthChanged: Qt.callLater(control.layout)
            onClicked: {
                if (control.activeFocus)
                    control.focusChip(index, Qt.OtherFocusReason);
                else
                    control.focusIndex = index;
                control.activated(index);
            }
            Keys.onLeftPressed: control.moveFocus(-1)
            Keys.onRightPressed: control.moveFocus(1)
            Keys.onReturnPressed: clicked()
            Keys.onEnterPressed: clicked()
        }
    }
}
