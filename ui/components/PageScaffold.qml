import QtQuick
import Cirrus.Theme
import Cirrus.Controls

// A page in the shell's page stack: MD3 small top app bar (back button when
// `canGoBack`, title, trailing `actions`) and the page content below it.
//   PageScaffold { title: qsTr("音乐库"); canGoBack: …; onBackRequested: … }
Item {
    id: root

    property string title
    property bool canGoBack
    default property alias content: body.data
    property alias actions: actionRow.data

    signal backRequested

    Item {
        id: header
        anchors.left: parent.left
        anchors.right: parent.right
        height: 64

        IconButton {
            id: backButton
            x: 8
            anchors.verticalCenter: parent.verticalCenter
            visible: root.canGoBack
            glyph: Symbols.arrow_back
            text: qsTr("返回")
            onClicked: root.backRequested()
        }
        Text {
            anchors.left: root.canGoBack ? backButton.right : parent.left
            anchors.leftMargin: root.canGoBack ? 8 : 24
            anchors.right: actionRow.left
            anchors.rightMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            text: root.title
            elide: Text.ElideRight
            color: Theme.color.onSurface
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
