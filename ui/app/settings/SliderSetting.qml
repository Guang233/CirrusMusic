pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls

// A value on a range: a slider on the right with the value beside it.
//   SliderSetting {
//       text: qsTr("字重"); from: 100; to: 900; stepSize: 50
//       value: App.settings.lyricWeight
//       onMoved: value => App.settings.lyricWeight = value
//   }
SettingRow {
    id: row

    property real from: 0
    property real to: 100
    property real stepSize: 1
    property real value
    // How the value reads beside the slider.
    property var format: value => Math.round(value).toString()
    signal moved(real value)

    // The slider takes the focus and the drags, not the row.
    focusPolicy: Qt.NoFocus
    hoverEnabled: false

    trailing: Component {
        Row {
            spacing: 12
            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: 48
                horizontalAlignment: Text.AlignRight
                text: row.format(slider.value)
                color: ColorScope.scheme.onSurfaceVariant
                font: Theme.type.labelLarge.font
            }
            Slider {
                id: slider
                anchors.verticalCenter: parent.verticalCenter
                width: 200
                from: row.from
                to: row.to
                stepSize: row.stepSize
                snapMode: Slider.SnapAlways
                value: row.value
                Accessible.name: row.text
                onMoved: row.moved(value)
            }
        }
    }
}
