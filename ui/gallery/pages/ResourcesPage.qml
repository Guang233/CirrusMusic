pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Samples

// Phase 0 check: bundled fonts and variable axes, CJK fallback, icons, sample covers.
ScrollView {
    id: page

    required property color foreground
    required property color foregroundMuted

    contentWidth: availableWidth

    component SectionTitle: Label {
        color: page.foreground
        font.pixelSize: 22
        font.weight: Font.DemiBold
        Layout.topMargin: 24
    }

    component Caption: Label {
        color: page.foregroundMuted
        font.pixelSize: 13
        wrapMode: Text.Wrap
        Layout.fillWidth: true
    }

    component Sample: Text {
        color: page.foreground
        font.family: Fonts.sans
        font.pixelSize: 22
        elide: Text.ElideRight
        Layout.fillWidth: true
    }

    ColumnLayout {
        width: page.availableWidth - 48
        x: 24
        spacing: 8

        SectionTitle {
            text: qsTr("字体")
        }
        Caption {
            text: qsTr("拉丁：%1 · 图标：%2").arg(Fonts.sans || qsTr("未加载")).arg(Fonts.symbols || qsTr("未加载"))
        }
        Caption {
            text: qsTr("中文回退（已安装）：%1").arg(Fonts.installedFallbacks.length > 0 ? Fonts.installedFallbacks.join("、") : qsTr("无，使用系统默认"))
        }

        Caption {
            text: qsTr("字重 wght 100–1000（中文回退字体按 font.weight 选取最接近字重）")
            Layout.topMargin: 12
        }
        Repeater {
            model: [100, 200, 300, 400, 500, 600, 700, 800, 900, 1000]
            delegate: Sample {
                required property int modelData
                text: modelData + "  Harbor Lights 落日航线 0123456789"
                font.weight: Math.min(modelData, 900)
                font.variableAxes: ({
                        "wght": modelData
                    })
            }
        }

        Caption {
            text: qsTr("字宽 wdth、圆度 ROND、光学尺寸 opsz、倾斜 slnt")
            Layout.topMargin: 12
        }
        Repeater {
            model: [
                {
                    "label": "wdth 25",
                    "axes": {
                        "wdth": 25
                    }
                },
                {
                    "label": "wdth 151",
                    "axes": {
                        "wdth": 151
                    }
                },
                {
                    "label": "ROND 0",
                    "axes": {
                        "ROND": 0,
                        "wght": 700
                    }
                },
                {
                    "label": "ROND 100",
                    "axes": {
                        "ROND": 100,
                        "wght": 700
                    }
                },
                {
                    "label": "opsz 6",
                    "axes": {
                        "opsz": 6
                    }
                },
                {
                    "label": "opsz 144",
                    "axes": {
                        "opsz": 144
                    }
                },
                {
                    "label": "slnt -10",
                    "axes": {
                        "slnt": -10
                    }
                }
            ]
            delegate: Sample {
                required property var modelData
                text: modelData.label + "  Expressive Music Player"
                font.variableAxes: modelData.axes
            }
        }

        Caption {
            text: qsTr("混排与回退：中文、日文、希腊、西里尔、emoji")
            Layout.topMargin: 12
        }
        Sample {
            text: "Cirrus 云端音乐 · 雾港乐队《落日航线》— きいろい夢 · Ωμέγα · Привет · 🎵"
        }
        Sample {
            text: "播放中：Last Train Home — Night Transit, Prism Theory (2025)"
            font.pixelSize: 15
        }

        SectionTitle {
            text: qsTr("图标（%1 个）").arg(Symbols.names.length)
        }
        RowLayout {
            spacing: 16
            Label {
                text: qsTr("填充")
                color: page.foreground
            }
            Switch {
                id: fillSwitch
            }
            Label {
                text: qsTr("字重 %1").arg(weightSlider.value)
                color: page.foreground
            }
            Slider {
                id: weightSlider
                from: 100
                to: 700
                stepSize: 100
                value: 400
            }
            Label {
                text: qsTr("尺寸 %1").arg(sizeSlider.value)
                color: page.foreground
            }
            Slider {
                id: sizeSlider
                from: 20
                to: 48
                stepSize: 4
                value: 24
            }
        }
        Flow {
            Layout.fillWidth: true
            spacing: 4
            Repeater {
                model: Symbols.names
                delegate: Column {
                    id: iconCell
                    required property string modelData
                    width: 96
                    spacing: 2
                    Icon {
                        anchors.horizontalCenter: parent.horizontalCenter
                        glyph: Symbols[iconCell.modelData]
                        size: sizeSlider.value
                        fill: fillSwitch.checked ? 1 : 0
                        weight: weightSlider.value
                        color: page.foreground
                    }
                    Text {
                        width: iconCell.width
                        text: iconCell.modelData
                        color: page.foregroundMuted
                        font.pixelSize: 10
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideMiddle
                    }
                }
            }
        }

        SectionTitle {
            text: qsTr("示例封面（%1 张）").arg(Samples.covers.length)
        }
        Flow {
            Layout.fillWidth: true
            Layout.bottomMargin: 24
            spacing: 12
            Repeater {
                model: Samples.covers
                delegate: Column {
                    id: coverCell
                    required property string modelData
                    spacing: 4
                    Image {
                        width: 128
                        height: 128
                        source: Samples.cover(coverCell.modelData)
                        sourceSize: Qt.size(256, 256)
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                    }
                    Text {
                        text: coverCell.modelData
                        color: page.foregroundMuted
                        font.pixelSize: 12
                    }
                }
            }
        }
    }
}
