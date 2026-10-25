import QtQuick
import QtQuick.Templates as T
import Cirrus.Theme

// MD3 plain tooltip. Centered above its parent; shows after 500 ms.
//   ToolTip { parent: someItem; text: qsTr("下一首"); visible: someItem.hovered }
T.ToolTip {
    id: control

    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: -height - 4
    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)
    margins: 8
    topPadding: 4
    bottomPadding: 4
    leftPadding: 8
    rightPadding: 8
    delay: 500
    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutsideParent
                 | T.Popup.CloseOnReleaseOutsideParent

    enter: Transition {
        NumberAnimation {
            property: "opacity"
            from: 0
            to: 1
            duration: Theme.motion.fastEffects.duration
            easing: Theme.motion.fastEffects.easing
        }
    }
    exit: Transition {
        NumberAnimation {
            property: "opacity"
            from: 1
            to: 0
            duration: Theme.motion.fastEffects.duration
            easing: Theme.motion.fastEffects.easing
        }
    }

    contentItem: Text {
        text: control.text
        font: Theme.type.bodySmall.font
        color: Theme.color.inverseOnSurface
        wrapMode: Text.Wrap
        width: Math.min(implicitWidth, 280)
        verticalAlignment: Text.AlignVCenter
    }

    background: Rectangle {
        implicitHeight: 24
        color: Theme.color.inverseSurface
        radius: Theme.shape.extraSmall
    }
}
