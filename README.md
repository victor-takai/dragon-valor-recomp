# DragonValorUS Recompiled

<!-- retcomm-readme-metrics -->
[![GitHub downloads (all assets, all releases)](https://img.shields.io/github/downloads/RetroPortingToolKit/DragonValorUSRecomp/total)](https://github.com/RetroPortingToolKit/DragonValorUSRecomp/releases)
[![GitHub downloads (latest release)](https://img.shields.io/github/downloads/RetroPortingToolKit/DragonValorUSRecomp/latest/total)](https://github.com/RetroPortingToolKit/DragonValorUSRecomp/releases/latest)
[![GitHub release](https://img.shields.io/github/v/release/RetroPortingToolKit/DragonValorUSRecomp)](https://github.com/RetroPortingToolKit/DragonValorUSRecomp/releases/latest)
<!-- /retcomm-readme-metrics -->

Static recompilation of **DragonValorUS** built on
[psxrecomp](https://github.com/mstan/psxrecomp) and
[recomp-ui](https://github.com/RetroPortingToolKit/recomp-ui).

Action RPG by Namco; two-disc NTSC-U release.

| | |
|---|---|
| Players | 1 |
| Region | USA |
| Publisher | Namco |
| Year | 2000 |

Supported discs: **SLUS-01092** (disc 1) and **SLUS-01164** (disc 2), USA.
Both boot the same executable; the launcher lets you pick the disc. The PAL
release has its own project (`../DragonValorRecomp`); this is the native 60 Hz
version.

State, open issues and pitfalls: [`docs/STATUS.md`](docs/STATUS.md). Build /
debug workflow: [`docs/DEVELOPMENT.md`](docs/DEVELOPMENT.md). Ahead-of-time
overlays: [`docs/AOT.md`](docs/AOT.md).

## Playing

1. Build (see below) or unpack a release, then run **`Play.bat`**
   (`PlaySmooth.bat` adds the OpenGL render thread and frame generation).
2. The launcher opens. On the first run browse to your own `Dragon Valor (USA)
   (Disc 1).cue` (see [`DISC.md`](DISC.md) for the dumps that work); it checks
   the serial, region and ISO header and remembers the location. The same
   screen has the Disc 1 / Disc 2 choice, controller and its mode (Analog /
   D-Pad), memory cards, video and audio settings and mods.
3. **Play.** On the first screen Start skips the intro movie.

Xbox / PlayStation / Switch pads work out of the box; the game reads the
analog stick. Memory cards live in `saves/`.

The launcher's **Mods** page lists the bundled features, all off by default:
the framework's enhancements (HD texture packs, PGXP, CD speed, 8 MB RAM,
bezel) and this project's **Dragon Valor Cheats** package (Infinite HP,
Infinite MP, Val floor). Tick a feature, set its options, Play. See
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

## Quick start (dev)

```bash
git submodule update --init --recursive
./psxrecomp/tools/ci/build_emitters.sh
python3 psxrecomp/psxrecomp_cli.py generate \
  --config game.toml --project-root . --disc disc/<your>.cue
git add generated && git commit -m "Regenerate game C"
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --target psx-runtime
```

Releases: tag `vX.Y.Z` (or run the *Release builds* workflow). CI builds the
committed `generated/` C on Linux, Windows and macOS and attaches
`dvus-<version>-<platform>.zip`, the compiled game. Locally:
`scripts/package_release.sh build-release linux-x64`.

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
  <a href="https://discord.gg/Ad9BwSzctP"><img src=".github/raid-discord.png" alt="Join the Retro AI Development (R.A.I.D.) Discord" width="200"></a>
</p>
<!-- /retcomm-readme-raid -->
