pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Cirrus.Theme
import Cirrus.Controls

// The fixed seed's choices (Theme.seedPresets): round swatches, the chosen one
// shrinks inside an OnSurface ring (no check mark: no color is legible on every seed).
// The last one is a custom color (settings.md §2, S10): a "+" until one is chosen,
// then that color; clicking it asks for `customRequested`. One Tab stop; ←/→ choose,
// like a button group (moving onto the custom one only focuses it).
Row {
    id: swatches

    property color value
    signal chosen(color value)
    signal customRequested

    readonly property int chosenIndex: Theme.seedPresets.findIndex(seed => Qt.colorEqual(seed, value))
    readonly property bool customChosen: chosenIndex < 0

    function choose(index: int): void {
        const next = Math.max(0, Math.min(repeater.count, index));
        if (next === repeater.count) {
            custom.forceActiveFocus(Qt.TabFocusReason);
            return;
        }
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
            focusPolicy: index === swatches.chosenIndex ? Qt.StrongFocus : Qt.ClickFocus
            text: qsTr("主题色 %1").arg(index + 1)
            Accessible.role: Accessible.RadioButton
            Accessible.checkable: true
            Accessible.checked: selected
            onClicked: swatches.chosen(modelData)
            Keys.onLeftPressed: swatches.choose(index - 1)
            Keys.onRightPressed: swatches.choose(index + 1)

            HoverHandler {
                enabled: swatch.hoverEnabled
                cursorShape: Qt.PointingHandCursor
            }
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
                border.color: ColorScope.scheme.onSurface
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
                color: ColorScope.scheme.onSurface
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

    // The custom color.
    T.AbstractButton {
        id: custom

        implicitWidth: 40
        implicitHeight: 40
        focusPolicy: swatches.customChosen ? Qt.StrongFocus : Qt.ClickFocus
        text: qsTr("自定义主题色")
        Accessible.role: Accessible.RadioButton
        Accessible.checkable: true
        Accessible.checked: swatches.customChosen
        onClicked: swatches.customRequested()
        Keys.onLeftPressed: swatches.choose(repeater.count - 1)

        HoverHandler {
            enabled: custom.hoverEnabled
            cursorShape: Qt.PointingHandCursor
        }
        Spring {
            id: customRing
            target: swatches.customChosen ? 1 : 0
            spec: Theme.motion.fastSpatial
            threshold: 0.005
        }

        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: "transparent"
            border.width: 2
            border.color: ColorScope.scheme.onSurface
            opacity: Math.min(1, customRing.value)
        }
        // A "+" on a tonal circle until a custom color is chosen.
        Rectangle {
            anchors.centerIn: parent
            width: parent.width - 10 * customRing.value
            height: width
            radius: width / 2
            color: swatches.customChosen ? swatches.value
                                         : ColorScope.scheme.surfaceContainerHighest
            border.width: swatches.customChosen ? 0 : 1
            border.color: ColorScope.scheme.outlineVariant
        }
        Icon {
            anchors.centerIn: parent
            visible: !swatches.customChosen
            glyph: Symbols.add
            color: ColorScope.scheme.onSurfaceVariant
        }
        StateLayer {
            anchors.fill: parent
            radius: width / 2
            color: ColorScope.scheme.onSurface
            hovered: custom.hovered
            pressed: custom.down
            focused: custom.visualFocus
            pressX: custom.pressed ? custom.pressX : width / 2
            pressY: custom.pressed ? custom.pressY : height / 2
        }
        FocusRing {
            shown: custom.visualFocus
            containerRadius: custom.width / 2
        }
    }
}
