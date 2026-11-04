pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls

// One of several options, each with an optional line explaining it: the current one
// on an outlined button on the right; the row (or the button) opens a menu to pick.
//   MenuSetting {
//       text: qsTr("音质")
//       options: [{ value: 0, text: qsTr("标准"), description: qsTr("128 kbps") }, ...]
//       value: App.settings.audioQuality
//       onChosen: value => App.settings.audioQuality = value
//   }
SettingRow {
    id: row

    // [{ value, text, description? }]
    property var options: []
    property var value
    // Shown on the button when no option has the value.
    property string placeholder: qsTr("请选择")
    signal chosen(var value)

    readonly property var currentOption: options.find(option => option.value === value)

    function openMenu(): void {
        const button = row.trailingItem as Item;
        if (button)
            menu.popup(button, 0, button.height + 4);
    }

    onClicked: openMenu()
    Accessible.role: Accessible.ComboBox
    Accessible.description: currentOption ? currentOption.text : placeholder

    trailing: Component {
        Button {
            focusPolicy: Qt.NoFocus
            variant: Md.Outlined
            size: Md.Small
            trailingGlyph: Symbols.keyboard_arrow_down
            text: row.currentOption ? row.currentOption.text : row.placeholder
            onClicked: row.openMenu()
        }
    }

    Menu {
        id: menu
        Instantiator {
            // Built while open: there can be hundreds (font families).
            model: menu.visible ? row.options : []
            delegate: MenuItem {
                required property var modelData
                checkable: true
                checked: modelData.value === row.value
                text: modelData.text
                supportingText: modelData.description ?? ""
                onTriggered: row.chosen(modelData.value)
            }
            onObjectAdded: (index, object) => menu.insertItem(index, object as MenuItem)
            onObjectRemoved: (index, object) => menu.removeItem(object as MenuItem)
        }
    }
}
