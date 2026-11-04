pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Controls

// An on/off setting: a switch on the right, and the whole row toggles it.
//   SwitchSetting {
//       text: qsTr("显示翻译")
//       value: App.settings.showTranslation
//       onSwitched: value => App.settings.showTranslation = value
//   }
SettingRow {
    id: row

    property bool value
    signal switched(bool value)

    onClicked: switched(!value)
    Accessible.role: Accessible.CheckBox
    Accessible.checkable: true
    Accessible.checked: value

    trailing: Component {
        Switch {
            focusPolicy: Qt.NoFocus
            checked: row.value
            onToggled: {
                row.switched(checked);
                // A click breaks the binding: keep following the setting.
                checked = Qt.binding(() => row.value);
            }
        }
    }
}
