#!/usr/bin/env python3
"""Re-take the committed test save state (slot 0: Brize Forest, first screen).

  python tests/retake_state0.py [--port N]

Run after a framework pin bump or a runtime-flavour change refuses the old
slot. Expects a debug build with the skip-intro feature ENABLED in
<build>/mods/state.toml (so the intro movie needs no press), launched fresh:

  python tools/dvdbg.py launch

Route (mode byte 0x8008634C, see docs/DEVELOPMENT.md): title menu ready
(5/2) -> Start = New Game (6) -> Start skips the prologue text and the
opening cutscene -> action stage (8) with the stage id 0x800A2F1A == 2 ->
a few seconds for the HUD -> `state save 0` -> `state keep 0`. Note the pin in
tests/states/README.md (the states themselves stay out of git).
"""
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
from dvdbg import rpc, port_from_args, cmd_state, DEFAULT_DISC  # noqa: E402
import os


def main(argv):
    args = list(argv)
    port = port_from_args(args)

    def rd(addr, n):
        return bytes.fromhex(rpc(port, "read_ram", addr=f"{addr:08X}", len=n)["hex"])

    def mode():
        return rd(0x8008634C, 2)

    def stage():
        return int.from_bytes(rd(0x800A2F1A, 2), "little")

    def press_start():
        rpc(port, "press", buttons=0xFFFF & ~0x0008, frames=6); time.sleep(0.3); rpc(port, "clear_input")

    def wait(pred, limit, what):
        t0 = time.time()
        while time.time() - t0 < limit:
            if pred():
                return True
            time.sleep(0.25)
        sys.exit(f"timeout waiting for {what} (mode {mode()[0]} sub {mode()[1]})")

    print("waiting for the title menu (needs the skip-intro feature; otherwise press Start on the movie yourself)")
    wait(lambda: mode() == b"\x05\x02", 150, "menu ready (5/2)")
    time.sleep(1.5); press_start()
    wait(lambda: mode()[0] == 6, 15, "New Game (mode 6)")
    print("New Game; skipping the prologue text and the opening cutscene")
    t0 = time.time()
    while time.time() - t0 < 240:
        m = mode()
        if m[0] == 8 and stage() == 2:
            break
        press_start(); time.sleep(2.5)
    else:
        sys.exit("never reached the action stage")
    time.sleep(4)   # HUD up, tutorial text, first enemy walking in
    hp = int.from_bytes(rd(0x8008A5AE, 2), "little")
    print(f"action stage, stage id {stage()}, HP {hp}; saving slot 0")
    disc = os.environ.get("DVDBG_DISC") or DEFAULT_DISC
    cmd_state(port, ["save", "0"], disc)
    cmd_state(port, ["keep", "0"], disc)
    print("done: tests/states/ updated (not in git); note the pin in its README")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
