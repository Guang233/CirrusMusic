pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Effects
import Cirrus.Theme
import Cirrus.Components
import Cirrus.Presentation

// The player's cover (docs/ui/pages/player.md §4): a soft shadow in the cover's own
// darkest color, and a little smaller while paused, as if laid down (springy, the
// shadow shrinking with it). Without motion only the shadow stays.
Item {
    id: root

    required property PlayerPalette colors
    readonly property real radius: Theme.shape.medium
    // The drawn cover, scaled while paused: the shared element flies to its rect.
    readonly property Item art: drawn
    // The largest the cover gets, in device pixels.
    readonly property int decodeSize: Math.ceil(720 * Screen.devicePixelRatio)
    readonly property bool playing: App.player.status === Playback.Playing
                                    || App.player.status === Playback.Loading

    Spring {
        id: lift
        target: root.playing || Theme.motion.reduced ? 1 : 0
        spec: Theme.motion.defaultSpatial
    }

    Item {
        id: drawn
        anchors.fill: parent
        scale: 0.86 + 0.14 * lift.value

        RectangularShadow {
            anchors.fill: parent
            radius: root.radius
            offset.y: 8 + 16 * lift.value
            blur: 24 + 40 * lift.value
            spread: -4
            color: Qt.alpha(root.colors.shadow, (root.colors.dark ? 0.55 : 0.35)
                            * (0.6 + 0.4 * lift.value))
        }

        CoverArt {
            anchors.fill: parent
            radius: root.radius
            coverKey: App.player.current.coverKey
            // One decode at the largest size, not one per frame while it glides.
            sourceSize: Qt.size(root.decodeSize, root.decodeSize)
        }
    }
}
