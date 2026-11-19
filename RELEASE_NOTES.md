Dragon Valor (PlayStation, USA) running natively on PC, statically recompiled with [psxrecomp](https://github.com/RetroPortingToolKit/psxrecomp). This is a preview: it has been played into chapter 2, not beyond.

> Unofficial fan project, free and non-commercial; not affiliated with Bandai Namco or Sony. Dragon Valor belongs to Bandai Namco Entertainment. You need your own discs: the download carries the game's program, recompiled, and its cover art, but no graphics, audio, video or levels.

## How to run

1. Download the zip for your system and unpack it anywhere: `windows-x64` (Windows 10/11), `linux-x64`, `macos-arm64` (Apple Silicon) or `macos-x64` (Intel).
2. Run `DragonValorRecompiled` (Windows: the `.exe`). The executable is not signed, so Windows may say it protected your PC: *More info*, then *Run anyway*.
3. Point the launcher at your `Dragon Valor (USA) (Disc 1).cue` (`DISC.md` lists the dumps that work) and press Play.

No BIOS is needed. Controls are in `CONTROLS.md`.

## New in 0.2.0

- Play as (experimental, on by default): pick the hero of every action stage among all nine, with their model, moves, voice, portrait and name, story scenes included; give any hero any of the game's weapons. In the in-game menu, Hero section.
- A new in-game menu in the game's own look, easy with a pad, with confirmation dialogs; save states get their own screen with 12 slots, pictures and dates (`F7`, or Select + R1).
- Widescreen: the HUD sits at the screen edges, also during scene changes, stage reloads and the fade between rooms, and the scene changes run at full width.
- Smoother play where many enemies are on screen (the renderer runs on its own thread).
- A clearer Mods page in the launcher.

## Features

- Native 60 fps, rendered at 1080p by default, with PGXP for steadier 3D
- Widescreen (experimental, off by default)
- In-game menu (Esc, or Select + Start): pause, save states, Play as, texture filtering, scanlines, fullscreen, volume, disc change
- Cheats: infinite HP / MP, a gem or scroll every N kills, Val, stats, spell and item editors
- Skip Intro, rewind and turbo
- Xbox, PlayStation and Switch pads, and the keyboard

## Known issues

- Not played yet: disc 2 onwards, the ending, and the Linux and macOS builds.
- A `psx_freeze_dump_*.json` file appears next to the executable on every start; it is harmless and deleted at the next one.
- Save states from 0.1.0 may not load, and later releases may not load these; keep Memory Card saves for your progress.
- Widescreen: the soft-reset and ending-title transitions still show at 4:3; in the next-stage transition the middle turns light while the sides fade to black.

Bugs and feedback: [`docs/PLAYTESTING.md`](https://github.com/victor-takai/dragon-valor-recomp/blob/master/docs/PLAYTESTING.md) says what to test and which files to attach; report with the [bug form](https://github.com/victor-takai/dragon-valor-recomp/issues/new/choose).
