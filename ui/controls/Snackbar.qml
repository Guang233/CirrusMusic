import QtQuick
import Cirrus.Theme

// MD3 snackbar: a short message at the bottom of the window area it is placed in,
// on InverseSurface, gone after `timeout`. Place it inside the area and call show().
//   Snackbar { id: snackbar; anchors.bottom: parent.bottom } ... snackbar.show(qsTr("已保存"))
Rectangle {
    id: control

    property int timeout: 4000
    property string text

    function show(message: string): void {
        text = message;
        shown.target = 1;
        timer.restart();
    }

    anchors.horizontalCenter: parent.horizontalCenter
    anchors.bottomMargin: 16
    width: Math.min(parent.width - 32, Math.max(288, label.implicitWidth + 32))
    height: Math.max(48, label.implicitHeight + 28)
    radius: Theme.shape.extraSmall
    color: Theme.color.inverseSurface
    visible: shown.value > 0.01
    opacity: shown.value
    transform: Translate {
        y: (1 - shown.value) * 16
    }
    Accessible.role: Accessible.AlertMessage
    Accessible.name: text

    Spring {
        id: shown
        target: 0
        spec: Theme.motion.fastEffects
        threshold: 0.005
    }
    Timer {
        id: timer
        interval: control.timeout
        onTriggered: shown.target = 0
    }
    HoverHandler {
        // Stays while the pointer is on it.
        onHoveredChanged: hovered ? timer.stop() : timer.restart()
    }

    Text {
        id: label
        x: 16
        width: parent.width - 32
        anchors.verticalCenter: parent.verticalCenter
        wrapMode: Text.Wrap
        text: control.text
        color: Theme.color.inverseOnSurface
        font: Theme.type.bodyMedium.font
    }
}
