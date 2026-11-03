pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Cirrus.Theme

// MD3 outlined text field, multi-line: grows with its text up to `maximumLines`, then
// scrolls. Supporting text on the left and a counter (`counterText`) on the right sit
// under the field. Focus goes to the text (`forceActiveFocus()` on the control).
// Ctrl+Enter emits `submitted` (Enter alone starts a new line).
//   TextArea { placeholderText: qsTr("写下你的评论…"); onSubmitted: send(text) }
FocusScope {
    id: control

    property alias text: area.text
    property alias placeholderText: area.placeholderText
    property alias length: area.length
    readonly property T.TextArea textArea: area
    property int maximumLines: 6
    property bool error: false
    property string supportingText
    property string counterText
    // Rounder than MD3's 4 px outlined field: a box to write in, not a form row.
    property real radius: Theme.shape.large

    signal submitted

    readonly property bool hasSupporting: supportingText !== "" || counterText !== ""
    readonly property color accent: error ? Theme.color.error : Theme.color.primary
    readonly property color mutedColor: enabled ? Theme.color.onSurfaceVariant
                                                : Qt.alpha(Theme.color.onSurface,
                                                           Theme.state.disabledContent)
    readonly property real lineHeight: metrics.height
    readonly property real fieldHeight: Math.max(56, Math.min(
        area.implicitHeight, area.topPadding + area.bottomPadding + maximumLines * lineHeight))

    implicitWidth: 280
    implicitHeight: fieldHeight + (hasSupporting ? 20 : 0)
    activeFocusOnTab: true
    Accessible.role: Accessible.EditableText
    Accessible.name: placeholderText
    Accessible.description: supportingText

    FontMetrics {
        id: metrics
        font: Theme.type.bodyLarge.font
    }

    // Focus and hover blend the outline in (width 1 → 2 px with the focus).
    Spring {
        id: focusSpring
        target: area.activeFocus ? 1 : 0
        spec: Theme.motion.fastEffects
    }
    Spring {
        id: hoverSpring
        target: area.hovered ? 1 : 0
        spec: Theme.motion.fastEffects
    }

    Rectangle {
        id: frame
        width: control.width
        height: control.fieldHeight
        radius: control.radius
        color: "transparent"
        border.width: 1 + focusSpring.value
        border.color: !control.enabled ? Qt.alpha(Theme.color.onSurface, Theme.state.disabledContent)
                    : control.error ? Theme.color.error
                    : Theme.mix(Theme.mix(Theme.color.outline, Theme.color.onSurface,
                                          hoverSpring.value),
                                Theme.color.primary, focusSpring.value)
    }

    Flickable {
        id: flick
        anchors.fill: frame
        contentWidth: width
        contentHeight: area.implicitHeight
        interactive: contentHeight > height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}

        // Keeps the cursor in view while typing past the visible lines.
        function follow(rect: rect): void {
            if (rect.y < contentY)
                contentY = rect.y;
            else if (rect.y + rect.height > contentY + height - area.bottomPadding)
                contentY = rect.y + rect.height - height + area.bottomPadding;
        }

        T.TextArea {
            id: area
            width: flick.width
            // Templates leave the implicit size to the style.
            implicitHeight: contentHeight + topPadding + bottomPadding
            focus: true
            padding: 16
            wrapMode: TextEdit.Wrap
            font: Theme.type.bodyLarge.font
            color: control.enabled ? Theme.color.onSurface
                                   : Qt.alpha(Theme.color.onSurface, Theme.state.disabledContent)
            selectionColor: Qt.alpha(control.accent, 0.4)
            selectedTextColor: Theme.color.onSurface
            placeholderTextColor: control.mutedColor
            selectByMouse: true
            onCursorRectangleChanged: if (activeFocus) flick.follow(cursorRectangle)
            Keys.onPressed: event => {
                if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                        && (event.modifiers & Qt.ControlModifier)) {
                    event.accepted = true;
                    control.submitted();
                }
            }

            cursorDelegate: Rectangle {
                width: 2
                color: control.accent
                visible: area.activeFocus && area.selectionStart === area.selectionEnd && blink.on

                Timer {
                    id: blink
                    property bool on: true
                    interval: 530
                    repeat: true
                    running: area.activeFocus
                    onRunningChanged: blink.on = true
                    onTriggered: blink.on = !blink.on
                }
                Connections {
                    target: area
                    function onCursorPositionChanged() {
                        blink.on = true;
                        blink.restart();
                    }
                }
            }

            Text {
                x: area.leftPadding
                y: area.topPadding
                width: area.width - area.leftPadding - area.rightPadding
                visible: area.length === 0 && area.preeditText.length === 0
                text: area.placeholderText
                font: area.font
                color: area.placeholderTextColor
                elide: Text.ElideRight
            }
        }
    }

    Text {
        x: 16
        y: control.fieldHeight + 4
        width: counter.x - x - 16
        visible: control.supportingText !== ""
        text: control.supportingText
        elide: Text.ElideRight
        color: control.error ? Theme.color.error : control.mutedColor
        font: Theme.type.bodySmall.font
    }
    Text {
        id: counter
        x: control.width - width - 16
        y: control.fieldHeight + 4
        visible: control.counterText !== ""
        text: control.counterText
        color: control.error ? Theme.color.error : control.mutedColor
        font: Theme.type.bodySmall.font
    }
}
