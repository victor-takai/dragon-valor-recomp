#!/usr/bin/env python3
"""Check that Clovis (Old)'s charged attack, his double slash, hits twice.

  python tests/test_oldclovis_slash.py [--build build-debug] [--port N] [--stage A/S] [--tries N]

The slash is the boss's anim 21 (two cuts: frames 0..5 and 12..22, the
hero's slot 192) and 22 (recovery, slot 193). Played whole it hit once: a hit
gives the enemy hitCooldown and blink = what is left of the hero's animation
+ 3 frames (CollisionUpdate), so the second cut fell inside them, and the
swing's later hits did 1 damage (swingHits). Since 2026-11-25 the block plays
21 in two parts (heroswap.py SLASH_TEXT): up to frame 11 (animEnd written),
the hit off, then from frame 11 on, so each cut hits with full damage.

The test writes its own <build>/mods/state.toml (Play as Clovis (Old), the
cheats package with Infinite HP, Moves on Clovis (Old): the menu sets it with
him; the player's file is backed up and
restored, as are settings.toml and dragonvalor_settings.toml), launches the
build muted with a hidden window from test state 10 (1/7, never slot 0) and
warps to --stage (default 1/9) with Warp > Go now. For each try it takes
the nearest living enemy, gives it 200 HP, puts it 700 units from the hero,
holds Square for 1.5 s (the charge), releases it and reads the enemy's HP
about every 15 ms while the slash plays (the hero's anims 192, 193).
PASS when a try shows two HP drops during anim 192, each more than 1.
Only the runtime this test launched is stopped.
"""
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from dvdbg import BUTTONS, rpc, port_from_args  # noqa: E402

DV = str(ROOT / "tools" / "dvdbg.py")
OBJ, SIZE = 0x8008A4E0, 0x250
FLAGS, POS, ANIM, FRAME, HP, HPMAX = 0x1C, 0x20, 0x9E, 0xA0, 0xCE, 0xD0
MODE, AREA, STAGE, RECORD = 0x8008634C, 0x800A2F18, 0x800A2F1A, 0x8007D9C6
AREA_PHASE, STAGE_PHASE, EVENT_RUNNING = 0x800A2D14, 0x800A2D18, 0x8009F9F8
SECTIONS = ["Game", "Hero", "Cheats", "Magic", "Items", "Warp", "Video", "Options"]
STATE = """format_version = 2

[[package]]
id = "dragonvalor.cheats"
version = "1.4.0"

[[package]]
id = "dragonvalor.heroswap"
version = "2.0.0"

[[package]]
id = "dragonvalor.skip-intro"
version = "1.0.0"

[[feature]]
package_id = "dragonvalor.cheats"
id = "cheats"
enabled = true
[feature.values]
infinite_hp = "true"

[[feature]]
package_id = "dragonvalor.heroswap"
id = "play-as"
enabled = true
[feature.values]
hero = "oldclovis"
moves = "oldclovis"

[[feature]]
package_id = "dragonvalor.skip-intro"
id = "skip-intro"
enabled = true
"""


def main(argv):
    args = list(argv)
    port = port_from_args(args)
    build, target, tries = "build-debug", (1, 9), 4
    for flag in ("--build", "--stage", "--tries"):
        if flag in args:
            i = args.index(flag)
            v = args[i + 1]
            del args[i:i + 2]
            if flag == "--build":
                build = v
            elif flag == "--stage":
                target = tuple(int(x) for x in v.split("/"))
            else:
                tries = int(v)
    bdir = ROOT / build
    state = bdir / "mods" / "state.toml"
    kept = {f: f.read_bytes() for f in (state, bdir / "settings.toml", bdir / "dragonvalor_settings.toml") if f.is_file()}

    def cli(*a, timeout=120):
        r = subprocess.run([sys.executable, DV, *a, "--port", str(port)], capture_output=True, text=True, timeout=timeout)
        return (r.stdout + r.stderr).strip()

    ok = False
    try:
        state.write_text(STATE, encoding="utf-8")
        print("  launch:", cli("launch", "--state", "10", "--build", str(bdir)).splitlines()[-1])
        time.sleep(8)
        try:
            ok = run(port, target, tries)
        finally:
            print("  kill:", cli("kill"))
            time.sleep(2)
    finally:
        for f, b in kept.items():
            f.write_bytes(b)
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


def run(port, target, tries):
    def rd(a, n=2, signed=False):
        v = int.from_bytes(bytes.fromhex(rpc(port, "read_ram", addr=f"{a:08X}", len=n)["hex"]), "little")
        return v - (1 << 8 * n) if signed and v >> (8 * n - 1) else v

    def wr(a, v, n=2):
        for k in range(n):
            rpc(port, "write_ram", addr=f"{a + k:08X}", val=(v >> 8 * k) & 0xFF)

    def key(k, n=1):
        for _ in range(n):
            rpc(port, "host_key", name=k)
            time.sleep(0.3)

    def step_row(addr, want, cap):
        for _ in range(cap):
            v = rd(addr)
            if v == want:
                return True
            key("Right" if want > v else "Left")
        return rd(addr) == want

    # Test state 10 was saved with the stage's own hero, and a loaded save's
    # hero becomes the choice; so Clovis (Old) is picked again on Hero > Play as
    # (choice 13 of kPlayAsChoices: each stage's own, the nine heroes, the two
    # looks, the Dragon Valor, him; Moves follows him), and the stage restarts.
    key("Escape"); key("Down"); key("Return")
    key("Right", 13)
    key("Return"); key("Return"); time.sleep(1.0)    # Apply, then the dialog's Restart the stage
    t0 = time.time()
    while time.time() - t0 < 40 and not (rd(MODE, 1) == 8 and not rd(STAGE_PHASE, 4) and time.time() - t0 > 4):
        time.sleep(0.5)
    time.sleep(2)
    # Warp > Go now to the target (the hero's kit is applied as the stage loads)
    key("Escape"); time.sleep(0.5)
    key("Down", SECTIONS.index("Warp")); key("Return")
    if not step_row(AREA, target[0], 14):
        print("  the Area row does not reach", target); return False
    key("Down")
    if not step_row(RECORD, target[1], 60):
        print("  the Stage row does not reach", target); return False
    key("Down"); key("Return"); time.sleep(0.4); key("Return")
    t0 = time.time()
    while time.time() - t0 < 60:
        if (rd(AREA), rd(STAGE)) == target and rd(MODE, 1) == 8 and not rd(AREA_PHASE, 4) and not rd(STAGE_PHASE, 4):
            break
        time.sleep(0.2)
    time.sleep(3)
    while rd(EVENT_RUNNING, 4) and time.time() - t0 < 90:
        rpc(port, "press", buttons=0xFFFF & ~BUTTONS["cross"], frames=6); time.sleep(1.0)
    print(f"  in {rd(AREA)}/{rd(STAGE)}, hero anims {rd(OBJ + 0x9C, 2)} (Clovis (Old)'s kit: 200)")

    hero = OBJ
    good = False
    t0 = time.time()                 # the stage's enemy group comes a moment after the stage
    while time.time() - t0 < 15 and not any(
            rd(OBJ + j * SIZE + FLAGS, 1) & 3 == 3 and 0 < rd(OBJ + j * SIZE + 0x9C, 2) < 200 for j in range(5, 41)):
        time.sleep(0.5)
    for t in range(tries):
        foes = []
        hx, hy, hz = (rd(hero + POS + 4 * k, 4, True) for k in range(3))
        for j in range(5, 41):      # the stage's enemies (41.. are the room's other objects)
            o = OBJ + j * SIZE
            if rd(o + FLAGS, 1) & 3 == 3 and 0 < rd(o + 0x9C, 2) < 200:   # a model with animations, not a hero's
                x, z = rd(o + POS, 4, True), rd(o + POS + 8, 4, True)
                foes.append(((x - hx) ** 2 + (z - hz) ** 2, j))
        if not foes:
            print("  no enemy left"); break
        j = min(foes)[1]
        o = OBJ + j * SIZE
        # Brought next to the hero; a dormant enemy wakes (its script sets its HP)
        # as the hero comes near, so its HP is written after that.
        wr(o + POS, hx + 700, 4); wr(o + POS + 4, hy, 4); wr(o + POS + 8, hz, 4)
        time.sleep(1.5)
        wr(o + POS, hx + 700, 4); wr(o + POS + 4, hy, 4); wr(o + POS + 8, hz, 4)
        wr(o + HP, 200); wr(o + HPMAX, 200)
        time.sleep(0.2)
        rpc(port, "set_input", layer="host", buttons=f"0x{0xFFFF & ~BUTTONS['square']:04X}")
        time.sleep(1.5)
        rpc(port, "clear_input")
        rows, t1 = [], time.time()
        while time.time() - t1 < 2.5:
            rows.append((rd(hero + ANIM, 1), rd(hero + FRAME, 2, True), rd(o + HP, 2, True)))
            time.sleep(0.015)
        drops, last = [], 200
        for anim, frame, hp in rows:
            if hp < last:
                drops.append((anim, frame, last - hp))
            last = hp
        in_slash = [d for d in drops if d[0] == 192]
        anims = sorted({r[0] for r in rows})
        print(f"  try {t + 1}: enemy {j}, anims {anims}, HP drops (anim, frame, damage): {drops}")
        if len(in_slash) >= 2 and all(d[2] > 1 for d in in_slash[:2]):
            good = True
            break
        time.sleep(1.0)
    print(f"  {'ok  ' if good else 'FAIL'} two full hits during the slash (anim 192)")
    return good


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
