#!/usr/bin/env python3
"""Check the dragonvalor.skip-intro plugin on a fresh boot of the debug build.

  python tests/test_skip_intro.py [--build build-debug] [--port N]   # skip on
  python tests/test_skip_intro.py --off                              # control: feature off

The test writes its own <build>/mods/state.toml (the skip-intro feature on
with every option, or off; the player's file is backed up and restored),
launches the build muted with a hidden window and kills it by its PID.

It watches g_GameFlow.mode / subMode (0x8008634C) and the title's
g_TitleWork.screen / state (0x80156F32 / 0x80156F33): mode 1 the Namco logo
(sub 2 fade in, 3 the title-pack read, 4 hold, 5 fade out), 2 the movie,
3 the title, whose main screen (screen 0) runs states 1..3 (the logo flight)
and 5 (the menu: New Game / Load Game / Configuration). Mode 5 is the
attract demo (1500 idle frames on the menu), mode 17 a new game loading.

With the feature on: the logo never fades in (sub 2 is never run: its
fade-in counter in the plugin), the movie lasts under 1.5 s, the menu is up
within 8 s of the first poll and stays there 4 s with nothing chosen (no
Start leaks into it); a Start pressed then starts a new game (mode 17).
With --off: the movie is still playing 20 s after the first poll. Exit
code 0 on PASS.
"""
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from dvdbg import rpc, port_from_args  # noqa: E402

STATE_ON = """format_version = 2

[[package]]
id = "dragonvalor.skip-intro"
version = "1.0.0"

[[feature]]
package_id = "dragonvalor.skip-intro"
id = "skip-intro"
enabled = true
[feature.values]
logo = "true"
movie = "true"
title = "true"
"""
STATE_OFF = "format_version = 2\n"


def main(argv):
    args = list(argv)
    port = port_from_args(args)
    off = "--off" in args
    build = ROOT / "build-debug"
    if "--build" in args:
        build = (ROOT / args[args.index("--build") + 1]).resolve()
    state = build / "mods" / "state.toml"
    backup = state.with_suffix(".toml.skiptest")
    results = []

    def check(name, ok, detail=""):
        results.append(ok)
        print(f"  {'ok  ' if ok else 'FAIL'} {name}{': ' + detail if detail else ''}")

    def rd(addr, n):
        return bytes.fromhex(rpc(port, "read_ram", addr=f"{addr:08X}", len=n)["hex"])

    if state.exists():
        shutil.copyfile(state, backup)
    state.write_text(STATE_OFF if off else STATE_ON, encoding="utf-8")
    pid = None
    try:
        out = subprocess.run([sys.executable, str(ROOT / "tools/dvdbg.py"), "launch", "--build", str(build),
                              "--port", str(port)], capture_output=True, text=True, cwd=ROOT).stdout
        m = re.search(r"pid=(\d+)", out)
        pid = m.group(1) if m else None
        t0 = time.time()
        seen, last, menu_at = {}, None, None
        deadline = t0 + (20 if off else 15)
        while time.time() < deadline:
            mode, sub = rd(0x8008634C, 2)
            screen, tstate = rd(0x80156F32, 2)
            key = (mode, sub, screen if mode == 3 else 0, tstate if mode == 3 else 0)
            if key != last:
                print(f"t={time.time() - t0:5.2f}s frame={rpc(port, 'frame').get('frame')} mode={mode} sub={sub}"
                      + (f" screen={screen} state={tstate}" if mode == 3 else ""))
                seen.setdefault((mode, sub), time.time() - t0)
                last = key
            if not off and mode == 3 and sub == 1 and screen == 0 and tstate == 5:
                menu_at = time.time() - t0
                break
            time.sleep(0.05)
        if off:
            check("the movie still plays after 20 s (feature off)", rd(0x8008634C, 1)[0] == 2,
                  f"mode {rd(0x8008634C, 1)[0]}")
        else:
            check("the menu is up within 8 s", menu_at is not None and menu_at < 8,
                  f"{menu_at:.2f} s" if menu_at else "never")
            check("the logo's fade-in never ran", (1, 2) not in seen, "")
            if (2, 0) in seen and (3, 0) in seen:
                check("the movie lasted under 1.5 s", seen[(3, 0)] - seen[(2, 0)] < 1.5,
                      f"{seen[(3, 0)] - seen[(2, 0)]:.2f} s")
            rpc(port, "screenshot", path=str(build / "test_skip_intro_menu.png"))
            time.sleep(4)
            mode, sub = rd(0x8008634C, 2)
            screen, tstate = rd(0x80156F32, 2)
            check("still on the menu 4 s later, nothing chosen", (mode, sub, screen, tstate) == (3, 1, 0, 5),
                  f"mode {mode} sub {sub} screen {screen} state {tstate}")
            rpc(port, "press", buttons=0xFFFF & ~0x0008, frames=6)      # Start (active-low)
            time.sleep(0.5)
            rpc(port, "clear_input")
            t1 = time.time()
            while time.time() - t1 < 6 and rd(0x8008634C, 1)[0] != 17:
                time.sleep(0.1)
            check("Start on the menu starts a new game (mode 17)", rd(0x8008634C, 1)[0] in (17, 6, 8),
                  f"mode {rd(0x8008634C, 1)[0]}")
    finally:
        if pid:
            subprocess.run(["taskkill", "/F", "/PID", pid], capture_output=True)
            time.sleep(1)
        if backup.exists():
            shutil.copyfile(backup, state)
            backup.unlink()
        print("player files restored:", state.exists())
    ok = all(results)
    print(f"RESULT: {'PASS' if ok else 'FAIL'} ({sum(results)}/{len(results)})")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
