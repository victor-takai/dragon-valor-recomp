#!/usr/bin/env python3
"""Check the in-game menu's "Area (next Continue)" and "Go now" rows on a
debug build.

  python tests/test_area_row.py [--reloads] [--build build-debug] [--port N]

The Area row writes the area the next GAME OVER > CONTINUE reloads from
(0x800A2F18); the stage number is kept. It only steps to areas of the same
disc where that stage number is an action stage (kAreaActionStages in
mods/plugins/dragonvalor_menu.cpp, from `tools/datacdr.py kinds --c`): a
scene-type or empty stage left the screen black until the runtime's
watchdog closed the game. "Go now" writes HP 0, holds Infinite HP off and
presses Cross on the CONTINUE prompt through a pad transform, so the reload
needs no input.

The test writes its own <build>/mods/state.toml (the player's file is backed
up and restored, as are settings.toml and dragonvalor_settings.toml, which a
stray key could change) and launches the build itself twice, muted and with
a hidden window. No button is pressed through the debug server during a
Go now trip (a debug-server input override would bypass the pad transform).

  run 1, no cheats:
  1. slot 9 (Kodel, area 3 / stage 1): Right on the Area row -> area 4,
     Left -> back to 3.
  2. slot 0 (Clovis, area 0 / stage 2): Right leaves the area at 0 (stage 2
     is a scene in areas 1..6 and the row refuses).
  3. --reloads: from slot 9, every allowed area for stage 1 (0..6) is
     written and reloaded through GAME OVER > CONTINUE (Cross pressed by the
     test); each must come back to the action stage, running 8 s later.
  4. slot 9: Area -> 0, then Go now: the stage reloads as area 0 / stage 1
     with no input.
  run 2, Infinite HP on:
  5. slot 9: Area -> 2, Go now: area 2 / stage 1 is reached, and Infinite HP
     works again afterwards (HP written to 5 is refilled).

Exit code 0 on PASS.
"""
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from dvdbg import rpc, port_from_args  # noqa: E402

DV = str(ROOT / "tools" / "dvdbg.py")
AREA, STAGE, HP, HPMAX, MODE, VAL = 0x800A2F18, 0x800A2F1A, 0x8008A5AE, 0x8008A5B0, 0x8008634C, 0x800A2DD0
SECTIONS = ["Display", "Graphics", "Audio", "System", "Cheats", "Game"]
AREA_ROW = 9   # Cheats: Infinite HP, Infinite MP, Gem every, Scroll every, Val, Max HP, Max MP, STR, DEF, Area, Go now
STATE_PLAIN = "format_version = 2\n"
STATE_INFINITE_HP = """format_version = 2

[[package]]
id = "dragonvalor.cheats"
version = "1.3.0"

[[feature]]
package_id = "dragonvalor.cheats"
id = "cheats"
enabled = true
[feature.values]
infinite_hp = "true"
"""


def main(argv):
    args = list(argv)
    port = port_from_args(args)
    build = "build-debug"
    if "--build" in args:
        i = args.index("--build")
        build = args[i + 1]
        del args[i:i + 2]
    reloads = "--reloads" in args
    bdir = ROOT / build
    state = bdir / "mods" / "state.toml"
    guarded = [state, bdir / "settings.toml", bdir / "dragonvalor_settings.toml"]
    saved = {p: p.read_bytes() for p in guarded if p.is_file()}
    results = []

    def check(label, ok, detail=""):
        results.append(ok)
        print(f"  {'ok  ' if ok else 'FAIL'} {label}{(': ' + detail) if detail else ''}")

    def cli(*a, timeout=180):
        r = subprocess.run([sys.executable, DV, *a, "--port", str(port)], capture_output=True, text=True, timeout=timeout)
        return (r.stdout + r.stderr).strip()

    def rd16(a):
        return int.from_bytes(bytes.fromhex(rpc(port, "read_ram", addr=f"{a:08X}", len=2)["hex"]), "little")

    def rd8(a):
        return bytes.fromhex(rpc(port, "read_ram", addr=f"{a:08X}", len=1)["hex"])[0]

    def wr16(a, v):
        rpc(port, "write_ram", addr=f"{a:08X}", val=v & 0xFF)
        rpc(port, "write_ram", addr=f"{a + 1:08X}", val=(v >> 8) & 0xFF)

    def key(name, n=1):
        for _ in range(n):
            if not rpc(port, "host_key", name=name).get("ok"):
                sys.exit(f"host_key {name} failed")
            time.sleep(0.3)

    def launch(state_text):
        state.parent.mkdir(parents=True, exist_ok=True)
        state.write_text(state_text, encoding="utf-8")
        print("launch:", cli("launch", "--build", str(bdir)).splitlines()[-1])
        time.sleep(4)

    def load(slot):
        out = cli("state", "load", str(slot)).splitlines()[-1]
        time.sleep(4)
        return out

    def in_cheats(keys):
        """Open the menu, go to the Area row, run keys, close."""
        key("Escape"); time.sleep(0.4)
        key("Down", SECTIONS.index("Cheats")); key("Return")
        key("Down", AREA_ROW)
        for k in keys:
            key(k)
        key("Escape", 2); time.sleep(0.4)

    def area_step(direction):
        in_cheats([direction])
        return rd16(AREA)

    def go_now(area_keys):
        """Area row keys, then Down to Go now and Enter (the menu closes itself)."""
        key("Escape"); time.sleep(0.4)
        key("Down", SECTIONS.index("Cheats")); key("Return")
        key("Down", AREA_ROW)
        for k in area_keys:
            key(k)
        key("Down"); key("Return")
        time.sleep(0.5)

    def wait_reload(target_area, limit=60):
        """No input: wait for the GAME OVER, the automatic Continue and the stage."""
        t0, seen9 = time.time(), False
        while time.time() - t0 < limit:
            m = rd8(MODE)
            seen9 |= m == 9
            if seen9 and m == 8 and rd16(AREA) == target_area:
                time.sleep(2)
                return rd8(MODE) == 8, round(time.time() - t0, 1)
            time.sleep(0.5)
        return False, round(time.time() - t0, 1)

    def reload_via_game_over():
        wr16(HP, 0)
        t0 = time.time()
        while time.time() - t0 < 15 and rd8(MODE) == 8:
            time.sleep(0.5)
        time.sleep(6)
        cli("press", "cross")
        t0 = time.time()
        while time.time() - t0 < 45 and not (rd8(MODE) == 8 and time.time() - t0 > 4):
            time.sleep(0.5)
        return rd8(MODE) == 8

    try:
        launch(STATE_PLAIN)
        print("1. slot 9:", load(9))
        a0, s0 = rd16(AREA), rd16(STAGE)
        check("slot 9 is area 3 / stage 1", (a0, s0) == (3, 1), f"{a0}/{s0}")
        check("Right steps to area 4 (stage 1 is an action stage there)", area_step("Right") == 4, str(rd16(AREA)))
        check("Left steps back to area 3", area_step("Left") == 3, str(rd16(AREA)))

        print("2. slot 0:", load(0))
        a0, s0 = rd16(AREA), rd16(STAGE)
        check("slot 0 is area 0 / stage 2", (a0, s0) == (0, 2), f"{a0}/{s0}")
        check("Right is refused (stage 2 is a scene in areas 1..6)", area_step("Right") == 0, str(rd16(AREA)))

        if reloads:
            print("3. reloads from slot 9 into every allowed area")
            for area in (0, 1, 2, 4, 5, 6, 3):
                load(9)
                wr16(AREA, area)
                ok = reload_via_game_over()
                a, s = rd16(AREA), rd16(STAGE)
                f0 = rpc(port, "frame").get("frame")
                time.sleep(8)
                running = rpc(port, "frame").get("frame", 0) > (f0 or 0) and rd8(MODE) == 8
                rpc(port, "screenshot", path=str(bdir / f"test_area_row_{area}.png"))
                check(f"area {area}: back in an action stage and running", ok and running, f"area/stage {a}/{s}, mode {rd8(MODE)}")

        print("4. Go now from slot 9 into area 0:", load(9))
        val0 = rd16(VAL)
        blk0 = bytes.fromhex(rpc(port, "read_ram", addr="800A2D20", len=0x200)["hex"])
        go_now(["Left", "Left", "Left"])
        ok, secs = wait_reload(0)
        check("Go now reloaded area 0 / stage 1 with no input", ok and rd16(STAGE) == 1, f"{rd16(AREA)}/{rd16(STAGE)} after {secs} s")
        check("Go now gives back the Val the Continue costs", rd16(VAL) == val0, f"{val0} -> {rd16(VAL)}")
        blk1 = bytes.fromhex(rpc(port, "read_ram", addr="800A2D20", len=0x200)["hex"])
        diffs = [hex(0x800A2D20 + i) for i in range(0x200) if blk0[i] != blk1[i]]
        print(f"     save block bytes that changed (play time and area expected): {diffs}")
        rpc(port, "screenshot", path=str(bdir / "test_area_row_go_now.png"))
        print("health:", cli("health").replace("\n", "; "))
        cli("kill")

        launch(STATE_INFINITE_HP)
        print("5. Go now with Infinite HP on, slot 9 into area 2:", load(9))
        go_now(["Left"])   # 3 -> 2
        ok, secs = wait_reload(2)
        check("Go now reloaded area 2 / stage 1 with Infinite HP on", ok and rd16(STAGE) == 1, f"{rd16(AREA)}/{rd16(STAGE)} after {secs} s")
        wr16(HP, 5); time.sleep(0.6)
        check("Infinite HP refills again after the trip", rd16(HP) == rd16(HPMAX), f"HP {rd16(HP)}/{rd16(HPMAX)}")
        print("health:", cli("health").replace("\n", "; "))
    finally:
        print("kill:", cli("kill").splitlines()[-1])
        for p, b in saved.items():
            p.write_bytes(b)
        if state not in saved:
            state.unlink(missing_ok=True)
        print("player files restored:", all(p.read_bytes() == b for p, b in saved.items()))
    ok = all(results)
    print("RESULT:", "PASS" if ok else "FAIL", f"({sum(results)}/{len(results)})")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
