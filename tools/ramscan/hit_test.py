"""hit_test.py - the damage experiment of 2026-10-14 (docs/DEVELOPMENT.md, "How damage is computed").

From a running debug build in slot 0 with the five attack entries already
written to 500: swing until the enemy in slot 5 loses HP (one hit kills),
then reload slot 0 and swing with the default entries (6 per hit).
Muted launch: tools/dvdbg.py launch --state 0; then write 0x80086324.. and run this.
"""
import time, subprocess
import sys
from pathlib import Path
TOOLS = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(TOOLS))
import dvdbg
DV = str(TOOLS / "dvdbg.py")
def rpc(cmd, **kw): return dvdbg.rpc(4370, cmd, **kw)
def rd16(a): return int.from_bytes(bytes.fromhex(rpc("read_ram", addr="0x%08X" % a, len=2)["hex"]), "little")
def press(spec): subprocess.run([sys.executable, DV, "press", spec], check=True, capture_output=True)
def find_enemy():
    for k in range(1, 16):
        base = 0x8008A4E0 + k*0x250
        hp, hpmax = rd16(base+0xCE), rd16(base+0xD0)
        if hpmax: return k, base, hp, hpmax
    return None
def swings(label, n=10):
    e = find_enemy()
    if not e: print(label, "no enemy with HPmax found"); return
    k, base, hp, hpmax = e
    print("%s: enemy slot %d @%08x HP %d/%d, hero HP %d, components %s" % (label, k, base, hp, hpmax, rd16(0x8008A5AE),
          [rd16(0x80086324 + 2*i) for i in range(5)]))
    for i in range(n):
        press("square:8"); time.sleep(0.45)
        hp = rd16(base+0xCE)
        print("  swing %d -> enemy HP %d  (hero HP %d, frame %s)" % (i+1, hp, rd16(0x8008A5AE), rpc("frame").get("frame")))
        if hp == 0: break
    return base
# phase 1: components already 500 each
arm = rpc("wtrace_arm", lo="0x8008B13E", hi="0x8008B140"); print("wtrace", arm)
base = swings("components=500")
d = rpc("wtrace_dump", addr_lo="0x8008B13E", addr_hi="0x8008B140", count=200)
es = [e for e in d.get("entries", []) if int(str(e.get("addr", "0")), 16) & 0x1FFFFFFF in (0x0008B13E, 0x0008B13F)]
print("enemy HP writes:", len(es))
for e in es[-12:]: print("  ", {k: e.get(k) for k in ("addr", "val", "pc", "func_addr", "ra", "frame", "width")})
# phase 2: fresh state 0, default components
subprocess.run([sys.executable, DV, "state", "load", "0"], check=True, capture_output=True); time.sleep(1.5)
swings("default components", n=12)
