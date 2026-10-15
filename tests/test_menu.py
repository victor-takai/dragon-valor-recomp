#!/usr/bin/env python3
"""Drive the in-game menu (mods/plugins/dragonvalor_menu.cpp) on a debug build.

  python tools/dvdbg.py launch --state 0
  python tests/test_menu.py [--port N]

Injects keys through the debug server's host_key command (the same SDL
events a keyboard produces) and checks, from slot 0:
  1. Escape opens the menu and the guest pauses (frame counter stops).
  2. Cheats page: Enter on "Infinite HP" turns it on; after closing the menu
     the guest runs again and HP written to 5 refills to HPmax within a frame
     (the option reached the plugin live, through psx_mod_set_option_live).
  3. The same row turns it off again; a written HP stays. The Val row
     (third on the page) writes RAM at once: one step right = 100. The STR
     row (sixth) spreads its value over the five attack entries at
     0x80086324 (the game sums them into the shown STR): one step = +5.
  4. System page: "Save state" on slot 2 (runtime slot 1) writes the slot file.
The menu always opens on the first section (Display) with nothing selected,
so a path is Down x N then Enter. Exit code 0 on PASS.
"""
import os
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
from dvdbg import rpc, port_from_args, slot_file, SAVES_STATES_DIR, DEFAULT_DISC  # noqa: E402

HP, HPMAX = 0x8008A5AE, 0x8008A5B0
SECTIONS = ["Display", "Graphics", "Audio", "System", "Cheats", "Game"]


def main(argv):
    args = list(argv)
    port = port_from_args(args)
    results = []

    def frame():
        return rpc(port, "frame").get("frame")

    def rd16(a):
        return int.from_bytes(bytes.fromhex(rpc(port, "read_ram", addr=f"{a:08X}", len=2)["hex"]), "little")

    def wr16(a, v):
        rpc(port, "write_ram", addr=f"{a:08X}", val=v & 0xFF)
        rpc(port, "write_ram", addr=f"{a + 1:08X}", val=(v >> 8) & 0xFF)

    def key(name, n=1):
        for _ in range(n):
            r = rpc(port, "host_key", name=name)
            if not r.get("ok"):
                sys.exit(f"host_key {name} failed: {r}")
            time.sleep(0.3)

    def paused():
        f0 = frame(); time.sleep(0.8); return frame() == f0

    def check(label, ok):
        results.append(ok); print(f"  {'ok  ' if ok else 'FAIL'} {label}")

    def goto_section(name):
        key("Down", SECTIONS.index(name)); key("Return")

    def close():
        key("Escape", 2); time.sleep(0.3)

    if rd16(HPMAX) == 0:
        sys.exit("no hero loaded (HPmax = 0): launch with --state 0")

    # 1. open + pause
    key("Escape"); time.sleep(0.5)
    check("Escape opens the menu and pauses the guest", paused())

    # 2. Infinite HP on (first row of the Cheats page)
    goto_section("Cheats"); key("Return"); close()
    check("closing the menu resumes the guest", not paused())
    wr16(HP, 5); time.sleep(0.5)
    check("Infinite HP applied live (HP 5 -> HPmax)", rd16(HP) == rd16(HPMAX))

    # 3. and off again
    key("Escape"); goto_section("Cheats"); key("Return"); close()
    wr16(HP, 5); time.sleep(0.5)
    hp = rd16(HP)
    check("Infinite HP off again (written HP stays)", hp == 5)
    wr16(HP, rd16(HPMAX))   # leave the hero healthy

    # 3b. Val editor: fifth row of the Cheats page (after Infinite HP/MP and the two
    #     drop-rate options), one step right = +100, written at once
    VAL = 0x800A2DD0
    wr16(VAL, 0); time.sleep(0.2)
    key("Escape"); goto_section("Cheats"); key("Down", 4); key("Right"); close()
    val = rd16(VAL)
    check("Val editor wrote 100 into RAM", val == 100)
    wr16(VAL, 0)

    # 3c. STR editor: eighth row; +5 lands in the five attack entries (sum) and in STR
    STR, ATK = 0x8008A5B6, 0x80086324
    before = rd16(STR)
    key("Escape"); goto_section("Cheats"); key("Down", 7); key("Right"); close()
    comps = [rd16(ATK + 2 * i) for i in range(5)]
    check(f"STR editor wrote {before + 5} as attack entries {comps}",
          rd16(STR) == before + 5 and sum(comps) == before + 5 and min(comps) > 0)

    # 4. save state to slot 2 from the System page: Resume, State slot, Save state, ...
    target = SAVES_STATES_DIR / slot_file(1, os.environ.get("DVDBG_DISC") or DEFAULT_DISC)
    if target.exists():
        target.unlink()
    key("Escape"); goto_section("System"); key("Down"); key("Right"); key("Down"); key("Return")
    time.sleep(1.5); close(); time.sleep(2.5)
    check(f"Save state from the menu wrote slot 2 ({target.name})", target.exists())

    ok = all(results)
    print("RESULT:", "PASS" if ok else "FAIL", f"({sum(results)}/{len(results)})")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
