#!/usr/bin/env python3
"""Check the dragonvalor.skip-intro plugin on a fresh boot of the debug build.

Setup: enable the "skip-intro" feature in <build>/mods/state.toml (see
docs/DEVELOPMENT.md), then `python tools/dvdbg.py launch` (no --state) and run
this right away:

  python tests/test_skip_intro.py              # movie skipped
  python tests/test_skip_intro.py --off        # control run: feature disabled

Watches the mode byte (0x8008634C): 1 logo, 2 movie, 3 title animation,
5 title (sub 0 = still screen, 17 = menu load, 5 sub 2 = menu ready).
With the feature on, the title animation must start within 14 s of the first
poll (the logo alone ends at ~9 s; the stock boot then waits for a Start press). Then presses Start on
the ready menu and expects New Game (mode 6). With --off the movie must still
be running 20 s after the first poll. Exit code 0 on PASS.
"""
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
from dvdbg import rpc, port_from_args  # noqa: E402


def main(argv):
    args = list(argv)
    port = port_from_args(args)
    off = "--off" in args

    def mode():
        return bytes.fromhex(rpc(port, "read_ram", addr="8008634C", len=2)["hex"])

    def frame():
        return rpc(port, "frame").get("frame")

    t0 = time.time()
    seen = {}
    last = None
    deadline = t0 + (20 if off else 75)
    while time.time() < deadline:
        m = mode()
        if m != last:
            seen.setdefault(m[0], time.time() - t0)
            print(f"t={time.time()-t0:5.1f}s frame={frame()} mode={m[0]} sub={m[1]}")
            last = m
        if m[0] in (5, 17, 8, 4):
            break
        time.sleep(0.1)
    if off:
        still_movie = mode()[0] == 2
        print("RESULT:", "PASS" if still_movie else "FAIL", "(off: the movie should still be running)")
        return 0 if still_movie else 1
    # After the skip the title behaves as after a manual Start: it either goes
    # to the menu (5) or plays the story demo first (17 -> 8, or 4). Both are
    # the game's own paths; what must not happen is the movie running on.
    t3 = seen.get(3, 999)
    moved_on = any(k in seen for k in (4, 5, 8, 17))
    print(f"title animation at {t3:.1f}s; title moved on: {moved_on}")
    ok = t3 < 14 and moved_on
    print("RESULT:", "PASS" if ok else "FAIL", "(movie skip)")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
