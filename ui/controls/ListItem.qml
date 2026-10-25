pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Cirrus.Theme

// MD3 list item (one to three lines). `text` is the headline. Leading/trailing take
// a glyph or any component (cover, avatar, Switch...). `highlighted` is the selected
// state. `segmented` turns on the MD3E per-state corner rounding for gap lists.
//   ListItem { text: song.title; supportingText: song.artist; leadingGlyph: Symbols.music_note }
T.ItemDelegate {
    id: control

    property string supportingText
    property string overline
    property string trailingText
    property string leadingGlyph
    property string trailingGlyph
    property Component leading
    property Component trailing
    property bool segmented: false
    property color containerColor: "transparent"
    // 0 picks from content: 2 with supporting text, 3 with an overline or two-line support.
    property int lines: 0
    property int supportingMaximumLines: 1
    readonly property alias leadingItem: leadingLoader.item
    readonly property alias trailingItem: trailingLoader.item

    readonly property int lineCount: lines > 0 ? lines
                                   : overline !== "" || supportingText !== ""
                                     && supportingMaximumLines > 1 ? 3
                                   : supportingText !== "" ? 2 : 1
    readonly property color headlineColor: !enabled
        ? Qt.alpha(Theme.color.onSurface, Theme.state.disabledContent)
        : Theme.mix(Theme.color.onSurface, Theme.color.onSecondaryContainer, selection.value)
    readonly property color mutedColor: !enabled
        ? Qt.alpha(Theme.color.onSurface, Theme.state.disabledContent)
        : Theme.mix(Theme.color.onSurfaceVariant, Theme.color.onSecondaryContainer,
                    selection.value)
    readonly property real cornerTarget: !segmented ? 0
                                       : highlighted || down ? Theme.shape.large
                                       : hovered || visualFocus ? Theme.shape.medium
                                       : Theme.shape.extraSmall

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: [56, 72, 88][lineCount - 1]
    leftPadding: 16
    rightPadding: 16
    topPadding: 10
    bottomPadding: 10
    spacing: 12
    font: Theme.type.bodyLarge.font

    Spring {
        id: selection
        target: control.highlighted ? 1 : 0
        spec: Theme.motion.defaultEffects
    }
    Spring {
        id: corner
        target: control.cornerTarget
        spec: Theme.motion.fastSpatial
        threshold: 0.1
    }

    contentItem: Item {
        implicitWidth: (leadingLoader.active ? leadingLoader.implicitWidth + control.spacing : 0)
                       + Math.max(headline.implicitWidth, support.implicitWidth)
                       + (trailingLoader.active ? trailingLoader.implicitWidth
                                                  + control.spacing : 0)
        implicitHeight: textColumn.implicitHeight

        Loader {
            id: leadingLoader
            opacity: control.enabled || control.leading === null ? 1
                                                                 : Theme.state.disabledContent
            anchors.verticalCenter: control.lineCount === 3 ? undefined : parent.verticalCenter
            y: 0
            active: control.leading !== null || control.leadingGlyph !== ""
            sourceComponent: control.leading ?? leadingGlyphComponent
        }

        Column {
            id: textColumn
            x: leadingLoader.active ? leadingLoader.width + control.spacing : 0
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - x - (trailingLoader.active ? trailingLoader.width
                                                               + control.spacing : 0)

            Text {
                visible: control.overline !== ""
                width: parent.width
                text: control.overline
                font: Theme.type.labelSmall.font
                color: control.mutedColor
                elide: Text.ElideRight
            }
            Text {
                id: headline
                width: parent.width
                text: control.text
                font: control.font
                color: control.headlineColor
                elide: Text.ElideRight
            }
            Text {
                id: support
                visible: control.supportingText !== ""
                width: parent.width
                text: control.supportingText
                font: Theme.type.bodyMedium.font
                color: control.mutedColor
                elide: Text.ElideRight
                wrapMode: control.supportingMaximumLines > 1 ? Text.Wrap : Text.NoWrap
                maximumLineCount: control.supportingMaximumLines
            }
        }

        Loader {
            id: trailingLoader
            opacity: control.enabled || control.trailing === null ? 1
                                                                  : Theme.state.disabledContent
            x: parent.width - width
            anchors.verticalCenter: control.lineCount === 3 ? undefined : parent.verticalCenter
            active: control.trailing !== null || control.trailingGlyph !== ""
                    || control.trailingText !== ""
            sourceComponent: control.trailing ?? (control.trailingText !== ""
                                                  ? trailingTextComponent
                                                  : trailingGlyphComponent)
        }
    }

    Component {
        id: leadingGlyphComponent
        Icon {
            glyph: control.leadingGlyph
            color: control.mutedColor
        }
    }
    Component {
        id: trailingGlyphComponent
        Icon {
            glyph: control.trailingGlyph
            color: control.mutedColor
        }
    }
    Component {
        id: trailingTextComponent
        Text {
            text: control.trailingText
            font: Theme.type.labelSmall.font
            color: control.mutedColor
        }
    }

    background: Rectangle {
        implicitHeight: 56
        color: Theme.mix(control.containerColor, Theme.color.secondaryContainer, selection.value)
        radius: Math.max(0, corner.value)

        StateLayer {
            anchors.fill: parent
            radius: parent.radius
            color: Theme.color.onSurface
            hovered: control.hovered && control.enabled
            pressed: control.down
            focused: control.visualFocus
            pressX: control.pressed ? control.pressX : width / 2
            pressY: control.pressed ? control.pressY : height / 2
        }

        FocusRing {
            shown: control.visualFocus
            offset: -5
            containerRadius: parent.radius
        }
    }
}
