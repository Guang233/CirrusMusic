import QtQuick
import QtQuick.Templates as T
import Cirrus.Theme

// Desktop dialog in MD3E dress (controls.md §8c). Looks like the MD3 basic dialog
// (SurfaceContainerHigh, extra-large corners, level-3 shadow, 24 px padding, over a
// 32 % scrim) but behaves like a desktop one:
// - a title row with a close button; a click outside does not close it;
// - Esc rejects; Enter clicks the focused button, otherwise accepts;
// - Tab stays inside. On open the dialog itself takes the focus, not a control (so
//   no focus ring until the user presses Tab), unless `initialFocusItem` is set;
//   the focus goes back where it was when the dialog closes.
// `acceptText`/`rejectText` add the standard buttons (accept filled, on the right);
// `buttons` puts more to their left. Children are the content. The supporting text can
// be selected and copied; the part between the title and the buttons scrolls when the
// window is too low for it. `reading` is for text to read (a description): up to 720
// wide, body-large, and the focus in the text (Ctrl+A, Ctrl+C work at once).
//   Dialog { id: dialog; title: qsTr("删除歌单？"); supportingText: qsTr("…")
//            acceptText: qsTr("删除"); rejectText: qsTr("取消"); onAccepted: … }
T.Popup {
    id: control

    property string glyph
    property string title
    property string supportingText
    property string acceptText
    property string rejectText
    property bool acceptEnabled: true
    property bool reading: false
    readonly property real maximumWidth: reading ? 720 : 560
    property Item initialFocusItem
    default property alias content: body.data
    property alias buttons: extraButtons.data

    signal accepted
    signal rejected

    // Closes with `accepted`; ignored while accepting is not possible.
    function accept(): void {
        if (acceptText !== "" && !acceptEnabled)
            return;
        close();
        accepted();
    }
    function reject(): void {
        close();
        rejected();
    }

    property Item previousFocus: null

    parent: T.Overlay.overlay
    anchors.centerIn: parent
    modal: true
    focus: true
    closePolicy: T.Popup.NoAutoClose
    padding: 24
    width: Math.max(320, Math.min(maximumWidth, implicitContentWidth + leftPadding + rightPadding, parent ? parent.width - 48 : maximumWidth))
    height: Math.min(implicitContentHeight + topPadding + bottomPadding, parent ? parent.height - 48 : implicitHeight)
    transformOrigin: T.Popup.Center

    onAboutToShow: previousFocus = parent ? parent.Window.activeFocusItem : null
    onOpened: {
        if (initialFocusItem)
            initialFocusItem.forceActiveFocus(Qt.TabFocusReason);
        else if (reading && supporting.visible)
            supporting.focusQuietly();
        else
            contentItem.forceActiveFocus(Qt.PopupFocusReason);
    }
    onClosed: {
        if (previousFocus)
            previousFocus.forceActiveFocus();
        previousFocus = null;
    }

    enter: Transition {
        ParallelAnimation {
            NumberAnimation {
                property: "scale"
                from: 0.95
                to: 1
                duration: Theme.motion.standardFastSpatial.duration
                easing: Theme.motion.standardFastSpatial.easing
            }
            NumberAnimation {
                property: "opacity"
                from: 0
                to: 1
                duration: Theme.motion.fastEffects.duration
                easing: Theme.motion.fastEffects.easing
            }
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
            level: Theme.elevation.level3
            radius: Theme.shape.extraLarge
        }
        Rectangle {
            anchors.fill: parent
            radius: Theme.shape.extraLarge
            color: ColorScope.scheme.surfaceContainerHigh
        }
    }

    contentItem: FocusScope {
        id: scope

        // Enter on a button clicks it (as on GTK and KDE); elsewhere it accepts.
        function activate(event: KeyEvent): void {
            const focused = control.parent ? control.parent.Window.activeFocusItem : null;
            if (focused instanceof T.AbstractButton)
                (focused as T.AbstractButton).click();
            else if (control.acceptText !== "")
                control.accept();
            else
                event.accepted = false;
        }

        // From the content's own sizes, not the scroller's (which follows the height).
        implicitWidth: Math.max(titleRow.implicitWidth, supporting.visible ? measure.implicitWidth : 0, scope.bodyWidth)
        // The supporting text's unwrapped width (a wrapping TextEdit's follows its width).
        Text {
            id: measure
            visible: false
            text: control.supportingText
            font: supporting.font
        }
        // The content's widest natural width (its children follow the dialog's width).
        readonly property real bodyWidth: {
            let widest = 0;
            for (const child of body.children)
                widest = Math.max(widest, child.implicitWidth);
            return widest;
        }
        implicitHeight: titleRow.height + 16 + middle.implicitHeight + (buttonArea.visible ? 16 + buttonArea.height : 0)
        Accessible.role: Accessible.Dialog
        Accessible.name: control.title

        Keys.onEscapePressed: control.reject()
        Keys.onReturnPressed: event => scope.activate(event)
        Keys.onEnterPressed: event => scope.activate(event)
        // Scrolls the middle part (what the focused control left alone).
        Keys.onPressed: event => {
            const page = scroller.height - 48;
            const step = event.key === Qt.Key_PageDown ? page : event.key === Qt.Key_PageUp ? -page : event.key === Qt.Key_Down ? 48 : event.key === Qt.Key_Up ? -48 : 0;
            if (step === 0 || scroller.contentHeight <= scroller.height)
                return;
            scroller.contentY = Math.max(0, Math.min(scroller.contentHeight - scroller.height, scroller.contentY + step));
            event.accepted = true;
        }

        // Moves `area` (in `item`) into view.
        function reveal(item: Item, area: rect): void {
            const top = item.mapToItem(middle, area.x, area.y).y;
            if (top < scroller.contentY)
                scroller.contentY = Math.max(0, top - 8);
            else if (top + area.height > scroller.contentY + scroller.height)
                scroller.contentY = Math.min(scroller.contentHeight - scroller.height, top + area.height - scroller.height + 8);
        }

        // Title row: icon, headline, close button.
        Item {
            id: titleRow
            width: parent.width
            implicitWidth: titleText.implicitWidth + 40 + 16 + (control.glyph !== "" ? 36 : 0)
            height: Math.max(titleText.height, closeButton.height)

            Icon {
                id: titleIcon
                anchors.verticalCenter: parent.verticalCenter
                visible: control.glyph !== ""
                glyph: control.glyph
                size: 24
                color: ColorScope.scheme.secondary
            }
            Text {
                id: titleText
                anchors.left: titleIcon.visible ? titleIcon.right : parent.left
                anchors.leftMargin: titleIcon.visible ? 12 : 0
                anchors.right: closeButton.left
                anchors.rightMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                wrapMode: Text.Wrap
                text: control.title
                color: ColorScope.scheme.onSurface
                font: Theme.type.titleLarge.font
                Accessible.role: Accessible.Heading
            }
            IconButton {
                id: closeButton
                anchors.right: parent.right
                anchors.rightMargin: -8
                anchors.verticalCenter: parent.verticalCenter
                glyph: Symbols.close
                text: qsTr("关闭（Esc）")
                onClicked: control.reject()
            }
        }

        // The supporting text and the content; scrolls when the window is too low.
        Flickable {
            id: scroller
            anchors.top: titleRow.bottom
            anchors.topMargin: 16
            width: parent.width
            height: Math.max(0, Math.min(middle.implicitHeight, parent.height - titleRow.height - 16 - (buttonArea.visible ? 16 + buttonArea.height : 0)))
            contentWidth: width
            contentHeight: middle.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            clip: contentHeight > height
            ScrollBar.vertical: ScrollBar {}

            Column {
                id: middle
                width: scroller.width
                spacing: 16
                SelectableText {
                    id: supporting
                    width: parent.width
                    visible: control.supportingText !== ""
                    activeFocusOnTab: true
                    text: control.supportingText
                    font: control.reading ? Theme.type.bodyLarge.font : Theme.type.bodyMedium.font
                    color: control.reading ? ColorScope.scheme.onSurface : ColorScope.scheme.onSurfaceVariant
                    onCursorMoved: area => scope.reveal(supporting, area)
                }
                Column {
                    id: body
                    width: parent.width
                    visible: children.length > 0
                }
            }
        }

        // 24 above the buttons: the 16 below the scroller plus 8.
        Item {
            id: buttonArea
            anchors.bottom: parent.bottom
            width: parent.width
            height: buttonRow.height + 8
            visible: extraButtons.children.length > 0 || control.acceptText !== "" || control.rejectText !== ""
            Row {
                id: buttonRow
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                spacing: 8
                Row {
                    id: extraButtons
                    spacing: 8
                }
                Button {
                    id: rejectButton
                    visible: control.rejectText !== ""
                    variant: Md.Text
                    text: control.rejectText
                    onClicked: control.reject()
                }
                Button {
                    id: acceptButton
                    visible: control.acceptText !== ""
                    enabled: control.acceptEnabled
                    variant: Md.Filled
                    text: control.acceptText
                    onClicked: control.accept()
                }
            }
        }
    }

    T.Overlay.modal: Rectangle {
        color: Qt.alpha(ColorScope.scheme.scrim, 0.32)
        Behavior on opacity {
            NumberAnimation {
                duration: Theme.motion.fastEffects.duration
                easing: Theme.motion.fastEffects.easing
            }
        }
    }
}
