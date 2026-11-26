Dragon Valor (PlayStation, USA) running natively on PC, statically recompiled with [psxrecomp](https://github.com/RetroPortingToolKit/psxrecomp). This is a preview: it has been played into chapter 2, not beyond.

> Unofficial fan project, free and non-commercial; not affiliated with Bandai Namco or Sony. Dragon Valor belongs to Bandai Namco Entertainment. You need your own discs: the download carries the game's program, recompiled, and its cover art, but no graphics, audio, video or levels.

## How to run

1. Download the zip for your system and unpack it anywhere: `windows-x64` (Windows 10/11), `linux-x64`, `macos-arm64` (Apple Silicon) or `macos-x64` (Intel).
2. Run `DragonValorRecompiled` (Windows: the `.exe`). The executable is not signed, so Windows may say it protected your PC: *More info*, then *Run anyway*.
3. Point the launcher at your `Dragon Valor (USA) (Disc 1).cue` (`DISC.md` lists the dumps that work) and press Play.

No BIOS is needed. Controls are in `CONTROLS.md`.

## New in 0.7.0

- Hero > Weapon: the grown Clovis's long sword, for any hero or story character
- The grown Clovis's charged double slash hits with both cuts
- Warp > Go now takes about 2 s with no GAME OVER, and keeps your Val, items and flags (with the Cheats package on)
- Widescreen: the dialogue border reaches the screen edges in scenes with the blur, like the prologue's flashback and the dragon fight

## Features

- Native 60 fps at 1080p, with PGXP
- In-game menu with save states, cheats and settings
- Play as any of the nine heroes or nine characters of the story, with any hero's moves or the grown Clovis's own
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
- Play as a story character: played in short sessions only, not yet through a boss or a whole chapter.
- The grown Clovis's ball of fire: with one of two spells at level 3 selected, L2 does nothing until the next stage; in one stage (7/34) it has no sound, for lack of sound memory.
- Warp > Go now: tried on a handful of disc-1 stages only.

Bugs and feedback: [`docs/PLAYTESTING.md`](https://github.com/victor-takai/dragon-valor-recomp/blob/master/docs/PLAYTESTING.md) says what to test and which files to attach; report with the [bug form](https://github.com/victor-takai/dragon-valor-recomp/issues/new/choose).
