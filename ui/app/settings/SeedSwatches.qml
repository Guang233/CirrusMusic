pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Cirrus.Theme
import Cirrus.Controls

// The fixed seed's choices (Theme.seedPresets): round swatches, the chosen one
// shrinks inside an OnSurface ring (no check mark: no color is legible on every seed). One Tab stop; ←/→ choose, like a button group.
Row {
    id: swatches

    property color value
    signal chosen(color value)

    readonly property int chosenIndex: Theme.seedPresets.findIndex(seed => Qt.colorEqual(seed, value))

    function choose(index: int): void {
        const next = Math.max(0, Math.min(repeater.count - 1, index));
        (repeater.itemAt(next) as Item)?.forceActiveFocus(Qt.TabFocusReason);
        chosen(Theme.seedPresets[next]);
    }

    spacing: 12

    Repeater {
        id: repeater
        model: Theme.seedPresets
        delegate: T.AbstractButton {
            id: swatch
            required property int index
            required property color modelData
            readonly property bool selected: index === swatches.chosenIndex

            implicitWidth: 40
            implicitHeight: 40
            focusPolicy: index === Math.max(0, swatches.chosenIndex) ? Qt.StrongFocus
                                                                    : Qt.ClickFocus
            text: qsTr("主题色 %1").arg(index + 1)
            Accessible.role: Accessible.RadioButton
            Accessible.checkable: true
            Accessible.checked: selected
            onClicked: swatches.chosen(modelData)
            Keys.onLeftPressed: swatches.choose(index - 1)
            Keys.onRightPressed: swatches.choose(index + 1)

            Spring {
                id: ring
                target: swatch.selected ? 1 : 0
                spec: Theme.motion.fastSpatial
                threshold: 0.005
            }

            // The chosen one: an OnSurface ring with a gap, the color shrinking inside.
            Rectangle {
                anchors.fill: parent
                radius: width / 2
                color: "transparent"
                border.width: 2
                border.color: Theme.color.onSurface
                opacity: Math.min(1, ring.value)
            }
            Rectangle {
                anchors.centerIn: parent
                width: parent.width - 10 * ring.value
                height: width
                radius: width / 2
                color: swatch.modelData
            }
            StateLayer {
                anchors.fill: parent
                radius: width / 2
                color: Theme.color.onSurface
                hovered: swatch.hovered
                pressed: swatch.down
                focused: swatch.visualFocus
                pressX: swatch.pressed ? swatch.pressX : width / 2
                pressY: swatch.pressed ? swatch.pressY : height / 2
            }
            FocusRing {
                shown: swatch.visualFocus
                containerRadius: swatch.width / 2
            }
        }
    }
}
