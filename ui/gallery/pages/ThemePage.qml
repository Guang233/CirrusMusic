pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic as B
import QtQuick.Layouts
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Samples

// Phase 1 check: cover-driven scheme, color roles and contrast, type scale,
// shapes, state layers and spring motion tokens.
B.ScrollView {
    id: page

    ScrollBar.vertical: ScrollBar {
        parent: page
        x: page.width - width
        height: page.availableHeight
    }

    readonly property list<string> typeStyles: ["displayLarge", "displayMedium", "displaySmall", "headlineLarge", "headlineMedium", "headlineSmall", "titleLarge", "titleMedium", "titleSmall", "bodyLarge", "bodyMedium", "bodySmall", "labelLarge", "labelMedium", "labelSmall"]
    readonly property list<string> shapes: ["none", "extraSmall", "small", "medium", "large", "largeIncreased", "extraLarge", "extraLargeIncreased", "extraExtraLarge", "full"]
    readonly property list<string> springs: ["fastSpatial", "defaultSpatial", "slowSpatial", "standardFastSpatial", "standardDefaultSpatial", "standardSlowSpatial", "fastEffects", "defaultEffects", "slowEffects"]
    // Foreground / background pairs that carry text.
    readonly property var contrastPairs: [["onPrimary", "primary"], ["onPrimaryContainer", "primaryContainer"], ["onSecondary", "secondary"], ["onSecondaryContainer", "secondaryContainer"], ["onTertiary", "tertiary"], ["onTertiaryContainer", "tertiaryContainer"], ["onError", "error"], ["onErrorContainer", "errorContainer"], ["onSurface", "surface"], ["onSurfaceVariant", "surfaceContainerHighest"], ["inverseOnSurface", "inverseSurface"], ["primary", "surface"]]
    property bool moved: false

    contentWidth: availableWidth

    component SectionTitle: Text {
        color: Theme.color.onSurface
        font: Theme.type.headlineSmall.font
        Layout.topMargin: 24
    }

    component Caption: Text {
        color: Theme.color.onSurfaceVariant
        font: Theme.type.bodyMedium.font
        wrapMode: Text.Wrap
        Layout.fillWidth: true
    }

    component Swatch: Rectangle {
        required property color swatch
        width: 28
        height: 28
        radius: Theme.shape.full
        color: swatch
        border.color: Theme.color.outlineVariant
    }

    ColumnLayout {
        width: page.availableWidth - 48
        x: 24
        spacing: 8

        SectionTitle {
            text: qsTr("种子与封面")
            Layout.topMargin: 16
        }
        Flow {
            Layout.fillWidth: true
            spacing: 8
            Repeater {
                model: ["", ...Samples.covers]
                delegate: Rectangle {
                    id: coverCell
                    required property string modelData
                    readonly property bool selected: modelData === "" ? Theme.cover.toString() === "" : Theme.cover === Samples.cover(modelData)
                    width: 64
                    height: 64
                    radius: Theme.shape.medium
                    color: Theme.color.surfaceContainerHighest
                    border.width: selected ? 3 : 0
                    border.color: Theme.color.primary
                    Image {
                        anchors.fill: parent
                        anchors.margins: coverCell.selected ? 5 : 0
                        visible: coverCell.modelData !== ""
                        source: coverCell.modelData === "" ? "" : Samples.cover(coverCell.modelData)
                        sourceSize: Qt.size(128, 128)
                        fillMode: Image.PreserveAspectCrop
                    }
                    Text {
                        anchors.centerIn: parent
                        visible: coverCell.modelData === ""
                        text: qsTr("无")
                        color: Theme.color.onSurfaceVariant
                        font: Theme.type.labelLarge.font
                    }
                    TapHandler {
                        onTapped: Theme.cover = coverCell.modelData === "" ? "" : Samples.cover(coverCell.modelData)
                    }
                }
            }
        }

        GridLayout {
            columns: 5
            columnSpacing: 16
            rowSpacing: 4
            Caption {
                text: qsTr("种子来源")
                Layout.fillWidth: false
            }
            Caption {
                text: qsTr("方案变体")
                Layout.fillWidth: false
            }
            Caption {
                text: qsTr("明暗")
                Layout.fillWidth: false
            }
            Caption {
                text: qsTr("配色规范")
                Layout.fillWidth: false
            }
            Caption {
                text: qsTr("对比度 %1").arg(Theme.contrast.toFixed(1))
                Layout.fillWidth: false
            }
            B.ComboBox {
                model: [qsTr("封面"), qsTr("固定"), qsTr("系统强调色")]
                currentIndex: Theme.seedSource
                onActivated: index => Theme.seedSource = index
            }
            B.ComboBox {
                implicitContentWidthPolicy: B.ComboBox.WidestText
                model: ["TonalSpot", "Expressive", "Vibrant", "Content", "Fidelity", "Neutral", "Monochrome", "Rainbow", "FruitSalad"]
                currentIndex: Theme.variant
                onActivated: index => Theme.variant = index
            }
            B.ComboBox {
                model: [qsTr("跟随系统"), qsTr("亮色"), qsTr("暗色")]
                currentIndex: Theme.mode
                onActivated: index => Theme.mode = index
            }
            B.ComboBox {
                model: ["2021", "2025"]
                currentIndex: Theme.specVersion
                onActivated: index => Theme.specVersion = index
            }
            Slider {
                from: -1
                to: 1
                stepSize: 0.5
                value: Theme.contrast
                onMoved: Theme.contrast = value
            }
        }

        RowLayout {
            spacing: 12
            Caption {
                text: qsTr("系统强调色")
                Layout.fillWidth: false
            }
            Swatch {
                swatch: Theme.systemAccent.valid ? Theme.systemAccent : "transparent"
            }
            Caption {
                text: Theme.systemAccent.valid ? Theme.systemAccent.toString() : qsTr("无")
                Layout.fillWidth: false
            }
            Caption {
                text: qsTr("种子")
                Layout.leftMargin: 16
                Layout.fillWidth: false
            }
            Swatch {
                swatch: Theme.seed
            }
            Caption {
                text: Theme.seed.toString()
                Layout.fillWidth: false
            }
            Caption {
                text: qsTr("候选")
                Layout.leftMargin: 16
                Layout.fillWidth: false
            }
            Repeater {
                model: Theme.coverPalette.accents
                delegate: Swatch {
                    required property color modelData
                    swatch: modelData
                }
            }
            Caption {
                text: qsTr("主色（按占比）")
                Layout.leftMargin: 16
                Layout.fillWidth: false
            }
            Repeater {
                model: Theme.coverPalette.dominant
                delegate: Swatch {
                    required property color modelData
                    swatch: modelData
                }
            }
            Caption {
                text: Theme.extracting ? qsTr("取色中…") : (Theme.coverPalette.valid ? "" : qsTr("未取色，使用固定种子"))
                Layout.leftMargin: 16
            }
        }

        SectionTitle {
            text: qsTr("配色（%1 个角色）").arg(Theme.color.roles.length)
        }
        Flow {
            Layout.fillWidth: true
            spacing: 6
            Repeater {
                model: Theme.color.roles
                delegate: Rectangle {
                    id: roleCell
                    required property string modelData
                    readonly property color roleColor: Theme.color[modelData]
                    width: 168
                    height: 56
                    radius: Theme.shape.small
                    color: roleColor
                    border.color: Theme.color.outlineVariant
                    Column {
                        anchors.fill: parent
                        anchors.margins: 8
                        Text {
                            width: parent.width
                            text: roleCell.modelData
                            elide: Text.ElideRight
                            font: Theme.type.labelMedium.font
                            color: roleCell.roleColor.hslLightness > 0.55 ? "black" : "white"
                        }
                        Text {
                            text: roleCell.roleColor.toString()
                            font: Theme.type.labelSmall.font
                            color: roleCell.roleColor.hslLightness > 0.55 ? "black" : "white"
                        }
                    }
                }
            }
        }

        SectionTitle {
            text: qsTr("对比度（正文要求 ≥ 4.5）")
        }
        Flow {
            Layout.fillWidth: true
            spacing: 6
            Repeater {
                model: page.contrastPairs
                delegate: Rectangle {
                    id: pairCell
                    required property var modelData
                    readonly property real ratio: Theme.contrastRatio(Theme.color[modelData[0]], Theme.color[modelData[1]])
                    width: 252
                    height: 44
                    radius: Theme.shape.small
                    color: Theme.color[modelData[1]]
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        x: 12
                        width: parent.width - 24
                        elide: Text.ElideRight
                        text: pairCell.modelData[0] + " · " + pairCell.ratio.toFixed(1) + (pairCell.ratio >= 4.5 ? "" : "  ✗")
                        color: Theme.color[pairCell.modelData[0]]
                        font: Theme.type.labelLarge.font
                    }
                }
            }
        }

        SectionTitle {
            text: qsTr("字体层级（基础 / 强调）")
        }
        Repeater {
            model: page.typeStyles
            delegate: RowLayout {
                id: typeRow
                required property string modelData
                Layout.fillWidth: true
                spacing: 16
                Text {
                    text: typeRow.modelData
                    color: Theme.color.onSurfaceVariant
                    font: Theme.type.labelMedium.font
                    Layout.preferredWidth: 120
                }
                Text {
                    text: qsTr("落日航线 Harbor Lights")
                    color: Theme.color.onSurface
                    font: Theme.type[typeRow.modelData].font
                    lineHeight: Theme.type[typeRow.modelData].lineHeight
                    lineHeightMode: Text.FixedHeight
                    elide: Text.ElideRight
                    Layout.preferredWidth: 520
                }
                Text {
                    text: qsTr("落日航线 Harbor Lights")
                    color: Theme.color.onSurface
                    font: Theme.type[typeRow.modelData + "Emphasized"].font
                    lineHeight: Theme.type[typeRow.modelData + "Emphasized"].lineHeight
                    lineHeightMode: Text.FixedHeight
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }
        }

        SectionTitle {
            text: qsTr("圆角")
        }
        Flow {
            Layout.fillWidth: true
            spacing: 12
            Repeater {
                model: page.shapes
                delegate: Column {
                    id: shapeCell
                    required property string modelData
                    spacing: 4
                    Rectangle {
                        width: 96
                        height: 72
                        radius: Theme.shape[shapeCell.modelData]
                        color: Theme.color.primaryContainer
                    }
                    Text {
                        text: shapeCell.modelData + " " + (shapeCell.modelData === "full" ? "" : Theme.shape[shapeCell.modelData])
                        color: Theme.color.onSurfaceVariant
                        font: Theme.type.labelSmall.font
                    }
                }
            }
        }

        SectionTitle {
            text: qsTr("状态层")
        }
        Row {
            spacing: 12
            Repeater {
                model: ["hover", "focus", "pressed", "dragged"]
                delegate: Rectangle {
                    id: layerCell
                    required property string modelData
                    width: 120
                    height: 48
                    radius: Theme.shape.full
                    color: Theme.color.secondaryContainer
                    Rectangle {
                        anchors.fill: parent
                        radius: parent.radius
                        color: Theme.color.onSecondaryContainer
                        opacity: Theme.state[layerCell.modelData]
                    }
                    Text {
                        anchors.centerIn: parent
                        text: layerCell.modelData + " " + Theme.state[layerCell.modelData]
                        color: Theme.color.onSecondaryContainer
                        font: Theme.type.labelLarge.font
                    }
                }
            }
        }

        SectionTitle {
            text: qsTr("动效")
        }
        RowLayout {
            spacing: 16
            Button {
                text: qsTr("播放")
                onClicked: page.moved = !page.moved
            }
            Switch {
                text: qsTr("减少动态效果")
                checked: Theme.motion.reduced
                onToggled: Theme.motion.reduced = checked
            }
            Caption {
                text: qsTr("曲线：0–700 ms 内 0→1 的位置；虚线为目标值。圆点由 Spring 驱动，可在运动中再次点击以检验速度连续。")
            }
        }
        Repeater {
            model: page.springs
            delegate: RowLayout {
                id: springRow
                required property string modelData
                readonly property var spec: Theme.motion[modelData]
                Layout.fillWidth: true
                spacing: 16
                Text {
                    text: springRow.modelData + "\n" + springRow.spec.dampingRatio + " / " + springRow.spec.stiffness + " · " + springRow.spec.duration + " ms"
                    color: Theme.color.onSurfaceVariant
                    font: Theme.type.labelMedium.font
                    Layout.preferredWidth: 180
                }
                Canvas {
                    id: plot
                    Layout.preferredWidth: 220
                    Layout.preferredHeight: 56
                    readonly property color lineColor: Theme.color.primary
                    readonly property color guideColor: Theme.color.outlineVariant
                    onLineColorChanged: requestPaint()
                    onPaint: {
                        const ctx = getContext("2d");
                        ctx.reset();
                        const top = 8, bottom = height - 4;
                        const y = v => bottom - (bottom - top) * v;
                        ctx.strokeStyle = guideColor;
                        ctx.setLineDash([3, 3]);
                        ctx.beginPath();
                        ctx.moveTo(0, y(1));
                        ctx.lineTo(width, y(1));
                        ctx.stroke();
                        ctx.setLineDash([]);
                        ctx.strokeStyle = lineColor;
                        ctx.lineWidth = 2;
                        ctx.beginPath();
                        const points = Theme.motion.curve(springRow.spec, 120, 0.7);
                        for (let i = 0; i < points.length; ++i) {
                            const px = width * i / (points.length - 1);
                            if (i === 0)
                                ctx.moveTo(px, y(points[i]));
                            else
                                ctx.lineTo(px, y(points[i]));
                        }
                        ctx.stroke();
                    }
                }
                Item {
                    id: track
                    Layout.fillWidth: true
                    Layout.preferredHeight: 28
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width
                        height: 4
                        radius: 2
                        color: Theme.color.surfaceContainerHighest
                    }
                    Spring {
                        id: dot
                        target: page.moved ? track.width - 28 : 0
                        spec: springRow.spec
                        threshold: 0.5
                    }
                    Rectangle {
                        x: dot.value
                        width: 28
                        height: 28
                        radius: Theme.shape.full
                        color: Theme.color.primary
                    }
                }
            }
        }

        Caption {
            text: qsTr("对照：同一 defaultSpatial 令牌，上为 Spring，中为 NumberAnimation + 弹簧缓动，下为 Qt SpringAnimation 换算参数（spring = k·0.016，damping = 2ζ√k·0.016）。")
            Layout.topMargin: 8
        }
        Item {
            id: springTrack
            Layout.fillWidth: true
            Layout.preferredHeight: 32
            Spring {
                id: compareSpring
                target: page.moved ? springTrack.width - 28 : 0
                spec: Theme.motion.defaultSpatial
                threshold: 0.5
            }
            Rectangle {
                x: compareSpring.value
                width: 28
                height: 28
                radius: Theme.shape.full
                color: Theme.color.primary
            }
        }
        Item {
            id: easingTrack
            Layout.fillWidth: true
            Layout.preferredHeight: 32
            Rectangle {
                x: page.moved ? easingTrack.width - 28 : 0
                width: 28
                height: 28
                radius: Theme.shape.full
                color: Theme.color.tertiary
                Behavior on x {
                    NumberAnimation {
                        duration: Theme.motion.defaultSpatial.duration
                        easing: Theme.motion.defaultSpatial.easing
                    }
                }
            }
        }
        Item {
            id: qtTrack
            Layout.fillWidth: true
            Layout.preferredHeight: 32
            Rectangle {
                x: page.moved ? qtTrack.width - 28 : 0
                width: 28
                height: 28
                radius: Theme.shape.full
                color: Theme.color.error
                Behavior on x {
                    SpringAnimation {
                        spring: 380 * 0.016
                        damping: 2 * 0.8 * Math.sqrt(380) * 0.016
                    }
                }
            }
        }
        Item {
            Layout.preferredHeight: 24
        }
    }
}
