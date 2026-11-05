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
// `buttons` puts more to their left. Children are the content.
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
    width: Math.max(320, Math.min(560, implicitContentWidth + leftPadding + rightPadding,
                                  parent ? parent.width - 48 : 560))
    height: Math.min(implicitContentHeight + topPadding + bottomPadding,
                     parent ? parent.height - 48 : implicitHeight)
    transformOrigin: T.Popup.Center

    onAboutToShow: previousFocus = parent ? parent.Window.activeFocusItem : null
    onOpened: {
        if (initialFocusItem)
            initialFocusItem.forceActiveFocus(Qt.TabFocusReason);
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

        implicitWidth: column.implicitWidth
        implicitHeight: column.implicitHeight
        Accessible.role: Accessible.Dialog
        Accessible.name: control.title

        Keys.onEscapePressed: control.reject()
        Keys.onReturnPressed: event => scope.activate(event)
        Keys.onEnterPressed: event => scope.activate(event)

        Column {
            id: column
            width: parent.width
            spacing: 16

            // Title row: icon, headline, close button.
            Item {
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
            Text {
                width: parent.width
                visible: control.supportingText !== ""
                wrapMode: Text.Wrap
                text: control.supportingText
                color: ColorScope.scheme.onSurfaceVariant
                font: Theme.type.bodyMedium.font
            }
            Column {
                id: body
                width: parent.width
                visible: children.length > 0
            }
            // 24 above the buttons: the column's 16 plus 8.
            Item {
                width: parent.width
                height: buttonRow.height + 8
                visible: extraButtons.children.length > 0 || control.acceptText !== ""
                         || control.rejectText !== ""
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
