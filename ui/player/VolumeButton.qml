pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Cirrus.Theme
import Cirrus.Presentation

// Volume (docs/ui/pages/player.md §1.1): an icon button that follows the volume and
// opens a vertical bar above it; the wheel over the button changes the volume without
// opening it. In the open bar ↑/↓ step 5%, M mutes or restores, Esc closes.
PlayerButton {
    id: root

    required property PlayerPalette colors
    readonly property bool barOpen: bar.opened
    // The volume asked for last; the player reports it a moment later.
    property real pending: -1
    readonly property real volume: pending >= 0 ? pending : App.player.volume
    // Volume before muting with M, to restore it.
    property real unmuted: 0.5

    function setVolume(value: real): void {
        pending = Math.max(0, Math.min(1, value));
        App.player.setVolume(pending);
        settled.restart();
    }
    function toggleMute(): void {
        if (volume > 0) {
            unmuted = volume;
            setVolume(0);
        } else {
            setVolume(unmuted > 0 ? unmuted : 0.5);
        }
    }

    color: colors.content
    glyph: volume <= 0 ? Symbols.volume_off : volume < 0.34 ? Symbols.volume_mute
         : volume < 0.67 ? Symbols.volume_down : Symbols.volume_up
    text: qsTr("音量")
    Accessible.description: Math.round(volume * 100) + "%"
    onClicked: bar.opened ? bar.close() : bar.open()

    Timer {
        id: settled
        interval: 1000
        onTriggered: root.pending = -1
    }

    WheelHandler {
        onWheel: event => root.setVolume(root.volume + (event.angleDelta.y > 0 ? 0.05 : -0.05))
    }

    T.Popup {
        id: bar

        x: (root.width - width) / 2
        y: -height - 8
        width: 48
        height: 176
        padding: 16
        focus: true
        closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutsideParent

        enter: Transition {
            NumberAnimation {
                property: "opacity"
                from: 0
                to: 1
                duration: Theme.motion.fastEffects.duration
            }
        }
        exit: Transition {
            NumberAnimation {
                property: "opacity"
                from: 1
                to: 0
                duration: Theme.motion.fastEffects.duration
            }
        }

        background: Rectangle {
            radius: width / 2
            color: Qt.alpha(root.colors.solid, 0.82)
            border.width: 1
            border.color: Qt.alpha(root.colors.content, 0.12)
        }

        contentItem: Item {
            focus: true
            Accessible.role: Accessible.Slider
            Accessible.name: qsTr("音量")
            Keys.onUpPressed: root.setVolume(root.volume + 0.05)
            Keys.onDownPressed: root.setVolume(root.volume - 0.05)
            Keys.onPressed: event => {
                if (event.key === Qt.Key_M) {
                    root.toggleMute();
                    event.accepted = true;
                }
            }

            Rectangle {
                id: track
                anchors.horizontalCenter: parent.horizontalCenter
                width: 8
                height: parent.height
                radius: width / 2
                color: Qt.alpha(root.colors.content, 0.25)

                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: Math.max(width, parent.height * root.volume)
                    radius: parent.radius
                    color: root.colors.content
                    visible: root.volume > 0
                }
            }

            MouseArea {
                function setAt(y: real): void {
                    root.setVolume(1 - y / height);
                }
                anchors.fill: parent
                anchors.margins: -8
                onPressed: mouse => setAt(mouse.y - 8)
                onPositionChanged: mouse => {
                    if (pressed)
                        setAt(mouse.y - 8);
                }
                onWheel: wheel => root.setVolume(root.volume + (wheel.angleDelta.y > 0 ? 0.05 : -0.05))
            }
        }
    }
}
