pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Cirrus.Theme
import Cirrus.Controls

// Phase 2 check: linear and circular progress, loading indicator.
ControlPage {
    id: page

    SectionTitle {
        text: qsTr("线性进度")
    }
    Caption {
        text: qsTr("确定进度：指示与轨道之间 4 px 间隙，末端停止点。拖动滑块检查进度平滑过渡。")
    }
    Slider {
        id: progressValue
        value: 0.42
        Accessible.name: qsTr("进度")
        Layout.preferredWidth: 360
    }
    ProgressBar {
        value: progressValue.value
        Layout.preferredWidth: 360
    }
    Repeater {
        model: [0, 0.1, 0.7, 1]
        delegate: ProgressBar {
            required property real modelData
            value: modelData
            Layout.preferredWidth: 360
        }
    }
    Caption {
        text: qsTr("不确定进度")
    }
    ProgressBar {
        indeterminate: true
        Layout.preferredWidth: 360
    }
    ProgressBar {
        value: 0.5
        enabled: false
        Layout.preferredWidth: 360
    }
    Caption {
        text: qsTr("水波（wavy）：波动时向前流动，关掉波动后平滑地变回直线；软件渲染下是直线。")
    }
    ProgressBar {
        wavy: true
        waving: waveSwitch.checked
        value: progressValue.value
        Layout.preferredWidth: 360
    }
    Switch {
        id: waveSwitch
        checked: true
        Accessible.name: qsTr("波动")
    }

    SectionTitle {
        text: qsTr("环形进度")
    }
    RowLayout {
        spacing: 24
        CircularProgress {
            value: progressValue.value
        }
        CircularProgress {
            value: 0.1
        }
        CircularProgress {
            value: 0.9
        }
        CircularProgress {
            indeterminate: true
        }
        CircularProgress {
            implicitWidth: 64
            implicitHeight: 64
            thickness: 6
            indeterminate: true
        }
    }

    SectionTitle {
        text: qsTr("加载指示器")
    }
    Caption {
        text: qsTr("七种形状循环变形（m3shapes），每 650 ms 一次并旋转；开启“减少动态效果”后只缓慢旋转。软件渲染下为旋转的圆角方块。")
    }
    RowLayout {
        spacing: 24
        LoadingIndicator {}
        LoadingIndicator {
            contained: true
        }
        LoadingIndicator {
            implicitWidth: 96
            implicitHeight: 96
        }
        Switch {
            text: qsTr("减少动态效果")
            checked: Theme.motion.reduced
            onToggled: Theme.motion.reduced = checked
        }
    }
}
