#!/usr/bin/env python3
"""dvdbg.py - drive a Dragon Valor debug build over the TCP debug server.

The runtime in build-debug/ (PSX_DEBUG_TOOLS=ON) listens on 127.0.0.1:4370.
This wraps the handful of commands a test session needs; anything else goes
through `raw`. Windows only (detached process, taskkill); all waits are
in-process, so it is safe from any shell.

  python tools/dvdbg.py launch [--sw] [--port N] [--sound] [--pad] [--build DIR] [--disc CUE] [--state N]
        start the runtime detached, wait for the server, print pid and the
        controller line. Muted (SDL dummy audio driver) unless --sound.
        --state N then loads save state slot N: the way gameplay tests start.
  python tools/dvdbg.py press start cross down:8 ...
        tap buttons in order (NAME[:frames]); PS1 pad words are active-low,
        the complement is computed here.
  python tools/dvdbg.py shot out.png      screenshot of the guest display
  python tools/dvdbg.py frame             guest frame counter
  python tools/dvdbg.py pad               SIO pad status (connected/analog/type)
  python tools/dvdbg.py health            unknown dispatches + segment misses
  python tools/dvdbg.py state save|load|keep [N]
        runtime save state in slot N (saves/openbios/, restored from the
        committed tests/states/ copy when missing); keep copies the slot into
        tests/states/. Slot 0 = first screen of gameplay (skips the 5 min intro).
  python tools/dvdbg.py raw <cmd> [k=v ...]
  python tools/dvdbg.py kill              stop every DragonValorUS_Recompiled.exe

PORT env or --port selects another instance (--renderer software runs are
started on 4371 by `launch --sw`).
"""
import json
import os
import re
import shutil
import socket
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EXE_NAME = "DragonValorUS_Recompiled.exe"


def _entry_pc():
    """Entry PC from game.toml (the runtime keys slot files by it)."""
    try:
        import tomllib
        v = tomllib.loads((ROOT / "game.toml").read_text(encoding="utf-8"))["game"]["entry_pc"]
        return f"{int(str(v), 0):08X}"
    except Exception:
        return "80012EA0"


ENTRY_PC = _entry_pc()
STATES_DIR = ROOT / "tests" / "states"            # committed test states
SAVES_STATES_DIR = ROOT / "saves" / "openbios"    # where the runtime reads/writes slots
DEFAULT_DISC = str(ROOT.parent / "Dragon Valor (USA) (Disc 1)" / "Dragon Valor (USA) (Disc 1).cue")
BUTTONS = {
    "select": 0x0001, "l3": 0x0002, "r3": 0x0004, "start": 0x0008,
    "up": 0x0010, "right": 0x0020, "down": 0x0040, "left": 0x0080,
    "l2": 0x0100, "r2": 0x0200, "l1": 0x0400, "r1": 0x0800,
    "triangle": 0x1000, "circle": 0x2000, "cross": 0x4000, "square": 0x8000,
}


def flag_value(args, name, default=None):
    """Value after a `--flag`, or default; a flag without a value is an error."""
    if name not in args:
        return default
    i = args.index(name)
    if i + 1 >= len(args) or args[i + 1].startswith("--"):
        sys.exit(f"{name} needs a value")
    return args[i + 1]


def port_from_args(args):
    if "--port" in args:
        i = args.index("--port")
        p = int(flag_value(args, "--port"))
        del args[i:i + 2]
        return p
    return int(os.environ.get("PORT", "4370"))


def rpc(port, cmd, **params):
    s = socket.create_connection(("127.0.0.1", port), timeout=15)
    s.sendall((json.dumps({"id": 1, "cmd": cmd, **params}) + "\n").encode())
    buf = b""
    while not buf.endswith(b"\n"):
        chunk = s.recv(1 << 20)
        if not chunk:
            break
        buf += chunk
    s.close()
    return json.loads(buf.decode(errors="replace") or "{}")


def cmd_launch(args):
    sw = "--sw" in args
    sound = "--sound" in args
    build = Path(flag_value(args, "--build", str(ROOT / "build-debug")))
    port = port_from_args(args) if "--port" in args else (4371 if sw else 4370)
    # --no-launcher: skip the recomp-ui launcher window and boot the game directly.
    # --disc: game.toml names the discs by Redump file name only (players point
    # the launcher at their dumps), so scripted runs pass the local path.
    disc = flag_value(args, "--disc", os.environ.get("DVDBG_DISC") or DEFAULT_DISC)
    if not Path(disc).is_file():
        sys.exit(f"disc not found: {disc}\n"
                 "pass --disc <cue> or set DVDBG_DISC (docs/DEVELOPMENT.md, Prerequisites)")
    if not (build / EXE_NAME).is_file():
        sys.exit(f"no {EXE_NAME} in {build}; build it first (docs/DEVELOPMENT.md, Build)")
    # A second runtime on a taken port logs "bind FAILED" and keeps running, and
    # every poll below would be answered by the old process; refuse up front.
    try:
        rpc(port, "frame")
    except OSError:
        pass
    else:
        sys.exit(f"port {port} already answers: another instance is running "
                 "(`dvdbg.py kill`, or pick --port N)")
    # The framework's freeze watchdog writes a ~35 MB psx_freeze_dump_*.json
    # on every boot of this title (a false positive on statically compiled
    # code, upstream #646); nothing reads them, so clear the pile first.
    dumps = list(build.glob("psx_freeze_dump_*.json"))
    for d in dumps:
        d.unlink(missing_ok=True)
    if dumps:
        print(f"cleared {len(dumps)} freeze dump(s) in {build}")
    exe_args = ["--debug-port", str(port), "--no-launcher", "--disc", disc]
    if sw:
        exe_args += ["--renderer", "software"]
    env = dict(os.environ, PSX_FPS_TELEMETRY="1")   # the debug server is compiled in (PSX_DEBUG_TOOLS=ON)
    if not sound:
        env["SDL_AUDIO_DRIVER"] = "dummy"   # SDL3 name; SDL2 reads the next one
        env["SDL_AUDIODRIVER"] = "dummy"
    if "--pad" not in args:
        # A connected pad is opened by the runtime and drives the game: a stick
        # that rests off-centre walked the hero out of every scripted scene on
        # 2026-10-15. Scripted runs see no pad unless asked (--pad).
        env["SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT"] = "0x0000/0x0000"
    log_path = build / f"dvdbg_{port}.log"
    flags = subprocess.DETACHED_PROCESS | subprocess.CREATE_NEW_PROCESS_GROUP
    with open(log_path, "w", encoding="utf-8", errors="replace") as log:   # the child keeps its own handle
        proc = subprocess.Popen([str(build / EXE_NAME), *exe_args], cwd=str(build), env=env,
                                stdout=log, stderr=subprocess.STDOUT, creationflags=flags)
    print(f"pid={proc.pid} port={port} renderer={'software' if sw else 'opengl'} "
          f"audio={'on' if sound else 'muted'} log={log_path}")
    t0 = time.time()
    while time.time() - t0 < 90:
        if proc.poll() is not None:
            sys.exit(f"runtime exited early rc={proc.returncode}; see {log_path}")
        try:
            rpc(port, "frame")
            break
        except OSError:
            time.sleep(1)
    else:
        sys.exit("debug server did not come up (is this build PSX_DEBUG_TOOLS=ON?)")
    time.sleep(3)
    for line in log_path.read_text(encoding="utf-8", errors="replace").splitlines():
        if "opened controller" in line:
            print(line.strip())
            break
    else:
        print("no gamepad opened (check [controller] p1_device in game.toml)")
    if "--state" in args:
        cmd_state(port, ["load", flag_value(args, "--state")], disc)


def slot_file(slot, disc_path):
    """File name the runtime uses for a slot: entry PC, disc token, slot number."""
    m = re.search(r"\(Disc (\d+)\)", str(disc_path))
    disc = int(m.group(1)) if m else 1
    return f"state_{ENTRY_PC}_disc{disc}_slot{slot:02d}.pst"


def wait_savestate(port, generation, timeout=30):
    """Block until the staged save/load has run: the status generation advances
    once savestate_poll applied or rejected the request (the ack comes earlier)."""
    t0 = time.time()
    while time.time() - t0 < timeout:
        st = rpc(port, "savestate_status")
        if st.get("generation", 0) > generation and not st.get("pending"):
            return st
        time.sleep(0.2)
    return {}


def cmd_state(port, args, disc_path):
    """state save|load|keep [slot]: the runtime's save states are the fast way
    to start a test at a known point instead of replaying the intro."""
    op = args[0] if args else "load"
    slot = int(args[1]) if len(args) > 1 else 0
    name = slot_file(slot, disc_path)
    local, kept = SAVES_STATES_DIR / name, STATES_DIR / name
    if op == "keep":
        if not local.exists():
            sys.exit(f"no local slot {slot}: {local}")
        STATES_DIR.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(local, kept)
        print(f"kept {kept} ({kept.stat().st_size} bytes); commit tests/states/")
        return
    if op not in ("save", "load"):
        sys.exit("state save|load|keep [slot]")
    if op == "load" and kept.exists():
        # Restore the committed copy when the local slot is missing or older
        # (a pull re-took it); a locally re-taken, newer slot is kept.
        stale = (not local.exists() or local.stat().st_size != kept.stat().st_size
                 or local.stat().st_mtime < kept.stat().st_mtime)
        if stale:
            SAVES_STATES_DIR.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(kept, local)
            print(f"restored slot {slot} from {kept}")
    gen = rpc(port, "savestate_status").get("generation", 0)
    ack = rpc(port, "savestate", op=op, slot=slot)
    if not ack.get("ok"):
        sys.exit(f"savestate {op} {slot} refused: {ack}")
    st = wait_savestate(port, gen)
    if not st or not st.get("last_ok"):
        hint = (" (a slot taken on an older framework pin can be refused: re-take it with"
                " `state save N`, then `state keep N`)" if op == "load" else "")
        sys.exit(f"savestate {op} {slot} did not complete: {st or 'timeout'}{hint}")
    if op == "load":
        time.sleep(1)   # let the restored frame reach the display before the caller shoots
    print(f"{op} slot {slot} ok (frame {rpc(port, 'frame').get('frame')}, {local})")


def cmd_press(port, specs):
    for spec in specs:
        name, _, frames = spec.partition(":")
        mask = BUTTONS.get(name.lower())
        if mask is None:
            sys.exit(f"unknown button {name!r}; one of: {', '.join(BUTTONS)}")
        rpc(port, "press", buttons=0xFFFF & ~mask, frames=int(frames or 6))
        time.sleep(0.5)
        rpc(port, "clear_input")
        time.sleep(0.5)
        print(f"{name:<9} frame={rpc(port, 'frame').get('frame')}")


def main(argv):
    if not argv or argv[0] in ("-h", "--help"):
        print(__doc__)
        return 0
    cmd, args = argv[0], argv[1:]
    if cmd == "launch":
        cmd_launch(args)
        return 0
    if cmd == "kill":
        subprocess.run(["taskkill", "/F", "/IM", EXE_NAME], check=False)
        return 0
    port = port_from_args(args)
    if cmd == "press":
        cmd_press(port, args)
    elif cmd == "shot":
        out = Path(args[0] if args else "dv_shot.png").resolve()
        print(rpc(port, "screenshot", path=str(out)))
    elif cmd == "frame":
        print(rpc(port, "frame").get("frame"))
    elif cmd == "pad":
        r = rpc(port, "pad_status")
        print(json.dumps({k: r.get(k) for k in ("slot0", "slot1", "override")}, indent=1))
    elif cmd == "health":
        for c in ("unknown_dispatch_log", "segment_misses"):
            r = rpc(port, c)
            print(f"{c}: total={r.get('total')} unique={r.get('unique')}")
    elif cmd == "state":
        cmd_state(port, args, os.environ.get("DVDBG_DISC") or DEFAULT_DISC)
    elif cmd == "raw":
        if not args:
            sys.exit("raw <cmd> [k=v ...]")
        params = {}
        for kv in args[1:]:
            k, _, v = kv.partition("=")
            if re.fullmatch(r"-?0[xX][0-9a-fA-F]+", v):
                params[k] = int(v, 16)
            elif re.fullmatch(r"-?\d+", v):
                params[k] = int(v, 10)
            else:
                params[k] = v
        text = json.dumps(rpc(port, args[0], **params), indent=1)
        print(text[:4000] + ("\n...truncated" if len(text) > 4000 else ""))
    else:
        sys.exit(f"unknown command {cmd!r}; see --help")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
