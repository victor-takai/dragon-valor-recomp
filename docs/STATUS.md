# Dragon Valor (USA) Recompiled — status

Last updated: 2026-10-14. Framework: psxrecomp @ 54abcb70 (submodule), OpenBIOS only.
Sister project: `../DragonValorRecomp` (PAL, SCES-01705) — same game, 50 Hz; its
`docs/STATUS.md` holds the investigation history (controller, dissolve
rendering, first play session) that applies to this build too.

## Identity

| | |
|---|---|
| Discs | SLUS-01092 (disc 1), SLUS-01164 (disc 2) — USA, NTSC, 2 tracks each |
| Boot EXE | `SLUS_010.92` / `SLUS_011.64`, identical bytes (sha256 `ab40d44b…`) |
| Entry / load / text | `0x80012EA0` / `0x80010000` / `0x67800` |
| Generated C | 35 shards, 1,302,445 lines, 1962 dispatch entries |
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
- Release build: window LIVE, title bar `[Game 60 fps 1.00x]`, NTSC-U detected
  (59.94 Hz pacing).

## Open items

1. Human play session (menus, saves, Load Game, disc 2) — not done yet.
2. `game.toml` points at absolute local disc paths (development only).
3. Everything listed as open in the PAL project's STATUS.md (dissolve
   brightness at high internal scale, boot-time freeze dumps) is expected here
   too and has not been re-checked.

## Tooling

Same as the PAL project: `Play.bat`, `PlayDebug.bat`, `PlaySmooth.bat`,
`tools/dvdbg.py`, `tools/session_monitor.py`. See [`DEVELOPMENT.md`](DEVELOPMENT.md).
