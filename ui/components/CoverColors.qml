import QtQuick
import Cirrus.Theme
import Cirrus.Presentation

// The current cover's colors for the Material player page and the mini and side
// players (docs/ui/pages/player-expressive.md §1): seeded with a color that is in the
// cover, in the palette style chosen in the player's display settings.
//   CoverColors { id: coverColors; dark: Theme.dark }   // coverColors.scheme
PlayerPalette {
    palette: Theme.coverPalette
    coverSeed: true
    variant: [Theme.Variant.TonalSpot, Theme.Variant.Vibrant, Theme.Variant.Expressive,
        Theme.Variant.Fidelity][Math.max(0, Math.min(3, App.settings.playerColorStyle))]
}
