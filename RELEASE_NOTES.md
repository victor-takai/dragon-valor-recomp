Dragon Valor (PlayStation, USA) running natively on PC, statically recompiled with [psxrecomp](https://github.com/RetroPortingToolKit/psxrecomp). This is a preview: it has been played into chapter 2, not beyond.

> Unofficial fan project, free and non-commercial; not affiliated with Bandai Namco or Sony. Dragon Valor belongs to Bandai Namco Entertainment. You need your own discs: the download carries the game's program, recompiled, and its cover art, but no graphics, audio, video or levels.

## How to run

1. Download the zip for your system and unpack it anywhere: `windows-x64` (Windows 10/11), `linux-x64`, `macos-arm64` (Apple Silicon) or `macos-x64` (Intel).
2. Run `DragonValorRecompiled` (Windows: the `.exe`). The executable is not signed, so Windows may say it protected your PC: *More info*, then *Run anyway*.
3. Point the launcher at your `Dragon Valor (USA) (Disc 1).cue` (`DISC.md` lists the dumps that work) and press Play.

No BIOS is needed. Controls are in `CONTROLS.md`.

## Features

- Native 60 fps, rendered at 1080p by default, with PGXP for steadier 3D
- Widescreen (experimental, off by default)
- In-game menu (Esc, or Select + Start): pause, save states, texture filtering, scanlines, fullscreen, volume, disc change
- Cheats: infinite HP / MP, a gem or scroll every N kills, Val, stats, spell and item editors
- Skip Intro, rewind and turbo
- Xbox, PlayStation and Switch pads, and the keyboard

## Known issues

- Not played yet: disc 2 onwards, the ending, and the Linux and macOS builds.
- A `psx_freeze_dump_*.json` file appears next to the executable on every start; it is harmless and deleted at the next one.
- Widescreen: a small block of texture noise can sit at the right edge in some stages.
- Save states may not load in a later release; keep Memory Card saves for your progress.

Bugs and feedback: [`docs/PLAYTESTING.md`](https://github.com/victor-takai/dragon-valor-recomp/blob/master/docs/PLAYTESTING.md) says what to test and which files to attach; report with the [bug form](https://github.com/victor-takai/dragon-valor-recomp/issues/new/choose).
