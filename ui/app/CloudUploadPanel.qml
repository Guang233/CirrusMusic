pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Presentation

// The cloud page's uploads (docs/ui/pages/cloud.md §5): one line (what is uploading, how far)
// that opens to a row per file, each with its state, cancel or retry. 全部取消 while any is
// left; 关闭 once all have ended.
Rectangle {
    id: panel

    property bool expanded: false
    readonly property var uploads: App.cloudUploads

    width: parent ? parent.width : 0
    implicitHeight: column.implicitHeight + 16
    radius: Theme.shape.large
    color: ColorScope.scheme.surfaceContainer

    Column {
        id: column
        x: 16
        y: 8
        width: parent.width - 32

        Item {
            width: parent.width
            height: 48

            Icon {
                id: icon
                anchors.verticalCenter: parent.verticalCenter
                glyph: Symbols.cloud_upload
                size: 24
                color: ColorScope.scheme.onSurfaceVariant
            }
            Column {
                anchors.left: icon.right
                anchors.leftMargin: 16
                anchors.right: buttons.left
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                spacing: 6
                Text {
                    width: parent.width
                    text: panel.uploads.summary
                    elide: Text.ElideMiddle
                    color: ColorScope.scheme.onSurface
                    font: Theme.type.bodyMedium.font
                }
                ProgressBar {
                    width: parent.width
                    visible: panel.uploads.active
                    indeterminate: panel.uploads.progress < 0
                    value: Math.max(0, panel.uploads.progress)
                }
            }
            Row {
                id: buttons
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: 4
                Button {
                    variant: Md.Text
                    text: panel.uploads.active ? qsTr("全部取消") : qsTr("关闭")
                    onClicked: {
                        if (panel.uploads.active)
                            panel.uploads.cancelAll();
                        else
                            panel.uploads.clearFinished();
                    }
                }
                IconButton {
                    glyph: panel.expanded ? Symbols.expand_less : Symbols.expand_more
                    text: panel.expanded ? qsTr("收起") : qsTr("展开")
                    onClicked: panel.expanded = !panel.expanded
                }
            }
        }

        Repeater {
            model: panel.expanded ? panel.uploads : null
            delegate: Item {
                id: row
                required property int index
                required property string name
                required property int uploadState
                required property real progress
                required property string status

                readonly property bool ended: uploadState >= CloudUploads.Done
                readonly property bool retryable: uploadState === CloudUploads.Failed
                                                  || uploadState === CloudUploads.Canceled

                width: column.width
                height: 52

                Column {
                    anchors.left: parent.left
                    anchors.leftMargin: 40
                    anchors.right: rowButton.left
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 4
                    Text {
                        width: parent.width
                        text: row.name
                        elide: Text.ElideMiddle
                        color: ColorScope.scheme.onSurface
                        font: Theme.type.bodyMedium.font
                    }
                    Text {
                        width: parent.width
                        text: row.status
                        elide: Text.ElideRight
                        color: row.uploadState === CloudUploads.Failed
                               ? ColorScope.scheme.error : ColorScope.scheme.onSurfaceVariant
                        font: Theme.type.bodySmall.font
                    }
                }
                IconButton {
                    id: rowButton
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    visible: !row.ended || row.retryable
                    glyph: row.retryable ? Symbols.refresh : Symbols.close
                    text: row.retryable ? qsTr("重试") : qsTr("取消上传")
                    onClicked: {
                        if (row.retryable)
                            panel.uploads.retry(row.index);
                        else
                            panel.uploads.cancel(row.index);
                    }
                }
            }
        }
    }
}
