pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components
import Cirrus.Presentation

// Phase 3 check: the presentation layer on fake data. A PlaylistPageModel plus the
// fake source knobs (App.debug), to trigger every load state, and App.player on the
// fake playback with the mini player.
ControlPage {
    id: page

    readonly property list<string> stateNames: ["Idle", "Loading", "Ready", "Empty", "Error",
        "Offline", "SignedOut", "Partial"]
    readonly property list<string> statusNames: ["Empty", "Loading", "Playing", "Paused", "Ended",
        "Failed"]

    // Screenshots show the mini player mid-song.
    Component.onCompleted: {
        if (GalleryState.capturing)
            App.player.playSongs(["so-001", "so-002"], 0);
    }

    PlaylistPageModel {
        id: playlist
        playlistKey: "pl-01"
    }

    component Label: Text {
        color: Theme.color.onSurfaceVariant
        font: Theme.type.labelLarge.font
        Layout.preferredWidth: 96
    }

    SectionTitle {
        text: qsTr("假数据调节")
    }
    Caption {
        text: qsTr("App.debug。改完后点“重新加载”或切换歌单。离线时页面进入 Offline，关掉离线后自动重试；刷新失败时保留内容，只更新 error。")
    }
    RowLayout {
        spacing: 16
        Label {
            text: qsTr("延迟 %1 ms").arg(App.debug.delayMs)
        }
        Slider {
            from: 0
            to: 3000
            stepSize: 100
            value: App.debug.delayMs
            onMoved: App.debug.delayMs = value
            Accessible.name: qsTr("延迟")
            Layout.preferredWidth: 320
        }
    }
    RowLayout {
        spacing: 16
        Label {
            text: qsTr("下一次失败")
        }
        ConnectedButtonGroup {
            exclusive: true
            Repeater {
                model: [{
                        kind: -1,
                        label: qsTr("无")
                    }, {
                        kind: Failure.Network,
                        label: qsTr("网络")
                    }, {
                        kind: Failure.Timeout,
                        label: qsTr("超时")
                    }, {
                        kind: Failure.RateLimited,
                        label: qsTr("限流")
                    }, {
                        kind: Failure.SignedOut,
                        label: qsTr("未登录")
                    }, {
                        kind: Failure.Rejected,
                        label: qsTr("拒绝")
                    }]
                delegate: Button {
                    required property var modelData
                    size: Md.XSmall
                    variant: Md.Tonal
                    checkable: true
                    checked: App.debug.failNext === modelData.kind
                    text: modelData.label
                    onClicked: App.debug.failNext = modelData.kind
                }
            }
        }
    }
    RowLayout {
        spacing: 24
        Switch {
            text: qsTr("离线")
            checked: App.debug.offline
            onToggled: App.debug.offline = checked
        }
        Switch {
            text: qsTr("空结果")
            checked: App.debug.empty
            onToggled: App.debug.empty = checked
        }
        Switch {
            text: qsTr("部分数据")
            checked: App.debug.partial
            onToggled: App.debug.partial = checked
        }
    }

    SectionTitle {
        text: qsTr("歌单页模型")
    }
    Caption {
        text: qsTr("PlaylistPageModel：设置 playlistKey 即开始加载；pl-05 是空歌单，pl-99 不存在（Rejected，不可重试）。封面经 image://cover 异步解码。")
    }
    RowLayout {
        spacing: 16
        ConnectedButtonGroup {
            exclusive: true
            Repeater {
                model: ["pl-01", "pl-02", "pl-03", "pl-04", "pl-05", "pl-99"]
                delegate: Button {
                    required property string modelData
                    size: Md.XSmall
                    variant: Md.Tonal
                    checkable: true
                    checked: playlist.playlistKey === modelData
                    text: modelData
                    onClicked: playlist.playlistKey = modelData
                }
            }
        }
        Button {
            size: Md.XSmall
            variant: Md.Outlined
            glyph: Symbols.refresh
            text: qsTr("重新加载")
            onClicked: playlist.refresh()
        }
        Button {
            size: Md.XSmall
            variant: Md.Outlined
            text: qsTr("重试")
            enabled: playlist.error.retryable
            onClicked: playlist.retry()
        }
    }
    Text {
        color: Theme.color.onSurface
        font: Theme.type.bodyMedium.font
        text: qsTr("state：%1　refreshing：%2　online：%3　歌曲：%4").arg(page.stateNames[playlist.state])
            .arg(playlist.refreshing ? "true" : "false").arg(App.network.online ? "true" : "false")
            .arg(playlist.songs.count)
    }
    Text {
        visible: playlist.error.kind >= 0
        color: Theme.color.error
        font: Theme.type.bodyMedium.font
        text: qsTr("error：%1（%2）").arg(playlist.error.message)
            .arg(playlist.error.retryable ? qsTr("可重试") : qsTr("不可重试"))
    }

    RowLayout {
        spacing: 16
        opacity: playlist.state === LoadState.Loading ? 0.4 : 1

        CoverArt {
            Layout.preferredWidth: 120
            Layout.preferredHeight: 120
            radius: Theme.shape.medium
            glyph: Symbols.queue_music
            coverKey: playlist.coverKey
        }
        ColumnLayout {
            spacing: 4
            Text {
                text: playlist.title
                color: Theme.color.onSurface
                font: Theme.type.headlineSmall.font
            }
            Text {
                text: playlist.description
                color: Theme.color.onSurfaceVariant
                font: Theme.type.bodyMedium.font
            }
            Text {
                text: playlist.creator !== "" ? qsTr("%1 · %2 首").arg(playlist.creator)
                                                  .arg(App.format.count(playlist.songs.count)) : ""
                color: Theme.color.onSurfaceVariant
                font: Theme.type.bodySmall.font
            }
        }
        LoadingIndicator {
            visible: playlist.state === LoadState.Loading || playlist.refreshing
        }
    }

    Rectangle {
        Layout.preferredWidth: 560
        Layout.preferredHeight: songColumn.implicitHeight
        visible: playlist.songs.count > 0
        radius: Theme.shape.large
        color: Theme.color.surfaceContainerLow
        clip: true

        Column {
            id: songColumn
            width: parent.width
            Repeater {
                model: playlist.songs
                delegate: ListItem {
                    id: song
                    required property int index
                    required property bool current
                    required property string title
                    required property string artistText
                    required property string albumTitle
                    required property string durationText
                    required property string coverKey
                    width: songColumn.width
                    highlighted: current
                    text: title
                    supportingText: artistText + " · " + albumTitle
                    trailingText: durationText
                    onClicked: App.player.playSongs(playlist.songs.keys(), index)
                    leading: Component {
                        CoverArt {
                            width: 56
                            height: 56
                            coverKey: song.coverKey
                        }
                    }
                }
            }
        }
    }

    SectionTitle {
        text: qsTr("播放")
    }
    Caption {
        text: qsTr("App.player 接假播放器：点上面的歌曲开始播放。加载时长取上面的延迟；so-012 播放时报“不可用”，离线时都会失败；so-008 时长未知，按 200 秒模拟，不能拖动进度。")
    }
    Text {
        color: Theme.color.onSurface
        font: Theme.type.bodyMedium.font
        text: qsTr("status：%1　canPrevious：%2　canNext：%3").arg(page.statusNames[App.player.status])
            .arg(App.player.canPrevious ? "true" : "false").arg(App.player.canNext ? "true" : "false")
    }
    RowLayout {
        spacing: 16
        Label {
            text: qsTr("顺序")
        }
        ConnectedButtonGroup {
            exclusive: true
            Repeater {
                model: [{
                        value: Playback.Sequential,
                        label: qsTr("顺序")
                    }, {
                        value: Playback.Shuffle,
                        label: qsTr("随机")
                    }]
                delegate: Button {
                    required property var modelData
                    size: Md.XSmall
                    variant: Md.Tonal
                    checkable: true
                    checked: App.player.order === modelData.value
                    text: modelData.label
                    onClicked: App.player.setOrder(modelData.value)
                }
            }
        }
    }
    RowLayout {
        spacing: 16
        Label {
            text: qsTr("循环")
        }
        ConnectedButtonGroup {
            exclusive: true
            Repeater {
                model: [{
                        value: Playback.RepeatOff,
                        label: qsTr("关")
                    }, {
                        value: Playback.RepeatAll,
                        label: qsTr("列表")
                    }, {
                        value: Playback.RepeatOne,
                        label: qsTr("单曲")
                    }]
                delegate: Button {
                    required property var modelData
                    size: Md.XSmall
                    variant: Md.Tonal
                    checkable: true
                    checked: App.player.repeat === modelData.value
                    text: modelData.label
                    onClicked: App.player.setRepeat(modelData.value)
                }
            }
        }
    }
    RowLayout {
        spacing: 16
        Label {
            text: qsTr("进度")
        }
        PlaybackClock {
            id: clock
        }
        Slider {
            from: 0
            to: Math.max(1, App.player.durationMs)
            enabled: App.player.seekable
            value: pressed ? value : clock.positionMs
            onMoved: App.player.seek(value)
            Accessible.name: qsTr("进度")
            Layout.preferredWidth: 320
        }
    }
    Item {
        Layout.preferredWidth: 720
        Layout.preferredHeight: mini.implicitHeight + mini.bottomMargin + 12

        MiniPlayer {
            id: mini
            width: parent.width
            y: 12
        }
    }
}
