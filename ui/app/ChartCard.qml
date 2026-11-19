pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import M3Shapes
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components

// A chart: cover, name and update schedule, then its top songs with their ranks. The
// whole card opens the chart (a playlist). `wide` (discover's official charts) puts the
// songs beside a larger cover and adds a play button at the top right (`playRequested`).
// On the home page (`badge`: the card's place in the row) there is no cover: the name
// is in heavy type and a shape badge sits at the top right, each place with its own
// shape and container color (docs/ui/redesign-md3e.md §6.4).
T.AbstractButton {
    id: control

    property string coverKey
    property string updateLabel
    // [{title, subtitle}], the first few songs.
    property var previews: []
    property bool wide: false
    property int badge: -1

    readonly property bool badged: badge >= 0 && !wide
    // What the chart is about, where its name says so.
    readonly property string badgeGlyph: text.includes("飙升") ? Symbols.trending_up
                                       : text.includes("热") ? Symbols.local_fire_department
                                       : text.includes("原创") ? Symbols.mic
                                       : text.includes("新") ? Symbols.music_note
                                       : Symbols.leaderboard

    signal playRequested

    // The badge turns a little while the card is hovered.
    Spring {
        id: lean
        target: control.hovered ? 1 : 0
        spec: Theme.motion.defaultSpatial
    }

    implicitHeight: wide ? 184 : 216
    focusPolicy: Qt.TabFocus
    Accessible.role: Accessible.Button
    Accessible.name: text

    HoverHandler {
        enabled: control.hoverEnabled
        cursorShape: Qt.PointingHandCursor
    }
    background: Rectangle {
        radius: control.badged ? Theme.shape.extraLarge : Theme.shape.large
        color: Theme.color.surfaceContainer

        StateLayer {
            anchors.fill: parent
            radius: parent.radius
            color: Theme.color.onSurface
            hovered: control.hovered
            pressed: control.down
            focused: control.visualFocus
            pressX: control.pressX
            pressY: control.pressY
        }
        FocusRing {
            shown: control.visualFocus
            containerTopLeftRadius: parent.radius
            containerTopRightRadius: parent.radius
            containerBottomRightRadius: parent.radius
            containerBottomLeftRadius: parent.radius
        }
    }

    contentItem: Item {
        CoverArt {
            id: cover
            x: 16
            y: 16
            width: control.wide ? 152 : 72
            height: width
            visible: !control.badged
            coverKey: control.badged ? "" : control.coverKey
            radius: Theme.shape.medium
        }
        Item {
            id: badgeItem
            readonly property int place: Math.max(0, control.badge) % 4
            x: parent.width - width - 16
            y: 16
            width: 56
            height: 56
            visible: control.badged
            rotation: 16 * lean.value

            MorphShape {
                anchors.fill: parent
                shape: [MaterialShape.Cookie9Sided, MaterialShape.Clover4Leaf,
                        MaterialShape.Sunny, MaterialShape.Pentagon][badgeItem.place]
                color: [Theme.color.primaryContainer, Theme.color.tertiaryContainer,
                        Theme.color.secondaryContainer, Theme.color.primary][badgeItem.place]
                fallbackRadius: width / 2
            }
            Icon {
                anchors.centerIn: parent
                rotation: -badgeItem.rotation
                glyph: control.badgeGlyph
                size: 24
                color: [Theme.color.onPrimaryContainer, Theme.color.onTertiaryContainer,
                        Theme.color.onSecondaryContainer, Theme.color.onPrimary][badgeItem.place]
            }
        }
        Column {
            id: heading
            anchors.left: control.badged ? parent.left : cover.right
            anchors.leftMargin: control.badged ? 20 : 16
            anchors.right: play.visible ? play.left : badgeItem.visible ? badgeItem.left
                                                                       : parent.right
            anchors.rightMargin: play.visible ? 8 : badgeItem.visible ? 12 : 16
            y: control.wide ? cover.y : control.badged ? 20
                            : cover.y + (cover.height - height) / 2
            spacing: control.badged ? 2 : 4
            Text {
                width: parent.width
                text: control.text
                elide: Text.ElideRight
                color: Theme.color.onSurface
                font: control.badged ? Fonts.font(800, 24) : Theme.type.titleMedium.font
            }
            Text {
                width: parent.width
                visible: text !== ""
                text: control.updateLabel
                elide: Text.ElideRight
                color: Theme.color.onSurfaceVariant
                font: Theme.type.bodySmall.font
            }
        }

        IconButton {
            id: play
            x: parent.width - width - 12
            y: 12
            visible: control.wide
            variant: Md.Tonal
            glyph: Symbols.play_arrow
            iconFill: 1
            text: qsTr("播放%1").arg(control.text)
            onClicked: control.playRequested()
        }

        // Ranked top songs.
        Column {
            x: control.wide ? heading.x : control.badged ? 20 : 16
            y: control.wide ? cover.y + cover.height - height : cover.y + cover.height + 12
            width: parent.width - x - (control.badged ? 20 : 16)
            Repeater {
                model: control.previews
                Item {
                    id: line
                    required property int index
                    required property var modelData
                    width: parent.width
                    height: control.wide ? 32 : 36
                    Text {
                        id: rank
                        width: 20
                        anchors.verticalCenter: parent.verticalCenter
                        text: line.index + 1
                        color: Theme.color.primary
                        font: Theme.type.titleSmall.font
                    }
                    Text {
                        anchors.left: rank.right
                        anchors.leftMargin: 4
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        text: line.modelData.subtitle
                              ? qsTr("%1 · %2").arg(line.modelData.title).arg(line.modelData.subtitle)
                              : line.modelData.title
                        elide: Text.ElideRight
                        color: Theme.color.onSurface
                        font: Theme.type.bodyMedium.font
                    }
                }
            }
        }
    }
}
