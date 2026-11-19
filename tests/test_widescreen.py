#!/usr/bin/env python3
"""Check dragonvalor.enhancement.widescreen on the debug build (no pad needed).

  python tests/test_widescreen.py on      # package enabled, View 16:9
  python tests/test_widescreen.py off     # control run: fresh mod state
  python tests/test_widescreen.py on --build build-debug --port 4370 --state 9

The test writes its own <build>/mods/state.toml (the player's state.toml,
settings.toml and dragonvalor_settings.toml are backed up and restored on
exit, even on failure), launches the game muted from test save state 10 (an
action stage; 9 also works, never the player's slot 0) with tools/dvdbg.py
and reads the debug server's `ws_nw` and `gpu_state` readouts:

  on:  the native-wide path is engaged (mode 2) with 128 extra columns
       (16:9 on this title's 384-pixel stage), it stays engaged with the
       pause screen up (the plugin's world-scene predicate: mode byte
       0x8008634C == 8), the magic / item menu (Triangle) and a screen
       transition started from RAM present at 4:3 (the plugin's
       native-scene predicate) and the stage is wide again after each, and
       the window opened at 16:9.
  off: mode 0, no extra columns, 4:3 window.

Exit code 0 on PASS. The debug server (PSX_DEBUG_TOOLS=ON) is required.
"""
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
NW_EXTRA_169 = 128          # 384 * (16/9) / (4/3) - 384

STATE_ON = """format_version = 2

[[package]]
id = "dragonvalor.enhancement.widescreen"
version = "1.0.0"

[[feature]]
package_id = "dragonvalor.enhancement.widescreen"
id = "widescreen"
enabled = true
[feature.values]
aspect = "16:9"
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
    slot = "10"
    if "--state" in args:
        i = args.index("--state")
        slot = args[i + 1]
        del args[i:i + 2]
    if slot == "0":
        sys.exit("slot 0 is the player's quick save; use the test states 10 or 9")
    mode = args[0] if args else "on"
    if mode not in ("on", "off"):
        sys.exit("usage: test_widescreen.py on|off [--build DIR] [--port N]")
    expect_on = mode == "on"

    state = ROOT / build / "mods" / "state.toml"
    guarded = [state, ROOT / build / "settings.toml", ROOT / build / "dragonvalor_settings.toml"]
    saved = {p: p.read_bytes() for p in guarded if p.is_file()}
    state.parent.mkdir(parents=True, exist_ok=True)
    state.write_text(STATE_ON if expect_on else STATE_OFF, encoding="utf-8")

    def cli(*a, timeout=120):
        r = subprocess.run([sys.executable, DV, *a, "--port", str(port)], capture_output=True, text=True, timeout=timeout)
        return (r.stdout + r.stderr).strip()

    results = []

    def check(label, ok, detail=""):
        results.append(ok)
        print(f"  {'ok  ' if ok else 'FAIL'} {label}{(': ' + detail) if detail else ''}")

    def native43():
        return rpc(port, "gpu_state").get("ws", {}).get("present_native_43")

    def write_word(addr, value):
        for k in range(4):
            rpc(port, "write_ram", addr=f"{addr + k:08X}", val=(value >> (8 * k)) & 0xFF)

    def read_word(addr):
        return int.from_bytes(bytes.fromhex(rpc(port, "read_ram", addr=f"{addr:08X}", len=4)["hex"]), "little")

    try:
        print("launch:", cli("launch", "--state", slot, "--build", str(ROOT / build)))
        time.sleep(9)
        b = bytes.fromhex(rpc(port, "read_ram", addr=f"{MODE_BYTE:08X}", len=1)["hex"])
        check(f"save state {slot} is in the action stage (mode 8)", b[0] == 8, f"mode {b[0]}")
        nw = rpc(port, "ws_nw")
        vi = rpc(port, "video_info")
        print(f"  ws_nw {nw}\n  video_info {vi}")
        if expect_on:
            check("native-wide engaged in play (mode 2)", nw.get("mode") == 2, str(nw.get("mode")))
            check("16:9 reveals 128 extra columns", nw.get("nw_extra") == NW_EXTRA_169, str(nw.get("nw_extra")))
            cli("press", "start:6")
            time.sleep(1.5)                       # past the GTE detector's 0.75 s hysteresis
            nwp = rpc(port, "ws_nw")
            check("still wide with the pause screen up", nwp.get("mode") == 2 and nwp.get("nw_extra") == NW_EXTRA_169,
                  f"mode {nwp.get('mode')} extra {nwp.get('nw_extra')}")
            cli("press", "start:6")
            time.sleep(1.5)
            cli("press", "triangle:6")
            time.sleep(2.5)
            menu = rpc(port, "read_ram", addr=f"{MODE_BYTE:08X}", len=1)["hex"]
            check("magic / item menu (Triangle) at 4:3", menu == "07" and native43() == 1,
                  f"mode byte {menu} present_native_43 {native43()}")
            cli("press", "triangle:6")
            time.sleep(2.5)
            check("wide again after the menu", native43() == 0, f"present_native_43 {native43()}")
            # The common scene change (effect 2, scroll-flicker) stays wide:
            # its full-width frame-buffer copies are tagged and continued
            # into the margins (patches/psxrecomp/0002 + 0003); the melt
            # (effect 3, quads) still presents at 4:3.
            def counter(name):
                for c in rpc(port, "mod_counters").get("counters", []):
                    if c.get("name") == name:
                        return c.get("count", 0)
                return 0
            tagged0 = counter("dv.ws.transition_copies_tagged")
            write_word(TRANSITION + 4, 2)
            write_word(TRANSITION, 1)
            time.sleep(0.6)
            tagged = counter("dv.ws.transition_copies_tagged") - tagged0
            check("scene-change transition (effect 2) stays wide, its copies tagged",
                  read_word(TRANSITION) != 0 and native43() == 0 and tagged > 0,
                  f"state {read_word(TRANSITION)} present_native_43 {native43()} copies tagged {tagged}")
            # A transition started from RAM over play reuses the stage's work
            # area (the effects allocate from g_GameState.arena), so the stage
            # cannot resume after it: reload the state instead (a real scene
            # change loads the next stage first).
            print("  reload:", cli("state", "load", slot).splitlines()[-1])
            time.sleep(3)
            write_word(TRANSITION + 4, 3)
            write_word(TRANSITION, 1)
            time.sleep(0.6)
            check("melt transition (effect 3) at 4:3", read_word(TRANSITION) != 0 and native43() == 1,
                  f"state {read_word(TRANSITION)} present_native_43 {native43()}")
            print("  reload:", cli("state", "load", slot).splitlines()[-1])
            time.sleep(3)
            check("wide again after the transition (state reloaded)",
                  read_word(TRANSITION) == 0 and native43() == 0, f"present_native_43 {native43()}")
            # A stage reload with no screen transition (mode 17, black, as
            # the hero-swap restart writes it): the HUD stays up, so the
            # frame stays wide and the HUD at the edges (the player saw the
            # HP / MP block go to its 4:3 place there, 2026-11-18).
            for addr, val in ((MODE_BYTE, 17), (MODE_BYTE + 1, 0), (0x800A2D18, 1)):
                rpc(port, "write_ram", addr=f"{addr:08X}", val=val)
            time.sleep(0.5)
            reload_mode = rpc(port, "read_ram", addr=f"{MODE_BYTE:08X}", len=1)["hex"]
            check("stage reload (mode 17, HUD up) stays wide", reload_mode == "11" and native43() == 0,
                  f"mode byte {reload_mode} present_native_43 {native43()}")
        else:
            check("stock path in play (mode 0)", nw.get("mode") == 0, str(nw.get("mode")))
            check("no extra columns", nw.get("nw_extra", 0) == 0, str(nw.get("nw_extra")))
        w, h = vi.get("window_w") or vi.get("win_w") or 0, vi.get("window_h") or vi.get("win_h") or 0
        if w and h:
            ratio = w / h
            check("window aspect " + ("16:9" if expect_on else "4:3"),
                  abs(ratio - (16 / 9 if expect_on else 4 / 3)) < 0.03, f"{w}x{h}")
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
    print("RESULT:", "PASS" if ok else "FAIL", f"({sum(results)}/{len(results)}, widescreen {mode})")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
