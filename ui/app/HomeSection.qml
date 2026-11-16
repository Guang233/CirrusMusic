pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components
import Cirrus.Presentation

// One section of the home page (docs/ui/pages/home.md §6): title, "see all" (an icon
// button at the right of the title), and its
// own states. It loads and fails on its own: a skeleton while loading, one line with
// retry when it failed, hidden when empty or signed out. The content rises in when
// it first arrives.
Column {
    id: section

    property string title
    property bool showsAll: false
    property LoadableModel model
    property Component skeleton
    // At the right of the title once there is content, instead of "查看全部" (e.g. play all).
    property Component trailing: null
    default property alias content: body.data

    signal allRequested

    readonly property int loadState: model ? model.state : LoadState.Idle
    readonly property bool hasContent: loadState === LoadState.Ready
                                       || loadState === LoadState.Partial
    readonly property bool failed: loadState === LoadState.Error
                                   || loadState === LoadState.Offline
    readonly property bool loading: loadState === LoadState.Loading
    // Something to show. Layout around the section reads this, not `visible` (which is
    // false whenever a parent is hidden).
    readonly property bool wanted: hasContent || failed || loading

    visible: wanted
    spacing: 12

    Reveal {
        target: body
        shown: section.hasContent
        once: false
    }

    Item {
        width: section.width
        height: 40

        Text {
            anchors.left: parent.left
            anchors.right: allButton.visible ? allButton.left
                         : trailingLoader.active ? trailingLoader.left : parent.right
            anchors.rightMargin: allButton.visible ? 12 : 0
            anchors.verticalCenter: parent.verticalCenter
            text: section.title
            elide: Text.ElideRight
            color: Theme.color.onSurface
            font: Theme.type.titleLargeEmphasized.font
            Accessible.role: Accessible.Heading
        }
        IconButton {
            id: allButton
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            visible: section.showsAll && section.hasContent
            variant: Md.Tonal
            glyph: Symbols.arrow_forward
            text: qsTr("查看全部")
            onClicked: section.allRequested()
        }
        Loader {
            id: trailingLoader
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            active: section.trailing !== null && section.hasContent && !allButton.visible
            sourceComponent: section.trailing
        }
    }

    Loader {
        width: section.width
        active: section.loading && !section.hasContent
        visible: active
        sourceComponent: section.skeleton
    }

    // Failed without content: one line with retry, not a whole-page error.
    Row {
        visible: section.failed && !section.hasContent
        height: 40
        spacing: 12
        Icon {
            anchors.verticalCenter: parent.verticalCenter
            glyph: section.loadState === LoadState.Offline ? Symbols.wifi_off : Symbols.error
            size: 20
            color: Theme.color.onSurfaceVariant
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: section.loadState === LoadState.Offline
                  ? qsTr("网络不可用，%1没有加载出来").arg(section.title)
                  : qsTr("%1没有加载出来").arg(section.title)
            color: Theme.color.onSurfaceVariant
            font: Theme.type.bodyMedium.font
        }
        Button {
            anchors.verticalCenter: parent.verticalCenter
            variant: Md.Text
            size: Md.Small
            text: qsTr("重试")
            onClicked: section.model.retry()
        }
    }

    Column {
        id: body
        width: section.width
        visible: section.hasContent
    }
}
