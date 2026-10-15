#!/usr/bin/env python3
"""Exercise the scroll_every option of dragonvalor.cheats with a magic kill.

  python tests/test_scroll_cheat.py on      # scroll_every = 1
  python tests/test_scroll_cheat.py off     # control run (game rule)
  python tests/test_scroll_cheat.py on --build build-debug --port 4370

Starts from the second test state (slot 10, menu slot 11: a town stage with
Fire selected and two enemies of 15 HP in object slots 5 and 7, the nearer
one in front of the hero). The test writes its own <build>/mods/state.toml
(the player's file is backed up and restored, even on failure), launches
the debug build muted with tools/dvdbg.py, casts Fire (Circle, action 0x08)
until an enemy dies, waits for the drop and reads the pickup pool at
0x800A6A98.

  on:  before the kill the magic-kill counter 0x800A2DB4 already sits at
       scrollTable[scrolls] - 1 (6 with two scrolls collected), and a scroll
       (pickup id 2) lies on the floor afterwards.
  off: the counter counts the kill (0 -> 1) and no scroll appears.

Exit code 0 on PASS. Needs the debug server (PSX_DEBUG_TOOLS=ON).
"""
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from dvdbg import rpc, port_from_args  # noqa: E402

DV = str(ROOT / "tools" / "dvdbg.py")
MAGIC_KILLS, SCROLLS, HPMAX, MP = 0x800A2DB4, 0x800A2DBC, 0x8008A5B0, 0x8008A5B2
SCROLL_TABLE = 0x8006CAB8                       # u16[5] 2, 4, 7, 12, 20
OBJECTS, OBJ_STRIDE, OBJ_HP = 0x8008A4E0, 0x250, 0xCE
ENEMY_SLOTS = (5, 7)
PICKUPS = 0x800A6A98                            # 32 pickup objects, 0x250 bytes each
SCROLL_ID = 2
MAX_CASTS = 14

STATE_ON = """format_version = 2

[[package]]
id = "dragonvalor.cheats"
version = "1.3.0"

[[feature]]
package_id = "dragonvalor.cheats"
id = "cheats"
enabled = true
[feature.values]
scroll_every = "1"
"""
STATE_OFF = "format_version = 2\n"


def main(argv):
    args = list(argv)
    port = port_from_args(args)
    build = "build-debug"
    if "--build" in args:
        i = args.index("--build")
        build = args[i + 1]
        del args[i:i + 2]
    mode = args[0] if args else "on"
    if mode not in ("on", "off"):
        sys.exit("usage: test_scroll_cheat.py on|off [--build DIR] [--port N]")
    expect_on = mode == "on"

    state = ROOT / build / "mods" / "state.toml"
    backup = state.with_suffix(".toml.test_scroll_cheat.bak")
    had_state = state.is_file()
    if had_state:
        shutil.copy2(state, backup)
    state.parent.mkdir(parents=True, exist_ok=True)
    state.write_text(STATE_ON if expect_on else STATE_OFF, encoding="utf-8")

    def cli(*a, timeout=120):
        r = subprocess.run([sys.executable, DV, *a, "--port", str(port)], capture_output=True, text=True, timeout=timeout)
        return (r.stdout + r.stderr).strip()

    def rd(a, n):
        return bytes.fromhex(rpc(port, "read_ram", addr=f"{a:08X}", len=n)["hex"])

    def rd16(a):
        return int.from_bytes(rd(a, 2), "little")

    def rd32(a):
        return int.from_bytes(rd(a, 4), "little")

    def enemy_hp():
        return {s: rd16(OBJECTS + s * OBJ_STRIDE + OBJ_HP) for s in ENEMY_SLOTS}

    def pickups():
        p = rd(PICKUPS, 32 * OBJ_STRIDE)
        out = []
        for i in range(32):
            s = p[i * OBJ_STRIDE:(i + 1) * OBJ_STRIDE]
            if int.from_bytes(s[0x238:0x23C], "little"):
                out.append(int.from_bytes(s[0x244:0x248], "little"))
        return out

    results = []

    def check(label, ok, detail=""):
        results.append(ok)
        print(f"  {'ok  ' if ok else 'FAIL'} {label}{(': ' + detail) if detail else ''}")

    try:
        print("launch:", cli("launch", "--state", "10", "--build", str(ROOT / build)))
        time.sleep(9)
        if rd16(HPMAX) == 0:
            print("RESULT: FAIL (no hero loaded)")
            return 1
        scrolls = rd16(SCROLLS)
        threshold = rd16(SCROLL_TABLE + 2 * scrolls)
        before, hp0, mp0 = rd32(MAGIC_KILLS), enemy_hp(), rd16(MP)
        print(f"before: magicKills {before} scrolls {scrolls} (threshold {threshold}) enemies {hp0} MP {mp0} "
              f"scroll on floor {SCROLL_ID in pickups()}")
        if expect_on:
            check("counter primed to threshold - 1 before the kill", before == threshold - 1, str(before))
        else:
            check("counter untouched before the kill", before < threshold - 1, str(before))
        check("no scroll on the floor before the kill", SCROLL_ID not in pickups())

        dead = None
        for i in range(MAX_CASTS):
            cli("press", "circle:8")
            time.sleep(0.9)
            hp = enemy_hp()
            if any(v == 0 for v in hp.values()):
                dead = i + 1
                print(f"  an enemy died after cast {dead}: {hp}, MP {rd16(MP)}")
                break
        if dead is None:
            print(f"RESULT: FAIL (no enemy died in {MAX_CASTS} casts: {enemy_hp()}, MP {rd16(MP)})")
            return 1
        check("the casts spent MP (a real cast, not a swing)", rd16(MP) < mp0, f"{mp0} -> {rd16(MP)}")
        time.sleep(2.5)
        after, drops = rd32(MAGIC_KILLS), pickups()
        print(f"after: magicKills {after} pickups {drops}")
        if expect_on:
            check("a scroll (id 2) lies on the floor", SCROLL_ID in drops)
            check("counter reached the threshold", after >= threshold)
        else:
            check("no scroll on the floor", SCROLL_ID not in drops)
            check("counter counted one magic kill", after == before + 1)
        print("health:", cli("health").replace("\n", "; "))
    finally:
        print("kill:", cli("kill"))
        if had_state:
            shutil.move(backup, state)
        else:
            state.unlink(missing_ok=True)
    ok = all(results)
    print("RESULT:", "PASS" if ok else "FAIL", f"({sum(results)}/{len(results)}, scroll_every {'1' if expect_on else '0'})")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
