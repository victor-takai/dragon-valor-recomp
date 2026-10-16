#!/usr/bin/env python3
"""Drive the in-game menu (mods/plugins/dragonvalor_menu.cpp) on a debug build.

  python tests/test_menu.py [--build build-debug] [--port N]
  python tests/test_menu.py --attach          # use the game already running

The test writes its own <build>/mods/state.toml (cheats enabled, Infinite
HP off; the player's file is backed up and restored, even on failure) and
launches the build muted from slot 0 with tools/dvdbg.py, because the menu
persists every toggle to that file and the checks below assume Infinite HP
starts off. With --attach it uses the running game and the player's file as
they are (the Infinite HP checks then expect whatever state the file holds).
Injects keys through the debug server's host_key command (the same SDL
events a keyboard produces) and checks:
  1. Escape opens the menu and the guest pauses (frame counter stops).
  2. Cheats page: Enter on "Infinite HP" turns it on; after closing the menu
     the guest runs again and HP written to 5 refills to HPmax within a frame
     (the option reached the plugin live, through psx_mod_set_option_live).
  3. The same row turns it off again; a written HP stays. The Val row
     (third on the page) writes RAM at once: one step right = 100. The STR
     row (sixth) spreads its value over the five attack entries at
     0x80086324 (the game sums them into the shown STR): one step = +5.
  4. System page: "Save state" on slot 12 (runtime slot 11) writes the slot
     file; the slot's previous content is restored afterwards. No other
     slot is written (slot 0/1 is only ever restored from tests/states/).
  5. System page: "Return to launcher" restarts the program with the same
     command line (PSX_MENU_EXIT_RELAUNCH in builds without the netplay
     lobby client; with the player's arguments that opens the launcher).
     Skipped with --attach. The player's settings.toml and
     dragonvalor_settings.toml are backed up and restored around the run.
The menu always opens on the first section (Display) with nothing selected,
so a path is Down x N then Enter. Exit code 0 on PASS.
"""
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from dvdbg import rpc, port_from_args, slot_file, wait_savestate, SAVES_STATES_DIR, DEFAULT_DISC  # noqa: E402

DV = str(ROOT / "tools" / "dvdbg.py")
HP, HPMAX = 0x8008A5AE, 0x8008A5B0
SECTIONS = ["Display", "Graphics", "Audio", "System", "Cheats", "Game"]
STATE = """format_version = 2

[[package]]
id = "dragonvalor.cheats"
version = "1.3.0"

[[feature]]
package_id = "dragonvalor.cheats"
id = "cheats"
enabled = true
[feature.values]
infinite_hp = "false"
infinite_mp = "false"
"""


def main(argv):
    args = list(argv)
    port = port_from_args(args)
    build = "build-debug"
    if "--build" in args:
        i = args.index("--build")
        build = args[i + 1]
        del args[i:i + 2]
    attach = "--attach" in args

    def cli(*a, timeout=120):
        r = subprocess.run([sys.executable, DV, *a, "--port", str(port)], capture_output=True, text=True, timeout=timeout)
        return (r.stdout + r.stderr).strip()

    state = ROOT / build / "mods" / "state.toml"
    settings = {f: f.read_bytes() for f in (ROOT / build / "settings.toml", ROOT / build / "dragonvalor_settings.toml") if f.is_file()}
    backup = state.with_suffix(".toml.test_menu.bak")
    had_state = state.is_file()
    if not attach:
        if had_state:
            shutil.copy2(state, backup)
        state.parent.mkdir(parents=True, exist_ok=True)
        state.write_text(STATE, encoding="utf-8")
        try:
            print("launch:", cli("launch", "--state", "0", "--build", str(ROOT / build)))
            time.sleep(9)
            return run(port, attach=False, exe=ROOT / build / "DragonValorUS_Recompiled.exe")
        finally:
            print("kill:", cli("kill"))
            for f, b in settings.items():
                f.write_bytes(b)
            if had_state:
                shutil.move(backup, state)
            else:
                state.unlink(missing_ok=True)
    return run(port, attach=True)


def game_processes(exe):
    """{pid: command line} of the running copies of the game executable."""
    ps = ("Get-CimInstance Win32_Process -Filter \"Name='" + exe.name + "'\" | "
          "ForEach-Object { \"$($_.ProcessId)`t$($_.CommandLine)\" }")
    out = subprocess.run(["powershell", "-NoProfile", "-Command", ps], capture_output=True, text=True).stdout
    procs = {}
    for line in out.splitlines():
        if "\t" in line:
            pid, cmd = line.split("\t", 1)
            procs[int(pid)] = cmd.strip()
    return procs


def run(port, attach, exe=None):
    results = []

    def frame():
        return rpc(port, "frame").get("frame")

    def rd16(a):
        return int.from_bytes(bytes.fromhex(rpc(port, "read_ram", addr=f"{a:08X}", len=2)["hex"]), "little")

    def wr16(a, v):
        rpc(port, "write_ram", addr=f"{a:08X}", val=v & 0xFF)
        rpc(port, "write_ram", addr=f"{a + 1:08X}", val=(v >> 8) & 0xFF)

    def key(name, n=1):
        for _ in range(n):
            r = rpc(port, "host_key", name=name)
            if not r.get("ok"):
                sys.exit(f"host_key {name} failed: {r}")
            time.sleep(0.3)

    def paused():
        f0 = frame(); time.sleep(0.8); return frame() == f0

    def check(label, ok):
        results.append(ok); print(f"  {'ok  ' if ok else 'FAIL'} {label}")

    def goto_section(name):
        key("Down", SECTIONS.index(name)); key("Return")

    def close():
        key("Escape", 2); time.sleep(0.3)

    if rd16(HPMAX) == 0:
        sys.exit("no hero loaded (HPmax = 0): launch with --state 0")

    # 1. open + pause
    key("Escape"); time.sleep(0.5)
    check("Escape opens the menu and pauses the guest", paused())

    # 2. Infinite HP on (first row of the Cheats page). The menu persists the
    #    toggle to mods/state.toml; the test wrote that file with the option
    #    off, so Enter turns it on. With --attach the row starts wherever the
    #    player's file left it: probe first and expect the opposite.
    was_on = False
    if attach:
        close(); wr16(HP, 5); time.sleep(0.5)
        was_on = rd16(HP) == rd16(HPMAX)
        key("Escape"); time.sleep(0.5)
    goto_section("Cheats"); key("Return"); close()
    check("closing the menu resumes the guest", not paused())
    wr16(HP, 5); time.sleep(0.5)
    hp = rd16(HP)
    if was_on:
        check("Infinite HP off live (written HP stays; it was on)", hp == 5)
    else:
        check("Infinite HP applied live (HP 5 -> HPmax)", hp == rd16(HPMAX))

    # 3. and back again
    key("Escape"); goto_section("Cheats"); key("Return"); close()
    wr16(HP, 5); time.sleep(0.5)
    hp = rd16(HP)
    if was_on:
        check("Infinite HP on again (HP 5 -> HPmax)", hp == rd16(HPMAX))
    else:
        check("Infinite HP off again (written HP stays)", hp == 5)
    wr16(HP, rd16(HPMAX))   # leave the hero healthy

    # 3b. Val editor: fifth row of the Cheats page (after Infinite HP/MP and the two
    #     drop-rate options), one step right = +100, written at once
    VAL = 0x800A2DD0
    wr16(VAL, 0); time.sleep(0.2)
    key("Escape"); goto_section("Cheats"); key("Down", 4); key("Right"); close()
    val = rd16(VAL)
    check("Val editor wrote 100 into RAM", val == 100)
    wr16(VAL, 0)

    # 3c. STR editor: eighth row; +5 lands in the five attack entries (sum) and in STR
    STR, ATK = 0x8008A5B6, 0x80086324
    before = rd16(STR)
    key("Escape"); goto_section("Cheats"); key("Down", 7); key("Right"); close()
    comps = [rd16(ATK + 2 * i) for i in range(5)]
    check(f"STR editor wrote {before + 5} as attack entries {comps}",
          rd16(STR) == before + 5 and sum(comps) == before + 5 and min(comps) > 0)

    # 4. save state from the System page (Resume, State slot, Save state, ...)
    #    into the LAST slot, menu slot 12 = runtime slot 11, so a player's low
    #    slots are never touched; whatever was in slot 12 is put back after.
    target = SAVES_STATES_DIR / slot_file(11, os.environ.get("DVDBG_DISC") or DEFAULT_DISC)
    thumb = target.with_suffix(".thumb")
    saved = {}
    for f in (target, thumb):
        if f.exists():
            saved[f] = f.read_bytes()
            f.unlink()
    try:
        key("Escape"); goto_section("System"); key("Down")
        key("Left", 12); key("Right", 11)          # slot row: 1 -> 12 whatever it showed
        gen = rpc(port, "savestate_status").get("generation", 0)
        key("Down"); key("Return")
        wait_savestate(port, gen)                   # the save is staged; wait for it to land
        close(); time.sleep(0.5)
        check(f"Save state from the menu wrote slot 12 ({target.name})", target.exists())
    finally:
        for f in (target, thumb):
            if f in saved:
                f.write_bytes(saved[f])
            elif f.exists():
                f.unlink()

    # 5. Return to launcher (System rows: Resume, State slot, Save state, Load
    #    state, Pause in menu, Return to launcher, Quit): the process restarts
    #    with its own command line and the debug server comes back.
    if not attach and exe is not None:
        before = game_processes(exe)
        key("Escape"); goto_section("System"); key("Down", 5); key("Return", 2)   # the row asks to press twice
        after, t0 = {}, time.time()
        while time.time() - t0 < 60:
            after = game_processes(exe)
            fresh = {pid: cmd for pid, cmd in after.items() if pid not in before}
            if fresh and not (set(before) & set(after)):
                break
            time.sleep(1)
        fresh = {pid: cmd for pid, cmd in after.items() if pid not in before}
        same_cmd = bool(fresh) and set(fresh.values()) == set(before.values())
        check(f"Return to launcher restarted the program with the same command line ({len(fresh)} new process)",
              same_cmd and not (set(before) & set(after)))
        t0, up = time.time(), False
        while time.time() - t0 < 60 and not up:
            try:
                up = frame() is not None
            except OSError:
                time.sleep(1)
        check("the restarted program answers on the debug port", up)

    ok = all(results)
    print("RESULT:", "PASS" if ok else "FAIL", f"({sum(results)}/{len(results)})")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
