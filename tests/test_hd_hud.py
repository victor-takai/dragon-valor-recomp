#!/usr/bin/env python3
"""Check that the HUD is known to the HD Texture Packs mod again after a save
state is loaded (mods/plugins/dragonvalor_cheats.c, "HD texture packs: the
HUD after a save state"), on a debug build.

  python tests/test_hd_hud.py [--build build-debug] [--port N]

The HUD's images are uploaded once, by the title, from \\TITLE\\DB_TITLE.TIP;
a loaded state restores VRAM without its uploads, and the mod knows a texture
by its upload, so the cheats plugin sends the same images again at the first
action frame after a state load. No pack or game art is needed to see it: the
test points the mod at an empty temporary pack with Dump textures on (and
MaxVRAMWriteSplits in its config.yaml, as a real pack has), launches the
build from test state 10 (muted, hidden window), lets the HUD draw, quits the
game cleanly (the dumps are written then) and looks at the dumped names:
  1. the plugin logs that it sent the images again;
  2. the dumps hold the HUD's two sources, the frames / gauges / spell icons
     (1CF9981FCCDB5A1C) and the digits (7DF5DD761498BFD9): drawn from uploads
     the mod knows;
  3. control run, the cheats feature off (no plugin): neither source is dumped
     (the same state, so without the plugin the HUD stays unknown);
  4. in both runs the mod was active and dumping, looking up the draws
     (the hd_textures command).
The temporary pack (dumps are game art) is deleted afterwards. The player's
settings.toml, dragonvalor_settings.toml and mods/state.toml are backed up
and restored. Exit code 0 on PASS.
"""
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from dvdbg import rpc, port_from_args  # noqa: E402

DV = str(ROOT / "tools" / "dvdbg.py")
MODE = 0x8008634C
HUD_SOURCES = {"1CF9981FCCDB5A1C": "frames, gauges, spell icons", "7DF5DD761498BFD9": "digits"}
LOG_LINE = "the HUD's images sent again"
STATE_TOML = """format_version = 2

[[package]]
id = "dragonvalor.cheats"
version = "1.4.0"

[[package]]
id = "psx.enhancement.hd-textures"

[[feature]]
package_id = "dragonvalor.cheats"
id = "cheats"
enabled = {cheats}

[[feature]]
package_id = "psx.enhancement.hd-textures"
id = "hd-textures"
enabled = true
[feature.values]
dump = "true"
replacements = "false"
[feature.resources]
pack = "{pack}"
"""


def main(argv):
    args = list(argv)
    port = port_from_args(args)
    build = "build-debug"
    if "--build" in args:
        i = args.index("--build")
        build = args[i + 1]
        del args[i:i + 2]
    bdir = ROOT / build
    state = bdir / "mods" / "state.toml"
    kept = {f: f.read_bytes() for f in (bdir / "settings.toml", bdir / "dragonvalor_settings.toml", state)
            if f.is_file()}
    results = []
    try:
        for cheats in (True, False):
            results += run(port, bdir, state, cheats)
    finally:
        for f, b in kept.items():
            f.write_bytes(b)
    ok = all(results) and results
    print("PASS" if ok else "FAIL", f"({sum(results)}/{len(results)})")
    return 0 if ok else 1


def run(port, bdir, state, cheats):
    results = []

    def check(label, good):
        results.append(bool(good))
        print(f"  {'ok  ' if good else 'FAIL'} {label}")

    def cli(*a, timeout=120):
        r = subprocess.run([sys.executable, DV, *a, "--port", str(port)], capture_output=True, text=True,
                           timeout=timeout)
        return (r.stdout + r.stderr).strip()

    def alive():
        try:
            rpc(port, "frame")
            return True
        except OSError:
            return False

    pack = Path(tempfile.mkdtemp(prefix="dv_hd_hud_"))
    try:
        (pack / "replacements").mkdir()
        (pack / "dumps").mkdir()
        (pack / "config.yaml").write_text("MaxVRAMWriteSplits: 64\n", encoding="utf-8")
        state.parent.mkdir(parents=True, exist_ok=True)
        state.write_text(STATE_TOML.replace("{cheats}", "true" if cheats else "false")
                         .replace("{pack}", str(pack).replace("\\", "\\\\")), encoding="utf-8")
        print(f"cheats plugin {'on' if cheats else 'off (control)'}, state 10, dump into {pack}")
        print("  launch:", cli("launch", "--state", "10", "--build", str(bdir)).splitlines()[-1])
        log = bdir / f"dvdbg_{port}.log"
        t0, sent = time.time(), False
        while time.time() - t0 < 30:
            sent = LOG_LINE in log.read_text(encoding="utf-8", errors="replace")
            if sent or (not cheats and time.time() - t0 > 12):
                break
            time.sleep(0.5)
        mode = int.from_bytes(bytes.fromhex(rpc(port, "read_ram", addr=f"{MODE:08X}", len=1)["hex"]), "little")
        time.sleep(6)                                    # the HUD draws; the dump tracks its use
        diag = rpc(port, "hd_textures")
        try:
            rpc(port, "quit")                            # a clean exit writes the dumps
        except OSError:
            pass
        t1 = time.time()
        while alive() and time.time() - t1 < 20:
            time.sleep(0.5)
        time.sleep(3)
        cli("kill")
        time.sleep(2)
        names = [p.name for p in (pack / "dumps").rglob("*.png")]
        found = {s: sum(f"-{s}-" in n for n in names) for s in HUD_SOURCES}
        print(f"  mode {mode} after the state load; {len(names)} dumps")
        if cheats:
            check(f"the plugin sent the HUD's images again ({LOG_LINE!r} in the log)", sent)
            for s, what in HUD_SOURCES.items():
                check(f"the HUD's {what} ({s}) were drawn from a known upload ({found[s]} dumps)", found[s] > 0)
        else:
            check("no plugin, no images sent", not sent)
            check(f"no plugin, the HUD stays unknown ({sum(found.values())} HUD dumps)", sum(found.values()) == 0)
        check(f"the mod was capturing (active {diag.get('active')}, dump {diag.get('dump')}, "
              f"{diag.get('draw_queries')} draws looked up)",
              diag.get("active") and diag.get("dump") and (diag.get("draw_queries") or 0) > 0)
    finally:
        shutil.rmtree(pack, ignore_errors=True)
    return results


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
