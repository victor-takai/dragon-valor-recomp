Dragon Valor (PlayStation, USA) running natively on PC, statically recompiled with [psxrecomp](https://github.com/RetroPortingToolKit/psxrecomp). This is a preview: it has been played into chapter 2, not beyond.

> Unofficial fan project, free and non-commercial; not affiliated with Bandai Namco or Sony. Dragon Valor belongs to Bandai Namco Entertainment. You need your own discs: the download carries the game's program, recompiled, and its cover art, but no graphics, audio, video or levels.

## How to run

1. Download the zip for your system and unpack it anywhere: `windows-x64` (Windows 10/11), `linux-x64`, `macos-arm64` (Apple Silicon) or `macos-x64` (Intel).
2. Run `DragonValorRecompiled` (Windows: the `.exe`). The executable is not signed, so Windows may say it protected your PC: *More info*, then *Run anyway*.
3. Point the launcher at your `Dragon Valor (USA) (Disc 1).cue` (`DISC.md` lists the dumps that work) and press Play.

No BIOS is needed. Controls are in `CONTROLS.md`.

## New in 0.5.0

- Play as nine characters of the story: the prologue's Dragon Valor, the grown Clovis, a skeleton, Raimun, Lodonya, Shalya, Fannah, Princess Jeanne, and Phillip with Kadum on his back. Each has its own look, portrait and name on the Status screen
- Their own sword-trail colours; the grown Clovis, Raimun and Lodonya also have their own voices, and the grown Clovis's special ends with his own tired pose and breath
- Hero > Moves: a story character fights with any hero's moves (Anna's or Anita's suit Shalya, Fannah and Jeanne)
- Two more looks: Mihael's green look and Gerome's later look (read from disc 2 once it has been started)
- New weapons: Raimun's sword, Jeanne's crystal sword and Kodel's cutlass
- Skip Intro goes straight to the title menu, and the black part of the boot runs faster: the menu is up in a few seconds
- Fix: disc-2 weapons no longer fall back to the hero's own when the game is started from a shortcut

## Features

- Native 60 fps at 1080p, with PGXP
- In-game menu with save states, cheats and settings
- Play as any of the nine heroes or nine characters of the story, with any hero's moves
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

Bugs and feedback: [`docs/PLAYTESTING.md`](https://github.com/victor-takai/dragon-valor-recomp/blob/master/docs/PLAYTESTING.md) says what to test and which files to attach; report with the [bug form](https://github.com/victor-takai/dragon-valor-recomp/issues/new/choose).
