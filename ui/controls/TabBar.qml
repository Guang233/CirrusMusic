pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Cirrus.Theme

// MD3 primary tabs: TabButtons share the width, a divider runs along the bottom and
// a 3 px indicator under the current label slides to the new tab (fastSpatial).
// Tabs scroll horizontally when they don't fit.
T.TabBar {
    id: control

    readonly property T.TabButton currentTab: currentItem as T.TabButton
    // Indicator target in the bar's coordinates.
    readonly property real indicatorWidth: currentTab ? Math.max(24, (currentTab as TabButton).labelWidth) : 0
    readonly property real indicatorX: currentTab
        ? currentTab.x + (currentTab.width - indicatorWidth) / 2 - (contentItem as ListView).contentX
        : 0

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            contentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             contentHeight + topPadding + bottomPadding)
    spacing: 0

    Spring {
        id: slideX
        target: control.indicatorX
        spec: Theme.motion.fastSpatial
        threshold: 0.1
    }
    Spring {
        id: slideWidth
        target: control.indicatorWidth
        spec: Theme.motion.fastSpatial
        threshold: 0.1
    }
    // No slide from 0 when the bar first lays out.
    Component.onCompleted: Qt.callLater(() => {
        slideX.snapTo(indicatorX);
        slideWidth.snapTo(indicatorWidth);
    })

    contentItem: ListView {
        model: control.contentModel
        currentIndex: control.currentIndex
        spacing: control.spacing
        orientation: ListView.Horizontal
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.AutoFlickIfNeeded
        snapMode: ListView.SnapToItem
        highlightMoveDuration: 0
        highlightRangeMode: ListView.ApplyRange
        preferredHighlightBegin: 48
        preferredHighlightEnd: width - 48
    }

    background: Item {
        implicitHeight: 48

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.color.surfaceContainerHighest
        }
        Rectangle {
            x: slideX.value
            y: parent.height - height
            width: slideWidth.value
            height: 3
            visible: control.currentTab !== null
            topLeftRadius: 3
            topRightRadius: 3
            color: control.enabled ? Theme.color.primary
                                   : Qt.alpha(Theme.color.onSurface, Theme.state.disabledContent)
        }
    }
}
