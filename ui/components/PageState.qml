pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Presentation

// The states of a loading page around its content (README §6.4, presentation.md §4.1):
// a pulsing skeleton on first load (after `skeletonDelay`, so fast loads don't flash),
// the empty, error, offline and signed-out views, and a loading indicator while
// refreshing. The page keeps its own content and fades it with `contentOpacity`:
//   PlaylistPageModel { id: pageModel }
//   PageState { id: pageState; anchors.fill: parent; model: pageModel }
//   ListView { opacity: pageState.contentOpacity; visible: opacity > 0 }
// Partial content is content: pages show a NoticeBar for it.
Item {
    id: root

    property LoadableModel model
    // Follow the model; the gallery sets them directly.
    property int loadState: model ? model.state : LoadState.Idle
    property loadError error: model ? model.error : ({})
    property bool refreshing: model ? model.refreshing : false

    property Component skeleton: ListSkeleton {}
    property int skeletonDelay: 150
    // Empty counts as content: the page shows its own empty view (e.g. under a header).
    property bool emptyInContent: false
    property string emptyGlyph: Symbols.queue_music
    property string emptyText: qsTr("这里还没有内容")

    readonly property bool showsContent: loadState === LoadState.Ready
                                         || loadState === LoadState.Partial
                                         || (emptyInContent && loadState === LoadState.Empty)
    readonly property real contentOpacity: contentFade.value

    // Sign-in from the signed-out view.
    signal signInRequested

    // Above the page's content, for the refresh indicator; the state views are hidden
    // while content shows.
    z: 1

    Spring {
        id: contentFade
        target: root.showsContent ? 1 : 0
        spec: Theme.motion.defaultEffects
        threshold: 0.002
        Component.onCompleted: snapTo(target)
    }

    // A view of one state that fades in and out with it.
    component StateView: Item {
        id: view
        property bool shown
        anchors.fill: parent
        visible: opacity > 0
        opacity: fade.value

        Spring {
            id: fade
            target: view.shown ? 1 : 0
            spec: Theme.motion.defaultEffects
            threshold: 0.002
        }
    }

    StateView {
        id: skeletonView
        // Shown once loading has lasted skeletonDelay.
        shown: root.loadState === LoadState.Loading && !delay.running

        Timer {
            id: delay
            interval: root.skeletonDelay
            running: root.loadState === LoadState.Loading
        }

        Loader {
            anchors.fill: parent
            active: skeletonView.visible
            sourceComponent: root.skeleton
            // One pulse for the whole skeleton; still with reduced motion.
            opacity: Theme.motion.reduced ? 0.8 : 0.6 + 0.4 * pulse.phase

            QtObject {
                id: pulse
                property real phase: 1
            }
            SequentialAnimation {
                running: skeletonView.visible && !Theme.motion.reduced
                loops: Animation.Infinite
                onStopped: pulse.phase = 1
                NumberAnimation {
                    target: pulse
                    property: "phase"
                    to: 0
                    duration: 800
                    easing.type: Easing.InOutSine
                }
                NumberAnimation {
                    target: pulse
                    property: "phase"
                    to: 1
                    duration: 800
                    easing.type: Easing.InOutSine
                }
            }
        }
    }

    StateView {
        shown: root.loadState === LoadState.Empty && !root.emptyInContent
        StateMessage {
            anchors.centerIn: parent
            width: Math.min(parent.width - 48, 360)
            glyph: root.emptyGlyph
            message: root.emptyText
        }
    }

    StateView {
        shown: root.loadState === LoadState.Error
        StateMessage {
            anchors.centerIn: parent
            width: Math.min(parent.width - 48, 360)
            glyph: Symbols.error
            message: root.error.message
            actionText: root.error.retryable ? qsTr("重试") : ""
            actionGlyph: Symbols.refresh
            onActionTriggered: root.model?.retry()
        }
    }

    StateView {
        shown: root.loadState === LoadState.Offline
        StateMessage {
            anchors.centerIn: parent
            width: Math.min(parent.width - 48, 360)
            glyph: Symbols.wifi_off
            title: qsTr("没有网络连接")
            message: root.error.message
        }
    }

    StateView {
        shown: root.loadState === LoadState.SignedOut
        StateMessage {
            anchors.centerIn: parent
            width: Math.min(parent.width - 48, 360)
            glyph: Symbols.account_circle
            message: root.error.message
            actionText: qsTr("登录")
            actionGlyph: Symbols.login
            onActionTriggered: root.signInRequested()
        }
    }

    // Refreshing: content stays, a contained indicator drops in at the top.
    LoadingIndicator {
        id: refreshIndicator
        anchors.horizontalCenter: parent.horizontalCenter
        y: 16 - (1 - drop.value) * (height + 16)
        visible: drop.value > 0
        contained: true
        opacity: drop.value

        Spring {
            id: drop
            target: root.refreshing ? 1 : 0
            spec: Theme.motion.fastSpatial
            threshold: 0.002
        }
    }
}
