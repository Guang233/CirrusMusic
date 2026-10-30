pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components

// Phase 3 check: navigation rail (collapsed, expanded, modal, and the morph between)
// and the page stack transitions.
ControlPage {
    id: page

    readonly property var destinations: [{
            glyph: Symbols.home,
            text: qsTr("首页")
        }, {
            glyph: Symbols.explore,
            text: qsTr("发现")
        }, {
            glyph: Symbols.search,
            text: qsTr("搜索")
        }, {
            glyph: Symbols.library_music,
            text: qsTr("音乐库")
        }, {
            glyph: Symbols.settings,
            text: qsTr("设置")
        }]

    // A rail in a framed area standing in for the window.
    component Demo: Rectangle {
        id: demo
        property alias rail: rail
        property alias expanded: rail.expanded
        property alias modal: rail.modal
        property bool menuButton: true

        Layout.preferredWidth: 400
        Layout.preferredHeight: 520
        color: Theme.color.surfaceContainerHighest
        radius: Theme.shape.medium
        border.width: 1
        border.color: Theme.color.outlineVariant
        clip: true

        NavigationRail {
            id: rail
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.topMargin: 1
            anchors.bottomMargin: 1
            x: 1
            currentIndex: 1
            header: demo.menuButton ? menuComponent : null

            Component {
                id: menuComponent
                IconButton {
                    x: 8
                    glyph: rail.expanded ? Symbols.menu_open : Symbols.menu
                    text: rail.expanded ? qsTr("收起导航栏") : qsTr("展开导航栏")
                    onClicked: rail.expanded = !rail.expanded
                }
            }

            Repeater {
                model: page.destinations
                delegate: NavigationRailItem {
                    required property var modelData
                    glyph: modelData.glyph
                    text: modelData.text
                    enabled: modelData.text !== qsTr("设置") || !demo.modal
                }
            }
        }

        Text {
            anchors.left: rail.right
            anchors.leftMargin: 24
            anchors.top: parent.top
            anchors.topMargin: 48
            visible: !demo.modal
            text: qsTr("当前：%1").arg(page.destinations[rail.currentIndex].text)
            color: Theme.color.onSurface
            font: Theme.type.titleMedium.font
        }
    }

    SectionTitle {
        text: qsTr("导航栏")
    }
    Caption {
        text: qsTr("收起 96、展开 220–360（按最长标签）。点菜单按钮展开／收起：推挤内容时宽度用 standardDefaultSpatial，浮层用 defaultSpatial；顶部标签在进度 0–0.3 淡出，侧边标签在 0.6–1 淡入。选中指示条从中心展开（fastSpatial），图标填充随选中过渡。")
    }
    RowLayout {
        spacing: 24
        Demo {}
        Demo {
            expanded: true
            menuButton: false
        }
    }
    Caption {
        text: qsTr("浮层样式（modal）：SurfaceContainer、右侧大圆角、2 级阴影；最后一项为禁用状态。遮罩由外壳绘制。")
    }
    Demo {
        expanded: true
        modal: true
    }

    SectionTitle {
        text: qsTr("页面栈")
    }
    Caption {
        text: qsTr("PageStack：前进／返回为横向共享轴（位移 30），切换目的地为淡出后缩放淡入；一个 standardDefaultSpatial 进度驱动。前进途中点返回会反向播放；连续点击时新页面从当前画面开始过渡。减少动态效果时只做 fastEffects 交叉淡入淡出。")
    }
    RowLayout {
        spacing: 12
        Button {
            size: Md.XSmall
            variant: Md.Tonal
            glyph: Symbols.arrow_forward
            text: qsTr("前进")
            onClicked: stack.push(demoPage, {
                number: stack.counter++
            })
        }
        Button {
            size: Md.XSmall
            variant: Md.Tonal
            glyph: Symbols.arrow_back
            text: qsTr("返回")
            enabled: stack.depth > 1
            onClicked: stack.pop()
        }
        Button {
            size: Md.XSmall
            variant: Md.Tonal
            glyph: Symbols.home
            text: qsTr("切换目的地")
            onClicked: stack.replaceAll(demoPage, {
                number: stack.counter++
            })
        }
        Switch {
            text: qsTr("减少动态效果")
            checked: Theme.motion.reduced
            onToggled: Theme.motion.reduced = checked
        }
    }
    Rectangle {
        Layout.preferredWidth: 560
        Layout.preferredHeight: 240
        color: Theme.color.surface
        radius: Theme.shape.medium
        border.width: 1
        border.color: Theme.color.outlineVariant
        clip: true

        PageStack {
            id: stack
            property int counter: 1
            anchors.fill: parent
            anchors.margins: 1
            Component.onCompleted: replaceAll(demoPage, {
                number: counter++
            })
        }
        Text {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 12
            text: qsTr("深度 %1").arg(stack.depth)
            color: Theme.color.onSurfaceVariant
            font: Theme.type.labelMedium.font
        }
    }

    Component {
        id: demoPage
        Item {
            id: demo
            property int number
            Rectangle {
                anchors.fill: parent
                anchors.margins: 16
                radius: Theme.shape.large
                color: [Theme.color.primaryContainer, Theme.color.secondaryContainer,
                    Theme.color.tertiaryContainer][demo.number % 3]
                Text {
                    anchors.centerIn: parent
                    text: qsTr("页面 %1").arg(demo.number)
                    color: [Theme.color.onPrimaryContainer, Theme.color.onSecondaryContainer,
                        Theme.color.onTertiaryContainer][demo.number % 3]
                    font: Theme.type.headlineMedium.font
                }
            }
        }
    }

    SectionTitle {
        text: qsTr("标签页")
    }
    Caption {
        text: qsTr("MD3 主标签页：选中为 Primary，3 px 指示条与标签同宽，切换时滑动（fastSpatial）；放不下时可横向滚动。第二组带图标（高 64）。")
    }
    TabBar {
        Layout.preferredWidth: 560
        TabButton {
            text: qsTr("歌曲")
        }
        TabButton {
            text: qsTr("专辑")
        }
        TabButton {
            text: qsTr("歌手")
        }
        TabButton {
            text: qsTr("歌单")
        }
    }
    TabBar {
        Layout.preferredWidth: 560
        currentIndex: 1
        TabButton {
            glyph: Symbols.favorite
            text: qsTr("喜欢")
        }
        TabButton {
            glyph: Symbols.queue_music
            text: qsTr("歌单")
        }
        TabButton {
            glyph: Symbols.album
            text: qsTr("专辑")
        }
        TabButton {
            glyph: Symbols.history
            text: qsTr("最近播放")
        }
    }
}
