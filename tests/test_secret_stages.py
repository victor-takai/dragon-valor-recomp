#!/usr/bin/env python3
"""Check the in-game menu's Warp > "Cut prologue scene" row on a debug build:
the cut scene 0/5 is reached from test state 10 and shows in English.

  python tests/test_secret_stages.py [--build build-debug] [--port N] [--shots DIR]

The test launches the build (muted, hidden window) from test state 10, opens
the menu (Escape), enters the Warp section, presses Enter on Cut prologue
scene and confirms the dialog (Enter on its focused Go button). Nothing else
is pressed: the trip is the game's own ModeTransition, called by
dragonvalor_cheats.c at the first quiet frame (kind 0). Checks:
  1. the menu closes and the guest runs again;
  2. g_StageSelect (0x800A2F18 / 0x800A2F1A) becomes 0/5, the load ends
     (areaPhase / stagePhase 0x800A2D14 / 0x800A2D18 back to 0) and the game
     is in mode 8 (g_GameFlow.mode 0x8008634C);
  3. the guest still runs 3 s later (the frame counter moves);
  4. the room's message table (g_EventVm.messages) points at the English
     lines dragonvalor_cheats.c wrote ({pos 88,188}"..." first);
  5. the trip's argument bytes (g_CdUnused8768) are zero again;
  6. test state 9 still loads afterwards (the trip leaves the save-state
     layout alone: no expansion memory).
A picture of the scene is written to --shots (default <build>/test_shots).
The player's settings.toml, dragonvalor_settings.toml and mods/state.toml
are backed up and restored. Exit code 0 on PASS.
"""
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from dvdbg import rpc, port_from_args  # noqa: E402

DV = str(ROOT / "tools" / "dvdbg.py")
HPMAX = 0x8008A5B0
MODE, AREA, STAGE = 0x8008634C, 0x800A2F18, 0x800A2F1A
AREA_PHASE, STAGE_PHASE = 0x800A2D14, 0x800A2D18
EVENT_MESSAGES = 0x8009FA18          # g_EventVm.messages
TRIP_ARGS = 0x80078768               # g_CdUnused8768, the trip's argument bytes
ENGLISH_MESSAGE0 = [0x90B2, 0x1180, 0x1180, 0x1180]   # {pos 88,188}"..." of "...Elena?!"
SECTIONS = ["Game", "Hero", "Cheats", "Magic", "Items", "Warp", "Video", "Options"]
WARP_ROWS = ["Area", "Stage", "Go now", "Cut prologue scene"]
TARGET = (0, 5)


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
    bdir = ROOT / build
    shots = shots or bdir / "test_shots"
    shots.mkdir(parents=True, exist_ok=True)

    def cli(*a, timeout=120):
        r = subprocess.run([sys.executable, DV, *a, "--port", str(port)], capture_output=True, text=True, timeout=timeout)
        return (r.stdout + r.stderr).strip()

    kept = {f: f.read_bytes() for f in (bdir / "settings.toml", bdir / "dragonvalor_settings.toml",
                                        bdir / "mods" / "state.toml") if f.is_file()}
    results = []
    try:
        print(f"cut prologue scene ({TARGET[0]}/{TARGET[1]})")
        print("  launch:", cli("launch", "--state", "10", "--build", str(bdir)).splitlines()[-1])
        time.sleep(9)
        try:
            results = run(port, shots)
        finally:
            cli("kill")
            time.sleep(2)
    finally:
        for f, b in kept.items():
            f.write_bytes(b)
    ok = all(results) and results
    print("PASS" if ok else "FAIL", f"({sum(results)}/{len(results)})")
    return 0 if ok else 1


def run(port, shots):
    results = []
    area, stage = TARGET

    def frame():
        return rpc(port, "frame").get("frame")

    def rd(a, n=2):
        return int.from_bytes(bytes.fromhex(rpc(port, "read_ram", addr=f"{a:08X}", len=n)["hex"]), "little")

    def key(k, n=1):
        for _ in range(n):
            r = rpc(port, "host_key", name=k)
            if not r.get("ok"):
                sys.exit(f"host_key {k} failed: {r}")
            time.sleep(0.3)

    def check(label, good):
        results.append(bool(good))
        print(f"  {'ok  ' if good else 'FAIL'} {label}")

    def runs():
        f0 = frame(); time.sleep(0.8); return frame() != f0

    if rd(HPMAX) == 0:
        sys.exit("no hero loaded (HPmax = 0): state 10 did not load")
    start = (rd(AREA), rd(STAGE))
    key("Escape"); time.sleep(0.5)
    key("Down", SECTIONS.index("Warp")); key("Return")
    key("Down", WARP_ROWS.index("Cut prologue scene")); key("Return"); time.sleep(0.4)   # the dialog
    key("Return"); time.sleep(1.0)                                                      # Go
    check(f"the menu closes and the game runs (from {start[0]}/{start[1]})", runs())
    there, t0 = False, time.time()
    while time.time() - t0 < 40:
        if (rd(AREA), rd(STAGE)) == (area, stage) and rd(AREA_PHASE, 4) == 0 and rd(STAGE_PHASE, 4) == 0 \
                and rd(MODE, 1) == 8:
            there = True
            break
        time.sleep(0.5)
    check(f"{area}/{stage} loaded, mode 8 (after {time.time() - t0:.0f} s; now "
          f"{rd(AREA)}/{rd(STAGE)} mode {rd(MODE, 1)})", there)
    time.sleep(3)
    check("the game still runs", runs())
    out = shots / f"secret_{area}-{stage}.png"
    rpc(port, "present_shot", path=str(out))
    time.sleep(1)
    print(f"  picture: {out}")
    table = rd(EVENT_MESSAGES, 4)
    m0 = rd(table, 4) if table >> 21 == 0x80000000 >> 21 else 0
    words = [rd(m0 + 2 * i) for i in range(4)] if m0 else []
    check(f"the scene's messages are the English ones (message 0 at {m0:08X}: "
          f"{' '.join(f'{w:04X}' for w in words)})", words == ENGLISH_MESSAGE0)
    check("args scratch g_CdUnused8768 is zero again", rd(TRIP_ARGS, 4) == 0)
    # A state saved before the trip must still load (no layout change).
    r = subprocess.run([sys.executable, DV, "state", "load", "9", "--port", str(port)],
                       capture_output=True, text=True, timeout=60)
    time.sleep(2)
    check(f"test state 9 loads after the trip ({(r.stdout + r.stderr).strip().splitlines()[-1][:70]})",
          r.returncode == 0 and rd(HPMAX) != 0 and runs())
    return results


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
