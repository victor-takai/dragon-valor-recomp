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
  - the stage runs (mode 8, frames advancing) through --play seconds of
    walking and attacking, with no unknown dispatch or segment miss.

Screenshots (VRAM display) before and after go to <build>/test_heroswap_*.png.
The game is killed by its PID only. Exit code 0 on PASS.
"""
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

        if (rd16(AREA), rd16(STAGE)) != target:
            wr16(AREA, target[0])
            wr16(RECORD, target[1])
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
        check("the target stage is running again", (rd16(AREA), rd16(STAGE)) == target and rd(MODE, 1)[0] == 8,
              f"{rd16(AREA)}/{rd16(STAGE)} mode {rd(MODE, 1)[0]}")

        text = log.read_text(encoding="utf-8", errors="replace")
        for n in (0, 1):
            check(f"file {n} replaced by the plugin", f"file {n} replaced" in text)
        check("action pack in RAM has the rebuilt head", rd(PACK, len(pack_head)) == pack_head)
        for addr, value in re.findall(r"^patch (0x[0-9A-Fa-f]+) (\d+)$", spec, re.M):
            got = int.from_bytes(rd(int(addr, 16), 4), "little")
            check(f"layout {addr} = {value} after the reload", got == int(value), str(got))
        script = int.from_bytes(rd(HERO_SCRIPT, 4), "little")
        check("hero script = rebuilt pack's entry 2", script == pack_e2, f"0x{script:08X} vs 0x{pack_e2:08X}")
        rpc(port, "screenshot", path=str(bdir / "test_heroswap_after.png"))

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
