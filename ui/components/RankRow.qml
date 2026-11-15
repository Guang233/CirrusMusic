pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Cirrus.Theme
import Cirrus.Controls

// One row of a ranking (docs/ui/pages/stats.md §2.5, §2.7): the rank, a cover or an
// icon, the title, a bar (8 px, its length relative to the first row) and the value.
// The bar drops under the title when the row is narrow. Clickable rows respond and
// emit clicked().
//   RankRow { rank: 1; title: "雾港乐队"; fraction: 1; valueText: "31 次" }
T.AbstractButton {
    id: row

    property int rank: 1
    property string coverKey
    property bool showCover: false
    property bool roundCover: false
    property string glyph
    property real fraction: 0
    property string valueText
    property bool clickable: true
    property color barColor: ColorScope.scheme.primary

    readonly property bool narrow: width < 480
    readonly property real barSpace: Math.min(160, width * 0.28)

    implicitHeight: narrow ? 64 : 52
    enabled: true
    hoverEnabled: clickable
    focusPolicy: clickable ? Qt.TabFocus : Qt.NoFocus
    Accessible.role: clickable ? Accessible.Button : Accessible.ListItem
    Accessible.name: qsTr("第 %1 名，%2，%3").arg(rank).arg(text).arg(valueText)

    HoverHandler {
        enabled: row.clickable
        cursorShape: Qt.PointingHandCursor
    }
    background: Item {
        StateLayer {
            anchors.fill: parent
            radius: Theme.shape.medium
            color: ColorScope.scheme.onSurface
            visible: row.clickable
            hovered: row.hovered
            pressed: row.down
            focused: row.visualFocus
            pressX: row.pressX
            pressY: row.pressY
        }
        FocusRing {
            shown: row.visualFocus
            containerTopLeftRadius: Theme.shape.medium
            containerTopRightRadius: Theme.shape.medium
            containerBottomRightRadius: Theme.shape.medium
            containerBottomLeftRadius: Theme.shape.medium
        }
    }

    contentItem: Item {
        Text {
            id: rankText
            x: 8
            width: 24
            anchors.verticalCenter: parent.verticalCenter
            horizontalAlignment: Text.AlignHCenter
            text: row.rank
            color: row.rank <= 3 ? ColorScope.scheme.primary : ColorScope.scheme.onSurfaceVariant
            font: Fonts.tabular(Theme.type.titleSmall.font)
        }
        Item {
            id: art
            x: rankText.x + rankText.width + 8
            width: row.showCover || row.glyph !== "" ? 40 : 0
            height: 40
            anchors.verticalCenter: parent.verticalCenter
            CoverArt {
                anchors.fill: parent
                visible: row.showCover
                coverKey: row.coverKey
                radius: row.roundCover ? width / 2 : Theme.shape.small
                glyph: row.glyph !== "" ? row.glyph : Symbols.music_note
            }
            Rectangle {
                anchors.fill: parent
                visible: !row.showCover && row.glyph !== ""
                radius: row.roundCover ? width / 2 : Theme.shape.small
                color: ColorScope.scheme.surfaceContainerHigh
                Icon {
                    anchors.centerIn: parent
                    glyph: row.glyph
                    size: 22
                    color: ColorScope.scheme.onSurfaceVariant
                }
            }
        }
        Text {
            id: value
            anchors.right: parent.right
            anchors.rightMargin: 8
            // On the title's baseline in a narrow row.
            y: row.narrow ? title.y + title.baselineOffset - baselineOffset
                          : (parent.height - height) / 2
            text: row.valueText
            color: ColorScope.scheme.onSurfaceVariant
            font: Fonts.tabular(Theme.type.labelLarge.font)
        }
        Text {
            id: title
            x: art.x + art.width + (art.width > 0 ? 12 : 0)
            width: (row.narrow ? value.x : bar.x) - x - 12
            // A narrow row centers the title and the bar under it together.
            y: row.narrow ? (parent.height - height - 8 - bar.height) / 2
                          : (parent.height - height) / 2
            text: row.text
            elide: Text.ElideRight
            color: ColorScope.scheme.onSurface
            font: Theme.type.bodyLarge.font
        }
        Item {
            id: bar
            // Beside the value, or under the title in a narrow row.
            x: row.narrow ? title.x : value.x - 16 - width
            y: row.narrow ? title.y + title.height + 8 : (parent.height - height) / 2
            width: row.narrow ? parent.width - title.x - 8 : row.barSpace
            height: 8
            Rectangle {
                width: Math.max(height, parent.width * Math.max(0, Math.min(1, row.fraction)))
                height: parent.height
                radius: height / 2
                color: row.barColor
                visible: row.fraction > 0
            }
        }
    }
}
