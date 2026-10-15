"""drop_test.py - the drop experiment of 2026-10-15 (docs/DEVELOPMENT.md, "How drops are decided").

Needs a running debug build in slot 0 (muted: tools/dvdbg.py launch --state 0).
Run A primes the sword-kill counter to the first gem threshold (15), kills the
enemy in slot 5 with the sword and expects a gem (pickup id 0) on the floor;
it then walks onto it and shows the counter being consumed (16 -> 1) and the
five attack entries growing by one gem row. Run B kills the same enemy with the
counter untouched: the random table of its drop class decides instead (class 2:
nothing or a 1-Val coin, id 12).
"""
import sys, time, subprocess
from pathlib import Path
TOOLS = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(TOOLS))
import dvdbg
DV = str(TOOLS / "dvdbg.py")

SWORD_KILLS, MAGIC_KILLS = 0x800A2DB0, 0x800A2DB4          # u32, since the last gem / scroll
GEMS, SCROLLS, BOTH = 0x800A2EDC, 0x800A2EDE, 0x800A2EE0   # u16
ENEMY = 0x8008B070                                         # object slot 5 in slot 0's first screen
PICKUPS = 0x800A6A98                                       # 32 pickup objects, 0x250 bytes each


def rpc(cmd, **kw): return dvdbg.rpc(4370, cmd, **kw)
def rd(a, n): return bytes.fromhex(rpc("read_ram", addr="0x%08X" % a, len=n)["hex"])
def u16(a): return int.from_bytes(rd(a, 2), "little")
def u32(a): return int.from_bytes(rd(a, 4), "little")
def wr8(a, v): rpc("write_ram", addr="%08X" % a, val=v & 0xFF)
def press(spec): subprocess.run([sys.executable, DV, "press", spec], check=True, capture_output=True)


def state0():
    subprocess.run([sys.executable, DV, "state", "load", "0"], check=True, capture_output=True)
    time.sleep(1.5)


def pickups():
    """(pool slot, frames left, pickup id) for every pickup lying on the floor."""
    p = rd(PICKUPS, 32 * 0x250)
    out = []
    for i in range(32):
        s = p[i * 0x250:(i + 1) * 0x250]
        left = int.from_bytes(s[0x238:0x23C], "little")
        if left:
            out.append((i, left, int.from_bytes(s[0x244:0x248], "little")))
    return out


def status(label):
    e = rd(ENEMY, 0x250)
    print("%s: swordKills=%d magicKills=%d gems=%d scrolls=%d both=%d | enemy HP=%d credit=%d kind=%d"
          " | STR=%d entries=%s | pickups=%s | frame=%s" % (
              label, u32(SWORD_KILLS), u32(MAGIC_KILLS), u16(GEMS), u16(SCROLLS), u16(BOTH),
              int.from_bytes(e[0xCE:0xD0], "little"), int.from_bytes(e[0xC:0x10], "little"), e[0xBC],
              u16(0x8008A5B6), [u16(0x80086324 + 2 * i) for i in range(5)], pickups(), rpc("frame").get("frame")))


def kill_with_sword(max_swings=24):
    for i in range(max_swings):
        press("square:8")
        time.sleep(0.5)
        hp = u16(ENEMY + 0xCE)
        print("  swing %d -> enemy HP %d" % (i + 1, hp))
        if hp == 0:
            break
    time.sleep(2.0)   # death animation, then func_800187D4 spawns the drop


# Run A: guaranteed gem.
state0()
wr8(SWORD_KILLS, 15)
status("A primed (swordKills=15)")
kill_with_sword()
status("A after the kill (expect credit=2, a pickup with id 0)")
for step in range(10):
    press("right:16")
    time.sleep(0.4)
    if not pickups():
        break
time.sleep(1.0)
status("A after walking onto it (expect swordKills=1, gems=1, entries +1,+1,+2,+2,+3)")

# Run B: control, random table of the enemy's drop class.
state0()
status("B start")
kill_with_sword()
status("B after the kill (expect swordKills=1, pickups [] or id 12)")
