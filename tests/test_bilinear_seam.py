#!/usr/bin/env python3
"""Check the bilinear page-edge seam fix (patches/psxrecomp/0001-…) on the
debug build, OpenGL.

  python tests/test_bilinear_seam.py
  python tests/test_bilinear_seam.py --build build-debug --port 4370 --state 9

Launches the game muted from test save state 10 (an action stage; 9 also
works, never the player's slot 0) with the bilinear texture filter
(PSX_TEXTURE_FILTER=1 for this run only), starts screen transition 2 (the
common scene change, scroll-flicker) from RAM and, a second later, compares
VRAM column 256 of both frame buffers with column 255. The effect copies the
frame in two halves (texture pages x 0 and 256) and feeds each frame back
into the next; before the fix the bilinear neighbour of the right half's
first column wrapped to u 255 and was clamped to the prim's right edge, and
that column grew into a coloured stripe two thirds across the screen
(20 colour levels away from its neighbour; with the fix 2). The same seam was
a dark line through the title screen.

The player's state.toml, settings.toml and dragonvalor_settings.toml are
backed up and restored on exit, even on failure. Exit code 0 on PASS. The
debug server (PSX_DEBUG_TOOLS=ON) is required.
"""
import os
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from dvdbg import rpc, port_from_args  # noqa: E402

DV = str(ROOT / "tools" / "dvdbg.py")
MODE_BYTE = 0x8008634C
TRANSITION = 0x800A2EF8     # g_Transition: s32 state, s32 effect
ROWS = (40, 80, 120, 160, 200)


def levels(px):
    return (px & 31, (px >> 5) & 31, (px >> 10) & 31)


def main(argv):
    args = list(argv)
    port = port_from_args(args)
    build = "build-debug"
    if "--build" in args:
        i = args.index("--build")
        build = args[i + 1]
        del args[i:i + 2]
    slot = "10"
    if "--state" in args:
        i = args.index("--state")
        slot = args[i + 1]
        del args[i:i + 2]
    if slot == "0":
        sys.exit("slot 0 is the player's quick save; use the test states 10 or 9")

    state = ROOT / build / "mods" / "state.toml"
    guarded = [state, ROOT / build / "settings.toml", ROOT / build / "dragonvalor_settings.toml"]
    saved = {p: p.read_bytes() for p in guarded if p.is_file()}
    state.parent.mkdir(parents=True, exist_ok=True)
    state.write_text("format_version = 2\n", encoding="utf-8")
    env = dict(os.environ, PSX_TEXTURE_FILTER="1")

    def cli(*a, timeout=120):
        r = subprocess.run([sys.executable, DV, *a, "--port", str(port)], capture_output=True, text=True,
                           timeout=timeout, env=env)
        return (r.stdout + r.stderr).strip()

    def write_word(addr, value):
        for k in range(4):
            rpc(port, "write_ram", addr=f"{addr + k:08X}", val=(value >> (8 * k)) & 0xFF)

    results = []

    def check(label, ok, detail=""):
        results.append(ok)
        print(f"  {'ok  ' if ok else 'FAIL'} {label}{(': ' + detail) if detail else ''}")

    try:
        print("launch:", cli("launch", "--state", slot, "--build", str(ROOT / build)))
        time.sleep(6)
        b = bytes.fromhex(rpc(port, "read_ram", addr=f"{MODE_BYTE:08X}", len=1)["hex"])
        check(f"save state {slot} is in the action stage (mode 8)", b[0] == 8, f"mode {b[0]}")
        flt = rpc(port, "video_info").get("texture_filter")
        check("bilinear texture filter", flt == 1, f"texture_filter {flt}")
        write_word(TRANSITION + 4, 2)
        write_word(TRANSITION, 1)
        time.sleep(1.0)
        worst = 0
        for buf in (0, 256):
            for y in ROWS:
                h = rpc(port, "vram_peek", x=255, y=buf + y, w=2, h=1)["hex"]
                a, c = levels(int(h[0:4], 16)), levels(int(h[4:8], 16))
                worst = max(worst, max(abs(p - q) for p, q in zip(a, c)))
        check("column 256 follows column 255 during the transition", worst <= 4, f"largest step {worst} levels")
        print("health:", cli("health").replace("\n", "; "))
    finally:
        print("kill:", cli("kill"))
        time.sleep(1)
        for p in guarded:
            if p in saved:
                p.write_bytes(saved[p])
            elif p == state:
                state.unlink(missing_ok=True)
        print("player files restored:", all(p.read_bytes() == b for p, b in saved.items()))
    ok = all(results)
    print("RESULT:", "PASS" if ok else "FAIL", f"({sum(results)}/{len(results)})")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
