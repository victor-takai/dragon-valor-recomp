# Dragon Valor Recompiled

<!-- retcomm-readme-metrics -->
[![GitHub downloads (all assets, all releases)](https://img.shields.io/github/downloads/victor-takai/dragon-valor-recomp/total)](https://github.com/victor-takai/dragon-valor-recomp/releases)
[![GitHub downloads (latest release)](https://img.shields.io/github/downloads-pre/victor-takai/dragon-valor-recomp/latest/total)](https://github.com/victor-takai/dragon-valor-recomp/releases)
[![GitHub release](https://img.shields.io/github/v/release/victor-takai/dragon-valor-recomp?include_prereleases)](https://github.com/victor-takai/dragon-valor-recomp/releases)
<!-- /retcomm-readme-metrics -->

**Dragon Valor**, Namco's PlayStation action RPG, running natively on PC: a
static recompilation of the USA release, built on
[psxrecomp](https://github.com/RetroPortingToolKit/psxrecomp) and
[recomp-ui](https://github.com/RetroPortingToolKit/recomp-ui). The game's
code is translated to C ahead of time; you bring the discs, no BIOS dump
needed.

> **Unofficial fan project.** Not affiliated with, endorsed or sponsored by
> Bandai Namco Entertainment or Sony Interactive Entertainment. Dragon Valor,
> its characters, art and music belong to Bandai Namco Entertainment
> (originally Namco); PlayStation is a trademark of Sony Interactive
> Entertainment. Free and non-commercial. Apart from the game's program,
> recompiled, and the launcher's cover art, the download holds nothing of the
> game and does not run on its own: graphics, audio, video and levels are
> read from your own discs ([`DISC.md`](DISC.md)).

| | |
|---|---|
| Players | 1 |
| Discs | SLUS-01092 (disc 1), SLUS-01164 (disc 2), USA |
| Publisher | Namco |
| Year | 2000 |

**Status:** playable preview, played into chapter 2 on Windows. Disc 2, the
ending and the Linux / macOS builds are untested
([`docs/STATUS.md`](docs/STATUS.md)).

## Features

- Native 60 fps at 1080p, with PGXP
- In-game menu with save states, cheats and settings
- Play as any of the nine heroes
- Watch a scene cut from the prologue, its Japanese lines in English
- Unlock the title menu's illustration gallery
- HD texture packs (DuckStation format)
- Widescreen
- Skip Intro, rewind and turbo
- Xbox, PlayStation and Switch pads, or the keyboard

## Playing

1. Download the zip for your system from
   [Releases](https://github.com/victor-takai/dragon-valor-recomp/releases)
   and unpack it anywhere.
2. Run `DragonValorRecompiled`. It is not signed, so Windows may warn you:
   *More info*, then *Run anyway*.
3. In the launcher, point it at your `Dragon Valor (USA) (Disc 1).cue`
   ([`DISC.md`](DISC.md) lists the dumps that work) and press **Play**.

In the game, **Esc** or **Select + Start** on the pad opens the menu, and
**F7** or **Select + R1** the save states. Cheats, Skip Intro, widescreen
and HD texture packs are switched on in the launcher's **Mods** page; Play
as is on from the start, and its hero and weapon are picked in the menu.
Keys and buttons: [`CONTROLS.md`](CONTROLS.md). Memory card saves live in
`saves/`.

Found a bug? [`docs/PLAYTESTING.md`](docs/PLAYTESTING.md) says what to test
and what to attach.

## Documentation

| | |
|---|---|
| [`docs/STATUS.md`](docs/STATUS.md) | What works, open issues, pitfalls |
| [`docs/DEVELOPMENT.md`](docs/DEVELOPMENT.md) | Build, debugging, mods, game research (RAM map, drops, heroes) |
| [`docs/AOT.md`](docs/AOT.md) | Ahead-of-time overlays (`BIN/*.BIN`) |
| [`docs/BACKLOG.md`](docs/BACKLOG.md) | Planned work and upstream reports |
| [`docs/HISTORY.md`](docs/HISTORY.md) | The PAL bring-up that preceded this version |

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

An unofficial fan project (notice at the top). You must own the original
game. Disc images under `disc/` are gitignored and
must never be committed. Retail BIOS dumps are not redistributed and no C
derived from one may be committed; releases run on the bundled MIT OpenBIOS.

`generated/` (the recompiled game C) **is committed**: releases ship the
compiled game, built by CI from that tree. Regenerate and commit it whenever
seeds or the framework pin change.

App icon: `assets/psxrecomp.ico` (and `.png` / `.svg`), our own pixel art, a sword on a crimson field with a cyan glint, after the idea of the game's memory-card save icon (no pixel of it is used); `tools/make_app_icon.py` draws all three. Windows builds embed the `.ico` via `APP_ICON`; the `.png` beside the exe is the window, taskbar and launcher icon.

Optional box art under `launcher_assets/img/` may come from
[libretro-thumbnails](https://github.com/libretro-thumbnails/libretro-thumbnails)
(`Named_Boxarts`); see `BOXART_SOURCE.txt` when present.

## Licensing

Our own files (`tools/`, `scripts/`, `tests/`, `docs/`, `assets/` (the app
icon), `mods/`, `patches/`,
`aot/`, `seeds/`, `codegen_setup.c`, the CMake and config files) are MIT, see
[`LICENSE`](LICENSE). The committed
`generated/` tree is machine output derived from the game's code and carries
no licence grant from us. The framework (`psxrecomp/`) is PolyForm
Noncommercial 1.0.0 and the launcher (`recomp-ui/`) is MIT, so a built
executable inherits the framework's noncommercial terms. Apart from that
code and the launcher's cover art (`launcher_assets/img/`, see
`BOXART_SOURCE.txt`), nothing of the game is included: its graphics, audio,
video and levels come from the player's discs. [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) lists every
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
`DragonValor.bat` runs the `build-release` exe (`DragonValorDebug.bat` the
debug one).

Releases: tag `vX.Y.Z` or run the *Release builds* workflow
(`.github/workflows/release.yml`, generated from the framework template). CI
builds the committed `generated/` C on Linux, Windows and macOS and attaches
`DragonValorRecompiled-<version>-<platform>.zip`, the compiled game. 0.x
versions publish as GitHub pre-releases. A dispatch with `publish` unticked
is a rehearsal: the zips land as run artifacts, with no tag and no release.
Locally: `scripts/package_release.sh build-release windows-x64`.

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
