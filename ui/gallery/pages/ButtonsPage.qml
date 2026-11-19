pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import M3Shapes
import Cirrus.Theme
import Cirrus.Controls

// Phase 2 check: buttons, toggle buttons, icon buttons, button groups, shapes.
ControlPage {
    id: page

    readonly property var variants: [
        { name: "Filled", value: Md.Filled },
        { name: "Tonal", value: Md.Tonal },
        { name: "Outlined", value: Md.Outlined },
        { name: "Elevated", value: Md.Elevated },
        { name: "Text", value: Md.Text }
    ]
    readonly property var iconVariants: [
        { name: "Standard", value: Md.Standard },
        { name: "Filled", value: Md.Filled },
        { name: "Tonal", value: Md.Tonal },
        { name: "Outlined", value: Md.Outlined }
    ]
    readonly property list<string> sizeNames: ["XSmall", "Small", "Medium", "Large", "XLarge"]

    SectionTitle {
        text: qsTr("按钮")
    }
    Caption {
        text: qsTr("五种样式 × 启用／禁用。按下时圆角收紧（defaultEffects，不回弹）；Tab 键可检查焦点环。")
    }
    Repeater {
        model: page.variants
        delegate: RowLayout {
            id: variantRow
            required property var modelData
            spacing: 12
            Caption {
                text: variantRow.modelData.name
                Layout.preferredWidth: 80
                Layout.fillWidth: false
            }
            Button {
                variant: variantRow.modelData.value
                text: qsTr("播放全部")
            }
            Button {
                variant: variantRow.modelData.value
                glyph: Symbols.play_arrow
                text: qsTr("播放全部")
            }
            Button {
                variant: variantRow.modelData.value
                shape: Md.Square
                glyph: Symbols.shuffle
                text: qsTr("随机播放")
            }
            Button {
                variant: variantRow.modelData.value
                trailingGlyph: Symbols.expand_more
                text: qsTr("排序")
            }
            Button {
                variant: variantRow.modelData.value
                glyph: Symbols.download
                text: qsTr("下载")
                enabled: false
            }
        }
    }

    Caption {
        text: qsTr("状态（静态检查用）：静止／键盘焦点／按下（涟漪停在扩散后）／禁用")
    }
    RowLayout {
        spacing: 16
        Button {
            text: qsTr("静止")
        }
        Button {
            id: focusSample
            text: qsTr("焦点")
        }
        Button {
            id: downSample
            text: qsTr("按下")
        }
        IconButton {
            id: downIconSample
            variant: Md.Tonal
            glyph: Symbols.favorite
            text: qsTr("按下")
        }
        Button {
            text: qsTr("禁用")
            enabled: false
        }
        Timer {
            running: true
            interval: 300
            onTriggered: {
                focusSample.forceActiveFocus(Qt.TabFocusReason);
                downSample.down = true;
                downIconSample.down = true;
            }
        }
    }

    SectionTitle {
        text: qsTr("尺寸")
    }
    Flow {
        Layout.fillWidth: true
        spacing: 12
        Repeater {
            model: 5
            delegate: Button {
                required property int index
                size: index
                glyph: Symbols.play_arrow
                text: page.sizeNames[index]
            }
        }
    }
    Flow {
        Layout.fillWidth: true
        spacing: 12
        Repeater {
            model: 5
            delegate: Button {
                required property int index
                size: index
                shape: Md.Square
                variant: Md.Tonal
                text: page.sizeNames[index]
            }
        }
    }

    SectionTitle {
        text: qsTr("切换按钮")
    }
    Caption {
        text: qsTr("checkable：选中后圆形变方形（fastSpatial，带回弹），颜色按 defaultEffects 过渡。")
    }
    Flow {
        Layout.fillWidth: true
        spacing: 12
        Repeater {
            model: page.variants.slice(0, 4)
            delegate: Button {
                required property var modelData
                variant: modelData.value
                checkable: true
                checked: modelData.value === Md.Tonal
                glyph: Symbols.favorite
                text: modelData.name
            }
        }
        Button {
            size: Md.Medium
            shape: Md.Square
            checkable: true
            glyph: Symbols.repeat
            text: qsTr("方形 → 圆形")
        }
    }

    SectionTitle {
        text: qsTr("图标按钮")
    }
    Caption {
        text: qsTr("四种样式；第二行为可切换（选中时图标由空心变实心）。悬停 500 ms 显示提示。")
    }
    Repeater {
        model: page.iconVariants
        delegate: RowLayout {
            id: iconRow
            required property var modelData
            spacing: 8
            Caption {
                text: iconRow.modelData.name
                Layout.preferredWidth: 80
                Layout.fillWidth: false
            }
            IconButton {
                variant: iconRow.modelData.value
                glyph: Symbols.skip_previous
                text: qsTr("上一首")
            }
            IconButton {
                variant: iconRow.modelData.value
                glyph: Symbols.more_vert
                text: qsTr("更多")
            }
            IconButton {
                variant: iconRow.modelData.value
                checkable: true
                glyph: Symbols.favorite
                text: qsTr("收藏")
            }
            IconButton {
                variant: iconRow.modelData.value
                checkable: true
                checked: true
                shape: Md.Square
                glyph: Symbols.lyrics
                text: qsTr("歌词")
            }
            IconButton {
                variant: iconRow.modelData.value
                glyph: Symbols.delete_
                text: qsTr("删除")
                enabled: false
            }
        }
    }
    Caption {
        text: qsTr("悬浮操作按钮（Fab）：主色、第二色、第三色容器")
    }
    Row {
        spacing: 16
        Fab {
            glyph: Symbols.play_arrow
            iconFill: 1
            text: qsTr("播放")
        }
        Fab {
            tone: Md.Secondary
            glyph: Symbols.queue_music
            text: qsTr("播放队列")
        }
        Fab {
            tone: Md.Tertiary
            glyph: Symbols.lyrics
            text: qsTr("播放页")
        }
    }
    Caption {
        text: qsTr("尺寸 XSmall–XLarge，宽度 窄／默认／宽")
    }
    Flow {
        Layout.fillWidth: true
        spacing: 12
        Repeater {
            model: 5
            delegate: IconButton {
                required property int index
                size: index
                variant: Md.Tonal
                glyph: Symbols.play_arrow
                text: page.sizeNames[index]
            }
        }
        Repeater {
            model: [Md.Narrow, Md.Default, Md.Wide]
            delegate: IconButton {
                required property int modelData
                size: Md.Medium
                widthMode: modelData
                variant: Md.Filled
                shape: Md.Square
                glyph: Symbols.shuffle
                text: qsTr("随机")
            }
        }
    }

    SectionTitle {
        text: qsTr("按钮组")
    }
    Caption {
        text: qsTr("标准按钮组：按住一个按钮，它会变宽，相邻按钮让出空间。")
    }
    StandardButtonGroup {
        IconButton {
            size: Md.Medium
            variant: Md.Tonal
            glyph: Symbols.skip_previous
            text: qsTr("上一首")
        }
        IconButton {
            size: Md.Medium
            widthMode: Md.Wide
            variant: Md.Filled
            shape: Md.Square
            glyph: Symbols.play_arrow
            text: qsTr("播放")
        }
        IconButton {
            size: Md.Medium
            variant: Md.Tonal
            glyph: Symbols.skip_next
            text: qsTr("下一首")
        }
    }
    Caption {
        text: qsTr("连接按钮组（单选）：选中项变为全圆角。")
    }
    ConnectedButtonGroup {
        exclusive: true
        Repeater {
            model: [qsTr("日榜"), qsTr("周榜"), qsTr("月榜"), qsTr("总榜")]
            delegate: Button {
                required property string modelData
                required property int index
                variant: Md.Tonal
                checkable: true
                checked: index === 1
                text: modelData
            }
        }
    }
    ConnectedButtonGroup {
        IconButton {
            variant: Md.Outlined
            checkable: true
            glyph: Symbols.shuffle
            text: qsTr("随机")
        }
        IconButton {
            variant: Md.Outlined
            checkable: true
            glyph: Symbols.repeat
            text: qsTr("列表循环")
        }
        IconButton {
            variant: Md.Outlined
            checkable: true
            glyph: Symbols.repeat_one
            text: qsTr("单曲循环")
        }
    }

    SectionTitle {
        text: qsTr("形状变形")
    }
    Caption {
        text: qsTr("m3shapes：点击切换形状，变形沿 defaultSpatial。软件渲染下显示为圆角矩形。")
    }
    RowLayout {
        spacing: 16
        MorphShape {
            id: morph
            property int step: 0
            readonly property list<int> sequence: [MaterialShape.Circle, MaterialShape.Cookie9Sided,
                MaterialShape.Sunny, MaterialShape.Pill, MaterialShape.Burst, MaterialShape.Heart,
                MaterialShape.Square, MaterialShape.Flower]
            implicitWidth: 96
            implicitHeight: 96
            shape: sequence[step % sequence.length]
            color: Theme.color.primary
        }
        Button {
            variant: Md.Tonal
            text: qsTr("下一个形状")
            onClicked: morph.step++
        }
    }
}
