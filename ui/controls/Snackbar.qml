import QtQuick
import Cirrus.Theme

// MD3 snackbar: a short message at the bottom of the window area it is placed in, on
// InverseSurface. It rises in with a spatial spring and leaves after `timeout`, or at
// once with its close button; the pointer resting on it holds it. A new message while
// one shows replaces it with a small bounce. Place it inside the area and call show(),
// or showAction() for a message with one action (a text button in InversePrimary
// before the close button; it runs the handler and closes the snackbar).
//   Snackbar { id: snackbar; anchors.bottom: parent.bottom } ... snackbar.show(qsTr("已保存"))
//   snackbar.showAction(qsTr("有新版本"), qsTr("查看"), () => dialog.open())
Rectangle {
    id: control

    property int timeout: 4000
    property string text
    property string actionText
    property var actionHandler: null

    function show(message: string): void {
        showAction(message, "", null);
    }
    function showAction(message: string, action: string, handler: var): void {
        text = message;
        actionText = action;
        actionHandler = handler;
        if (shown.target === 1 && rise.value > 0.5)
            rise.snapTo(0.85);
        shown.target = 1;
        rise.target = 1;
        if (!hover.hovered)
            timer.restart();
    }
    function dismiss(): void {
        timer.stop();
        shown.target = 0;
        rise.target = 0;
    }

    anchors.horizontalCenter: parent.horizontalCenter
    anchors.bottomMargin: 16
    width: Math.min(parent.width - 32,
                    Math.max(288, label.implicitWidth + 16 + 4
                              + (actionButton.visible ? actionButton.width + 4 : 0) + closeButton.width + 8))
    height: Math.max(48, label.implicitHeight + 28)
    radius: Theme.shape.large
    color: ColorScope.scheme.inverseSurface
    visible: shown.value > 0.01
    opacity: shown.value
    transform: [
        Scale {
            origin.x: control.width / 2
            origin.y: control.height
            xScale: 0.9 + 0.1 * rise.value
            yScale: xScale
        },
        Translate {
            y: (1 - rise.value) * (control.height + control.anchors.bottomMargin)
        }
    ]
    Accessible.role: Accessible.AlertMessage
    Accessible.name: text

    // Opacity, and the rise (position and scale, may overshoot).
    Spring {
        id: shown
        target: 0
        spec: Theme.motion.fastEffects
        threshold: 0.005
    }
    Spring {
        id: rise
        target: 0
        spec: Theme.motion.defaultSpatial
        threshold: 0.005
    }
    Timer {
        id: timer
        interval: control.timeout
        onTriggered: control.dismiss()
    }
    HoverHandler {
        id: hover
        // Stays while the pointer is on it.
        onHoveredChanged: {
            if (hovered)
                timer.stop();
            else if (shown.target === 1)
                timer.restart();
        }
    }

    Text {
        id: label
        x: 16
        width: (actionButton.visible ? actionButton.x : closeButton.x) - x - 4
        anchors.verticalCenter: parent.verticalCenter
        wrapMode: Text.Wrap
        text: control.text
        color: ColorScope.scheme.inverseOnSurface
        font: Theme.type.bodyMedium.font
    }
    Button {
        id: actionButton
        anchors.right: closeButton.left
        anchors.rightMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        visible: control.actionText !== ""
        variant: Md.Text
        tint: ColorScope.scheme.inversePrimary
        focusPolicy: Qt.NoFocus
        text: control.actionText
        onClicked: {
            const handler = control.actionHandler;
            control.dismiss();
            if (handler)
                handler();
        }
    }
    IconButton {
        id: closeButton
        anchors.right: parent.right
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        size: Md.XSmall
        inverse: true
        focusPolicy: Qt.NoFocus
        glyph: Symbols.close
        text: qsTr("关闭")
        showToolTip: false
        onClicked: control.dismiss()
    }
}
