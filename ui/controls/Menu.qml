pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Cirrus.Theme

// MD3E standard menu: SurfaceContainerLow, 16 px corners, level-2 shadow, 4 px inset.
// Fill it with MenuItem / MenuSeparator / Action / Menu (submenu).
//   Menu { id: menu; MenuItem { text: qsTr("下一首播放") } }   ... menu.popup()
T.Menu {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)
    margins: 8
    padding: 4
    overlap: 4
    transformOrigin: T.Popup.Top

    delegate: MenuItem {}

    enter: Transition {
        ParallelAnimation {
            NumberAnimation {
                property: "scale"
                from: 0.85
                to: 1
                duration: Theme.motion.standardFastSpatial.duration
                easing: Theme.motion.standardFastSpatial.easing
            }
            NumberAnimation {
                property: "opacity"
                from: 0
                to: 1
                duration: Theme.motion.fastEffects.duration
                easing: Theme.motion.fastEffects.easing
            }
        }
    }
    exit: Transition {
        NumberAnimation {
            property: "opacity"
            from: 1
            to: 0
            duration: Theme.motion.fastEffects.duration
            easing: Theme.motion.fastEffects.easing
        }
    }

    contentItem: ListView {
        implicitWidth: Math.max(112, Math.min(280, contentItemWidth()))
        implicitHeight: contentHeight
        model: control.contentModel
        interactive: contentHeight + control.topPadding + control.bottomPadding > control.height
        clip: interactive
        currentIndex: control.currentIndex
        boundsBehavior: Flickable.StopAtBounds

        function contentItemWidth(): real {
            let widest = 0;
            for (let i = 0; i < count; ++i) {
                const item = control.itemAt(i);
                if (item)
                    widest = Math.max(widest, item.implicitWidth);
            }
            return widest;
        }

        ScrollBar.vertical: ScrollBar {}
    }

    background: Item {
        implicitWidth: 112
        implicitHeight: 48

        Elevation {
            anchors.fill: parent
            level: Theme.elevation.level2
            radius: Theme.shape.large
        }
        Rectangle {
            anchors.fill: parent
            radius: Theme.shape.large
            color: ColorScope.scheme.surfaceContainerLow
        }
    }

    T.Overlay.modal: Rectangle {
        color: Qt.alpha(ColorScope.scheme.scrim, 0.32)
    }
    T.Overlay.modeless: Item {}
}
