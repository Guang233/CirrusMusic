pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls

// An action of a song page (SongCollectionPage.actionsContent): a button with words
// in the header; in the info pane (`iconOnly`) a square tonal icon button as tall as
// the pane's play button, the words in its tooltip (docs/ui/redesign-md3e.md §7.1).
Item {
    id: action

    property string glyph
    property string text
    property int variant: Md.Tonal
    property bool iconOnly: false
    // Filled icon (e.g. a saved state).
    property real iconFill: 0
    readonly property Item control: iconOnly ? iconButton : button

    signal clicked

    implicitWidth: control.implicitWidth
    implicitHeight: control.implicitHeight

    Button {
        id: button
        anchors.fill: parent
        visible: !action.iconOnly
        variant: action.variant
        glyph: action.glyph
        iconFill: action.iconFill
        text: action.text
        onClicked: action.clicked()
    }
    IconButton {
        id: iconButton
        anchors.fill: parent
        visible: action.iconOnly
        variant: Md.Tonal
        size: Md.Large
        shape: Md.Square
        widthMode: Md.Narrow
        glyph: action.glyph
        iconFill: action.iconFill
        text: action.text
        onClicked: action.clicked()
    }
}
