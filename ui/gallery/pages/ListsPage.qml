pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Samples

// Phase 2 check: list items, menus, tooltips.
ControlPage {
    id: page

    readonly property var songs: [
        { title: "落日航线", artist: "雾港乐队", album: "港口来信", cover: "sunset", time: "4:12" },
        { title: "Last Train Home", artist: "Night Transit · Prism Theory", album: "Midnight Line", cover: "neon", time: "3:48" },
        { title: "一首标题非常非常长、长到需要省略号才能显示完整的歌曲（Live 版）", artist: "林间信号", album: "森林广播", cover: "forest", time: "6:30" },
        { title: "Low Tide", artist: "Pastel Orbit", album: "Soft Machines", cover: "pastel", time: "--:--" }
    ]

    SectionTitle {
        text: qsTr("列表项")
    }
    Caption {
        text: qsTr("一行／两行／三行；第二首为选中（highlighted）。右键歌曲打开菜单。")
    }
    Rectangle {
        Layout.preferredWidth: 560
        Layout.preferredHeight: songList.implicitHeight
        radius: Theme.shape.large
        color: Theme.color.surfaceContainerLow
        clip: true

        Column {
            id: songList
            width: parent.width
            Repeater {
                model: page.songs
                delegate: ListItem {
                    id: songItem
                    required property var modelData
                    required property int index
                    width: songList.width
                    first: index === 0
                    last: index === page.songs.length - 1
                    text: modelData.title
                    supportingText: modelData.artist + " · " + modelData.album
                    trailingText: modelData.time
                    highlighted: index === 1
                    enabled: index !== 3
                    leading: Component {
                        Image {
                            width: 56
                            height: 56
                            source: Samples.cover(songItem.modelData.cover)
                            sourceSize: Qt.size(112, 112)
                            fillMode: Image.PreserveAspectCrop
                        }
                    }

                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        onTapped: (point, button) => songMenu.popup(songItem)
                    }
                }
            }
        }
    }

    Rectangle {
        Layout.preferredWidth: 560
        Layout.preferredHeight: settingsList.implicitHeight
        radius: Theme.shape.large
        color: Theme.color.surfaceContainerLow
        clip: true

        Column {
            id: settingsList
            width: parent.width
            ListItem {
                width: parent.width
                first: true
                text: qsTr("音质")
                leadingGlyph: Symbols.graphic_eq
                trailingGlyph: Symbols.chevron_right
            }
            ListItem {
                id: lyricItem
                width: parent.width
                text: qsTr("桌面歌词")
                supportingText: qsTr("在其他窗口之上显示当前歌词")
                leadingGlyph: Symbols.lyrics
                onClicked: (trailingItem as Switch)?.toggle()
                trailing: Component {
                    Switch {
                        focusPolicy: Qt.NoFocus
                        checked: true
                    }
                }
            }
            ListItem {
                width: parent.width
                overline: qsTr("账号")
                text: qsTr("雾港听众")
                supportingText: qsTr("黑胶会员 · 2027-03-01 到期；三行列表项的辅助文字可以换行，最多显示两行。")
                supportingMaximumLines: 2
                last: true
                leadingGlyph: Symbols.account_circle
            }
        }
    }

    Caption {
        text: qsTr("分段列表（segmented）：项间距 2，静止 4 px 圆角，悬停 12 px，按下／选中 16 px；首末项外侧 16 px（first／last）。放在圆角容器里的普通列表也设 first／last，悬停层和涟漪跟随容器圆角。")
    }
    Column {
        spacing: 2
        Repeater {
            model: [qsTr("我喜欢的音乐"), qsTr("深夜通勤"), qsTr("周末慢歌")]
            delegate: ListItem {
                required property string modelData
                required property int index
                width: 360
                segmented: true
                first: index === 0
                last: index === 2
                containerColor: Theme.color.surfaceContainer
                highlighted: index === 0
                text: modelData
                leadingGlyph: index === 0 ? Symbols.favorite : Symbols.queue_music
            }
        }
    }

    SectionTitle {
        text: qsTr("菜单")
    }
    Caption {
        text: qsTr("MD3E 标准菜单：首末项外侧圆角 12，选中项 TertiaryContainer；方向键与 Esc 可操作。")
    }
    RowLayout {
        spacing: 16
        Button {
            id: menuButton
            variant: Md.Tonal
            trailingGlyph: Symbols.expand_more
            text: qsTr("排序方式")
            onClicked: sortMenu.open()

            Menu {
                id: sortMenu
                y: menuButton.height + 4
                MenuItem {
                    text: qsTr("默认排序")
                    checkable: true
                    checked: true
                    T.ButtonGroup.group: sortGroup
                }
                MenuItem {
                    text: qsTr("按歌名")
                    checkable: true
                    T.ButtonGroup.group: sortGroup
                }
                MenuItem {
                    text: qsTr("按歌手")
                    checkable: true
                    T.ButtonGroup.group: sortGroup
                }
                MenuItem {
                    text: qsTr("按添加时间")
                    checkable: true
                    T.ButtonGroup.group: sortGroup
                }
            }
            T.ButtonGroup {
                id: sortGroup
            }
        }
        IconButton {
            id: moreButton
            glyph: Symbols.more_vert
            text: qsTr("更多操作")
            onClicked: songMenu.popup(moreButton, 0, moreButton.height)
        }
        IconButton {
            id: tipButton
            glyph: Symbols.info
            text: qsTr("提示：悬停 500 ms 后出现")

            ToolTip {
                visible: GalleryState.capturing
                text: tipButton.text
            }
        }
    }

    Menu {
        id: songMenu
        MenuItem {
            glyph: Symbols.playlist_play
            text: qsTr("下一首播放")
        }
        MenuItem {
            glyph: Symbols.playlist_add
            text: qsTr("收藏到歌单")
        }
        Menu {
            title: qsTr("分享")
            MenuItem {
                text: qsTr("复制链接")
            }
            MenuItem {
                text: qsTr("生成海报")
            }
        }
        MenuSeparator {}
        MenuItem {
            glyph: Symbols.download
            text: qsTr("下载")
            trailingText: "Ctrl+D"
        }
        MenuItem {
            glyph: Symbols.delete_
            text: qsTr("从歌单中删除")
            enabled: false
        }
    }

    // Screenshot runs open the menu so it shows up in the grab.
    Timer {
        running: GalleryState.capturing
        interval: 200
        onTriggered: songMenu.popup(menuButton, 0, menuButton.height + 4)
    }
}
