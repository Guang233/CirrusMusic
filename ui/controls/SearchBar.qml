pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Cirrus.Theme

// MD3 search bar (the input field only; the suggestion surface belongs to the page).
// Esc clears the text, or drops focus when already empty. `accepted` fires on Enter.
//   SearchBar { placeholderText: qsTr("搜索歌曲、歌手、专辑"); onAccepted: search(text) }
T.TextField {
    id: control

    property string leadingGlyph: Symbols.search
    property color containerColor: Theme.color.surfaceContainerHigh

    implicitWidth: 360
    implicitHeight: 56
    leftPadding: 56
    rightPadding: length > 0 ? 56 : 16
    verticalAlignment: TextInput.AlignVCenter
    font: Theme.type.bodyLarge.font
    color: enabled ? Theme.color.onSurface : Qt.alpha(Theme.color.onSurface,
                                                      Theme.state.disabledContent)
    selectionColor: Qt.alpha(Theme.color.primary, 0.4)
    selectedTextColor: Theme.color.onSurface
    placeholderTextColor: Theme.color.onSurfaceVariant
    Accessible.name: placeholderText
    Accessible.role: Accessible.EditableText

    Keys.onEscapePressed: event => {
        if (length > 0)
            clear();
        else
            focus = false;
    }

    cursorDelegate: Rectangle {
        width: 2
        color: Theme.color.primary
        visible: control.activeFocus && control.selectionStart === control.selectionEnd
                 && blink.on

        Timer {
            id: blink
            property bool on: true
            interval: 530
            repeat: true
            running: control.activeFocus
            onRunningChanged: blink.on = true
            onTriggered: blink.on = !blink.on
        }
    }

    Text {
        x: control.leftPadding
        width: control.width - control.leftPadding - control.rightPadding
        height: control.height
        visible: control.length === 0 && control.preeditText.length === 0
        text: control.placeholderText
        font: control.font
        color: control.placeholderTextColor
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    IconButton {
        x: control.width - width - 8
        anchors.verticalCenter: parent.verticalCenter
        visible: control.length > 0
        glyph: Symbols.close
        text: qsTr("清除")
        focusPolicy: Qt.NoFocus
        onClicked: {
            control.clear();
            control.forceActiveFocus();
        }
    }

    background: Rectangle {
        implicitWidth: 360
        implicitHeight: 56
        radius: height / 2
        color: control.enabled ? control.containerColor
                               : Qt.alpha(Theme.color.onSurface, Theme.state.disabledContainer)

        StateLayer {
            anchors.fill: parent
            radius: parent.radius
            color: Theme.color.onSurface
            hovered: control.hovered && control.enabled && !control.activeFocus
        }

        Icon {
            x: 16
            anchors.verticalCenter: parent.verticalCenter
            glyph: control.leadingGlyph
            color: control.enabled ? Theme.color.onSurface
                                   : Qt.alpha(Theme.color.onSurface, Theme.state.disabledContent)
        }
    }
}
