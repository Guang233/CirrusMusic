pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import M3Shapes
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components
import Cirrus.Presentation

// Phase 3 check: every page state of PageState side by side (README §6.4). The states
// are set directly; the "数据与状态" page drives them through a real model.
ControlPage {
    id: page

    PlaylistPageModel {
        id: sample
        playlistKey: "pl-01"
    }

    // A small page frame with one state.
    component Panel: ColumnLayout {
        id: panel
        property string label
        property int loadState
        property var error: ({})
        property bool refreshing: false
        property bool notice: false
        width: 380
        spacing: 8

        Text {
            text: panel.label
            color: Theme.color.onSurfaceVariant
            font: Theme.type.labelLarge.font
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 360
            radius: Theme.shape.large
            color: Theme.color.surface
            border.color: Theme.color.outlineVariant
            clip: true

            PageState {
                id: pageState
                anchors.fill: parent
                loadState: panel.loadState
                error: panel.error
                refreshing: panel.refreshing
                skeletonDelay: 0
            }
            SongList {
                anchors.fill: parent
                topMargin: 16
                sideMargin: 12
                opacity: pageState.contentOpacity
                visible: opacity > 0
                model: sample.songs
                header: NoticeBar {
                    x: 12
                    width: parent.width - 24
                    visible: panel.notice
                    height: visible ? implicitHeight + 12 : 0
                    text: qsTr("部分歌曲信息暂时无法获取")
                }
            }
        }
    }

    SectionTitle {
        text: qsTr("图标容器")
    }
    Caption {
        text: qsTr("IconContainer（expressive.md §3.1）：32、40、56 是圆，96 起可用形状（软件渲染退回圆）；五种色调，inverted 用在同色调的容器上。")
    }
    Flow {
        Layout.fillWidth: true
        spacing: 16
        Repeater {
            model: [Md.Primary, Md.Secondary, Md.Tertiary, Md.Error, Md.Neutral]
            delegate: Row {
                id: toneRow
                required property int modelData
                spacing: 12
                Repeater {
                    model: [32, 40, 56]
                    delegate: IconContainer {
                        required property int modelData
                        anchors.verticalCenter: parent.verticalCenter
                        size: modelData
                        tone: toneRow.modelData
                        glyph: Symbols.favorite
                    }
                }
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 56
                    height: 56
                    radius: Theme.shape.large
                    // The tone's own container, which the inverted circle stands out on.
                    color: inverted.colors[0]
                    IconContainer {
                        id: inverted
                        anchors.centerIn: parent
                        tone: toneRow.modelData
                        inverted: true
                        glyph: Symbols.local_fire_department
                    }
                }
            }
        }
    }
    Row {
        spacing: 16
        IconContainer {
            size: 96
            shape: MaterialShape.Cookie9Sided
            glyph: Symbols.queue_music
        }
        IconContainer {
            size: 96
            shape: MaterialShape.SoftBurst
            tone: Md.Error
            glyph: Symbols.error
        }
        IconContainer {
            size: 96
            shape: MaterialShape.Cookie12Sided
            tone: Md.Tertiary
            glyph: Symbols.leaderboard
        }
    }

    SectionTitle {
        text: qsTr("卡片")
    }
    Caption {
        text: qsTr("CollectionCard：方形封面静止圆角 16，悬停 28，按下 12（fastSpatial，带过冲）；歌手是圆形。")
    }
    Row {
        spacing: 16
        CollectionCard {
            kind: Collection.Playlist
            coverKey: sample.coverKey
            text: sample.title
            subtitle: qsTr("歌单")
            plays: 12345
        }
        CollectionCard {
            kind: Collection.Artist
            coverKey: sample.coverKey
            text: qsTr("歌手")
        }
    }

    SectionTitle {
        text: qsTr("页面状态")
    }
    Caption {
        text: qsTr("PageState：首次加载显示骨架屏（超过 150 ms 才出现，整体呼吸；减少动态效果时静止），空、错误、离线、未登录各有说明和操作，刷新时内容保留、顶部落下加载指示器，部分数据由页面加提示条。")
    }
    Flow {
        Layout.fillWidth: true
        spacing: 16

        Panel {
            label: qsTr("首次加载（Loading）")
            loadState: LoadState.Loading
        }
        Panel {
            label: qsTr("刷新（refreshing）")
            loadState: LoadState.Ready
            refreshing: true
        }
        Panel {
            label: qsTr("部分数据（Partial）")
            loadState: LoadState.Partial
            notice: true
        }
        Panel {
            label: qsTr("空（Empty）")
            loadState: LoadState.Empty
        }
        Panel {
            label: qsTr("错误，可重试（Error）")
            loadState: LoadState.Error
            error: ({
                    message: qsTr("网络连接失败"),
                    retryable: true
                })
        }
        Panel {
            label: qsTr("错误，不可重试（Error）")
            loadState: LoadState.Error
            error: ({
                    message: qsTr("内容暂时无法获取"),
                    retryable: false
                })
        }
        Panel {
            label: qsTr("离线（Offline）")
            loadState: LoadState.Offline
            error: ({
                    message: qsTr("网络不可用，恢复后会自动重试")
                })
        }
        Panel {
            label: qsTr("未登录（SignedOut）")
            loadState: LoadState.SignedOut
            error: ({
                    message: qsTr("登录后才能查看")
                })
        }
    }
}
