pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Controls

// Something to do rather than a value (clear the cache): a text button on the right.
// Ask before anything that deletes data.
//   ActionSetting { text: qsTr("缓存"); actionText: qsTr("清除"); onTriggered: dialog.open() }
SettingRow {
    id: row

    property string actionText
    property bool actionEnabled: true
    signal triggered

    // The button takes the focus and the clicks, not the row.
    focusPolicy: Qt.NoFocus
    hoverEnabled: false

    trailing: Component {
        Button {
            variant: Md.Text
            enabled: row.actionEnabled
            text: row.actionText
            onClicked: row.triggered()
        }
    }
}
