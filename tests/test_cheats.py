#!/usr/bin/env python3
"""Exercise the dragonvalor.cheats plugins on a running debug build.

Setup (see docs/DEVELOPMENT.md, "This project's package"):
  1. In <build>/mods/state.toml set the cheat options (or leave them off for
     the control run); the launcher or the in-game menu writes that file, or
     append:
        [[package]]
        id = "dragonvalor.cheats"
        version = "1.2.0"
        [[feature]]
        package_id = "dragonvalor.cheats"
        id = "cheats"
        enabled = true
        [feature.values]
        infinite_hp = "true"
        infinite_mp = "true"
  2. python tools/dvdbg.py launch --state 0
  3. python tests/test_cheats.py on      # or: off  (control run)

Writes HP=5, MP=3, Val=12 through the debug server and reads them back half
a second later: with the options on HP/MP must be back at HPmax/MPmax (Val
is never touched by the plugin), with them off they must stay. Exit code 0
on PASS.
"""
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
from dvdbg import rpc, port_from_args  # noqa: E402

HP, HPMAX, MP, MPMAX, VAL = 0x8008A5AE, 0x8008A5B0, 0x8008A5B2, 0x8008A5B4, 0x800A2DD0


def main(argv):
    args = list(argv)
    port = port_from_args(args)
    mode = args[0] if args else "on"
    if mode not in ("on", "off"):
        sys.exit("usage: test_cheats.py on|off [--port N]")
    expect_on = mode == "on"

    def rd16(a):
        return int.from_bytes(bytes.fromhex(rpc(port, "read_ram", addr=f"{a:08X}", len=2)["hex"]), "little")

    def wr16(a, v):
        rpc(port, "write_ram", addr=f"{a:08X}", val=v & 0xFF)
        rpc(port, "write_ram", addr=f"{a + 1:08X}", val=(v >> 8) & 0xFF)

    hpmax, mpmax = rd16(HPMAX), rd16(MPMAX)
    if hpmax == 0:
        sys.exit("no hero loaded (HPmax = 0): start from a gameplay state, e.g. launch --state 0")
    print(f"before: HP {rd16(HP)}/{hpmax} MP {rd16(MP)}/{mpmax} Val {rd16(VAL)} frame {rpc(port, 'frame').get('frame')}")
    wr16(HP, 5); wr16(MP, 3); wr16(VAL, 12)
    time.sleep(0.5)
    hp, mp, val = rd16(HP), rd16(MP), rd16(VAL)
    print(f"after writing HP=5 MP=3 Val=12 and waiting 0.5 s: HP {hp} MP {mp} Val {val}")
    if expect_on:
        ok = hp == hpmax and mp == mpmax and val == 12
    else:
        ok = hp == 5 and mp == 3 and val == 12
    print("RESULT:", "PASS" if ok else "FAIL", f"(cheats expected {'ON' if expect_on else 'OFF'})")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
