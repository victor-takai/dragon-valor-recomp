# Dragon Valor (USA) Recompiled — release notes

These notes travel with every release zip. The current version is in
`VERSION` and in `psx_game_version.txt` next to the executable.

## What this is

The PlayStation game Dragon Valor (USA, SLUS-01092 / SLUS-01164), statically
recompiled to a native program with [psxrecomp](https://github.com/RetroPortingToolKit/psxrecomp).
You bring your own disc dumps; no BIOS is needed (the bundled OpenBIOS
runs the game) and nothing has to be built.

An unofficial fan project, free and non-commercial: not affiliated with,
endorsed or sponsored by Bandai Namco Entertainment or Sony Interactive
Entertainment. Dragon Valor belongs to Bandai Namco Entertainment
(originally Namco); PlayStation is a trademark of Sony Interactive
Entertainment. Apart from the game's program, recompiled, and the
launcher's cover art, this download holds nothing of the game and does not
run on its own: graphics, audio, video and levels are read from your discs.

| Zip | Platform |
|---|---|
| `DragonValorRecompiled-<version>-windows-x64.zip` | Windows 10/11, x64 |
| `DragonValorRecompiled-<version>-linux-x64.zip` | Linux x64 (glibc) |
| `DragonValorRecompiled-<version>-macos-arm64.zip` | macOS, Apple Silicon |
| `DragonValorRecompiled-<version>-macos-x64.zip` | macOS, Intel |

Only the Windows build has been played. The Linux and macOS zips are built
by the same CI from the same code, but nobody has run them yet.

Unpack anywhere, run `DragonValorRecompiled` (Windows: the `.exe`), point
the launcher at `Dragon Valor (USA) (Disc 1).cue` (see `DISC.md` for the
dumps that work), Play. Controls are in `CONTROLS.md`.

The executable is not signed, so Windows may say it protected your PC the
first time: *More info*, then *Run anyway*.

## What was verified on this build

- Boot, intro movie (Start skips it), title and menus, New Game, the first
  chapter at 60 fps, the Configuration screens, disc 2 boot.
- The in-game menu (Escape, or Select + Start): cheats, video rows, volume,
  save states, disc change; mouse, keyboard and pad. Return to launcher
  restarts the program into the launcher.
- Bundled mods: Cheats (Infinite HP / MP, a gem or scroll every N kills, Val
  and stat editors), Skip Intro, PGXP (on by default), Widescreen
  (experimental, off by default).
- Not verified: a full playthrough, Load Game after a restart, the disc
  change in play, controller rumble.

## Known issues

- Widescreen: a small block of texture noise can sit just right of the 4:3
  area in some stages (a parked model part that the console's 4:3 picture
  clipped). Off by default.
- The runtime writes a `psx_freeze_dump_*.json` next to the executable on
  every boot (a false positive of the framework's freeze watchdog on this
  title); it is deleted at the next start and can be deleted by hand.
- Save states are the framework's, not the game's: a state from one
  release may refuse to load on the next. Keep Memory Card saves for
  progress.

## Reporting

The tester's guide is
[`docs/PLAYTESTING.md`](https://github.com/victor-takai/dragon-valor-recomp/blob/master/docs/PLAYTESTING.md):
what we most want tested, how to report, which files to attach. In short,
most wanted are a full playthrough (nobody has played past chapter 2 on
this build), the disc change, Load Game after a restart, rumble, and any run
of the Linux or macOS zips. Try the problem again from a fresh boot, then
use the [bug report form](https://github.com/victor-takai/dragon-valor-recomp/issues/new/choose),
one problem per issue, with what you did and what happened (a screenshot
helps) and these files from the game's folder:

- `psx_last_run_report.json`, written when the game exits, crash included
  (the most useful single file);
- `psx_crash.txt`, when there is one;
- `settings.toml` and `mods/state.toml` (what was on);
- `psx_game_version.txt` (which build you ran);
- `psx_freeze_heartbeat.json` if the game froze rather than exited;
- a save state taken just before the spot (`F7`, then `S`; the file is
  `saves/openbios/state_*_slotNN.pst`) or the Memory Card
  (`saves/card1.mcd`) with a save near it.

Never send disc images. The `psx_freeze_dump_*.json` files are not needed
(see Known issues).
