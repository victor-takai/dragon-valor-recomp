#!/usr/bin/env python3
"""dvdbg.py - drive a Dragon Valor debug build over the TCP debug server.

The runtime in build-debug/ (PSX_DEBUG_TOOLS=ON) listens on 127.0.0.1:4370.
This wraps the handful of commands a test session needs; anything else goes
through `raw`. All waits are in-process, so it is safe from any shell.

  python tools/dvdbg.py launch [--sw] [--port N] [--sound] [--build DIR] [--disc CUE]
        start the runtime detached, wait for the server, print pid and the
        controller line. Muted (SDL dummy audio driver) unless --sound.
  python tools/dvdbg.py press start cross down:8 ...
        tap buttons in order (NAME[:frames]); PS1 pad words are active-low,
        the complement is computed here.
  python tools/dvdbg.py shot out.png      screenshot of the guest display
  python tools/dvdbg.py frame             guest frame counter
  python tools/dvdbg.py pad               SIO pad status (connected/analog/type)
  python tools/dvdbg.py health            unknown dispatches + segment misses
  python tools/dvdbg.py state save|load [N]
        runtime save state in slot N (files under saves/openbios/); slot 0 is
        kept at the start of gameplay so tests can skip the 5 min intro
  python tools/dvdbg.py raw <cmd> [k=v ...]
  python tools/dvdbg.py kill              stop every DragonValorUS_Recompiled.exe

PORT env or --port selects another instance (--renderer software runs are
started on 4371 by `launch --sw`).
"""
import json
import os
import socket
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EXE_NAME = "DragonValorUS_Recompiled.exe"
BUTTONS = {
    "select": 0x0001, "l3": 0x0002, "r3": 0x0004, "start": 0x0008,
    "up": 0x0010, "right": 0x0020, "down": 0x0040, "left": 0x0080,
    "l2": 0x0100, "r2": 0x0200, "l1": 0x0400, "r1": 0x0800,
    "triangle": 0x1000, "circle": 0x2000, "cross": 0x4000, "square": 0x8000,
}


def port_from_args(args):
    if "--port" in args:
        i = args.index("--port")
        p = int(args[i + 1])
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
    build = ROOT / "build-debug"
    if "--build" in args:
        build = Path(args[args.index("--build") + 1])
    port = port_from_args(args) if "--port" in args else (4371 if sw else 4370)
    # --no-launcher: skip the recomp-ui launcher window and boot the game directly.
    # --disc: game.toml names the discs by Redump file name only (players point
    # the launcher at their dumps), so scripted runs pass the local path.
    disc = os.environ.get("DVDBG_DISC") or str(ROOT.parent / "Dragon Valor (USA) (Disc 1)" / "Dragon Valor (USA) (Disc 1).cue")
    if "--disc" in args:
        disc = args[args.index("--disc") + 1]
    exe_args = ["--debug-port", str(port), "--no-launcher", "--disc", disc]
    if sw:
        exe_args += ["--renderer", "software"]
    env = dict(os.environ, PSX_DEBUG_SERVER="1", PSX_FPS_TELEMETRY="1")
    if not sound:
        env["SDL_AUDIO_DRIVER"] = "dummy"   # SDL3 name; SDL2 reads the next one
        env["SDL_AUDIODRIVER"] = "dummy"
    log_path = build / f"dvdbg_{port}.log"
    log = open(log_path, "w", encoding="utf-8", errors="replace")
    flags = subprocess.DETACHED_PROCESS | subprocess.CREATE_NEW_PROCESS_GROUP
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
    log.flush()
    for line in log_path.read_text(encoding="utf-8", errors="replace").splitlines():
        if "opened controller" in line:
            print(line.strip())
            break
    else:
        print("no gamepad opened (check [controller] p1_device in game.toml)")


def cmd_press(port, specs):
    for spec in specs:
        name, _, frames = spec.partition(":")
        mask = BUTTONS[name.lower()]
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
        # state save|load <slot>: the runtime's save states (persist under saves/),
        # the fast way to start a test at a known point instead of replaying the intro
        op, slot = args[0], int(args[1]) if len(args) > 1 else 0
        print(rpc(port, "savestate", op=op, slot=slot))
    elif cmd == "raw":
        params = {}
        for kv in args[1:]:
            k, _, v = kv.partition("=")
            params[k] = int(v, 0) if v.lstrip("-").isdigit() or v.startswith("0x") else v
        print(json.dumps(rpc(port, args[0], **params), indent=1)[:4000])
    else:
        sys.exit(f"unknown command {cmd!r}; see --help")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
