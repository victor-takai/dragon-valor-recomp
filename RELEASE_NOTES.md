Dragon Valor (PlayStation, USA) running natively on PC, statically recompiled with [psxrecomp](https://github.com/RetroPortingToolKit/psxrecomp). This is a preview: it has been played into chapter 2, not beyond.

> Unofficial fan project, free and non-commercial; not affiliated with Bandai Namco or Sony. Dragon Valor belongs to Bandai Namco Entertainment. You need your own discs: the download carries the game's program, recompiled, and its cover art, but no graphics, audio, video or levels.

## How to run

1. Download the zip for your system and unpack it anywhere: `windows-x64` (Windows 10/11), `linux-x64`, `macos-arm64` (Apple Silicon) or `macos-x64` (Intel).
2. Run `DragonValorRecompiled` (Windows: the `.exe`). The executable is not signed, so Windows may say it protected your PC: *More info*, then *Run anyway*.
3. Point the launcher at your `Dragon Valor (USA) (Disc 1).cue` (`DISC.md` lists the dumps that work) and press Play.

No BIOS is needed. Controls are in `CONTROLS.md`.

## New in 0.4.0

- Widescreen: the story scenes with a motion blur (the prologue's red flashback, the trails of the dragon fight) stay wide instead of showing black bars
- Widescreen: the chapter title cards show at 4:3, so side-bar art can frame them
- `make_bezel.py`, next to the game: side-bar art for the screens made for 4:3, from the paintings of the game's illustration gallery, read from your own disc (Python 3; see the README, *Art in the side bars*)

## Features

- Native 60 fps at 1080p, with PGXP
- In-game menu with save states, cheats and settings
- Play as any of the nine heroes
- Watch a scene cut from the prologue, its Japanese lines in English
- Unlock the title menu's illustration gallery
- HD texture packs (DuckStation format)
- Widescreen, with the game's own art in the side bars if you like
- Skip Intro, rewind and turbo
- Xbox, PlayStation and Switch pads, and the keyboard

## Known issues

- Not played yet: disc 2 onwards, the ending, and the Linux and macOS builds.
- A `psx_freeze_dump_*.json` file appears next to the executable on every start; it is harmless.
- Save states may not load in other releases; keep Memory Card saves for your progress.
- Widescreen: a few transitions still show at 4:3, like the menus, the world map and the movies.

Bugs and feedback: [`docs/PLAYTESTING.md`](https://github.com/victor-takai/dragon-valor-recomp/blob/master/docs/PLAYTESTING.md) says what to test and which files to attach; report with the [bug form](https://github.com/victor-takai/dragon-valor-recomp/issues/new/choose).
