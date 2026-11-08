pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Presentation

// Picks a custom theme color (settings.md §2, S10): hue and chroma sliders (an MD3
// scheme only uses those two of its seed; the tone is kept) and a #RRGGBB field. The
// whole UI previews the color while it changes; 确定 keeps it, 取消 and Esc put back
// the color and its source from before.
//   CustomSeedDialog { id: customSeed }   customSeed.start()
Dialog {
    id: dialog

    property real hue: 0
    property real chroma: 48
    property real tone: 50
    readonly property color picked: Theme.hct(hue, chroma, tone)
    readonly property string hex: picked.toString().toUpperCase()
    // What to put back on cancel.
    property color savedSeed
    property int savedSource: 0
    property bool previewing: false

    // Opens on the color in use.
    function start(): void {
        savedSeed = App.settings.fixedSeed;
        savedSource = App.settings.seedSource;
        setFrom(savedSeed);
        previewing = true;
        open();
    }

    function setFrom(color: color): void {
        const value = Theme.hctOf(color);
        tone = value.tone;
        chroma = value.chroma;
        // A gray has no hue of its own: keep the slider where it is.
        if (value.chroma > 1)
            hue = value.hue;
        if (!field.activeFocus)
            field.text = hex;
    }

    title: qsTr("自定义主题色")
    acceptText: qsTr("确定")
    rejectText: qsTr("取消")
    width: Math.min(440, (parent ? parent.width : 440) - 48)

    onPickedChanged: {
        if (!previewing)
            return;
        App.settings.fixedSeed = picked;
        App.settings.seedSource = 1; // Theme.SeedSource.Fixed
        if (!field.activeFocus)
            field.text = hex;
    }
    onAccepted: previewing = false
    onRejected: {
        previewing = false;
        App.settings.fixedSeed = savedSeed;
        App.settings.seedSource = savedSource;
    }

    Column {
        width: parent.width
        spacing: 16

        Row {
            spacing: 16
            Rectangle {
                width: 56
                height: 56
                radius: width / 2
                color: dialog.picked
                border.width: 1
                border.color: ColorScope.scheme.outlineVariant
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: dialog.hex
                font: Theme.type.titleMedium.font
                color: ColorScope.scheme.onSurface
            }
        }

        Text {
            text: qsTr("色相")
            font: Theme.type.labelLarge.font
            color: ColorScope.scheme.onSurfaceVariant
        }
        Slider {
            width: parent.width
            from: 0
            to: 360
            value: dialog.hue
            Accessible.name: qsTr("色相")
            // The hues at a vivid chroma and a middle tone.
            trackColors: Array.from({
                length: 13
            }, (_, i) => Theme.hct(i * 30, 60, 60))
            onMoved: dialog.hue = value
        }

        Text {
            text: qsTr("色彩度")
            font: Theme.type.labelLarge.font
            color: ColorScope.scheme.onSurfaceVariant
        }
        Slider {
            width: parent.width
            from: 0
            to: 100
            value: dialog.chroma
            Accessible.name: qsTr("色彩度")
            // Gray to vivid at this hue and a middle tone.
            trackColors: Array.from({
                length: 6
            }, (_, i) => Theme.hct(dialog.hue, i * 20, 60))
            onMoved: dialog.chroma = value
        }

        TextField {
            id: field
            width: 200
            variant: Md.Outlined
            gapColor: ColorScope.scheme.surfaceContainerHigh
            label: qsTr("色值")
            validator: RegularExpressionValidator {
                regularExpression: /#?[0-9A-Fa-f]{0,6}/
            }
            onTextEdited: {
                const digits = text.replace("#", "");
                if (digits.length === 6)
                    dialog.setFrom("#" + digits);
            }
            onEditingFinished: text = dialog.hex
        }
    }
}
