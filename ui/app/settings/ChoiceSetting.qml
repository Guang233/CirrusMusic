pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Controls

// One of two or three short options: a connected button group on the right (one Tab
// stop, ←/→ choose). For more options, or ones that need explaining, use MenuSetting.
//   ChoiceSetting {
//       text: qsTr("明暗")
//       options: [{ value: 0, text: qsTr("跟随系统") }, { value: 1, text: qsTr("亮色") }]
//       value: App.settings.themeMode
//       onChosen: value => App.settings.themeMode = value
//   }
SettingRow {
    id: row

    // [{ value, text }]
    property var options: []
    property var value
    signal chosen(var value)

    // The buttons take the focus and the clicks, not the row.
    focusPolicy: Qt.NoFocus
    hoverEnabled: false

    trailing: Component {
        ConnectedButtonGroup {
            exclusive: true
            Repeater {
                id: buttons
                model: row.options
                delegate: Button {
                    required property var modelData
                    size: Md.Small
                    variant: Md.Tonal
                    checkable: true
                    checked: row.value === modelData.value
                    text: modelData.text
                    onClicked: row.chosen(modelData.value)
                }
            }
            // A click breaks the checked bindings; the value may also change elsewhere.
            Connections {
                target: row
                function onValueChanged() {
                    for (let i = 0; i < buttons.count; ++i)
                        (buttons.itemAt(i) as Button).checked = row.options[i].value === row.value;
                }
            }
        }
    }
}
