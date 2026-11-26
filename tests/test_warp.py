#!/usr/bin/env python3
"""Check the in-game menu's Warp > Go now on a debug build with the cheats
package on: the target stage loads with no GAME OVER.

  python tests/test_warp.py [--build build-debug] [--port N] [--shots DIR] [--targets A/S,A/S,...]

Since 2026-11-25 Go now is a trip (mods/plugins/dragonvalor_cheats.c,
dv_trip_warp): the game's own ModeTransition, kind 0, at the first quiet
frame, as a stage exit would, with HP and MP refilled as the CONTINUE did.
Without the cheats package the old GAME OVER > CONTINUE trip is kept.
tests/test_area_row.py covers that one; it uses slot 0, so it is not run here.

The test launches the build (muted, hidden window) from test state 10. For
each target it opens the menu (Escape), enters Warp, steps the Area and
Stage rows with Right / Left until they read the target, presses Enter on
Go now and confirms the dialog. Then it checks:
  1. the game never shows GAME OVER (g_GameFlow.mode 0x8008634C never 9);
  2. the target loads: g_StageSelect 0x800A2F18 / 0x800A2F1A, the load
     phases 0x800A2D14 / 0x800A2D18 back to 0, mode 8;
  3. HP and MP are full (0x8008A5AE / 0x8008A5B2 = their maxima);
  4. the checkpoint (the stage-start record 0x8007D9C6) is the target, so
     a later CONTINUE starts there;
  5. Val (0x800A2DD0) is unchanged;
  6. the hero walks (Right held: g_Objects[0].pos changes) and attacks
     (Square: g_Objects[0].anim changes), and the guest still runs.
A picture of each stage goes to --shots (default <build>/test_shots). The
player's settings.toml and dragonvalor_settings.toml are backed up and
restored. Only the runtime this test launched is stopped. Exit code 0 on PASS.
"""
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from dvdbg import BUTTONS, rpc, port_from_args  # noqa: E402

DV = str(ROOT / "tools" / "dvdbg.py")
MODE, AREA, STAGE, RECORD = 0x8008634C, 0x800A2F18, 0x800A2F1A, 0x8007D9C6
AREA_PHASE, STAGE_PHASE = 0x800A2D14, 0x800A2D18
HP, HPMAX, MP, MPMAX, VAL = 0x8008A5AE, 0x8008A5B0, 0x8008A5B2, 0x8008A5B4, 0x800A2DD0
HERO_POS, HERO_ANIM = 0x8008A500, 0x8008A57E      # g_Objects[0].pos[0], .anim
EVENT_RUNNING, TRANSITION_STATE = 0x8009F9F8, 0x800A2EF8   # g_EventVm.running, g_Transition.state
SECTIONS = ["Game", "Hero", "Cheats", "Magic", "Items", "Warp", "Video", "Options"]
WARP_ROWS = ["Area", "Stage", "Go now", "Cut prologue scene"]


def main(argv):
    args = list(argv)
    port = port_from_args(args)
    build, shots, targets = "build-debug", None, None
    for flag in ("--build", "--shots", "--targets"):
        if flag in args:
            i = args.index(flag)
            v = args[i + 1]
            del args[i:i + 2]
            if flag == "--build":
                build = v
            elif flag == "--shots":
                shots = Path(v).resolve()
            else:
                targets = [tuple(int(x) for x in t.split("/")) for t in v.split(",")]
    bdir = ROOT / build
    shots = shots or bdir / "test_shots"
    shots.mkdir(parents=True, exist_ok=True)

    def cli(*a, timeout=120):
        r = subprocess.run([sys.executable, DV, *a, "--port", str(port)], capture_output=True, text=True, timeout=timeout)
        return (r.stdout + r.stderr).strip()

    kept = {f: f.read_bytes() for f in (bdir / "settings.toml", bdir / "dragonvalor_settings.toml") if f.is_file()}
    results = []
    try:
        print("  launch:", cli("launch", "--state", "10", "--build", str(bdir)).splitlines()[-1])
        time.sleep(9)
        try:
            results = run(port, shots, targets)
        finally:
            print("  kill:", cli("kill"))
            time.sleep(2)
    finally:
        for f, b in kept.items():
            f.write_bytes(b)
    ok = all(results) and results
    print("PASS" if ok else "FAIL", f"({sum(results)}/{len(results)})")
    return 0 if ok else 1


def run(port, shots, targets):
    results = []

    def rd(a, n=2):
        return int.from_bytes(bytes.fromhex(rpc(port, "read_ram", addr=f"{a:08X}", len=n)["hex"]), "little")

    def rds(a, n=4):
        v = rd(a, n)
        return v - (1 << (8 * n)) if v >> (8 * n - 1) else v

    def frame():
        return rpc(port, "frame").get("frame")

    def key(k, n=1):
        for _ in range(n):
            r = rpc(port, "host_key", name=k)
            if not r.get("ok"):
                sys.exit(f"host_key {k} failed: {r}")
            time.sleep(0.3)

    def press(name, frames):
        rpc(port, "press", buttons=0xFFFF & ~BUTTONS[name], frames=frames)
        time.sleep(frames / 60 + 0.4)
        rpc(port, "clear_input")

    def check(label, good):
        results.append(bool(good))
        print(f"  {'ok  ' if good else 'FAIL'} {label}")

    def runs():
        f0 = frame(); time.sleep(0.8); return frame() != f0

    def step_row(addr, want, cap):
        for _ in range(cap):
            v = rd(addr)
            if v == want:
                return True
            key("Right" if want > v else "Left")
        return rd(addr) == want

    if rd(HPMAX) == 0:
        sys.exit("no hero loaded (HPmax = 0): state 10 did not load")
    here = (rd(AREA), rd(STAGE))
    print(f"  state 10: area {here[0]}, stage {here[1]}")
    if targets is None:
        # town -> an action stage of the area, action -> action, a boss stage,
        # another area (disc 1: areas 0..6; the rows never cross discs)
        targets = [(1, 3), (1, 5), (1, 20), (3, 3)] if here[0] == 1 else [(here[0], 1)]
    for area, stage in targets:
        print(f"warp {rd(AREA)}/{rd(STAGE)} -> {area}/{stage}")
        hp_before_val = rd(VAL)
        key("Escape"); time.sleep(0.5)
        key("Down", SECTIONS.index("Warp")); key("Return")
        set_area = step_row(AREA, area, 14)
        key("Down")
        set_stage = step_row(RECORD, stage, 60)
        if not (set_area and set_stage):
            check(f"the rows reach {area}/{stage} (they read {rd(AREA)}/{rd(RECORD)}: not an action stage?)", False)
            key("Escape", 2); time.sleep(0.5)
            continue
        key("Down"); key("Return"); time.sleep(0.4)   # Go now: the dialog
        key("Return"); time.sleep(0.3)                # its Warp button (focused)
        seen9, there, t0 = False, False, time.time()
        while time.time() - t0 < 60:
            m = rd(MODE, 1)
            seen9 |= m == 9
            if (rd(AREA), rd(STAGE)) == (area, stage) and m == 8 and rd(AREA_PHASE, 4) == 0 \
                    and rd(STAGE_PHASE, 4) == 0 and time.time() - t0 > 1.0:
                there = True
                break
            time.sleep(0.1)
        took = time.time() - t0
        check(f"{area}/{stage} loaded in {took:.1f} s (now {rd(AREA)}/{rd(STAGE)}, mode {rd(MODE, 1)})", there)
        check("no GAME OVER on the way (mode 9 never seen)", not seen9)
        # Opening scenes and the boss's words run as event scripts: Cross
        # moves them on, as a player would; then the fade-in ends.
        t0, still = time.time(), 0
        while time.time() - t0 < 60 and still < 4:
            if rd(EVENT_RUNNING, 4):
                still = 0
                press("cross", 6)
            elif rd(TRANSITION_STATE, 4):
                still = 0
            else:
                still += 1
            time.sleep(0.5)
        check(f"playable after {time.time() - t0:.0f} s (no event script, no transition)", still >= 4)
        check(f"HP {rd(HP)}/{rd(HPMAX)} and MP {rd(MP)}/{rd(MPMAX)} full", rd(HP) == rd(HPMAX) and rd(MP) == rd(MPMAX))
        check(f"checkpoint stage {rd(RECORD)} is the target", rd(RECORD) == stage)
        check(f"Val {rd(VAL)} unchanged ({hp_before_val})", rd(VAL) == hp_before_val)
        x0 = rds(HERO_POS)
        press("right", 40)
        x1 = rds(HERO_POS)
        check(f"the hero walks (x {x0} -> {x1})", x1 != x0)
        time.sleep(1.5)                       # the run's stop motion ends first
        a0 = rd(HERO_ANIM, 1)
        rpc(port, "press", buttons=0xFFFF & ~BUTTONS["square"], frames=6)
        seen = set()
        for _ in range(30):
            seen.add(rd(HERO_ANIM, 1))
            time.sleep(0.04)
        rpc(port, "clear_input")
        check(f"the hero attacks (anim {a0} -> {sorted(seen)})", seen - {a0})
        check("the game still runs", runs())
        out = shots / f"warp_{area}-{stage}.png"
        rpc(port, "present_shot", path=str(out))
        time.sleep(1.0)
        print(f"  picture: {out}")
    return results


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
