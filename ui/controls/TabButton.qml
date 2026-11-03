pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Cirrus.Theme

// MD3 primary tab: label (and optional icon above it), primary when selected. The
// moving indicator belongs to TabBar. Height 48, or 64 with an icon.
//   TabBar { TabButton { text: qsTr("歌曲") } TabButton { text: qsTr("专辑") } }
T.TabButton {
    id: control

    property string glyph

    readonly property T.TabBar bar: T.TabBar.tabBar

    readonly property color contentColor: !enabled
        ? Qt.alpha(Theme.color.onSurface, Theme.state.disabledContent)
        : Theme.mix(Theme.color.onSurfaceVariant, Theme.color.primary, selection.value)
    // Width of the label (or icon), which the indicator matches.
    readonly property real labelWidth: Math.max(label.implicitWidth, glyph !== "" ? 24 : 0)

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding, 90)
    implicitHeight: glyph !== "" ? 64 : 48
    leftPadding: 16
    rightPadding: 16
    font: Theme.type.titleSmall.font
    // The bar is one Tab stop: the current tab; ←/→, Home, End switch tabs (TabBar).
    focusPolicy: !bar || T.TabBar.index === bar.currentIndex ? Qt.StrongFocus : Qt.ClickFocus
    Keys.onLeftPressed: (bar as TabBar)?.select(T.TabBar.index - 1)
    Keys.onRightPressed: (bar as TabBar)?.select(T.TabBar.index + 1)
    Keys.onPressed: event => {
        if (!bar || (event.key !== Qt.Key_Home && event.key !== Qt.Key_End))
            return;
        (bar as TabBar).select(event.key === Qt.Key_Home ? 0 : bar.count - 1);
        event.accepted = true;
    }

    Spring {
        id: selection
        target: control.checked ? 1 : 0
        spec: Theme.motion.defaultEffects
    }

    contentItem: Item {
        implicitWidth: control.labelWidth
        implicitHeight: column.implicitHeight

        Column {
            id: column
            anchors.centerIn: parent
            spacing: 2

            Icon {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: control.glyph !== ""
                glyph: control.glyph
                fill: selection.value
                color: control.contentColor
            }
            Text {
                id: label
                anchors.horizontalCenter: parent.horizontalCenter
                text: control.text
                font: control.font
                color: control.contentColor
                elide: Text.ElideRight
            }
        }
    }

    background: Item {
        StateLayer {
            anchors.fill: parent
            color: control.checked ? Theme.color.primary : Theme.color.onSurface
            hovered: control.hovered && control.enabled
            pressed: control.down
            focused: control.visualFocus
            pressX: control.pressed ? control.pressX : width / 2
            pressY: control.pressed ? control.pressY : height / 2
        }
        FocusRing {
            shown: control.visualFocus
            offset: -4
            containerRadius: Theme.shape.small
        }
    }
}
