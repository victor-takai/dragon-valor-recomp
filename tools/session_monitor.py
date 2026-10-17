"""Passive monitor for a human play session on the debug build.

Usage: python tools/session_monitor.py <out_dir> [--port N]
       (start the game with DragonValorDebug.bat first; PORT env also works)

Every 30 s: frame, fps (delta), unknown dispatches, segment misses, pad mode,
appended to <out_dir>/monitor.csv. Dumps the unknown-dispatch tail whenever
the unique count grows. Screenshot every 5 min. Stops after 8 consecutive
connection failures (game closed) or after 2 hours."""
import json, sys, time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from dvdbg import rpc, port_from_args   # noqa: E402

args = sys.argv[1:]
port = port_from_args(args)
if len(args) != 1:
    sys.exit(__doc__)
# Resolve: the runtime writes screenshots relative to ITS cwd (the build dir).
out = Path(args[0]).resolve(); out.mkdir(parents=True, exist_ok=True)
csv_path = out / "monitor.csv"
new_file = not csv_path.exists() or csv_path.stat().st_size == 0
csv = open(csv_path, "a", encoding="utf-8")
if new_file:
    csv.write("time,frame,fps,unknown_total,unknown_unique,segment_total,analog\n")
last_frame, last_t, last_unique, fails, t0 = None, None, 0, 0, time.time()
next_shot = t0
while fails < 8 and time.time() - t0 < 2 * 3600:
    try:
        fr = int(rpc(port, "frame").get("frame", 0)); now = time.time()
        u = rpc(port, "unknown_dispatch_log"); sg = rpc(port, "segment_misses")
        pad = rpc(port, "pad_status").get("slot0", {})
        fps = (fr - last_frame) / (now - last_t) if last_frame is not None else 0.0
        line = (f"{time.strftime('%H:%M:%S')},{fr},{fps:.1f},{u.get('total')},{u.get('unique')},"
                f"{sg.get('total')},{pad.get('analog')}")
        csv.write(line + "\n"); csv.flush(); print(line, flush=True)
        if (u.get("unique") or 0) > last_unique:
            tail = rpc(port, "unknown_dispatch_log", tail=64)
            (out / f"unknown_f{fr:07d}.json").write_text(json.dumps(tail, indent=1))
            print(f"  !! new unknown dispatch addresses (unique {last_unique} -> {u.get('unique')})"
                  f" -> unknown_f{fr:07d}.json", flush=True)
            last_unique = u.get("unique") or 0
        if now >= next_shot:
            shot = out / f"shot_{time.strftime('%H%M%S')}_f{fr:07d}.png"
            r = rpc(port, "screenshot", path=str(shot))
            if not r.get("ok"):
                print(f"  screenshot failed: {r}", flush=True)
            next_shot = now + 300
        last_frame, last_t, fails = fr, now, 0
    except OSError as e:
        fails += 1; print(f"{time.strftime('%H:%M:%S')} no connection ({fails}/8): {e}", flush=True)
    time.sleep(30)
csv.close()
print("monitor finished", flush=True)
