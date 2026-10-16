#!/usr/bin/env python3
"""datacdr.py - read the stage layout of Dragon Valor's DATA.CDR (read-only).

  python tools/datacdr.py areas [--disc CUE]
  python tools/datacdr.py kinds [--c] [--disc CUE]   # action stages vs scenes per area
  python tools/datacdr.py stage AREA STAGE [--disc CUE]
  python tools/datacdr.py hero AREA STAGE [--disc CUE]

Layout (decoded 2026-10-16 from the CD read log of stage reloads; see
docs/DEVELOPMENT.md, "DATA.CDR layout and the hero"):

  sector 0          area table: 13 x [min sec frame 0] sector offsets of the
                    area headers (+1); areas 7..12 restart at 0 = disc 2.
  area header       container of 3 sections; section 1 is the stage table,
                    48 x [m s f 0] offsets of each stage index from the header.
  stage index       container of 4 sections; section 3 lists the start of
                    every file of the stage, [m s f 0] relative to the index.
                    A file runs to the next start; the game reads exactly that.
  files             mostly containers: u32 count, count+1 u32 offsets, data.
                    Section tags VAB_CODE (sound bank), TIM_CODE (TIM image
                    with "TIM_CODE" in place of the 0x10 magic), SEQ_CODE.

Per stage, the first file (start 1) carries a sound bank and the TIMs the
stage uploads, the hero's texture page among them (VRAM x 384, y 0). The
second file is the action pack the game keeps at 0x800C5E98: entry 0 holds
the render nodes, 1 hero sounds, 2 skeleton / animations, 3..16 the hero's
14 body parts as GPU packet templates, 17..30 their vertex data.
"""
import hashlib
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_CUE = ROOT.parent / "Dragon Valor (USA) (Disc 1)" / "Dragon Valor (USA) (Disc 1).cue"


class Disc:
    def __init__(self, cue):
        cue = Path(cue)
        text = cue.read_text(encoding="utf-8", errors="replace")
        m = re.search(r'FILE\s+"([^"]+)"', text)
        if not m:
            sys.exit(f"no FILE line in {cue}")
        self.f = (cue.parent / m.group(1)).open("rb")
        pvd = self.sector(16)
        if pvd[1:6] != b"CD001":
            sys.exit(f"{cue}: first track is not ISO9660")
        root = pvd[156:190]
        rlba, rsize = struct.unpack_from("<I", root, 2)[0], struct.unpack_from("<I", root, 10)[0]
        self.data_lba = self._find(rlba, rsize, "DATA.CDR")
        # Disc 1 boots SLUS_010.92 and holds areas 0..6; disc 2 boots SLUS_011.64
        # and holds 7..12 (its area table restarts at 0 for area 7).
        self.areas = range(0, 7) if self._find(rlba, rsize, "SLUS_010.92", must=False) else range(7, 13)

    def sector(self, lba):
        self.f.seek(lba * 2352)
        raw = self.f.read(2352)
        if len(raw) < 2352:   # an unused table entry can point past the image
            return bytes(2048)
        return raw[24:2072] if raw[15] == 2 else raw[16:2064]

    def _find(self, lba, size, want, must=True):
        data = b"".join(self.sector(lba + i) for i in range((size + 2047) // 2048))
        i = 0
        while i < len(data):
            n = data[i]
            if n == 0:
                i = (i // 2048 + 1) * 2048
                continue
            rec = data[i:i + n]
            name = rec[33:33 + rec[32]].decode("ascii", "replace").split(";")[0]
            if name == want:
                return struct.unpack_from("<I", rec, 2)[0]
            i += n
        if must:
            sys.exit(f"{want} not found in the root directory")
        return None

    def cdr(self, off, n=1):
        return b"".join(self.sector(self.data_lba + off + i) for i in range(n))


def msf(b):
    return (b[0] * 60 + b[1]) * 75 + b[2]


def split(b):
    """Entries of a container, or None when b is not one."""
    if len(b) < 8:
        return None
    n = struct.unpack_from("<I", b)[0]
    if not 1 <= n <= 4096 or 4 * (n + 2) > len(b):
        return None
    offs = struct.unpack_from(f"<{n + 1}I", b, 4)
    if offs[0] != 4 * (n + 2) or offs[-1] > len(b) or any(offs[i] > offs[i + 1] for i in range(n)):
        return None
    return [b[offs[i]:offs[i + 1]] for i in range(n)]


def tag(b):
    m = re.match(rb"[A-Z]{3}_CODE", b[:8])
    return m.group().decode() if m else None


def tims(sec):
    out, p = [], 0
    while p + 16 <= len(sec) and sec[p:p + 8] == b"TIM_CODE":
        flags = struct.unpack_from("<I", sec, p + 12)[0]
        q, clut = p + 16, None
        if flags & 8:
            ln, x, y, w, h = struct.unpack_from("<IHHHH", sec, q)
            clut, q = (x, y, w, h), q + ln
        ln, x, y, w, h = struct.unpack_from("<IHHHH", sec, q)
        q += ln
        out.append({"bpp": {0: 4, 1: 8, 2: 16}.get(flags & 3, 24), "img": (x, y, w, h), "clut": clut, "data": sec[p:q]})
        p = q
    return out


def area_header(d, area):
    if area not in d.areas:
        return None, None
    off = 1 + msf(d.cdr(0)[4 * area:4 * area + 3])
    h = split(d.cdr(off))
    if not h or len(h) != 3:
        return None, None
    tab = h[1]
    return off, [msf(tab[4 * i:4 * i + 3]) for i in range(len(tab) // 4)]


def stage_index(d, area, stage):
    """(index offset, [(start, sectors)]) of a stage, or None when the area is
    not on this disc, the stage entry is empty or its index is not a stage
    index (unused table entries hold leftovers)."""
    ah, table = area_header(d, area)
    if ah is None or stage < 1 or stage >= len(table) or not table[stage]:
        return None
    idx = ah + table[stage]
    sec = split(d.cdr(idx))
    if not sec or len(sec) != 4:
        return None
    starts = [msf(sec[3][4 * i:4 * i + 3]) for i in range(len(sec[3]) // 4)]
    starts = [s for s in starts if s]
    files = []
    for i, s in enumerate(starts):
        nxt = next((t for t in starts[i + 1:] if t > s), None)
        if nxt is None:
            break
        files.append((s, nxt - s))
    return idx, files


def action_pack(d, idx, files):
    """(start, sectors, entries) of the stage's action pack, or None (a scene)."""
    for s, n in files[:4]:
        e = split(d.cdr(idx + s, n))
        if e and len(e) > 100 and tag(e[1]) == "VAB_CODE":
            return s, n, e
    return None


def stage_files(d, area, stage):
    r = stage_index(d, area, stage)
    if r is None:
        if area not in d.areas:
            sys.exit(f"area {area} is not on this disc (disc 1 holds areas 0..6, disc 2 holds 7..12)")
        sys.exit(f"area {area} has no stage {stage}")
    return r


def describe(b):
    parts = split(b)
    if parts is None:
        return tag(b) or "raw"
    tags = [tag(p) for p in parts]
    if any(tags):
        return " + ".join(f"{t or 'data'} {len(p):#x}" for t, p in zip(tags, parts))
    return f"container, {len(parts)} entries"


def cmd_areas(d):
    t = d.cdr(0)
    for a in range(13):
        ah, table = area_header(d, a)
        if ah is None:
            print(f"area {a:2d}: not on this disc")
            continue
        stages = [s for s in range(1, len(table)) if table[s]]
        print(f"area {a:2d}: header +{ah}, stages {stages[0]}..{stages[-1]} ({len(stages)})" if stages else f"area {a:2d}: header +{ah}, no stages")


def cmd_stage(d, area, stage):
    idx, files = stage_files(d, area, stage)
    print(f"area {area} stage {stage}: index at DATA.CDR +{idx} (LBA {d.data_lba + idx}), {len(files)} files")
    for s, n in files:
        print(f"  +{s:5d} {n:4d} sect  {describe(d.cdr(idx + s, n))}")


def cmd_kinds(d, as_c):
    """Action stages (an action pack) and scenes per area of this disc; with
    --c, the bitmask table the in-game menu's Area row is built from."""
    masks = {}
    for a in d.areas:
        ah, table = area_header(d, a)
        acts, scenes = [], []
        for s in range(1, len(table) if table else 0):
            r = stage_index(d, a, s)
            if r is None:
                continue
            (acts if action_pack(d, *r) else scenes).append(s)
        masks[a] = sum(1 << s for s in acts)
        if not as_c:
            print(f"area {a:2d}: action {acts}\n          scene  {scenes}")
    if as_c:
        for a, m in masks.items():
            print(f"    0x{m:016X}ull,   /* area {a:2d} */")


def cmd_hero(d, area, stage):
    idx, files = stage_files(d, area, stage)
    pack = action_pack(d, idx, files)
    if not pack:
        sys.exit("no action pack (container > 100 entries with a sound bank in entry 1) among the first files")
    s, n, e = pack
    h = lambda b: hashlib.md5(b).hexdigest()[:8]
    print(f"area {area} stage {stage}: action pack at index +{s} ({n} sectors, {len(e)} entries, loaded to 0x800C5E98)")
    groups = (("render nodes / stage header", [0]), ("hero sounds", [1]), ("skeleton / animations", [2]),
              ("body-part packet templates", range(3, 17)), ("body-part vertices", range(17, 31)))
    for name, ids in groups:
        ids = list(ids)
        size = sum(len(e[i]) for i in ids)
        print(f"  e{ids[0]}{'..e' + str(ids[-1]) if len(ids) > 1 else '':5s} {size:7d} B  {h(b''.join(e[i] for i in ids))}  {name}")
    first = split(d.cdr(idx + files[0][0], files[0][1]))
    for t in tims(first[2]) if first and len(first) > 2 else []:
        if t["img"][:2] == (384, 0):
            print(f"  hero texture page: {t['bpp']}bpp image {t['img']} clut {t['clut']} {len(t['data'])} B {h(t['data'])} (first file, TIM section)")


def main(argv):
    args = list(argv)
    cue = DEFAULT_CUE
    if "--disc" in args:
        i = args.index("--disc")
        cue = Path(args[i + 1])
        del args[i:i + 2]
    if not args or args[0] not in ("areas", "kinds", "stage", "hero"):
        sys.exit(__doc__)
    d = Disc(cue)
    if args[0] == "areas":
        cmd_areas(d)
    elif args[0] == "kinds":
        cmd_kinds(d, "--c" in args)
    elif len(args) < 3:
        sys.exit("stage|hero need AREA STAGE")
    elif args[0] == "stage":
        cmd_stage(d, int(args[1]), int(args[2]))
    else:
        cmd_hero(d, int(args[1]), int(args[2]))


if __name__ == "__main__":
    main(sys.argv[1:])
