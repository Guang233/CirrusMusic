import QtQuick
import Cirrus.Theme
import Cirrus.Controls

// A page in the shell's page stack: a header row (title, trailing `actions`) and the
// page content below it. Back and forward live in the app's top bar.
//   PageScaffold { title: qsTr("音乐库") }
Item {
    id: root

    property string title
    default property alias content: body.data
    property alias actions: actionRow.data

    Item {
        id: header
        anchors.left: parent.left
        anchors.right: parent.right
        height: 64

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 24
            anchors.right: actionRow.left
            anchors.rightMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            text: root.title
            elide: Text.ElideRight
            color: ColorScope.scheme.onSurface
            font: Theme.type.titleLarge.font
            Accessible.role: Accessible.Heading
        }
        Row {
            id: actionRow
            anchors.right: parent.right
            anchors.rightMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            spacing: 4
        }
    }

    Item {
        id: body
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: header.bottom
        anchors.bottom: parent.bottom
    }
}
