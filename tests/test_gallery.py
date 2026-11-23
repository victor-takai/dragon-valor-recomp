#!/usr/bin/env python3
"""Check the cheats package's Unlock gallery option on a debug build: the
title menu offers its fourth item, the illustration gallery, and every
picture 1..95 can be reached.

  python tests/test_gallery.py [--off] [--build build-debug] [--port N] [--shots DIR]

The test writes its own <build>/mods/state.toml (cheats enabled,
unlock_gallery on, or off with --off: the control run), launches the build
with no save state (muted, hidden window), presses Start until the title's
main menu is up (mode 3, OPENNING.BIN: g_TitleWork.screen 0, state 5) and
checks g_TitleHasClearData (0x80157248), the item count the menu reads every
pass: 1 with the option, 0 without it (a fresh boot has no ending flag).
With the option it then moves the cursor to the fourth item (Down x3),
presses Start, waits for the gallery (screen 5, state 3) and presses R1 95
times, each time waiting until the next picture is loaded and shown
(state 3 again): g_GalleryImage (0x801585E8) must visit every picture
1..95, and the guest must keep running. Pictures of the menu and of four
illustrations go to --shots (default <build>/test_shots). The player's
settings.toml, dragonvalor_settings.toml and mods/state.toml are backed up
and restored. Exit code 0 on PASS.
"""
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from dvdbg import rpc, port_from_args  # noqa: E402

DV = str(ROOT / "tools" / "dvdbg.py")
MODE = 0x8008634C                    # g_GameFlow.mode: 3 = title (OPENNING.BIN)
HAS_CLEAR = 0x80157248               # g_TitleHasClearData
SCREEN, STATE = 0x80156F32, 0x80156F33   # g_TitleWork.screen / .state
IMAGE = 0x801585E8                   # g_GalleryImage
START, DOWN, R1 = 0x0008, 0x0040, 0x0800
STATE_TOML = """format_version = 2

[[package]]
id = "dragonvalor.cheats"
version = "1.4.0"

[[feature]]
package_id = "dragonvalor.cheats"
id = "cheats"
enabled = true
[feature.values]
infinite_hp = "false"
infinite_mp = "false"
unlock_gallery = "{on}"
"""


def main(argv):
    args = list(argv)
    port = port_from_args(args)
    build, shots = "build-debug", None
    for flag in ("--build", "--shots"):
        if flag in args:
            i = args.index(flag)
            if flag == "--build":
                build = args[i + 1]
            else:
                shots = Path(args[i + 1]).resolve()
            del args[i:i + 2]
    on = "--off" not in args
    bdir = ROOT / build
    shots = shots or bdir / "test_shots"
    shots.mkdir(parents=True, exist_ok=True)

    def cli(*a, timeout=120):
        r = subprocess.run([sys.executable, DV, *a, "--port", str(port)], capture_output=True, text=True, timeout=timeout)
        return (r.stdout + r.stderr).strip()

    state = bdir / "mods" / "state.toml"
    kept = {f: f.read_bytes() for f in (bdir / "settings.toml", bdir / "dragonvalor_settings.toml", state)
            if f.is_file()}
    results = []
    try:
        state.parent.mkdir(parents=True, exist_ok=True)
        state.write_text(STATE_TOML.replace("{on}", "true" if on else "false"), encoding="utf-8")
        print(f"Unlock gallery {'on' if on else 'off'}")
        print("  launch:", cli("launch", "--build", str(bdir)).splitlines()[-1])
        try:
            results = run(port, on, shots)
        finally:
            cli("kill")
            time.sleep(2)
    finally:
        for f, b in kept.items():
            f.write_bytes(b)
    ok = all(results) and results
    print("PASS" if ok else "FAIL", f"({sum(results)}/{len(results)})")
    return 0 if ok else 1


def run(port, on, shots):
    results = []

    def rd(a, n=1):
        return int.from_bytes(bytes.fromhex(rpc(port, "read_ram", addr=f"{a:08X}", len=n)["hex"]), "little")

    def press(mask, frames=4):
        rpc(port, "press", buttons=0xFFFF & ~mask, frames=frames)   # PS1 pad words are active-low
        time.sleep(0.3)

    def check(label, good):
        results.append(bool(good))
        print(f"  {'ok  ' if good else 'FAIL'} {label}")

    def frame():
        return rpc(port, "frame").get("frame")

    def shot(name):
        out = shots / name
        rpc(port, "present_shot", path=str(out))
        time.sleep(1)
        print(f"  picture: {out}")

    # Boot: the Namco logo, the intro movie, the title; Start skips what it can.
    t0 = time.time()
    while time.time() - t0 < 120 and not (rd(MODE) == 3 and rd(SCREEN) == 0 and rd(STATE) == 5):
        press(START)
        time.sleep(0.7)
    check(f"the title's main menu is up after {time.time() - t0:.0f} s (mode {rd(MODE)}, "
          f"screen {rd(SCREEN)}, state {rd(STATE)})", rd(MODE) == 3 and rd(SCREEN) == 0 and rd(STATE) == 5)
    time.sleep(1)
    check(f"the menu has {'4' if on else '3'} items (g_TitleHasClearData {rd(HAS_CLEAR)})",
          rd(HAS_CLEAR) == (1 if on else 0))
    shot(f"gallery_menu_{'on' if on else 'off'}.png")
    if not on:
        return results
    press(DOWN); press(DOWN); press(DOWN)
    press(START)
    t0 = time.time()
    while time.time() - t0 < 20 and not (rd(SCREEN) == 5 and rd(STATE) == 3):
        time.sleep(0.25)
    check(f"Start on the fourth item opens the gallery (screen {rd(SCREEN)}, state {rd(STATE)})",
          rd(SCREEN) == 5 and rd(STATE) == 3)
    time.sleep(0.5)
    shot("gallery_0.png")
    seen, stuck = set(), None
    for n in range(95):
        press(R1)
        t1 = time.time()
        while time.time() - t1 < 15 and not (rd(STATE) == 3 and rd(IMAGE, 4) not in seen | {0}):
            time.sleep(0.1)
        image = rd(IMAGE, 4)
        if rd(STATE) != 3 or image in seen:
            stuck = (n, image, rd(STATE))
            break
        seen.add(image)
        if image in (9, 31, 50, 95):
            time.sleep(0.4)
            shot(f"gallery_{image}.png")
    check(f"R1 visits every picture 1..95 ({len(seen)} seen"
          f"{'' if stuck is None else f'; stopped at press {stuck[0]}, picture {stuck[1]}, state {stuck[2]}'})",
          seen == set(range(1, 96)))
    f0 = frame(); time.sleep(0.8)
    check("the game still runs", frame() != f0)
    return results


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
