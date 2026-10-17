#!/usr/bin/env python3
"""Draw the app icon: a pixel-art sword on a crimson field, with a cyan glint.

Our own drawing after the idea of Dragon Valor's memory-card save icon (a
sword on the diagonal over dark red, a cyan sparkle); no pixel of the game's
icon is used. The sword is a few shapes in a 512 grid, rasterised without
anti-aliasing on a small grid (32, 24, 16 px), outlined one pixel dark and
mapped to a fixed palette; the larger sizes are whole-number enlargements of
the 32 px drawing, so every size keeps square pixels.

Writes assets/psxrecomp.ico (16-256 px), assets/psxrecomp.png (512 px, the
window / taskbar / launcher icon) and assets/psxrecomp.svg (the 32 px
drawing). The names are the framework's: CMake embeds the .ico (APP_ICON)
and stages the .png beside the exe.

Usage (any cwd; needs Pillow):
  python tools/make_app_icon.py
  python tools/make_app_icon.py --preview out.png   # also a size sheet
"""

from __future__ import annotations

import argparse
import math
import pathlib

from PIL import Image, ImageDraw

ROOT = pathlib.Path(__file__).resolve().parent.parent
ASSETS = ROOT / "assets"

# The sword lies on the diagonal, tip at the bottom left. `at(s, w)` is the
# point s units from the tip along the blade and w across it (512 grid).
U = (1 / math.sqrt(2), -1 / math.sqrt(2))
V = (1 / math.sqrt(2), 1 / math.sqrt(2))
SCALE = 1.15
MID_S = 225          # this point of the sword sits at the icon centre
BLADE_END = 300      # tip to guard
TAPER = 70           # length of the point
GLINT_S = 178

PAL = {
    "bg": (139, 16, 22), "bg_shade": (98, 10, 14), "outline": (24, 8, 10), "frame": (18, 6, 8),
    "steel": (240, 244, 246), "steel_shade": (150, 162, 172),
    "gold": (224, 178, 82), "gold_shade": (168, 119, 44), "grip": (122, 74, 36),
    "glint": (190, 247, 255), "glint_edge": (90, 220, 255), "glint_core": (255, 255, 255),
}
SWORD_COLOURS = ("steel", "steel_shade", "gold", "gold_shade", "grip")


def at(s: float, w: float = 0.0) -> tuple[float, float]:
    s, w = (s - MID_S) * SCALE, w * SCALE
    return (256 + U[0] * s + V[0] * w, 256 + U[1] * s + V[1] * w)


def circle(c, r, n=48):
    return [(c[0] + r * math.cos(2 * math.pi * i / n), c[1] + r * math.sin(2 * math.pi * i / n))
            for i in range(n)]


def capsule(a, b, r, n=12):
    """A thick segment with round ends."""
    ang = math.atan2(b[1] - a[1], b[0] - a[0])
    pts = [(a[0] + r * math.cos(ang + math.pi / 2 + math.pi * i / n),
            a[1] + r * math.sin(ang + math.pi / 2 + math.pi * i / n)) for i in range(n + 1)]
    pts += [(b[0] + r * math.cos(ang - math.pi / 2 + math.pi * i / n),
             b[1] + r * math.sin(ang - math.pi / 2 + math.pi * i / n)) for i in range(n + 1)]
    return pts


def sword_shapes():
    """(polygon, palette key) in drawing order."""
    e = BLADE_END
    guard_a, guard_b = at(e + 14, -82), at(e + 14, 82)
    return [
        ([at(0), at(TAPER, -27), at(e, -27), at(e, 0)], "steel"),
        ([at(0), at(e, 0), at(e, 27), at(TAPER, 27)], "steel_shade"),
        ([at(e + 20, -16), at(e + 104, -16), at(e + 104, 16), at(e + 20, 16)], "grip"),
        (capsule(guard_a, guard_b, 17), "gold"),
        ([guard_a, guard_b, at(e + 31, 82), at(e + 31, -82)], "gold_shade"),
        (circle(guard_b, 17), "gold_shade"),
        (circle(at(e + 126), 25), "gold"),
        (circle(at(e + 130, 5), 12), "gold_shade"),
    ]


def sword_layer(n: int) -> Image.Image:
    """The sword at n x n: drawn at 512 x 4, averaged down, so each pixel
    knows how much of it the sword covers and which colour dominates."""
    big = Image.new("RGBA", (2048, 2048), (0, 0, 0, 0))
    d = ImageDraw.Draw(big)
    for poly, key in sword_shapes():
        d.polygon([(x * 4, y * 4) for x, y in poly], fill=PAL[key] + (255,))
    return big.resize((n, n), Image.BOX)


def pixel_icon(n: int) -> Image.Image:
    img = Image.new("RGB", (n, n), PAL["bg"])
    px = img.load()
    for y in range(n):
        for x in range(n):
            if x + y > n - 1:          # the half below the blade is darker
                px[x, y] = PAL["bg_shade"]
    layer = sword_layer(n).load()
    on = [[False] * n for _ in range(n)]
    cols = [PAL[k] for k in SWORD_COLOURS]
    for y in range(n):
        for x in range(n):
            r, g, b, a = layer[x, y]
            if a > 110:
                px[x, y] = min(cols, key=lambda c: (c[0] - r) ** 2 + (c[1] - g) ** 2 + (c[2] - b) ** 2)
                on[y][x] = True
    for y in range(n):
        for x in range(n):
            if not on[y][x] and any(0 <= x + dx < n and 0 <= y + dy < n and on[y + dy][x + dx]
                                    for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                px[x, y] = PAL["outline"]
    gx, gy = (int(v * n / 512) for v in at(GLINT_S))
    arm = 3 if n >= 32 else (2 if n >= 24 else 1)
    for d in range(-arm, arm + 1):
        for x, y in ((gx + d, gy), (gx, gy + d)):
            px[x, y] = PAL["glint_edge"] if abs(d) == arm else PAL["glint"]
    px[gx, gy] = PAL["glint_core"]
    if n >= 32:
        for dx, dy in ((-2, -2), (2, -2), (-2, 2), (2, 2)):
            px[gx + dx, gy + dy] = PAL["glint_edge"]
    for i in range(n):
        for x, y in ((i, 0), (i, n - 1), (0, i), (n - 1, i)):
            px[x, y] = PAL["frame"]
    return img.convert("RGBA")


def svg(img: Image.Image) -> str:
    """One rect per horizontal run of equal pixels."""
    n = img.size[0]
    px = img.load()
    out = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {n} {n}" '
           f'width="512" height="512" shape-rendering="crispEdges">']
    for y in range(n):
        x = 0
        while x < n:
            c = px[x, y]
            run = 1
            while x + run < n and px[x + run, y] == c:
                run += 1
            out.append(f'<rect x="{x}" y="{y}" width="{run}" height="1" fill="#{c[0]:02x}{c[1]:02x}{c[2]:02x}"/>')
            x += run
    out.append("</svg>")
    return "\n".join(out) + "\n"


def sizes() -> dict[int, Image.Image]:
    p16, p24, p32 = pixel_icon(16), pixel_icon(24), pixel_icon(32)
    big = {s: p32.resize((s, s), Image.NEAREST) for s in (64, 128, 256, 512)}
    return {16: p16, 24: p24, 32: p32, 48: p24.resize((48, 48), Image.NEAREST), **big}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--preview", type=pathlib.Path, help="also write a sheet of every size")
    args = ap.parse_args()
    imgs = sizes()
    ASSETS.mkdir(exist_ok=True)
    ico_sizes = [16, 24, 32, 48, 64, 128, 256]
    imgs[256].save(ASSETS / "psxrecomp.ico", format="ICO", sizes=[(s, s) for s in ico_sizes],
                   append_images=[imgs[s] for s in ico_sizes if s != 256], bitmap_format="bmp")
    imgs[512].save(ASSETS / "psxrecomp.png", optimize=True)
    (ASSETS / "psxrecomp.svg").write_text(svg(imgs[32]), encoding="utf-8", newline="\n")
    print(f"wrote {ASSETS / 'psxrecomp.ico'} ({', '.join(map(str, ico_sizes))} px), .png (512 px), .svg")
    if args.preview:
        row = [16, 24, 32, 48, 64, 128, 256]
        sheet = Image.new("RGBA", (sum(row) + 20 * (len(row) + 1), 256 + 40), (40, 40, 40, 255))
        x = 20
        for s in row:
            sheet.alpha_composite(imgs[s], (x, 20 + 256 - s))
            x += s + 20
        sheet.save(args.preview)
        print(f"wrote {args.preview}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
