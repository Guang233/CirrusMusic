pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Cirrus.Theme
import Cirrus.Controls
import Cirrus.Components

// A chart: cover, name and update schedule, then its top songs with their ranks. The
// whole card opens the chart (a playlist). On the home page the songs are under the
// cover; `wide` (discover's official charts) puts them beside a larger cover and adds
// a play button at the top right (`playRequested`).
T.AbstractButton {
    id: control

    property string coverKey
    property string updateLabel
    // [{title, subtitle}], the first few songs.
    property var previews: []
    property bool wide: false

    signal playRequested

    implicitHeight: wide ? 184 : 216
    focusPolicy: Qt.TabFocus
    Accessible.role: Accessible.Button
    Accessible.name: text

    HoverHandler {
        enabled: control.hoverEnabled
        cursorShape: Qt.PointingHandCursor
    }
    background: Rectangle {
        radius: Theme.shape.large
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
            coverKey: control.coverKey
            radius: Theme.shape.medium
        }
        Column {
            id: heading
            anchors.left: cover.right
            anchors.leftMargin: 16
            anchors.right: play.visible ? play.left : parent.right
            anchors.rightMargin: play.visible ? 8 : 16
            y: control.wide ? cover.y : cover.y + (cover.height - height) / 2
            spacing: 4
            Text {
                width: parent.width
                text: control.text
                elide: Text.ElideRight
                color: Theme.color.onSurface
                font: Theme.type.titleMedium.font
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
            x: control.wide ? heading.x : 16
            y: control.wide ? cover.y + cover.height - height : cover.y + cover.height + 12
            width: parent.width - x - 16
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
