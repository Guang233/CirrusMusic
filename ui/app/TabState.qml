import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components
import Cirrus.Presentation

// A tab's (or a page section's) own loading, failure, emptiness or sign-in prompt,
// inside a page that stays shown (artist.md §4, search.md §3). Collapses once the
// model is ready. `skeleton` replaces the loading indicator with the content's shape
// (discover's card grids).
Item {
    id: tabState

    required property LoadableModel model
    property string emptyGlyph: Symbols.person
    property string emptyText
    property string signedOutText
    property Component skeleton: null
    readonly property int loadState: model.state

    signal signInRequested

    width: parent ? parent.width : 0
    height: loadState === LoadState.Ready || loadState === LoadState.Partial
            || loadState === LoadState.Idle ? 0
          : skeletonLoader.active ? skeletonLoader.implicitHeight : 160
    visible: height > 0

    LoadingIndicator {
        anchors.centerIn: parent
        implicitWidth: 48
        implicitHeight: 48
        visible: tabState.loadState === LoadState.Loading && !skeletonLoader.active
    }
    Loader {
        id: skeletonLoader
        width: parent.width
        active: tabState.skeleton !== null && tabState.loadState === LoadState.Loading
        sourceComponent: tabState.skeleton
    }
    StateMessage {
        anchors.centerIn: parent
        width: Math.min(parent.width, 360)
        visible: tabState.loadState === LoadState.Empty
        glyph: tabState.emptyGlyph
        message: tabState.emptyText
    }
    StateMessage {
        anchors.centerIn: parent
        width: Math.min(parent.width, 360)
        visible: tabState.loadState === LoadState.Error
                 || tabState.loadState === LoadState.Offline
        glyph: Symbols.error
        message: tabState.model.error.message
        actionText: qsTr("重试")
        actionGlyph: Symbols.refresh
        onActionTriggered: tabState.model.retry()
    }
    StateMessage {
        anchors.centerIn: parent
        width: Math.min(parent.width, 360)
        visible: tabState.loadState === LoadState.SignedOut
        glyph: Symbols.account_circle
        message: tabState.signedOutText !== "" ? tabState.signedOutText
                                               : tabState.model.error.message
        actionText: qsTr("登录")
        actionGlyph: Symbols.login
        onActionTriggered: tabState.signInRequested()
    }
}
