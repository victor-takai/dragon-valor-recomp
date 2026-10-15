# DragonValorUS Recompiled

<!-- retcomm-readme-metrics -->
[![GitHub downloads (all assets, all releases)](https://img.shields.io/github/downloads/victor-takai/dragon-valor-recomp/total)](https://github.com/victor-takai/dragon-valor-recomp/releases)
[![GitHub downloads (latest release)](https://img.shields.io/github/downloads/victor-takai/dragon-valor-recomp/latest/total)](https://github.com/victor-takai/dragon-valor-recomp/releases/latest)
[![GitHub release](https://img.shields.io/github/v/release/victor-takai/dragon-valor-recomp)](https://github.com/victor-takai/dragon-valor-recomp/releases/latest)
<!-- /retcomm-readme-metrics -->

Static recompilation of **DragonValorUS** built on
[psxrecomp](https://github.com/RetroPortingToolKit/psxrecomp) and
[recomp-ui](https://github.com/RetroPortingToolKit/recomp-ui).

Action RPG by Namco; two-disc NTSC-U release.

| | |
|---|---|
| Players | 1 |
| Region | USA |
| Publisher | Namco |
| Year | 2000 |

Supported discs: **SLUS-01092** (disc 1) and **SLUS-01164** (disc 2), USA.
Both boot the same executable; the launcher lets you pick the disc. This is
the native 60 Hz version; the PAL bring-up that preceded it is recorded in
[`docs/HISTORY.md`](docs/HISTORY.md).

State, open issues and pitfalls: [`docs/STATUS.md`](docs/STATUS.md). Build /
debug workflow: [`docs/DEVELOPMENT.md`](docs/DEVELOPMENT.md). Ahead-of-time
overlays: [`docs/AOT.md`](docs/AOT.md). Controls: [`CONTROLS.md`](CONTROLS.md).
Testing the game and reporting: [`docs/PLAYTESTING.md`](docs/PLAYTESTING.md).

## Playing

1. Build (see below) or unpack a release, then run **`Play.bat`** (or
   double-click the executable itself: it opens no console window).
2. The launcher opens. On the first run browse to your own `Dragon Valor (USA)
   (Disc 1).cue` (see [`DISC.md`](DISC.md) for the dumps that work); it checks
   the serial, region and ISO header and remembers the location. The same
   screen has the Disc 1 / Disc 2 choice, controller and its mode (Analog /
   D-Pad), memory cards, video and audio settings and mods.
3. **Play.** Press Start during the intro movie to skip it.
4. **Escape** (or **Select+Start** on the pad) opens the in-game menu: pause,
   cheats (infinite HP/MP, a gem or scroll every N kills, Val, stats,
   spell levels, items), texture
   filtering, scanlines, fullscreen, volume, save/load state, disc change,
   back to the launcher. Arrows / D-pad move, Enter / Cross selects,
   Escape / Circle goes back; the mouse works too (hover, click, wheel,
   right button = back). Every row is remembered between runs (video rows
   in `settings.toml`, mods in `mods/state.toml`, volume and the menu's own
   look in `dragonvalor_settings.toml`, all next to the exe).

Xbox / PlayStation / Switch pads work out of the box; the game reads the
analog stick. Memory cards live in `saves/`.

The launcher's **Mods** page lists the bundled features, all off by default:
the framework's enhancements (HD texture packs, PGXP, CD speed, 8 MB RAM,
bezel) and this project's packages: **Dragon Valor Cheats** (Infinite HP,
Infinite MP, a gem or scroll every N kills; also switchable in-game, where
Val, stats, spells and items can be edited too) and **Dragon Valor Skip
Intro** (presses Start for you as soon as the intro movie starts) and
**Dragon Valor Widescreen** (experimental: the stages render at 16:9 or
wider with real scenery at the sides; title, menus and movies stay 4:3).
Tick a feature, set its options, Play. See
[`docs/DEVELOPMENT.md`](docs/DEVELOPMENT.md#modding-what-the-framework-offers).

| PlayStation | Keyboard | | PlayStation | Keyboard |
|---|---|---|---|---|
| D-pad | Arrow keys | | L1 / R1 | `Q` / `W` |
| Cross | `X` | | L2 / R2 | `E` / `R` |
| Circle | `S` | | L3 / R3 | `T` / `Y` |
| Square | `Z` | | Start | `Enter` |
| Triangle | `A` | | Select | `Right Shift` |

| Key | Action |
|---|---|
| `F` | FPS readout |
| `F7` / `F8` | Save state / Rewind |
| `Tab` (hold) / `F9` | Turbo |
| `Alt`+`Enter` | Fullscreen |

<!-- retcomm-readme-launcher -->
## Retro Launcher

You can run this title **standalone** (download the release zip, point it at
your disc, play), or manage installs, updates, and disc/BIOS wiring with
**[Retro Launcher](https://github.com/RetroPortingToolKit/Retro-Launcher)** —
the Retro Compilation Manager hub for self-compiling recomps.

[Downloads](https://github.com/RetroPortingToolKit/Retro-Launcher/releases) ·
[Full README & features](https://github.com/RetroPortingToolKit/Retro-Launcher#readme)

<p align="center">
  <img src="https://raw.githubusercontent.com/RetroPortingToolKit/Retro-Launcher/main/docs/screenshots/hub-and-game-launcher.png" alt="Retro hub with a background build, next to a title’s recomp-ui launcher" width="720">
</p>

<p align="center">
  <img src="https://raw.githubusercontent.com/RetroPortingToolKit/Retro-Launcher/main/docs/screenshots/queue-and-background-build.png" alt="Background cmake build with titles queued" width="720">
</p>

Retro checks for updates, installs the prebuilt release zips, and automates
BIOS/ROM/save plumbing so you are not stuck repeating each game’s first run by hand.
<!-- /retcomm-readme-launcher -->

## Legal

You must own the original game. Disc images under `disc/` are gitignored and
must never be committed. Retail BIOS dumps are not redistributed and no C
derived from one may be committed; releases run on the bundled MIT OpenBIOS.

`generated/` (the recompiled game C) **is committed**: releases ship the
compiled game, built by CI from that tree. Regenerate and commit it whenever
seeds or the framework pin change.

Default app icon: `assets/psxrecomp.ico` (and `.png` / `.svg`) — Retro-themed controller mark from `psxrecomp/assets/`. Windows builds embed it via `APP_ICON`.

Optional box art under `launcher_assets/img/` may come from
[libretro-thumbnails](https://github.com/libretro-thumbnails/libretro-thumbnails)
(`Named_Boxarts`); see `BOXART_SOURCE.txt` when present.

## Licensing

Our own files (`tools/`, `scripts/`, `docs/`, `mods/`, `codegen_setup.c`,
the CMake and config files) are MIT, see [`LICENSE`](LICENSE). The committed
`generated/` tree is machine output derived from the game's code and carries
no licence grant from us. The framework (`psxrecomp/`) is PolyForm
Noncommercial 1.0.0 and the launcher (`recomp-ui/`) is MIT, so a built
executable inherits the framework's noncommercial terms. No game data is
included. [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) lists every
component that ships and its licence.

## Quick start (dev)

The full recipe (toolchain on `PATH`, `generate`, `rebuild` of both build
dirs, `--disc`) is in [`docs/DEVELOPMENT.md`](docs/DEVELOPMENT.md#build). In
short, from Git Bash with `C:\retcomm-toolchain\bin` first on `PATH` and
`PSXRECOMP_TOOLCHAIN_DIR` set:

```bash
git submodule update --init --recursive
bash psxrecomp/tools/ci/build_emitters.sh
python psxrecomp/psxrecomp_cli.py rebuild --config game.toml --project-root . --build-dir build-release --no-pgo
```

`generated/` is committed, so a fresh clone needs no disc to build.

Releases: tag `vX.Y.Z` or run the *Release builds* workflow
(`.github/workflows/release.yml`, generated from the framework template). CI
builds the committed `generated/` C on Linux, Windows and macOS and attaches
`dvus-<version>-<platform>.zip`, the compiled game. Locally:
`scripts/package_release.sh build-release windows-x64`.

## Symbols

Progressive map: `symbols.toml` → `python3 tools/sync_symbols.py` →
`psx_symbols.h` (`PSX_FN_*`). See `psxrecomp/docs/SYMBOLS.md`.

## Framework pins

Submodule gitlinks (`psxrecomp`, optional `recomp-ui`, nested `recomp-net`)
are authoritative. `framework_pins.txt` is an optional scaffold snapshot;
release CI logs SHAs with `record_pins.sh` but builds whatever the gitlinks
resolve to. Bump submodules deliberately — do not float on `main`/`master`
in release CI.

<!-- retcomm-readme-raid -->
---

<p align="center">
  <sub><b>R.A.I.D. — Retro AI Development</b> · a Discord for AI-assisted retro reverse-engineering, decomp &amp; recomp</sub>
</p>

<p align="center">
  <a href="https://discord.gg/Ad9BwSzctP">Join the R.A.I.D. Discord</a>
</p>
<!-- /retcomm-readme-raid -->
