# Dragon Valor (USA) Recompiled — release notes

These notes travel with every release zip. The current version is in
`VERSION` and in `psx_game_version.txt` next to the executable.

## What this is

The PlayStation game Dragon Valor (USA, SLUS-01092 / SLUS-01164), statically
recompiled to a native program with [psxrecomp](https://github.com/RetroPortingToolKit/psxrecomp).
You bring your own disc dumps; no BIOS is needed (the bundled OpenBIOS
runs the game) and nothing has to be built.

| Zip | Platform |
|---|---|
| `DragonValorRecompiled-<version>-windows-x64.zip` | Windows 10/11, x64 |
| `DragonValorRecompiled-<version>-linux-x64.zip` | Linux x64 (glibc) |
| `DragonValorRecompiled-<version>-macos-arm64.zip` | macOS, Apple Silicon |
| `DragonValorRecompiled-<version>-macos-x64.zip` | macOS, Intel |

Unpack anywhere, run `DragonValorRecompiled` (Windows: the `.exe`), point
the launcher at `Dragon Valor (USA) (Disc 1).cue` (see `DISC.md` for the
dumps that work), Play. Controls are in `CONTROLS.md`.

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

`PLAYTESTING.md` in the repository says what to test and which files to
send; issues go to <https://github.com/victor-takai/dragon-valor-recomp/issues>.
