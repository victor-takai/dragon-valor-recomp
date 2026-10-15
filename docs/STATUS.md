# Dragon Valor (USA) Recompiled — status

Last updated: 2026-10-14. Framework: psxrecomp @ f9c287f5 (submodule, bumped from 54abcb70 the same day; see [`framework_pin_history.md`](framework_pin_history.md)), recomp-ui @ 41c88ff, OpenBIOS only.
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
| Generated C | 35 shards under `generated/`, committed, byte-identical across runs |
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
- **Release zip**: `scripts/package_release.sh build-release windows-x64`
  produced `dist/dvus-0.1.0-windows-x64.zip` (25 MB, unsigned); unpacked
  elsewhere it boots at 60 fps with the bundled OpenBIOS and the tcc-based
  overlay autocompile (no compiler on the player's machine). `game.toml` names
  the discs by Redump file name only and `DISC.md` ships at the zip root.
- **Widescreen** (2026-10-15): `dragonvalor.enhancement.widescreen`, off by
  default, experimental. Native-wide: the 384-pixel stage gains 128 real
  columns at 16:9 (288 at 21:9), the pause screen stays wide (mode-byte
  predicate), title / menus / movies stay 4:3, the HUD keeps its 4:3
  positions, and the game's own screen-X reject is widened with the
  margin (`[widescreen.cull] auto_screen_x`, five compare sites in the
  regenerated code). `tests/test_widescreen.py on|off` 5/5 and 4/4; see
  `DEVELOPMENT.md`, *This project's package:
  dragonvalor.enhancement.widescreen*.
- Tester material: [`PLAYTESTING.md`](PLAYTESTING.md), the GitHub bug-report
  form, [`CONTROLS.md`](../CONTROLS.md), [`THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md).

## Open items

1. Human play session (saves, Load Game, reaching disc 2 in-game, whether the
   Xbox pad actually rumbles with Vibration ON) — not done yet. Also watch
   what the title does after a Start during the intro movie: in scripted
   runs it sometimes goes to the menu and sometimes plays the story demo
   first (see `DEVELOPMENT.md`, *skip-intro*); the same race may show up
   with a real press.
2. Known from the PAL bring-up and not re-checked here: dissolve brightness at
   high internal scale (see [`HISTORY.md`](HISTORY.md)).
3. Hero/weapon swap: the hero-selecting field is not identified; needs a
   later-chapter save to diff against (see `DEVELOPMENT.md`, *Character and
   weapon*). Inventory, stats, Val, magic, area/stage ids, play time and the
   save-block layout are mapped. The only public save (GameFAQs, post-clear
   prologue) was loaded and is Clovis again, so the reference must come from
   a human play session.
4. The framework's freeze watchdog writes a ~35 MB `psx_freeze_dump_*.json`
   beside the exe on every boot (false positive on statically compiled code).
   Upstream PR #646 fixes it; until the pin includes it, delete the dumps
   before timing runs (no config switch exists).
5. Widescreen follow-ups: HUD anchored to the window edges, a check of
   later chapters and the field module, a real Fit resize; see
   `BACKLOG.md`, section F.

## Backlog

A read-only review on 2026-10-14 (code, docs, config, and a survey of other
psxrecomp titles) is recorded in [`BACKLOG.md`](BACKLOG.md), which marks what
has been applied since.

## Tooling

`Play.bat`, `PlayDebug.bat` (`PlaySmooth.bat` was dropped on 2026-10-14: frame
generation adds nothing at 60 Hz), `tools/dvdbg.py`, `tools/session_monitor.py`. See [`DEVELOPMENT.md`](DEVELOPMENT.md).

Automated tests start from save state slot 0 (first screen of gameplay) or
slot 10 (a town stage with Fire selected and an enemy in front, for spell
and magic-kill tests); both are committed in `tests/states/` and restored
into `saves/openbios/` by `python tools/dvdbg.py launch --state N`. RAM addresses for HP/MP/STR/DEF, Val and
magic levels (verified cheat map) and the save-state workflow are in [`DEVELOPMENT.md`](DEVELOPMENT.md#save-states).

Mods: the project ships `dragonvalor.cheats` 1.3.0 (Infinite HP, Infinite MP,
gem / scroll every N kills, added 2026-10-15) and `dragonvalor.skip-intro` (the intro movie is skipped by the game's
own Start path through a function-entry hook on the pad copy) as bundled
packages backed by plugins in `mods/plugins/`, verified on and off
(2026-10-14). The cheats feature is on by default with every option off, so
the **in-game menu** (Escape / Select+Start; `mods/plugins/dragonvalor_menu.cpp`
on psxrecomp's menu host + recomp-ui's runtime UI) can switch them live; the
menu also pauses, changes texture filtering / scanlines / fullscreen /
volume, saves and loads states, changes disc and returns to the launcher.
Its STR editor writes the game's real attack table (`0x80086324`, five
entries; the shown STR is only their sum, found 2026-10-14 after the player
saw a huge STR change nothing; `DEVELOPMENT.md`, *How damage is computed*).
The gem and scroll drops the player noticed are a rule, not luck: a gem
after 15 sword kills (then 22, 28, …), a scroll after 2 magic kills (then
4, 7, 12, 20), both confirmed live on 2026-10-15, the gem by script and
the scroll by the player on the pad (`DEVELOPMENT.md`, *How drops
are decided*). Items (code reading, 2026-10-15): Earrings take 1 MP off
every cast each, Pepper feeds a counter nothing reads, and Lithograph /
Ring / Wine / Talisman give their bonus when obtained, not while carried
(`DEVELOPMENT.md`, *Pickup ids and what they do*).
The Namco logo stays: shortening it breaks the title's loads. Title defaults
measured the same day: mipmapped 3D textures with 4 anisotropic taps ship in `game.toml` (a
90 ms audio buffer was tried and reverted: choppy sound on the title screen); FXAA and CD Speed do not help here
(see `DEVELOPMENT.md`, *Video and audio defaults*); linear filtering of the
final image and nearest texture filtering are the written-out defaults (the
player's choice; `Play.bat` no longer forces xBR). The runtime is the PGXP
hook flavour and PGXP ships on by default (66 % of vertices corrected, 60
fps kept; `DEVELOPMENT.md`, *PGXP*). The
framework's HD texture dumper works on this game once a stage reloads
(563 PNGs from a session from the title into Brize Forest). See [`DEVELOPMENT.md`](DEVELOPMENT.md#modding-what-the-framework-offers).
