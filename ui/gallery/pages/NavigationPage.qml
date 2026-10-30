pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Cirrus.Theme
import Cirrus.Controls

// Phase 3 check: navigation rail (collapsed, expanded, modal, and the morph between).
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
}
