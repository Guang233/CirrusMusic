pragma ComponentBehavior: Bound
import QtQuick
import Cirrus.Theme

// The player's background (docs/ui/pages/player.md §3.2). For now the still version
// of the flowing light: the cover's 5 × 5 colors averaged per row into a vertical
// gradient (also the fallback without shaders). The flowing mesh gradient replaces it
// in step 3.
Rectangle {
    id: root

    required property PlayerPalette colors

    // Row `r` of the grid, averaged.
    function rowColor(r: int): color {
        const grid = colors.grid;
        if (grid.length < 25)
            return colors.solid;
        let c = grid[r * 5];
        for (let i = 1; i < 5; ++i)
            c = Theme.mix(c, grid[r * 5 + i], 1 / (i + 1));
        return c;
    }

    gradient: Gradient {
        GradientStop {
            position: 0
            color: root.rowColor(0)
        }
        GradientStop {
            position: 0.25
            color: root.rowColor(1)
        }
        GradientStop {
            position: 0.5
            color: root.rowColor(2)
        }
        GradientStop {
            position: 0.75
            color: root.rowColor(3)
        }
        GradientStop {
            position: 1
            color: root.rowColor(4)
        }
    }
}
