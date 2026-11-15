#!/usr/bin/env python3
"""Draw the Cirrus app icon and render its bitmaps.

The mark is one cirrus wisp curled into a C, in rounded strokes: a light head at the
upper right (the hook of cirrus uncinus), a stroke that gains weight as it comes round
the left, and at the bottom a tail the wind carries off to the right, with two shorter
streaks trailing under it. Every end is round and the weight sits low; the head stays
small (a heavy round head, and pointed ends, were tried and dropped). It sits on a pale
tile, and the colour lives in the wisp: blue through violet to pink, like the player's
flowing light (designed 2026-11-15, replacing the dusk-sky picture). Flat shapes only,
no filters.
The drawing is deterministic: edit it here and run this again; never edit the outputs
by hand.

Outputs (ui/app/icons/):
  cirrus.svg          the master
  cirrus-<N>.png      N = 16 24 32 48 64 128 256 512 1024 (window, tray, packages)
  cirrus.ico          Windows (16–256)
Sizes up to 32 are rendered from a simplified drawing: a heavier stroke, one streak.
The macOS .icns is made from the PNGs by iconutil when packaging.
Requires rsvg-convert (librsvg) and ImageMagick (magick).
"""

import math
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "ui" / "app" / "icons"
SIZES = (16, 24, 32, 48, 64, 128, 256, 512, 1024)
SMALL_SIZES = (16, 24, 32)
ICO_SIZES = (16, 24, 32, 48, 64, 128, 256)

INK = (("0", "#2A6BFF"), ("0.5", "#7C55F5"), ("1", "#FF6FB0"))
TILE = (("0", "#FBFCFF"), ("1", "#DEE7FB"))
EDGE = "#20307A"  # a hairline, so the pale tile keeps its outline on a white page
TILT = -10
# The tile leaves a clear margin, as other apps' icons do: a tile that fills the canvas
# looks a size larger than its neighbours in a dock, a task bar or the tray. The small
# drawing keeps a little more of the canvas, where every pixel counts.
INSET, SMALL_INSET = 0.84, 0.88


def n(value):
    return f"{value:.2f}".rstrip("0").rstrip(".")


def stroke(line, width):
    """The outline of a stroke along the polyline `line`, `width(s)` wide at the fraction
    s of its length, with round caps."""
    lengths = [0.0]
    for a, b in zip(line, line[1:]):
        lengths.append(lengths[-1] + math.hypot(b[0] - a[0], b[1] - a[1]))
    total, last = lengths[-1], len(line) - 1

    def frame(i):  # the point, its unit tangent and half the width there
        a, b = line[max(0, i - 1)], line[min(last, i + 1)]
        dx, dy = b[0] - a[0], b[1] - a[1]
        d = math.hypot(dx, dy) or 1.0
        return line[i], (dx / d, dy / d), width(lengths[i] / total) / 2

    def cap(i, sign):
        (x, y), (tx, ty), half = frame(i)
        points = []
        for k in range(1, 12):
            a = math.pi * k / 12
            points.append((x + (-ty * math.cos(a) + sign * tx * math.sin(a)) * half,
                           y + (tx * math.cos(a) + sign * ty * math.sin(a)) * half))
        return points

    left, right = [], []
    for i in range(last + 1):
        (x, y), (tx, ty), half = frame(i)
        left.append((x - ty * half, y + tx * half))
        right.append((x + ty * half, y - tx * half))
    points = left + cap(last, 1) + right[::-1] + cap(0, -1)[::-1]
    return "M" + "L".join(f"{n(x)} {n(y)}" for x, y in points) + "Z"


def smooth(t):
    t = max(0.0, min(1.0, t))
    return t * t * (3 - 2 * t)


def wisp(radius, head_angle, tail, lift, head_width, full_width):
    """The C, centred on the origin: from the head at `head_angle` (degrees, y down) round
    the left to the bottom, then `tail` to the right, rising by `lift`. The stroke grows
    from `head_width` to `full_width` over the first five eighths of its length."""
    line, angle = [], head_angle + 360
    while angle > 90:
        line.append((radius * math.cos(math.radians(angle)), radius * math.sin(math.radians(angle))))
        angle -= 3
    for i in range(31):
        t = i / 30
        line.append((tail * t, radius - lift * t * t))
    return stroke(line, lambda s: head_width + (full_width - head_width) * smooth(s / 0.62))


def streak(x0, y0, x1, y1, width):
    return stroke([(x0 + (x1 - x0) * i / 8, y0 + (y1 - y0) * i / 8) for i in range(9)],
                  lambda s: width)


# (path, scale, where its origin goes so the mark sits in the middle of the tile: the
# centre of its box, pulled a third of the way toward its centre of mass)
FULL = (wisp(66, -40, 112, 3, 24, 32) + streak(40, 108, 104, 105, 21) + streak(2, 140, 46, 138, 16),
        0.84, (107.31, 100.37))
SMALL = (wisp(64, -40, 106, 3, 32, 40) + streak(30, 114, 96, 111, 28),
         0.86, (108.49, 108.91))


def squircle(centre=128.0, radius=120.0, exponent=5.0, steps=120):
    points = []
    for i in range(steps):
        t = 2 * math.pi * i / steps
        c, s = math.cos(t), math.sin(t)
        points.append((centre + radius * math.copysign(abs(c) ** (2 / exponent), c),
                       centre + radius * math.copysign(abs(s) ** (2 / exponent), s)))
    return "M" + "L".join(f"{n(x)} {n(y)}" for x, y in points) + "Z"


def stops(colours):
    return "".join(f'<stop offset="{offset}" stop-color="{colour}"/>' for offset, colour in colours)


def svg(mark, inset):
    path, scale, (x, y) = mark
    tile = squircle()
    return f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256">
  <!-- Generated by tools/ui/make_icon.py; edit that, not this. -->
  <defs>
    <linearGradient id="tile" x1="0.3" y1="0" x2="0.7" y2="1">{stops(TILE)}</linearGradient>
    <linearGradient id="ink" gradientUnits="userSpaceOnUse" x1="-75" y1="-70" x2="125" y2="120">{stops(INK)}</linearGradient>
  </defs>
  <g transform="translate(128 128) scale({n(inset / 0.9375)}) translate(-128 -128)">
    <path d="{tile}" fill="url(#tile)"/>
    <path d="{tile}" fill="none" stroke="{EDGE}" stroke-opacity="0.1" stroke-width="1.5"/>
    <path transform="translate({n(x)} {n(y)}) rotate({TILT}) scale({n(scale)})" d="{path}" fill="url(#ink)"/>
  </g>
</svg>
'''


def main():
    for tool in ("rsvg-convert", "magick"):
        if not shutil.which(tool):
            sys.exit(f"{tool} not found")
    OUT.mkdir(parents=True, exist_ok=True)
    master = OUT / "cirrus.svg"
    master.write_text(svg(FULL, INSET), encoding="utf-8")
    small = svg(SMALL, SMALL_INSET).encode()
    for size in SIZES:
        subprocess.run(["rsvg-convert", "-w", str(size), "-h", str(size),
                        "-o", str(OUT / f"cirrus-{size}.png")],
                       input=small if size in SMALL_SIZES else master.read_bytes(), check=True)
    subprocess.run(["magick", *[str(OUT / f"cirrus-{s}.png") for s in ICO_SIZES],
                    str(OUT / "cirrus.ico")], check=True)
    print(f"wrote {master.relative_to(ROOT)}, {len(SIZES)} PNGs and cirrus.ico")


if __name__ == "__main__":
    main()
