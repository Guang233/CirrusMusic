import QtQuick
import QtQuick.Layouts
import Cirrus.Controls

// Scrollable body shared by the control pages; children go into a ColumnLayout.
Flickable {
    id: page

    default property alias content: column.data

    contentWidth: width
    contentHeight: column.implicitHeight + 48
    clip: true
    boundsBehavior: Flickable.StopAtBounds

    ScrollBar.vertical: ScrollBar {}

    ColumnLayout {
        id: column
        x: 24
        y: 16
        width: page.width - 48
        spacing: 12
    }
}
