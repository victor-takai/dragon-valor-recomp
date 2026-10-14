"""Passive monitor for a human play session on the debug build (port 4370).

Usage: python tools/session_monitor.py <out_dir>   (start the game with PlayDebug.bat first)

Every 30 s: frame, fps (delta), unknown dispatches, segment misses, pad mode.
Dumps the unknown-dispatch tail whenever the unique count grows. Screenshot
every 5 min. Stops after 8 consecutive connection failures (game closed)."""
import json, socket, sys, time
from pathlib import Path
out = Path(sys.argv[1]); out.mkdir(parents=True, exist_ok=True)
def rpc(cmd, **p):
    s = socket.create_connection(("127.0.0.1", 4370), timeout=15)
    s.sendall((json.dumps({"id": 1, "cmd": cmd, **p}) + "\n").encode())
    b = b""
    while not b.endswith(b"\n"):
        c = s.recv(1 << 20)
        if not c: break
        b += c
    s.close()
    return json.loads(b.decode(errors="replace") or "{}")
csv = open(out / "monitor.csv", "a", encoding="utf-8"); csv.write("time,frame,fps,unknown_total,unknown_unique,segment_total,analog\n")
last_frame, last_t, last_unique, fails, t0 = None, None, 0, 0, time.time()
next_shot = t0
while fails < 8 and time.time() - t0 < 2 * 3600:
    try:
        fr = int(rpc("frame").get("frame", 0)); now = time.time()
        u = rpc("unknown_dispatch_log"); sg = rpc("segment_misses"); pad = rpc("pad_status").get("slot0", {})
        fps = (fr - last_frame) / (now - last_t) if last_frame is not None else 0.0
        line = f"{time.strftime('%H:%M:%S')},{fr},{fps:.1f},{u.get('total')},{u.get('unique')},{sg.get('total')},{pad.get('analog')}"
        csv.write(line + "\n"); csv.flush(); print(line, flush=True)
        if (u.get("unique") or 0) > last_unique:
            tail = rpc("unknown_dispatch_log", tail=64)
            (out / f"unknown_f{fr:07d}.json").write_text(json.dumps(tail, indent=1))
            print(f"  !! new unknown dispatch addresses (unique {last_unique} -> {u.get('unique')}) -> unknown_f{fr:07d}.json", flush=True)
            last_unique = u.get("unique") or 0
        if now >= next_shot:
            rpc("screenshot", path=str(out / f"shot_{time.strftime('%H%M%S')}_f{fr:07d}.png")); next_shot = now + 300
        last_frame, last_t, fails = fr, now, 0
    except OSError as e:
        fails += 1; print(f"{time.strftime('%H:%M:%S')} no connection ({fails}/8): {e}", flush=True)
    time.sleep(30)
print("monitor finished", flush=True)
