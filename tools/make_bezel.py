#!/usr/bin/env python3
"""make_bezel.py - side-bar artwork from the game's own gallery, read from your disc.

  python make_bezel.py --install . [--use dragons]   # in the release zip's folder
  python tools/make_bezel.py --install build-release  # in this repository
  python tools/make_bezel.py [--disc CUE] [--out DIR] # only the pictures (./bezel/)

The recomp leaves black bars at the sides of the screens made for 4:3 (the
chapter title cards, the menus, the world map, the movies). The framework's
Bezel Artwork (launcher: Mods > Bezel Artwork > Bezel image) draws a picture
of your choice there. This script makes two such pictures, 1920 x 1080 PNGs,
from the paintings of the title menu's illustration gallery
(ILLUST/ILLUST_U.CDR on disc 1):

  bezel_gallery_heroes.png    the heroes: gallery 36 left, 38 right
  bezel_gallery_dragons.png   the dragons: gallery 42 left, 41 right

Each side bar is a 240-pixel strip of a painting cut above or below its
"Image illustration" caption, a little darker than the painting, with a soft
shadow next to the game picture; the middle, where the game draws, is
black. They fit a 16:9 window (the bezel is stretched to the window).

--install GAME_DIR writes the pictures to GAME_DIR/bezel/ and turns the
bezel on in GAME_DIR/mods/state.toml with the one --use names (heroes by
default); the launcher shows the choice and can change it. Close the game
first: it rewrites state.toml when it exits.

The disc: --disc, else the [disc] path of settings.toml (GAME_DIR's, then
the one in the current folder), else the dev layout's default. Only the
gallery file is read, nothing is written next to the disc. Python 3.8+,
standard library only.

The pictures are the game's art: they stay on your computer (do not share
or commit them).
"""
import re
import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_CUE = ROOT.parent / "Dragon Valor (USA) (Disc 1)" / "Dragon Valor (USA) (Disc 1).cue"

W, H, BAR, SHADE = 1920, 1080, 240, 48
GALLERY_COUNT = 96
# Gallery index -> art rows kept (the painting is 210 x 480 or 384 x 480 inside a
# 384 x 480 image; its "Image illustration" caption lies outside these rows).
KEEP = {36: (0, 392), 38: (72, 480), 41: (0, 404), 42: (76, 480)}
# (left painting, how far into it the strip starts, right painting, same)
CHOICES = {"heroes": (36, 0.30, 38, 0.42), "dragons": (42, 0.12, 41, 0.30)}
DARKEN = 0.85


class Disc:
    """A .cue / .bin dump (2352-byte sectors) or a plain .iso (2048)."""

    def __init__(self, path):
        path = Path(path)
        if path.suffix.lower() == ".cue":
            m = re.search(r'FILE\s+"([^"]+)"', path.read_text(encoding="utf-8", errors="replace"))
            if not m:
                sys.exit(f"no FILE line in {path}")
            path = path.parent / m.group(1)
        if not path.is_file():
            sys.exit(f"disc image not found: {path}")
        self.f = path.open("rb")
        self.raw = path.stat().st_size % 2352 == 0 and path.suffix.lower() != ".iso"
        if self.sector(16)[1:6] != b"CD001":
            sys.exit(f"{path}: not an ISO9660 data track")

    def sector(self, lba):
        if not self.raw:
            self.f.seek(lba * 2048)
            return self.f.read(2048).ljust(2048, b"\0")
        self.f.seek(lba * 2352)
        s = self.f.read(2352).ljust(2352, b"\0")
        return s[24:2072] if s[15] == 2 else s[16:2064]

    def read(self, lba, size):
        return b"".join(self.sector(lba + i) for i in range((size + 2047) // 2048))[:size]

    def entries(self, lba, size):
        data, i = self.read(lba, size), 0
        while i < len(data):
            n = data[i]
            if n == 0:
                i = (i // 2048 + 1) * 2048
                continue
            rec = data[i:i + n]
            name = rec[33:33 + rec[32]].decode("ascii", "replace").split(";")[0]
            yield name, struct.unpack_from("<I", rec, 2)[0], struct.unpack_from("<I", rec, 10)[0]
            i += n

    def file(self, path):
        root = self.sector(16)[156:190]
        lba, size = struct.unpack_from("<I", root, 2)[0], struct.unpack_from("<I", root, 10)[0]
        for part in path.split("/"):
            for name, l, s in self.entries(lba, size):
                if name == part:
                    lba, size = l, s
                    break
            else:
                return None
        return self.read(lba, size)


def gallery(cdr):
    """The gallery's 16-bit TIMs in file order: [(width, height, pixels)]."""
    out = []
    for m in re.finditer(rb"\x10\x00\x00\x00\x02\x00\x00\x00", cdr):
        p = m.start() + 8
        if p + 12 > len(cdr):
            continue
        bnum, _, _, w, h = struct.unpack_from("<IHHHH", cdr, p)
        if bnum == 12 + w * h * 2 and p + bnum <= len(cdr):
            out.append((w, h, cdr[p + 12:p + bnum]))
    return out


def rgb_rows(w, h, pix):
    """15-bit BGR to rows of (r, g, b) tuples."""
    rows = []
    for y in range(h):
        vals = struct.unpack_from(f"<{w}H", pix, y * w * 2)
        rows.append([(((v & 31) * 527 + 23) >> 6, (((v >> 5) & 31) * 527 + 23) >> 6,
                      (((v >> 10) & 31) * 527 + 23) >> 6) for v in vals])
    return rows


def painting(img, index):
    """The painting's rows inside its black frame, its caption cut off."""
    w, h, pix = img
    rows = rgb_rows(w, h, pix)
    cols = [x for x in range(w) if any(max(rows[y][x]) > 12 for y in range(0, h, 8))]
    x0, x1 = cols[0], cols[-1] + 1
    y0, y1 = KEEP[index]
    return [r[x0:x1] for r in rows[y0:y1]]


def strip(rows, shift):
    """A BAR x H strip: the painting scaled to the full height (bilinear), cut
    at `shift` of the width left over, darkened."""
    sh, sw = len(rows), len(rows[0])
    scale = H / sh
    out_w = round(sw * scale)
    left = round((out_w - BAR) * shift)

    def taps(n_out, n_in, offset):
        t = []
        for o in range(n_out):
            s = (o + offset + 0.5) / scale - 0.5
            i = max(0, min(n_in - 1, int(s // 1)))
            f = min(max(s - i, 0.0), 1.0)
            t.append((i, min(i + 1, n_in - 1), f))
        return t

    xt, yt = taps(BAR, sw, left), taps(H, sh, 0)
    out = []
    for i0, i1, fy in yt:
        r0, r1 = rows[i0], rows[i1]
        line = []
        for j0, j1, fx in xt:
            a, b, c, d = r0[j0], r0[j1], r1[j0], r1[j1]
            line.append(tuple(int(((a[k] * (1 - fx) + b[k] * fx) * (1 - fy) + (c[k] * (1 - fx) + d[k] * fx) * fy)
                                  * DARKEN + 0.5) for k in range(3)))
        out.append(line)
    return out


def compose(left, right):
    """The 1920 x 1080 picture: the strips at the sides, a shadow by the game."""
    ramp = [1.0] * BAR
    for k in range(SHADE):
        ramp[BAR - SHADE + k] = 1 - 0.65 * (k + 1) / SHADE
    black = bytes(3 * (W - 2 * BAR))
    rows = []
    for y in range(H):
        lrow = bytes(int(v * ramp[x]) for x, px in enumerate(left[y]) for v in px)
        rrow = bytes(int(v * ramp[BAR - 1 - x]) for x, px in enumerate(right[y]) for v in px)
        rows.append(lrow + black + rrow)
    return rows


def write_png(path, rows):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
    raw = b"".join(b"\0" + r for r in rows)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def settings_disc(folder):
    p = Path(folder) / "settings.toml"
    if not p.is_file():
        return None
    m = re.search(r'(?ms)^\[disc\]\s*$.*?^path\s*=\s*"([^"]+)"', p.read_text(encoding="utf-8", errors="replace"))
    return Path(m.group(1)) if m else None


def enable(game_dir, picture):
    """Turn the bezel on in GAME_DIR/mods/state.toml with this picture."""
    state = Path(game_dir) / "mods" / "state.toml"
    if not state.is_file():
        sys.exit(f"{state} not found: start the game once first (it writes the file)")
    raw = state.read_bytes()
    crlf = b"\r\n" in raw
    s = raw.decode("utf-8").replace("\r\n", "\n")
    path = str(Path(picture).resolve()).replace("\\", "\\\\")
    block = ('[[feature]]\npackage_id = "psx.presentation.bezel"\nid = "bezel"\nenabled = true\n'
             f'[feature.resources]\nartwork = "{path}"')
    s, n = re.subn(r'\[\[feature\]\]\npackage_id = "psx\.presentation\.bezel"\n.*?(?=\n\[\[|\s*\Z)',
                   lambda _: block, s, flags=re.S)
    if n == 0:
        if 'id = "psx.presentation.bezel"' not in s:
            i = s.find("[[feature]]")
            pkg = '[[package]]\nid = "psx.presentation.bezel"\n\n'
            s = s + "\n" + pkg if i < 0 else s[:i] + pkg + s[i:]
        s = s.rstrip("\n") + "\n\n" + block
    if not s.endswith("\n"):
        s += "\n"
    state.write_bytes((s.replace("\n", "\r\n") if crlf else s).encode("utf-8"))
    print(f"bezel on in {state}: {picture}")


def main(argv):
    args = list(argv)

    def opt(name):
        if name in args:
            i = args.index(name)
            if i + 1 >= len(args):
                sys.exit(f"{name} needs a value")
            v = args[i + 1]
            del args[i:i + 2]
            return v
        return None

    disc, out, game_dir, use = opt("--disc"), opt("--out"), opt("--install"), opt("--use") or "heroes"
    if args:
        sys.exit(__doc__)
    if use not in CHOICES:
        sys.exit(f"--use takes one of: {', '.join(CHOICES)}")
    cue = Path(disc) if disc else (settings_disc(game_dir) if game_dir else None) or settings_disc(".") or DEFAULT_CUE
    print(f"disc: {cue}")
    cdr = Disc(cue).file("ILLUST/ILLUST_U.CDR")
    if cdr is None:
        sys.exit("ILLUST/ILLUST_U.CDR is not on this disc: use disc 1 (--disc)")
    images = gallery(cdr)
    if len(images) != GALLERY_COUNT or any(images[i][:2] != (384, 480) for i in KEEP):
        sys.exit(f"unexpected gallery ({len(images)} images): this script knows the USA disc 1")
    folder = Path(out) if out else (Path(game_dir) / "bezel" if game_dir else Path("bezel"))
    folder.mkdir(parents=True, exist_ok=True)
    made = {}
    for name, (li, ls, ri, rs) in CHOICES.items():
        rows = compose(strip(painting(images[li], li), ls), strip(painting(images[ri], ri), rs))
        made[name] = folder / f"bezel_gallery_{name}.png"
        write_png(made[name], rows)
        print(f"wrote {made[name]}")
    if game_dir:
        enable(game_dir, made[use])
    else:
        print("Launcher: Mods > Bezel Artwork on, Bezel image = one of these pictures.")


if __name__ == "__main__":
    main(sys.argv[1:])
