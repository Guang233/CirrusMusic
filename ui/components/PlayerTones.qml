import QtQuick
import Cirrus.Theme

// The tones of a surface in a cover color (docs/ui/pages/player-expressive.md §1), for
// the Material player page and the mini and side players: the surface is the cover
// scheme's PrimaryContainer and everything on it a step of the same hue, mixed between
// scheme roles (no fixed colors).
QtObject {
    required property ColorScheme scheme

    readonly property color background: scheme.primaryContainer
    readonly property color ink: scheme.onPrimaryContainer
    readonly property color inkVariant: Qt.alpha(ink, 0.72)
    // Toward the ink (lighter on dark, darker on light): previous and next.
    readonly property color raised: Theme.mix(background, ink, 0.12)
    // Away from the ink: the tray, the top bar's buttons, the side panels.
    readonly property color tray: Theme.mix(background, scheme.surfaceContainerLowest, 0.4)
    readonly property color trayButton: Theme.mix(tray, ink, 0.08)
    readonly property color track: Qt.alpha(ink, 0.24)
}
