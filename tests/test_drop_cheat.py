#!/usr/bin/env python3
"""Exercise the gem_every option of dragonvalor.cheats on a running debug build.

Setup (same file as tests/test_cheats.py, package version 1.3.0):
  1. In <build>/mods/state.toml set, under the cheats feature,
        [feature.values]
        gem_every = "1"          # "0" for the control run
  2. python tools/dvdbg.py launch --state 0
  3. python tests/test_drop_cheat.py on     # or: off  (control run)

Save state 0 starts next to one enemy (object slot 5, HP 15). The test swings
the sword until it dies, waits for the death animation and reads the pickup
pool at 0x800A6A98. With the option on, the sword-kill counter 0x800A2DB0
must already sit at the first gem threshold minus one (14) before the kill,
and a gem (pickup id 0) must lie on the floor afterwards. With it off the
counter must read 1 after the kill and no gem may appear (a class-2 enemy
leaves nothing or a 1-Val coin, id 12). Exit code 0 on PASS.
"""
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
from dvdbg import rpc, port_from_args  # noqa: E402

DV = str(Path(__file__).resolve().parent.parent / "tools" / "dvdbg.py")
SWORD_KILLS, GEMS, HPMAX = 0x800A2DB0, 0x800A2EDC, 0x8008A5B0
ENEMY_HP = 0x8008B070 + 0xCE          # object slot 5 of save state 0
PICKUPS = 0x800A6A98                  # 32 pickup objects, 0x250 bytes each
FIRST_GEM_THRESHOLD = 15


def main(argv):
    args = list(argv)
    port = port_from_args(args)
    mode = args[0] if args else "on"
    if mode not in ("on", "off"):
        sys.exit("usage: test_drop_cheat.py on|off [--port N]")
    expect_on = mode == "on"

    def rd(a, n):
        return bytes.fromhex(rpc(port, "read_ram", addr=f"{a:08X}", len=n)["hex"])

    def rd16(a):
        return int.from_bytes(rd(a, 2), "little")

    def rd32(a):
        return int.from_bytes(rd(a, 4), "little")

    def pickups():
        p = rd(PICKUPS, 32 * 0x250)
        out = []
        for i in range(32):
            s = p[i * 0x250:(i + 1) * 0x250]
            if int.from_bytes(s[0x238:0x23C], "little"):
                out.append(int.from_bytes(s[0x244:0x248], "little"))
        return out

    def press(spec):
        subprocess.run([sys.executable, DV, "press", spec, "--port", str(port)], check=True, capture_output=True)

    if rd16(HPMAX) == 0:
        sys.exit("no hero loaded (HPmax = 0): launch with --state 0")
    if rd16(ENEMY_HP) == 0:
        sys.exit("the slot-5 enemy is already dead: reload save state 0")
    time.sleep(0.5)                                   # a VBlank or two for the plugin
    before = rd32(SWORD_KILLS)
    print(f"before: swordKills {before} gems {rd16(GEMS)} enemy HP {rd16(ENEMY_HP)} pickups {pickups()}")
    results = []

    def check(label, ok):
        results.append(ok)
        print(f"  {'ok  ' if ok else 'FAIL'} {label}")

    check("counter primed to threshold - 1 before the kill" if expect_on else "counter untouched before the kill",
          before == FIRST_GEM_THRESHOLD - 1 if expect_on else before == 0)

    dead = False
    for i in range(30):
        press("square:8")
        time.sleep(0.5)
        if rd16(ENEMY_HP) == 0:
            dead = True
            print(f"  enemy dead after swing {i + 1}")
            break
    if not dead:
        print("RESULT: FAIL (the enemy did not die in 30 swings; stand still after loading state 0)")
        return 1
    time.sleep(2.5)                                   # death animation, then the drop spawns
    after, drops = rd32(SWORD_KILLS), pickups()
    print(f"after: swordKills {after} pickups {drops}")
    if expect_on:
        check("a gem (id 0) lies on the floor", 0 in drops)
        check("counter reached the threshold", after >= FIRST_GEM_THRESHOLD)
    else:
        check("no gem on the floor", 0 not in drops)
        check("counter counted one kill", after == 1)
    ok = all(results)
    print("RESULT:", "PASS" if ok else "FAIL", f"({sum(results)}/{len(results)}, gem_every expected {'1' if expect_on else '0'})")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
