import QtQuick
import Cirrus.Theme

// MD3 elevation shadow behind a container (fill the container's area and match
// its radii). `level` is in dp (Theme.elevation.level0-5). Hidden under the
// software renderer, where tonal surfaces carry the hierarchy alone.
Shadow {
    id: root

    property real level

    visible: level > 0 && GraphicsInfo.api !== GraphicsInfo.Software
             && GraphicsInfo.api !== GraphicsInfo.Unknown
    offset.x: 0
    offset.y: level > 0 ? 0.5 + level * 0.5 : 0
    blur: level > 0 ? 1 + level * 1.6 : 0
    color: Qt.alpha(ColorScope.scheme.shadow, Theme.dark ? 0.55 : 0.22)

    Behavior on level {
        NumberAnimation {
            duration: Theme.motion.defaultEffects.duration
            easing: Theme.motion.defaultEffects.easing
        }
    }
}
