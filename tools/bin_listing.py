#!/usr/bin/env python3
"""Pull the BIN/*.BIN mode modules out of disc 1 and write a listing of each.

The runtime only captures the overlay pages it has executed
(`psxrecomp/tools/overlay_xref.py` reads those), so code that was never run
(most of MAP.BIN, the field module) has no listing anywhere. This reads the
files straight from the Track 1 image through psxrecomp's ISO 9660 reader
and disassembles them at the loader slot 0x800BBEF0 (docs/AOT.md) with
tools/mipsdis.py, in the same `pc  word  text` format as the listings
tools/ramscan writes from RAM dumps.

Usage (any cwd):
  python tools/bin_listing.py OUT_DIR                 # every BIN/*.BIN
  python tools/bin_listing.py OUT_DIR MAP ACTION      # some of them
  python tools/bin_listing.py OUT_DIR --disc "X:/.../Dragon Valor (USA) (Disc 1) (Track 1).bin"

Without --disc the path comes from <build>/settings.toml (build-debug first,
then build-release), the file the launcher writes when the disc is picked.
Writes OUT_DIR/<name>.bin, <name>_ram.json ({addr, len, hex}, the shape
tools/ramscan/dump_ram.py produces) and <name>_full.lst.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LOAD_ADDR = 0x800BBEF0

sys.path.insert(0, str(ROOT / "psxrecomp" / "tools"))
import extract_overlays as eo  # noqa: E402


def disc_from_settings() -> Path | None:
    for build in ("build-debug", "build-release"):
        settings = ROOT / build / "settings.toml"
        if not settings.is_file():
            continue
        m = re.search(r'^path\s*=\s*"([^"]+\.cue)"', settings.read_text(encoding="utf-8"), re.M)
        if not m:
            continue
        cue = Path(m.group(1))
        if cue.is_file():
            for line in cue.read_text(encoding="utf-8", errors="replace").splitlines():
                f = re.match(r'\s*FILE\s+"([^"]+)"', line)
                if f:
                    track = cue.parent / f.group(1)
                    if track.is_file():
                        return track
    return None


def load_disassembler():
    src = (ROOT / "tools" / "mipsdis.py").read_text(encoding="utf-8")
    ns: dict = {}
    exec(src.split("\ndef collect")[0], ns)  # the decoder only, not the CLI
    return ns["dis"]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out", help="output directory")
    ap.add_argument("names", nargs="*", help="module names without extension (MAP, ACTION, ...); default all")
    ap.add_argument("--disc", help="Track 1 .bin (or .cue) of disc 1; default from <build>/settings.toml")
    args = ap.parse_args()

    disc = Path(args.disc) if args.disc else disc_from_settings()
    if disc is None or not disc.is_file():
        sys.exit("disc image not found: pass --disc or pick the disc in the launcher once")
    raw = None
    if disc.suffix.lower() == ".cue":
        bin_path, raw = eo.parse_cue(str(disc))      # (track 1 path, MODE2/2352?)
        disc = Path(bin_path)
    if raw is None:
        raw = disc.stat().st_size % 2352 == 0
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    wanted = {n.upper() for n in args.names}
    dis = load_disassembler()

    reader = eo.DiscReader(str(disc), raw=raw)
    try:
        written = 0
        for path, lba, size in eo.enumerate_files(reader):
            upper = str(path).upper().replace("\\", "/").lstrip("/")
            if not upper.startswith("BIN/") or not upper.endswith(".BIN"):
                continue
            name = upper[4:-4].split(";")[0]
            if wanted and name not in wanted:
                continue
            data = reader.read_file_bytes(lba, size)
            stem = name.lower()
            (out / f"{stem}.bin").write_bytes(data)
            (out / f"{stem}_ram.json").write_text(
                json.dumps({"addr": f"{LOAD_ADDR:08x}", "len": len(data), "hex": data.hex()}))
            with (out / f"{stem}_full.lst").open("w", encoding="utf-8") as f:
                for off in range(0, len(data) - 3, 4):
                    word = int.from_bytes(data[off:off + 4], "little")
                    pc = LOAD_ADDR + off
                    f.write("%08x  %08x  %s\n" % (pc, word, dis(pc, word)))
            print(f"BIN/{name}.BIN: lba {lba}, {size} bytes -> {stem}.bin, {stem}_ram.json, {stem}_full.lst")
            written += 1
    finally:
        reader.close()
    if not written:
        sys.exit("no BIN/*.BIN matched")
    return 0


if __name__ == "__main__":
    sys.exit(main())
