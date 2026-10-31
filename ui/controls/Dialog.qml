import QtQuick
import QtQuick.Templates as T
import Cirrus.Theme

// MD3 basic dialog: SurfaceContainerHigh, extra-large corners, level-3 shadow, 24 px
// padding, 280–560 wide, centered over a 32 % scrim. Optional `glyph` (centers the
// headline under it), `title` (headline), `supportingText`, any content as children,
// and `buttons` (text buttons, right-aligned). Esc and a click outside close it unless
// `closePolicy` says otherwise. Built on Popup: the headline, content and buttons
// are one column, so no header/footer layout of T.Dialog gets in the way.
//   Dialog { id: dialog; title: qsTr("删除歌单？"); supportingText: qsTr("…")
//            buttons: [Button { variant: Md.Text; text: qsTr("取消"); onClicked: dialog.close() }] }
T.Popup {
    id: control

    property string glyph
    property string title
    property string supportingText
    default property alias content: body.data
    property alias buttons: buttonRow.data

    parent: T.Overlay.overlay
    anchors.centerIn: parent
    modal: true
    focus: true
    padding: 24
    width: Math.max(280, Math.min(560, implicitContentWidth + leftPadding + rightPadding,
                                  parent ? parent.width - 48 : 560))
    height: Math.min(implicitContentHeight + topPadding + bottomPadding,
                     parent ? parent.height - 48 : implicitHeight)
    transformOrigin: T.Popup.Center

    enter: Transition {
        ParallelAnimation {
            NumberAnimation {
                property: "scale"
                from: 0.9
                to: 1
                duration: Theme.motion.standardDefaultSpatial.duration
                easing: Theme.motion.standardDefaultSpatial.easing
            }
            NumberAnimation {
                property: "opacity"
                from: 0
                to: 1
                duration: Theme.motion.defaultEffects.duration
                easing: Theme.motion.defaultEffects.easing
            }
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

    background: Item {
        Elevation {
            anchors.fill: parent
            level: Theme.elevation.level3
            radius: Theme.shape.extraLarge
        }
        Rectangle {
            anchors.fill: parent
            radius: Theme.shape.extraLarge
            color: Theme.color.surfaceContainerHigh
        }
    }

    contentItem: Column {
        spacing: 16
        Accessible.role: Accessible.Dialog
        Accessible.name: control.title

        // Icon, headline, supporting text.
        Icon {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: control.glyph !== ""
            glyph: control.glyph
            size: 24
            color: Theme.color.secondary
        }
        Text {
            width: parent.width
            visible: control.title !== ""
            horizontalAlignment: control.glyph !== "" ? Text.AlignHCenter : Text.AlignLeft
            wrapMode: Text.Wrap
            text: control.title
            color: Theme.color.onSurface
            font: Theme.type.headlineSmall.font
            Accessible.role: Accessible.Heading
        }
        Text {
            width: parent.width
            visible: control.supportingText !== ""
            wrapMode: Text.Wrap
            text: control.supportingText
            color: Theme.color.onSurfaceVariant
            font: Theme.type.bodyMedium.font
        }
        Column {
            id: body
            width: parent.width
            visible: children.length > 0
        }
        // 24 above the buttons: the column's 16 plus 8.
        Item {
            width: parent.width
            height: buttonRow.height + 8
            visible: buttonRow.children.length > 0
            Row {
                id: buttonRow
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                spacing: 8
            }
        }
    }

    T.Overlay.modal: Rectangle {
        color: Qt.alpha(Theme.color.scrim, 0.32)
        Behavior on opacity {
            NumberAnimation {
                duration: Theme.motion.defaultEffects.duration
                easing: Theme.motion.defaultEffects.easing
            }
        }
    }
}
