# Dragon Valor (USA) Recompiled — status

Last updated: 2026-10-14. Framework: psxrecomp @ 54abcb70 (submodule), OpenBIOS only.
The project started on the PAL release (SCES-01705, 50 Hz) and moved to this
USA version on 2026-10-14; the PAL tree was deleted. What that bring-up
established (controller, dissolve rendering, first play session, framework
quirks) is in [`HISTORY.md`](HISTORY.md).

## Identity

| | |
|---|---|
| Discs | SLUS-01092 (disc 1), SLUS-01164 (disc 2) — USA, NTSC, 2 tracks each |
| Boot EXE | `SLUS_010.92` / `SLUS_011.64`, identical bytes (sha256 `ab40d44b…`) |
| Entry / load / text | `0x80012EA0` / `0x80010000` / `0x67800` |
| Generated C | 35 shards, 1,302,445 lines, 1962 dispatch entries |
| Seeds | 572 first-pass JAL targets |
| AOT | `aot/overlays.json`: five `BIN/*.BIN` modules at `0x800BBEF0`, 4630 static variants ([`AOT.md`](AOT.md)) |

Why this project exists: the PAL release runs its logic at 50 Hz; this one is
the native 60 Hz version.

## What works (verified 2026-10-14, debug build, runtime autocompile OFF, muted)

- Boot → Namco logo → intro FMV (Start skips it) → title → story movie →
  main menu → attract gameplay demo (dungeon, 3D, HUD), 335 s route.
- **59.9–60.0 fps in 30 of 33 ten-second windows**; the other three
  (56.9–58.5) sit in the story movie right after the title, not in gameplay.
- 0 unknown dispatches, 0 segment misses; no runtime overlay compiles
  (`configured 0, runs 0`); 4217 static entries / 4630 variants registered,
  4.3 M native hits within the first 20 s.
- Pad: Xbox 360 opens as player 1 in DualShock mode (analog).
- **Menu flow (USA)**: the Namco logo ignores Start; Start during the intro
  FMV (from ~f1000) skips to the title, and the title screen *is* the main
  menu (New Game / Load Game / Configuration, New Game highlighted — Start
  there starts a New Game: prologue text → opening cutscene). Left idle, the
  title cycles title → gameplay demo → story movie.
- **Configuration** (reached by injected input, 2026-10-14): *Controller
  Setup* — in-game remap of L1/L2 Dash, R1/R2 Duck, △ Status, ○ Magic, × Jump,
  □ Attack, D-pad Move, Start Pause, Select = default, and **Vibration Setup
  ON/OFF (ON by default)**, so the game drives DualShock rumble; *Adjust
  Screen* — move the picture with the D-pad, Start confirms, △ default;
  *Audio Setup* (not photographed yet).
- Release build: window LIVE, title bar `[Game 60 fps 1.00x]`, NTSC-U detected
  (59.94 Hz pacing).
- **Disc 2** (`--disc` override, SLUS-01164): boots, 59.9 fps, 0 unknown
  dispatches at 80 s; its five `BIN/*.BIN` are byte-identical to disc 1's, so
  the static shard serves both discs (only `SYSTEM.CNF` and the EXE name differ).

## Open items

1. Human play session (saves, Load Game, reaching disc 2 in-game, whether the
   Xbox pad actually rumbles with Vibration ON) — not done yet.
2. ~~`game.toml` points at absolute local disc paths~~ — done 2026-10-14:
   `discs` carries the Redump cue names only, the launcher asks for the
   images, `DISC.md` ships at the zip root. `scripts/package_release.sh
   build-release windows-x64` produced `dist/dvus-0.1.0-windows-x64.zip`
   (25 MB, unsigned); unpacked elsewhere it boots at 60 fps with the bundled
   OpenBIOS and tcc-based overlay autocompile (no compiler on the player's
   machine).
3. Known from the PAL bring-up and not re-checked here: dissolve brightness at
   high internal scale (see [`HISTORY.md`](HISTORY.md)).
4. Hero/weapon swap: the hero-selecting field is not identified; needs a
   later-chapter save to diff against (see `DEVELOPMENT.md`, *Character and
   weapon*). Inventory, stats, Val, magic, area/stage ids, play time and the
   save-block layout are mapped. The only public save (GameFAQs, post-clear
   prologue) was loaded and is Clovis again, so the reference must come from
   a human play session.
5. The framework's freeze watchdog writes a ~35 MB `psx_freeze_dump_*.json`
   beside the exe on every boot (false positive during the disc load) — a
   player install accumulates them. Upstream issue to file; until then a note
   in README or a cleanup on launch.

## Backlog

A read-only review on 2026-10-14 (code, docs, config, and a survey of other
psxrecomp titles) is recorded in [`BACKLOG.md`](BACKLOG.md); nothing in it has
been applied yet.

## Tooling

`Play.bat`, `PlayDebug.bat` (`PlaySmooth.bat` was dropped on 2026-10-14: frame
generation adds nothing at 60 Hz), `tools/dvdbg.py`, `tools/session_monitor.py`. See [`DEVELOPMENT.md`](DEVELOPMENT.md).

Automated tests start from save state slot 0 (first screen of gameplay;
committed copy in `tests/states/`, restored into `saves/openbios/` by
`python tools/dvdbg.py launch --state 0`). RAM addresses for HP/MP/STR/DEF, Val and
magic levels (verified cheat map) and the save-state workflow are in [`DEVELOPMENT.md`](DEVELOPMENT.md#save-states).

Mods: the project ships `dragonvalor.cheats` (Infinite HP, Infinite MP, Val
floor) as a bundled package backed by VBlank plugins in `mods/plugins/`, all
off by default and verified on and off from slot 0 (2026-10-14). The
framework's HD texture dumper works on this game once a stage reloads
(67 PNGs from Brize Forest). See [`DEVELOPMENT.md`](DEVELOPMENT.md#modding-what-the-framework-offers).
