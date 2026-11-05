import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Presentation

// Footer of a paged list: a loading indicator while the next page loads, or the
// failure with a retry. Collapses otherwise.
Item {
    id: root

    property PagedModel model

    readonly property bool loading: model !== null && model.loadingMore
    readonly property bool failed: model !== null && !loading && model.loadMoreError.kind >= 0

    implicitHeight: loading || failed ? 80 : 0

    LoadingIndicator {
        anchors.centerIn: parent
        implicitWidth: 40
        implicitHeight: 40
        visible: root.loading
    }
    Row {
        anchors.centerIn: parent
        visible: root.failed
        spacing: 12
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.model ? root.model.loadMoreError.message : ""
            color: ColorScope.scheme.onSurfaceVariant
            font: Theme.type.bodyMedium.font
        }
        Button {
            anchors.verticalCenter: parent.verticalCenter
            size: Md.XSmall
            variant: Md.Tonal
            glyph: Symbols.refresh
            text: qsTr("重试")
            onClicked: root.model.loadMore()
        }
    }
}
