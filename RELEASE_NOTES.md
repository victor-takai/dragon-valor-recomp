Dragon Valor (PlayStation, USA) running natively on PC, statically recompiled with [psxrecomp](https://github.com/RetroPortingToolKit/psxrecomp). This is a preview: it has been played into chapter 2, not beyond.

> Unofficial fan project, free and non-commercial; not affiliated with Bandai Namco or Sony. Dragon Valor belongs to Bandai Namco Entertainment. You need your own discs: the download carries the game's program, recompiled, and its cover art, but no graphics, audio, video or levels.

## How to run

1. Download the zip for your system and unpack it anywhere: `windows-x64` (Windows 10/11), `linux-x64`, `macos-arm64` (Apple Silicon) or `macos-x64` (Intel).
2. Run `DragonValorRecompiled` (Windows: the `.exe`). The executable is not signed, so Windows may say it protected your PC: *More info*, then *Run anyway*.
3. Point the launcher at your `Dragon Valor (USA) (Disc 1).cue` (`DISC.md` lists the dumps that work) and press Play.

No BIOS is needed. Controls are in `CONTROLS.md`.

## New in 0.2.0

- Play as: play every action stage as any of the nine heroes, with their model, moves, voice, portrait and name, story scenes included, and give any hero any of the game's weapons. It is experimental and on from the start; the hero and the weapon are picked in the in-game menu.
- A new in-game menu in the game's own look, easy to use with a pad, that asks before anything you cannot undo.
- Save states have a screen of their own: twelve slots, each with a picture and the date it was saved. F7 or Select + R1 opens it.
- Widescreen keeps the HUD at the screen edges, also through scene changes, stage reloads and the fade between rooms, and the scene changes now fill the whole screen.
- Smoother play where many enemies are on screen: the renderer now runs on a thread of its own.
- A clearer Mods page in the launcher.

## Features

- Runs natively at 60 fps, rendered at 1080p by default, with PGXP for steadier 3D
- An in-game menu, opened with Esc or Select + Start, to pause, save and load states, pick the hero, warp to another stage, use the cheats, change the video and sound settings and swap discs
- Cheats: infinite HP and MP, a gem or scroll every N kills, Val, stats, spells and items
- Skip Intro, rewind and turbo
- Xbox, PlayStation and Switch pads, and the keyboard
- Two experimental options: Play as, on from the start, and widescreen, off until you switch it on in the launcher's Mods page

## Known issues

- Not played yet: disc 2 onwards, the ending, and the Linux and macOS builds.
- A `psx_freeze_dump_*.json` file appears next to the executable on every start; it is harmless and deleted at the next one.
- Save states from 0.1.0 may not load, and states saved now may not load in later releases; keep Memory Card saves for your progress.
- Widescreen: the soft-reset and ending-title transitions still show at 4:3, and in the transition to the next stage the middle of the screen turns light while the sides fade to black.

Bugs and feedback: [`docs/PLAYTESTING.md`](https://github.com/victor-takai/dragon-valor-recomp/blob/master/docs/PLAYTESTING.md) says what to test and which files to attach; report with the [bug form](https://github.com/victor-takai/dragon-valor-recomp/issues/new/choose).
