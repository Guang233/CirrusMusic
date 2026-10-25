import QtQuick
import QtQuick.Templates as T
import Cirrus.Theme

// Divider between menu groups.
T.MenuSeparator {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)
    topPadding: 4
    bottomPadding: 4
    leftPadding: 12
    rightPadding: 12

    contentItem: Rectangle {
        implicitWidth: 80
        implicitHeight: 1
        color: Theme.color.outlineVariant
    }
}
