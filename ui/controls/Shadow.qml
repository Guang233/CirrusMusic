import QtQuick
import QtQuick.Effects

// A RectangularShadow that frees its material with it (ShadowOwnsMaterial: Qt leaks it).
// Product QML uses this, never RectangularShadow itself (docs/ui/controls.md §2).
RectangularShadow {
    ShadowOwnsMaterial {}
}
