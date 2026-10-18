# Playtesting Dragon Valor (USA) Recompiled

What a tester needs: how to set up, what is known to work, what we most
want eyes on, and which files to send with a report. Controls are in
[`CONTROLS.md`](../CONTROLS.md), the accepted disc dumps in
[`DISC.md`](../DISC.md), the current state of the port in
[`STATUS.md`](STATUS.md).

## Setup

1. Download the release zip for your platform from the
   [releases page](https://github.com/victor-takai/dragon-valor-recomp/releases)
   and unpack it anywhere (a path without spaces is safest). Nothing is
   installed; delete the folder to remove it.
2. Run `DragonValorRecompiled` from the folder (Windows: the `.exe`). The
   launcher opens. The executable is not signed, so Windows may say it
   protected your PC the first time: *More info*, then *Run anyway*.
3. First run: browse to your own `Dragon Valor (USA) (Disc 1).cue` (the
   `.bin` must sit next to it with its Redump name). The launcher checks the
   serial and remembers the location in `settings.toml`. Disc 2 is picked
   the same way when the game asks for it, or beforehand on the same screen.
4. Leave the defaults for a first pass: OpenGL, 1080p internal resolution,
   PGXP on, every mod off except the ones you are testing. Play.

No BIOS is needed (the bundled OpenBIOS runs the game). The game's saves
go to a Memory Card image in `saves/`; the first run creates it.

## Known good (see STATUS.md for the measurements)

Boot, intro movie (Start skips it), title and menus, New Game, the first
chapter's stages at 60 fps, Configuration (controller remap, screen
position, vibration), disc 2 boot, the in-game menu (Escape or Select +
Start) with its cheats and save states, the Skip Intro mod, the Cheats mod.

## What we most want tested

Nobody has played the game through on this build yet. In rough priority:

1. **A full playthrough on a Memory Card save.** Save at the inn / save
   points as you normally would and tell us how far you got. Keep a save
   from right after each boss and one at the start of each chapter (they
   are useful to us beyond bug reports; we have the start of chapter 2).
2. **The disc change.** When the game asks for disc 2, use the in-game
   menu's disc row (or the launcher's). Does the game continue?
3. **Load Game** from a card save after restarting the program.
4. **Rumble.** Configuration > Vibration Setup is ON by default. Does your
   pad vibrate when the game says it should (hits, bosses)?
5. **The title after an early skip.** Press Start during the intro movie.
   Once in a while the title animation takes very long or drops into the
   story demo instead of the menu; we have seen it in scripted runs and
   want to know whether it happens by hand.
6. **Picture.** Anything at high internal resolution that looks wrong
   compared with the console: dissolve transitions too bright, seams,
   flicker, text misaligned. A screenshot (`F12` is not bound; use the OS
   shortcut) with the settings you used.
7. **Widescreen** (Mods > Display > Widescreen, experimental). Stages should
   show real scenery in the extra width; the title, menus and movies stay
   4:3 with side bars. Known: a small block of texture noise can sit just
   right of the 4:3 area in some stages. Report anything else: HUD pieces
   in odd places, a screen that should have stayed 4:3, missing scenery at
   the edges.
8. **Cheats mod.** Infinite HP / MP, "Gem every N sword kills", "Scroll every
   N magic kills", and the in-game
   menu's Val / stats / spell / item editors: anything that does not take
   effect or breaks a later screen.
9. **Linux and macOS.** Only the Windows build has been played; the other
   zips come from the same CI and code, but nobody has run them yet. Does
   it start, find the disc and reach gameplay?

Anything else counts: crashes, freezes, sound dropouts, a controller that
stops responding, a stage that runs slow. Note the chapter and the moment.

## Before you report

- **Try it again from a fresh boot**, with every mod off if one was on. Say
  whether it repeats.
- **Take a save state just before the spot** if you can: `F7` opens the
  slot picker (Left / Right, then `S`), or the in-game menu, System > Save
  state, slot 1 to 12. State files are
  `saves/openbios/state_*_slotNN.pst`; attach the one you took. A Memory
  Card save (`saves/card1.mcd`) that lands near the problem is just as good.
- **Collect the files** from the folder with the executable:
  - `psx_last_run_report.json`: written when the game exits, crash
    included. The most useful single file.
  - `psx_crash.txt`, when there is one (the runtime writes it on a crash).
  - `settings.toml` and `mods/state.toml`: what was on.
  - `psx_game_version.txt`: which build you ran.
  - `psx_freeze_heartbeat.json` if the game froze rather than exited.
  - A screenshot or a short clip of the problem.

  Never send disc images or anything from `disc/`.
- **File it** with the
  [bug report form](https://github.com/victor-takai/dragon-valor-recomp/issues/new/choose).
  One problem per issue.

## Save states and framework bumps

Save states are the framework's snapshot of the whole machine, not the
game's. A state taken on one release may refuse to load on the next when
the framework pin moved (`docs/framework_pin_history.md` says when). Keep a
Memory Card save as your real progress; use states for convenience.

## For developers running a session

`tools/session_monitor.py` samples the running debug build (fps, health
counters, periodic screenshots) for up to two hours; `python tools/dvdbg.py
health` must stay at 0 unknown dispatches and 0 segment misses.
`docs/DEVELOPMENT.md`, *Play sessions*, has the checklist we go through
after a human session (saves copied, report read, states re-taken).
