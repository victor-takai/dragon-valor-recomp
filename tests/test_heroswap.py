#!/usr/bin/env python3
"""Check dragonvalor.heroswap ("Play as") on the debug build:

  python tests/test_heroswap.py HERO [--ref FOLDER] [--state 10|9] [--leave A/S] [--prime-disc2]
                                     [--build build-debug] [--port N] [--play SECONDS] [--weapon HERO2]

--state 9 (Kodel, 3/1) with --leave 3/3 tests Clovis, who is 1/7's own hero.
--scene-shots N, with --leave into a stage that opens with a story scene
(3/1), takes N screenshots of it (test_heroswap_scene_NN.png), pressing
Cross between them: the portraits and the names in its dialogues.
--menu-weapon W then picks weapon model W (or `hero`) on the menu's Hero >
Weapon row and confirms it in the dialog: the stage must restart with it.
--weapon W sets the feature's `weapon` option: HERO holds weapon model W
(azos soul kadum igunis felippe dark red purple turquoise raimun jeanne
cutlass; the reference
then comes from `heroswap.py build ... --weapon A S` with W's source stage,
e.g. `build 2 1 1 7 --prune --weapon 8 1` for Arlen with Kadum).
--menu HERO2[,HERO3...|stage] then picks each on the in-game menu's Hero > Play as
row and confirms it in the dialog (Enter, then Enter on "Restart the
stage"): the stage must restart with HERO2 as the menu closes, with no GAME
OVER and Val / HP kept; the rest of the run continues with HERO2.

--moves HERO2 sets the `moves` option: the bodies wear HERO2's kit (its
moves and voice; g_Hero.index HERO2's); the reference then dresses HERO2's
kit stage, e.g. `heroswap.py build 7 1 1 7 --prune --body shalya` for
Shalya with Anna's moves.

HERO is one of clovis arlen kodel felippe phillip gerome anna mihael anita,
a body (dragonvalor oldclovis skeleton raimun lodonya shalya fannah jeanne
felippeshield guardian: another character in the prologue Clovis's kit,
g_Hero.index stays Clovis's 0; reference `heroswap.py build 0 1 1 7 --prune
--body NAME`, `felippe41` for felippeshield, Fannah's with `--weapon 3 1 80 0`),
or `default`: a state.toml that does not name the package, as on a fresh
install, so the feature runs as its manifest ships it (on, each stage's own
hero, the hero's own weapon). The stage must then load untouched (no
rebuild, the game's hero index), and --menu / --menu-weapon must still
restart it at once (the player's 2026-11-18 report: with the feature off,
the first menu choice only applied after restarting the game).
The test writes its own <build>/mods/state.toml (the feature on with that
hero, no cheats; the player's file is backed up and restored, as are
settings.toml and dragonvalor_settings.toml), launches the build muted with a
hidden window, loads test state 10 (area 1 / stage 7, Clovis; never slot 0)
and reloads the stage through GAME OVER > Cross on CONTINUE. Then:

  - the plugin found its kits (read from DATA.CDR or from its cache in
    <build>/mods/cache/dragonvalor.heroswap) and rebuilt 1/7's file 0 and
    action pack for HERO;
  - g_Hero.index (0x80086318) is HERO's;
  - with --ref, a folder written by the decomp's tools/heroswap.py from the
    plugin's kit stage (e.g. `heroswap.py build 3 1 1 7 --prune` for Kodel:
    Clovis 1/1, Arlen 2/1, ... Anita 9/1): the action pack in RAM starts
    with the reference pack's container head (the same entries and sizes),
    and VRAM holds the reference file 0's last hero-page TIM and portrait
    (vram_peek), byte for byte: the C rebuild agrees with the Python one;
  - Triangle opens the Status screen (mode 7; screenshot for the portrait
    and the name);
  - the stage runs through --play seconds of walking and attacking with no
    unknown dispatch or segment miss;
  - a second Continue into 1/9 loads it with HERO too (the choice holds for
    every stage) and the index stays HERO's.

Screenshots go to <build>/test_heroswap_*.png. The game is killed by its PID
only. Exit code 0 on PASS. Heroes of disc 2 (anna, mihael, anita) need their
kit in the cache: start disc 2 once with the feature on (--prime-disc2 does
that first, with no save state).
"""
import re
import struct
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from dvdbg import rpc, port_from_args  # noqa: E402

DV = str(ROOT / "tools" / "dvdbg.py")
AREA, STAGE, RECORD = 0x800A2F18, 0x800A2F1A, 0x8007D9C6
HP, MODE, VAL = 0x8008A5AE, 0x8008634C, 0x800A2DD0
PACK = 0x800C5E98          # row 0 of ACTION: the action pack while a stage runs
HERO_INDEX = 0x80086318    # g_Hero.index
HERO_PAGE = (384, 0, 64, 256)
PORTRAIT = (960, 432, 40, 64)
HEROES = ["clovis", "arlen", "kodel", "felippe", "phillip", "gerome", "anna", "mihael", "anita", "dragonvalor",
          "oldclovis", "skeleton", "raimun", "lodonya", "shalya", "fannah", "jeanne", "felippeshield", "guardian"]
BODIES = HEROES[9:]       # characters on the prologue Clovis's kit (g_Hero.index 0)


def hname(value):
    """The plugin's name for a hero option value (its log lines, the menu)."""
    return {"dragonvalor": "Dragon Valor", "oldclovis": "Old Clovis",
            "felippeshield": "Felippe (shield)"}.get(value, value.capitalize())


MOVES = None              # --moves: the hero whose kit the bodies wear (None: the prologue Clovis's)


def gindex(hero):
    """g_Hero.index for a HEROES index: a body plays its moves' hero's part
    (Clovis's 0 for the prologue Clovis's)."""
    if hero >= 0 and HEROES[hero] in BODIES:
        return HEROES.index(MOVES) if MOVES else 0
    return hero


def letters(glyphs):
    """Text of name glyph codes (decomp docs/FORMATS.md, Text: the low ten
    bits index the glyph, the rest its advance)."""
    out = ""
    for w in glyphs:
        if w in (0x1800, 0x3000):
            out += " "
            continue
        i = (w & 31) * 21 + ((w >> 5) & 31)
        out += chr(ord("A") + i - 43) if 43 <= i <= 68 else chr(ord("a") + i - 69) if 69 <= i <= 94 else "#"
    return out
# the `weapon` option's values, in the menu row's order (kWeapons of the plugin
# and their source stages: 1/1 4/15 8/1 10/2 4/3 10/1 11/1 12/6 12/1)
WEAPONS = ["hero", "azos", "soul", "kadum", "igunis", "felippe", "dark", "red", "purple", "turquoise",
           "raimun", "jeanne", "cutlass"]
DISC2 = ROOT.parent / "Dragon Valor (USA) (Disc 2)" / "Dragon Valor (USA) (Disc 2).cue"


def container(b):
    n = struct.unpack_from("<I", b)[0]
    offs = struct.unpack_from(f"<{n + 1}I", b, 4)
    return [b[offs[i]:offs[i + 1]] for i in range(n)]


def tims(b):
    """(clut rect, clut data, image rect, image data) of the TIM_CODE chain at b's head."""
    pos, out = 0, []
    while b[pos:pos + 8] == b"TIM_CODE":
        flags = struct.unpack_from("<I", b, pos + 0xC)[0]
        blocks, p = [], pos + 0x10
        for _ in range(1 if flags == 2 else 2):
            bn, x, y, w, h = struct.unpack_from("<I4h", b, p)
            blocks.append(((x, y, w, h), b[p + 12:p + 12 + 2 * w * h]))
            p += bn
        clut = blocks[0] if len(blocks) == 2 else (None, None)
        out.append((clut[0], clut[1], blocks[-1][0], blocks[-1][1]))
        pos = p
    return out


def overlaps(a, b):
    return a[0] < b[0] + b[2] and b[0] < a[0] + a[2] and a[1] < b[1] + b[3] and b[1] < a[1] + a[3]


def main(argv):
    args = list(argv)
    port = port_from_args(args)
    build, play, ref, prime = "build-debug", 20.0, None, "--prime-disc2" in args
    slot, leave, menu_hero = 10, (1, 9), None
    if prime:
        args.remove("--prime-disc2")
    global MOVES
    weapon = menu_weapon = None
    if "--moves" in args:
        i = args.index("--moves")
        MOVES = args[i + 1]
        del args[i:i + 2]
    scene_shots = 0
    if "--scene-shots" in args:
        i = args.index("--scene-shots")
        scene_shots = int(args[i + 1])
        del args[i:i + 2]
    for flag in ("--build", "--play", "--ref", "--state", "--leave", "--menu", "--weapon", "--menu-weapon"):
        if flag in args:
            i = args.index(flag)
            if flag == "--weapon":
                weapon = args[i + 1]
            elif flag == "--menu-weapon":
                menu_weapon = args[i + 1]
            elif flag == "--build":
                build = args[i + 1]
            elif flag == "--play":
                play = float(args[i + 1])
            elif flag == "--state":
                slot = int(args[i + 1])
            elif flag == "--leave":
                leave = tuple(int(x) for x in args[i + 1].split("/"))
            elif flag == "--menu":
                menu_hero = args[i + 1]
            else:
                ref = Path(args[i + 1]).resolve()
            del args[i:i + 2]
    if slot == 0:
        sys.exit("slot 0 is the player's quick save: use the test states 9 or 10")
    if len(args) != 1 or args[0] not in HEROES + ["default"]:
        sys.exit(__doc__)
    hero_name = args[0]
    default = hero_name == "default"
    hero = -1 if default else HEROES.index(hero_name)
    if default and ref:
        sys.exit("--ref compares a swapped stage: not with `default`")

    expect_head, expect_vram = None, {}
    if ref:
        spec = (ref / "heroswap.txt").read_text(encoding="utf-8")
        files = {int(n): name for n, name in re.findall(r"^file (\d+) (\S+) ", spec, re.M)}
        pack = (ref / files[1]).read_bytes()
        expect_head = pack[:4 * (int.from_bytes(pack[:4], "little") + 2)]
        chain = [t for e in container((ref / files[0]).read_bytes()) for t in tims(e)]
        page = [t for t in chain if any(r and overlaps(r, HERO_PAGE) for r in (t[0], t[2]))]
        portrait = [t for t in chain if t[2] == PORTRAIT]
        for name, found in (("hero texture", page), ("portrait", portrait)):
            if found:
                crect, cdata, irect, idata = found[-1]
                expect_vram[f"{name} image {irect}"] = (irect, idata)
                if crect:
                    expect_vram[f"{name} CLUT {crect}"] = (crect, cdata)

    bdir = ROOT / build
    state = bdir / "mods" / "state.toml"
    guarded = [state, bdir / "settings.toml", bdir / "dragonvalor_settings.toml"]
    saved = {p: p.read_bytes() for p in guarded if p.is_file()}
    state_text = f"""format_version = 2

[[package]]
id = "dragonvalor.heroswap"
version = "2.0.0"

[[feature]]
package_id = "dragonvalor.heroswap"
id = "play-as"
enabled = true
[feature.values]
hero = "{hero_name}"
""" + (f'weapon = "{weapon}"\n' if weapon else "") + (f'moves = "{MOVES}"\n' if MOVES else "")
    if default:
        state_text = "format_version = 2\n"
    results, pid = [], None

    def check(label, ok, detail=""):
        results.append(bool(ok))
        print(f"  {'ok  ' if ok else 'FAIL'} {label}{(': ' + detail) if detail else ''}")

    def cli(*a, timeout=180):
        r = subprocess.run([sys.executable, DV, *a, "--port", str(port)], capture_output=True, text=True, timeout=timeout)
        return (r.stdout + r.stderr).strip()

    def rd(a, n):
        return bytes.fromhex(rpc(port, "read_ram", addr=f"{a:08X}", len=n)["hex"])

    def rd16(a):
        return int.from_bytes(rd(a, 2), "little")

    def wr16(a, v):
        rpc(port, "write_ram", addr=f"{a:08X}", val=v & 0xFF)
        rpc(port, "write_ram", addr=f"{a + 1:08X}", val=(v >> 8) & 0xFF)

    def frame():
        return rpc(port, "frame").get("frame", 0)

    def launch(*extra):
        out = cli("launch", "--build", str(bdir), *extra)
        mm = re.search(r"pid=(\d+)", out)
        print("launch:", out.splitlines()[0] if out else "?")
        return mm.group(1) if mm else None

    def kill(p):
        if p:
            subprocess.run(["taskkill", "/F", "/PID", p], capture_output=True)
            time.sleep(1.5)

    def log_text():
        return log.read_text(encoding="utf-8", errors="replace")

    def reload_into(area, stage):
        """GAME OVER > CONTINUE into area / stage (the Continue reloads the stage record)."""
        if (rd16(AREA), rd16(STAGE)) != (area, stage):
            wr16(AREA, area)
            wr16(RECORD, stage)
        wr16(VAL, max(rd16(VAL), 500))      # the Continue costs 50 Val
        wr16(HP, 0)
        t0 = time.time()
        while time.time() - t0 < 15 and rd(MODE, 1)[0] == 8:
            time.sleep(0.5)
        # the prompt ignores the pad until it is up (its state 2): one press
        # after a fixed wait was missed now and then, so Cross again and
        # again while the GAME OVER (mode 9) lasts
        t0 = time.time()
        while time.time() - t0 < 40 and rd(MODE, 1)[0] == 9:
            cli("press", "cross")
            time.sleep(0.7)
        t0 = time.time()
        while time.time() - t0 < 60 and not (rd(MODE, 1)[0] == 8 and time.time() - t0 > 4):
            time.sleep(0.5)
        time.sleep(4)   # the stage fades in
        return (rd16(AREA), rd16(STAGE)) == (area, stage) and rd(MODE, 1)[0] == 8

    log = bdir / f"dvdbg_{port}.log"
    try:
        out = subprocess.run(["tasklist"], capture_output=True, text=True).stdout
        if "DragonValorRecompiled" in out:
            sys.exit("the game is already running: not launching a second instance")
        state.parent.mkdir(parents=True, exist_ok=True)
        state.write_text(state_text, encoding="utf-8")
        if prime:
            pid = launch("--disc", str(DISC2))
            t0 = time.time()
            while time.time() - t0 < 60 and "disc 2 " not in log_text():
                time.sleep(1)
            line = next((l for l in log_text().splitlines() if "dragonvalor.heroswap: disc 2" in l), "no disc 2 line")
            check("disc 2 primed (its kits in the cache)", "disc 2" in line, line)
            kill(pid)
            pid = None

        pid = launch()
        time.sleep(3)
        t0 = time.time()
        while time.time() - t0 < 60 and "play as" not in log_text():
            time.sleep(1)
        lines = [l for l in log_text().splitlines() if "dragonvalor.heroswap" in l]
        shown = "each stage's hero" if default else hname(hero_name)
        check("plugin active, play as " + shown,
              any(f"play as {shown}" in l for l in lines), " | ".join(lines))
        print(f"state {slot}:", cli("state", "load", str(slot)).splitlines()[-1])
        time.sleep(4)
        # test state 10 was made as Clovis (9 as Kodel): with another hero or a
        # weapon chosen, the save's choice becomes the current one (no dialog:
        # the game keeps running; a notice says so; mods/state.toml follows),
        # so the test picks its hero / weapon again in the in-game menu's Hero
        # rows, which restarts the stage with them (the player's 2026-11-18
        # wish: no question at every load)
        own = "clovis" if slot == 10 else "kodel"
        expect = not default and (hero_name != own or bool(weapon))
        f0 = frame(); time.sleep(0.8)
        check("the load asks nothing (the game runs)", frame() != f0, "")
        index_before = rd(HERO_INDEX, 1)[0]     # the save's own hero, before any re-pick
        if expect:
            t0, adopted = time.time(), ""
            while time.time() - t0 < 8:
                adopted = state.read_text(encoding="utf-8")
                if f'hero = "{hero_name}"' not in adopted:
                    break
                time.sleep(0.5)
            check("the save's hero became the choice (state.toml)", f'hero = "{hero_name}"' not in adopted,
                  " ".join(l for l in adopted.splitlines() if l.startswith(("hero", "weapon"))))

            def mkey(name, n=1):
                for _ in range(n):
                    rpc(port, "host_key", name=name)
                    time.sleep(0.35)
            restarts0 = log_text().count("restarts as")
            mkey("Escape"); mkey("Down"); mkey("Return")          # Hero: Play as (each stage's own now)
            mkey("Right", hero + 1)
            if weapon:
                mkey("Down"); mkey("Right", WEAPONS.index(weapon))
            mkey("Return"); mkey("Return")                        # the dialog: Restart the stage
            t0 = time.time()
            while time.time() - t0 < 40 and not (log_text().count("restarts as") > restarts0
                                                 and rd(MODE, 1)[0] == 8):
                time.sleep(0.5)
            time.sleep(4)
            check(f"picked {hero_name} again in the menu: the stage restarted",
                  log_text().count("restarts as") > restarts0, "")
        target = (rd16(AREA), rd16(STAGE))
        ts = f"{target[0]}/{target[1]}"
        print(f"  state {slot} is {ts}, mode {rd(MODE, 1)[0]}, hero index {index_before}")
        rpc(port, "screenshot", path=str(bdir / "test_heroswap_before.png"))

        ok = reload_into(*target)
        check(f"{ts} is running again", ok, f"{rd16(AREA)}/{rd16(STAGE)} mode {rd(MODE, 1)[0]}")
        text = log_text()
        if default:
            # the shipped defaults change nothing: no layout, no rebuild, the
            # game's own hero index
            check(f"{ts} loaded untouched (no rebuild)",
                  f"stage {ts} as " not in text and "rebuilt" not in text,
                  next((l for l in text.splitlines() if "rebuilt" in l or f"stage {ts} as " in l), ""))
            check(f"g_Hero.index = the game's ({index_before})", rd(HERO_INDEX, 1)[0] == index_before,
                  str(rd(HERO_INDEX, 1)[0]))
            leave = None      # the leave check below is about a chosen hero
        else:
            check(f"{ts} laid out for {hname(hero_name)}", f"stage {ts} as {hname(hero_name)}" in text)
        if not default:
            if weapon:
                check(f"{hname(hero_name)} holds weapon {weapon}",
                      f"stage {ts} as {hname(hero_name)} with weapon {weapon}:" in text)
            check("file 0 rebuilt", f"stage {ts} file 0 rebuilt" in text)
            check("action pack rebuilt", f"stage {ts} pack rebuilt" in text)
            check(f"g_Hero.index = {gindex(hero)}", rd(HERO_INDEX, 1)[0] == gindex(hero), str(rd(HERO_INDEX, 1)[0]))
            # the Status screen's name: g_HeroNames[g_Hero.index] (a body's own
            # name, but the skeleton's, which has none: its moves' hero's)
            gi = rd(HERO_INDEX, 1)[0]
            ptr = int.from_bytes(rd(0x800C5E14 + 4 * gi, 4), "little")
            raw = rd(ptr, 32) if 0x800BBF04 <= ptr < 0x800BBF84 else b""
            glyphs = []
            for k in range(0, len(raw), 2):
                g = int.from_bytes(raw[k:k + 2], "little")
                if g == 0x8000:
                    break
                glyphs.append(g)
            want_name = {"dragonvalor": "Dragon Valor", "oldclovis": "Clovis", "felippeshield": "Felippe",
                         "skeleton": hname(MOVES or "clovis")}.get(hero_name, hname(hero_name))
            check(f"the Status screen's name is {want_name}", letters(glyphs) == want_name, letters(glyphs))
        if expect_head:
            check("pack in RAM has the reference head (heroswap.py)", rd(PACK, len(expect_head)) == expect_head)
        for label, (rect, data) in expect_vram.items():
            x, y, w, h = rect
            rows = []
            for row in range(y, y + h, 128):
                hh = min(128, y + h - row)
                parts = []
                for col in range(x, x + w, 128):
                    ww = min(128, x + w - col)
                    r = rpc(port, "vram_peek", x=col, y=row, w=ww, h=hh)["hex"]
                    parts.append([bytes.fromhex(r[i * ww * 4:(i + 1) * ww * 4]) for i in range(hh)])
                for i in range(hh):
                    rows.append(b"".join(p[i] for p in parts))
            # vram_peek prints each pixel as big-endian hex; the TIM stores little-endian
            got = b"".join(bytes(v for k in range(0, len(r), 2) for v in (r[k + 1], r[k])) for r in rows)
            same = sum(a == b for a, b in zip(got, data))
            check(f"VRAM {label} = reference file 0's", got == data, f"{same} of {len(data)} bytes")
        rpc(port, "screenshot", path=str(bdir / "test_heroswap_after.png"))

        # a press while the stage still settles can be lost: Triangle again
        # until the Status screen is up, then until play is back
        for _ in range(4):
            cli("press", "triangle")
            time.sleep(4)
            if rd(MODE, 1)[0] == 7:
                break
        rpc(port, "screenshot", path=str(bdir / "test_heroswap_status.png"))
        check("Triangle opened the Status screen (mode 7)", rd(MODE, 1)[0] == 7, f"mode {rd(MODE, 1)[0]}")
        for _ in range(4):
            cli("press", "triangle")
            time.sleep(3)
            if rd(MODE, 1)[0] == 8:
                break

        f0, t0, k = frame(), time.time(), 0
        moves = ["square", "left:20", "square", "right:30", "up:20", "square:4", "down:20", "circle"]
        while time.time() - t0 < play:
            cli("press", moves[k % len(moves)])
            k += 1
            if k == 4:
                rpc(port, "screenshot", path=str(bdir / "test_heroswap_play.png"))
            time.sleep(0.5)
        f1 = frame()
        check(f"still running after {play:.0f} s of play", rd(MODE, 1)[0] in (8, 9) and f1 > f0 + 30 * play,
              f"mode {rd(MODE, 1)[0]}, {f1 - f0} frames")

        if menu_hero:
            # The in-game menu's Hero section (2nd) > Play as (its first row;
            # values 0 = each stage's own, 1.. the heroes): Enter asks, Enter
            # on the dialog's focused "Restart the stage" applies and closes
            # the menu; the stage restarts with no GAME OVER (mode 9 never
            # seen) and Val / HP kept. "stage" brings back the game's own hero
            # index, never one the plugin wrote.
            def key(name, n=1):
                for _ in range(n):
                    rpc(port, "host_key", name=name)
                    time.sleep(0.35)
            row_value = hero + 1
            for pick in menu_hero.split(","):
                time.sleep(2)
                val0, hp0, restarts0 = rd16(VAL), rd16(HP), log_text().count("restarts as")
                target_value = 0 if pick == "stage" else HEROES.index(pick) + 1
                steps = target_value - row_value
                key("Escape"); time.sleep(0.4)
                key("Down"); key("Return")
                key("Right" if steps > 0 else "Left", abs(steps))
                key("Return"); key("Return"); time.sleep(0.5)
                row_value = target_value
                modes, t0, seen17 = set(), time.time(), False
                while time.time() - t0 < 40:
                    m = rd(MODE, 1)[0]
                    modes.add(m)
                    seen17 |= m == 17
                    if seen17 and m == 8 and log_text().count("restarts as") > restarts0:
                        break
                    time.sleep(0.3)
                time.sleep(4)
                text = log_text()
                label = "its own hero" if pick == "stage" else hname(pick)
                want = index_before if pick == "stage" else gindex(HEROES.index(pick))
                check(f"menu -> {pick}: {ts} restarted as {label}", f"restarts as {label}" in text and seen17,
                      next((l for l in reversed(text.splitlines()) if "restarts as" in l), "no restart line"))
                check(f"menu -> {pick}: no GAME OVER on the way", 9 not in modes, f"modes seen {sorted(modes)}")
                check(f"menu -> {pick}: g_Hero.index = {want}", rd(HERO_INDEX, 1)[0] == want, str(rd(HERO_INDEX, 1)[0]))
                check(f"menu -> {pick}: Val and HP kept", (rd16(VAL), rd16(HP)) == (val0, hp0),
                      f"Val {val0} -> {rd16(VAL)}, HP {hp0} -> {rd16(HP)}")
                check(f"menu -> {pick}: back in play (mode 8)", rd(MODE, 1)[0] == 8, f"mode {rd(MODE, 1)[0]}")
                rpc(port, "screenshot", path=str(bdir / f"test_heroswap_menu_{pick}.png"))
            last = menu_hero.split(",")[-1]
            if last == "stage":
                leave = None          # the leave check below is about a chosen hero
                hero = -1             # each stage's own: Weapon below names the stage's hero
            else:
                hero = HEROES.index(last)
                hero_name = last

        if menu_weapon:
            # Hero > Weapon, the second row (0 = the hero's own, 1.. the
            # weapon models): confirmed in the dialog like Play as.
            def wkey(name, n=1):
                for _ in range(n):
                    rpc(port, "host_key", name=name)
                    time.sleep(0.35)
            values = WEAPONS
            steps = values.index(menu_weapon) - values.index(weapon or "hero")
            time.sleep(2)
            restarts0 = log_text().count("restarts as")
            wkey("Escape"); time.sleep(0.4)
            wkey("Down"); wkey("Return")
            wkey("Down")
            wkey("Right" if steps > 0 else "Left", abs(steps))
            wkey("Return"); wkey("Return"); time.sleep(0.5)
            t0 = time.time()
            while time.time() - t0 < 40:
                if rd(MODE, 1)[0] == 8 and log_text().count("restarts as") > restarts0:
                    break
                time.sleep(0.3)
            time.sleep(4)
            tail = log_text().split("restarts as")[-1]
            who = hname(hero_name if hero >= 0 else HEROES[index_before])   # `default`: the stage's own
            want = (f"stage {ts} as {who} with weapon {menu_weapon}:"
                    if menu_weapon != "hero" else f"stage {ts} as {who}:")
            check(f"menu Weapon -> {menu_weapon}: {ts} restarted with it", want in tail,
                  next((l for l in tail.splitlines() if f"stage {ts} as" in l), "no stage line after the restart"))
            check(f"menu Weapon -> {menu_weapon}: back in play (mode 8)", rd(MODE, 1)[0] == 8, f"mode {rd(MODE, 1)[0]}")
            rpc(port, "screenshot", path=str(bdir / f"test_heroswap_weapon_{menu_weapon}.png"))
            weapon = None if menu_weapon == "hero" else menu_weapon

        if leave:
            ok = reload_into(*leave)
            text = log_text()
            ls = f"{leave[0]}/{leave[1]}"
            check(f"{ls} is running", ok, f"{rd16(AREA)}/{rd16(STAGE)} mode {rd(MODE, 1)[0]}")
            check(f"{ls} rebuilt for {hname(hero_name)} too",
                  f"stage {ls} as {hname(hero_name)}" in text and f"stage {ls} pack rebuilt" in text)
            check(f"g_Hero.index still {gindex(hero)}", rd(HERO_INDEX, 1)[0] == gindex(hero), str(rd(HERO_INDEX, 1)[0]))
            rpc(port, "screenshot", path=str(bdir / "test_heroswap_left.png"))
            for k in range(scene_shots):
                # a story scene that opens the stage: its dialogues, Cross between shots
                time.sleep(1.2)
                rpc(port, "screenshot", path=str(bdir / f"test_heroswap_scene_{k:02}.png"))
                cli("press", "cross")
        h = cli("health")
        print("  health:", h.replace("\n", "; "))
        nums = [int(x) for x in re.findall(r"total=(\d+)", h)]
        check("no unknown dispatch / segment miss", nums and not any(nums), h.replace("\n", "; "))
        print("  plugin log:")
        for line in log_text().splitlines():
            if "dragonvalor.heroswap" in line:
                print("    " + line)
    finally:
        kill(pid)
        for p, b in saved.items():
            p.write_bytes(b)
        if state not in saved:
            state.unlink(missing_ok=True)
        print("player files restored:", all(p.read_bytes() == b for p, b in saved.items()))
    ok = all(results)
    print("RESULT:", "PASS" if ok else "FAIL", f"({sum(results)}/{len(results)})")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
