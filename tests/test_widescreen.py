#!/usr/bin/env python3
"""Check dragonvalor.enhancement.widescreen on the debug build (no pad needed).

  python tests/test_widescreen.py on      # package enabled, View 16:9
  python tests/test_widescreen.py off     # control run: fresh mod state
  python tests/test_widescreen.py on --build build-debug --port 4370

The test writes its own <build>/mods/state.toml (the player's file is backed
up and restored on exit, even on failure), launches the game muted from save
state 0 with tools/dvdbg.py and reads the debug server's `ws_nw` readout:

  on:  the native-wide path is engaged (mode 2) with 128 extra columns
       (16:9 on this title's 384-pixel stage), it stays engaged with the
       pause screen up (the plugin's world-scene predicate: mode byte
       0x8008634C == 8), and the window opened at 16:9.
  off: mode 0, no extra columns, 4:3 window.

Exit code 0 on PASS. The debug server (PSX_DEBUG_TOOLS=ON) is required.
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
MODE_BYTE = 0x8008634C
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
    mode = args[0] if args else "on"
    if mode not in ("on", "off"):
        sys.exit("usage: test_widescreen.py on|off [--build DIR] [--port N]")
    expect_on = mode == "on"

    state = ROOT / build / "mods" / "state.toml"
    backup = state.with_suffix(".toml.test_widescreen.bak")
    had_state = state.is_file()
    if had_state:
        shutil.copy2(state, backup)
    state.parent.mkdir(parents=True, exist_ok=True)
    state.write_text(STATE_ON if expect_on else STATE_OFF, encoding="utf-8")

    def cli(*a, timeout=120):
        r = subprocess.run([sys.executable, DV, *a, "--port", str(port)], capture_output=True, text=True, timeout=timeout)
        return (r.stdout + r.stderr).strip()

    results = []

    def check(label, ok, detail=""):
        results.append(ok)
        print(f"  {'ok  ' if ok else 'FAIL'} {label}{(': ' + detail) if detail else ''}")

    try:
        print("launch:", cli("launch", "--state", "0", "--build", str(ROOT / build)))
        time.sleep(9)
        b = bytes.fromhex(rpc(port, "read_ram", addr=f"{MODE_BYTE:08X}", len=1)["hex"])
        check("save state 0 is in the action stage (mode 8)", b[0] == 8, f"mode {b[0]}")
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
        if had_state:
            shutil.move(backup, state)
        else:
            state.unlink(missing_ok=True)
    ok = all(results)
    print("RESULT:", "PASS" if ok else "FAIL", f"({sum(results)}/{len(results)}, widescreen {mode})")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
