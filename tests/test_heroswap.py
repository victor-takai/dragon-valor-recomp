#!/usr/bin/env python3
"""Check dragonvalor.heroswap on the debug build with a folder written by the
decomp's tools/heroswap.py (github victor-takai/dragon-valor-decomp):

  python tests/test_heroswap.py FOLDER [--build build-debug] [--port N] [--play SECONDS]

e.g. FOLDER = F:/dragon-valor-decomp/build/heroswap/3-1_to_1-7 (Kodel into
1/7, `heroswap.py build 3 1 1 7 --prune`). The test writes its own
<build>/mods/state.toml (the package on, FOLDER as its resource, no cheats;
the player's file is backed up and restored, as are settings.toml and
dragonvalor_settings.toml), launches the build muted with a hidden window,
loads test state 10 (area 1 / stage 7, Clovis; never slot 0) and, when
heroswap.txt targets another stage, writes that stage as the next Continue's
(area 0x800A2F18, stage record 0x8007D9C6). Then HP 0 -> GAME OVER ->
Cross on CONTINUE reloads the stage, and:

  - the runtime log shows the plugin loaded the folder and replaced every
    file heroswap.txt lists that the reload reads (files 0 and 1);
  - the action pack in RAM (0x800C5E98) starts with the rebuilt pack's
    container head (its offset table is not relocated by the install);
  - every layout size heroswap.txt patches holds its value in RAM;
  - the hero object's behaviour script (object 0 +0x000) points at the
    rebuilt pack's entry 2;
  - g_Hero.index (0x80086318) is heroswap.txt's `hero` value, when it has one;
  - VRAM holds the rebuilt file 0's last hero-page TIM (image and CLUT at
    (384..447, 0..255)) and its portrait (image (960, 432) 40 x 64, CLUT row
    (0, 500)), read back with the debug server's vram_peek;
  - Triangle opens the Status screen (mode 7; screenshot
    test_heroswap_status.png, for the portrait and the name);
  - the stage runs (mode 8, frames advancing) through --play seconds of
    walking and attacking, with no unknown dispatch or segment miss;
  - leaving for another stage (1/9, or 1/7 when 1/9 is the target) through
    a second Continue: nothing is replaced there and g_Hero.index is back.

Screenshots (VRAM display) before and after go to <build>/test_heroswap_*.png.
The game is killed by its PID only. Exit code 0 on PASS.
"""
import struct
import re
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
HERO_SCRIPT = 0x8008A4E0   # g_Objects[0].script
HERO_INDEX = 0x80086318    # g_Hero.index
HERO_PAGE = (384, 0, 64, 256)
PORTRAIT = (960, 432, 40, 64)


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
    build, play = "build-debug", 20.0
    for flag in ("--build", "--play"):
        if flag in args:
            i = args.index(flag)
            if flag == "--build":
                build = args[i + 1]
            else:
                play = float(args[i + 1])
            del args[i:i + 2]
    if len(args) != 1:
        sys.exit(__doc__)
    folder = Path(args[0]).resolve()
    spec = (folder / "heroswap.txt").read_text(encoding="utf-8")
    m = re.search(r"^target (\d+) (\d+)$", spec, re.M)
    if not m:
        sys.exit(f"{folder}: heroswap.txt has no target line")
    target = (int(m.group(1)), int(m.group(2)))
    files = {int(n): (name, int(size)) for n, name, size in re.findall(r"^file (\d+) (\S+) (\d+) ", spec, re.M)}
    pack = (folder / files[1][0]).read_bytes()
    pack_head = pack[:4 * (int.from_bytes(pack[:4], "little") + 2)]
    pack_e2 = PACK + int.from_bytes(pack[12:16], "little")   # offset[2]
    mh = re.search(r"^hero (\d+)$", spec, re.M)
    hero = int(mh.group(1)) if mh else None
    # What file 0 leaves in VRAM: the last TIM in the hero's page, the last portrait.
    chain = [t for e in container((folder / files[0][0]).read_bytes()) for t in tims(e)]
    page = [t for t in chain if any(r and overlaps(r, HERO_PAGE) for r in (t[0], t[2]))]
    portrait = [t for t in chain if t[2] == PORTRAIT]
    expect_vram = {}
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
    path_toml = str(folder).replace("\\", "\\\\")
    state_text = f"""format_version = 2

[[package]]
id = "dragonvalor.heroswap"
version = "1.0.0"

[[feature]]
package_id = "dragonvalor.heroswap"
id = "heroswap"
enabled = true
[feature.resources]
build = "{path_toml}"
"""
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

    log = bdir / f"dvdbg_{port}.log"
    try:
        out = subprocess.run(["tasklist"], capture_output=True, text=True).stdout
        if "DragonValorRecompiled" in out:
            sys.exit("the game is already running: not launching a second instance")
        state.parent.mkdir(parents=True, exist_ok=True)
        state.write_text(state_text, encoding="utf-8")
        out = cli("launch", "--build", str(bdir))
        mm = re.search(r"pid=(\d+)", out)
        pid = mm.group(1) if mm else None
        print("launch:", out.splitlines()[0] if out else "?")
        time.sleep(3)
        print("state 10:", cli("state", "load", "10").splitlines()[-1])
        time.sleep(4)
        text = log.read_text(encoding="utf-8", errors="replace")
        check("plugin loaded the folder", f"dragonvalor.heroswap: stage {target[0]}/{target[1]}" in text,
              next((l for l in text.splitlines() if "dragonvalor.heroswap" in l), "no plugin line"))
        print(f"  state 10 is {rd16(AREA)}/{rd16(STAGE)}, mode {rd(MODE, 1)[0]}")
        rpc(port, "screenshot", path=str(bdir / "test_heroswap_before.png"))
        index_before = rd(HERO_INDEX, 1)[0]

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
            time.sleep(6)
            cli("press", "cross")
            t0 = time.time()
            while time.time() - t0 < 60 and not (rd(MODE, 1)[0] == 8 and time.time() - t0 > 4):
                time.sleep(0.5)
            time.sleep(4)   # the stage fades in
            return (rd16(AREA), rd16(STAGE)) == (area, stage) and rd(MODE, 1)[0] == 8

        ok = reload_into(*target)
        check("the target stage is running again", ok, f"{rd16(AREA)}/{rd16(STAGE)} mode {rd(MODE, 1)[0]}")

        text = log.read_text(encoding="utf-8", errors="replace")
        for n in (0, 1):
            check(f"file {n} replaced by the plugin", f"file {n} replaced" in text)
        check("action pack in RAM has the rebuilt head", rd(PACK, len(pack_head)) == pack_head)
        for addr, value in re.findall(r"^patch (0x[0-9A-Fa-f]+) (\d+)$", spec, re.M):
            got = int.from_bytes(rd(int(addr, 16), 4), "little")
            check(f"layout {addr} = {value} after the reload", got == int(value), str(got))
        script = int.from_bytes(rd(HERO_SCRIPT, 4), "little")
        check("hero script = rebuilt pack's entry 2", script == pack_e2, f"0x{script:08X} vs 0x{pack_e2:08X}")
        if hero is not None:
            check(f"g_Hero.index = {hero}", rd(HERO_INDEX, 1)[0] == hero, str(rd(HERO_INDEX, 1)[0]))
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
            check(f"VRAM {label} = rebuilt file 0's", got == data, f"{same} of {len(data)} bytes")
        rpc(port, "screenshot", path=str(bdir / "test_heroswap_after.png"))

        # The Status screen (Triangle in play, mode 7): its portrait and the hero's name.
        cli("press", "triangle")
        time.sleep(4)
        rpc(port, "screenshot", path=str(bdir / "test_heroswap_status.png"))
        check("Triangle opened the Status screen (mode 7)", rd(MODE, 1)[0] == 7, f"mode {rd(MODE, 1)[0]}")
        cli("press", "triangle")
        time.sleep(3)

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
        h = cli("health")
        print("  health:", h.replace("\n", "; "))
        nums = [int(x) for x in re.findall(r"total=(\d+)", h)]
        check("no unknown dispatch / segment miss", nums and not any(nums), h.replace("\n", "; "))

        # Leaving the stage: another action stage of the same disc loads with
        # its own hero, and the hero index comes back.
        leave = (1, 9) if target != (1, 9) else (1, 7)
        if target[0] >= 7:
            leave = None   # test state 10 is on disc 1; nothing to leave to here
        if leave:
            replaced_before = log.read_text(encoding="utf-8", errors="replace").count(" replaced at ")
            ok = reload_into(*leave)
            text = log.read_text(encoding="utf-8", errors="replace")
            check(f"left for {leave[0]}/{leave[1]}", ok, f"{rd16(AREA)}/{rd16(STAGE)} mode {rd(MODE, 1)[0]}")
            check("no file replaced there", text.count(" replaced at ") == replaced_before)
            if hero is not None:
                check(f"g_Hero.index back to {index_before}", rd(HERO_INDEX, 1)[0] == index_before,
                      str(rd(HERO_INDEX, 1)[0]))
            rpc(port, "screenshot", path=str(bdir / "test_heroswap_left.png"))
    finally:
        if pid:
            subprocess.run(["taskkill", "/F", "/PID", pid], capture_output=True)
            time.sleep(1)
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
