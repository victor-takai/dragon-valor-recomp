# Developing Dragon Valor Recompiled

How this repo is built, run and debugged on the machine it was brought up on
(Windows 11). Project state and known issues live in [`STATUS.md`](STATUS.md).

## Prerequisites

| Need | Where |
|---|---|
| Git for Windows (`bash` on PATH for the framework scripts) | `C:\Program Files\Git\bin` |
| Python 3.12+ (3.13 used); Pillow optional, for screenshot analysis | system |
| RetroPorting toolchain: cmake 3.31, ninja, clang 22 | `C:\retcomm-toolchain` (unzipped `cmake-clang-v1-windows-x64.zip`) |

Every build shell needs the toolchain first on `PATH` and the framework told
where it is:

```bash
# Git Bash
export PATH="/c/retcomm-toolchain/bin:$PATH" PSXRECOMP_TOOLCHAIN_DIR="/c/retcomm-toolchain"
```
```powershell
# PowerShell
$env:Path = "C:\retcomm-toolchain\bin;C:\Program Files\Git\bin;$env:Path"
$env:PSXRECOMP_TOOLCHAIN_DIR = "C:\retcomm-toolchain"
```

Discs are not in the repo. `game.toml` names them by Redump file name only
(`Dragon Valor (USA) (Disc N).cue`): the launcher asks the player for each
image and remembers it in `settings.toml`; the multi-disc roster matches a disc
by its file-name stem, so dumps must keep the Redump names. Convention for
scripted runs: the dumps sit **beside the repo**, as sibling folders named
`Dragon Valor (USA) (Disc N)` (here under `F:\dragon-valor-recomp\`); that is
the default of `tools/dvdbg.py launch`. Anywhere else, pass `--disc <cue>` or
set `DVDBG_DISC`.

## Build

```bash
git submodule update --init --recursive        # psxrecomp, recomp-ui, nested libs

# 1. recompiler + BIOS emitters (once per framework bump) -> build-recompiler/
bash psxrecomp/tools/ci/build_emitters.sh

# 2. game C from the disc (only when seeds or the framework pin change) -> generated/
python psxrecomp/psxrecomp_cli.py generate --config game.toml --project-root . \
    --disc "../Dragon Valor (USA) (Disc 1)/Dragon Valor (USA) (Disc 1).cue"

# 3a. player build (no debug server)            -> build-release/DragonValorRecompiled.exe
python psxrecomp/psxrecomp_cli.py rebuild --config game.toml --project-root . \
    --build-dir build-release --no-pgo

# 3b. debug build (TCP debug server on :4370)   -> build-debug/DragonValorRecompiled.exe
python psxrecomp/psxrecomp_cli.py rebuild --config game.toml --project-root . \
    --build-dir build-debug --no-pgo --cmake-extra=-DPSX_DEBUG_TOOLS=ON
```

Notes:
- `rebuild` forces `-DPSX_DEBUG_TOOLS=OFF`; the `--cmake-extra` in 3b is what
  turns the debug server on (later flags win).
- `generated/` is deterministic (35 shards, byte-identical across runs). It is
  committed; regenerate and commit together with the seed or pin change that
  caused it.
- `game.toml` / `game_options.toml` are re-staged next to the exe by the
  always-run `psx-stage-configs` target. After editing only `game.toml`, run
  `ninja -C build-release psx-stage-configs` (and the same for `build-debug`)
  or a full rebuild.
- Only OpenBIOS is linked (no retail BIOS C is generated).
- `generated/overlays_static*.c` is the AOT shard for the `BIN/*.BIN` mode
  modules; `generate` rebuilds it from `aot/overlays.json` (see [`AOT.md`](AOT.md)).
- The checkout folder's name is free; spaces work (this machine:
  `F:/dragon-valor-recomp/Dragon Valor Recomp`, renamed from
  `DragonValorUSRecomp` on 2026-10-16; `rebuild`, `generate` and the tests
  all ran there, `generated/` came out byte-identical). Build trees hold
  absolute paths, so after moving or renaming a checkout delete
  `CMakeCache.txt`, `CMakeFiles/` and `_deps/` in `build-release`,
  `build-debug` and `build-recompiler` (a stale `_deps/*-subbuild` cache
  stops the configure) and run steps 1 and 3 again. The player's
  `settings.toml` (`[memcard]` dir / card1 / card2) and `mods/state.toml`
  (texture pack path) also hold absolute paths: point them at the new
  folder, or the game opens empty Memory Cards.
- Names (2026-10-16, the player's choice): the window title and the
  packager's display name are "Dragon Valor Recompiled", the exe
  `DragonValorRecompiled` (CMake `EXE_NAME`; without it the framework
  derives `Dragon_Valor_Recompiled` from the title), release zips
  `DragonValorRecompiled-<version>-<platform>.zip`, `game.toml [game] name`
  "Dragon Valor" (shown by the launcher and the in-game menu; no file path
  depends on it, so saves and settings are untouched) and the CMake project
  `DragonValorRecomp`. The exe name is repeated in `Play.bat`,
  `tools/dvdbg.py`, `tests/test_menu.py`, `scripts/package_release.sh` and
  `codegen_setup.c`; the release workflow takes the zip prefix from the
  packager (it was renamed in place: `generate_ci.py --force` would drop
  this project's manifest-test step and release-notes body).

## Run

- `Play.bat` — player launch (`build-release`). `Play.bat -dbg` uses `build-debug`.
  The executable is a Windows-subsystem program (the framework links Release
  builds with `-mwindows`, `runtime.cmake`), so it opens no console of its
  own and double-clicking it is enough; the script starts it detached and
  closes its window at once (2026-10-15). `PLAY_WAIT=1` runs it in the
  foreground instead (`PlayDebug.bat` does, to keep a window with the exit
  code). The runtime never attaches to a parent console, so its stdout is
  only seen when redirected (`tools/dvdbg.py launch` writes `dvdbg_4370.log`);
  the exit report `psx_last_run_report.json` is the channel otherwise.
- `PlayDebug.bat` — debug build with the debug server and the FPS readout on.
- Frame generation (`[video] render_thread` / `frame_generation`, or
  `PSX_RENDER_THREAD=1 PSX_FRAME_GEN=1`) is of no use on this 60 Hz title and
  its launcher script was removed on 2026-10-14; the Settings page still
  exposes the switches.
- Both open the **recomp-ui launcher** first (disc check, Disc 1/2, pad mode,
  memory cards, Settings, Mods). `--no-launcher` or `PSX_NO_LAUNCHER=1` boots
  straight into the game.

Files the runtime writes, all gitignored:

| What | Where |
|---|---|
| Memory cards | `saves/card1.mcd`, `saves/card2.mcd` (project root, `[runtime] memcard_dir`); raw 128 KB images, a DexDrive `.gme` minus its 3904-byte header. The game's Load Game reads card 1 only. `card2.mcd` holds one save, the GameFAQs "Prolog Clovis 25:54" (entry 14, installed there on request 2026-10-14; a copy is also entry 14 on card 1). The game also writes card 2's write-test frame (block 0, frame 63; seen 2026-10-16, one second after a card 1 write), so the file date changes while the save itself stays as installed. |
| Launcher/user settings | `settings.toml` next to the exe (overrides `game.toml` defaults) |
| Title settings the launcher does not keep | `dragonvalor_settings.toml` next to the exe: `[audio] volume`, `[menu] pause_in_menu / dim / opacity`, written by the in-game menu provider (see *In-game menu*) |
| Keyboard binds | `keybinds.ini` next to the exe (generated on first run) |
| Host hotkeys / pad mapping | `config.ini` next to the exe (`[KeyMap]`, `[controller]`, `[mapping]`) |
| Overlay captures + native shards | `overlay_captures.json(.d)` and `cache/` next to the exe |
| Exit report / watchdog | `psx_last_run_report.json` (unknown dispatches, segment misses, overlay loader counters, CPU state at exit), `psx_freeze_heartbeat.json`, boot-time `psx_freeze_dump_*.json` (false positives, ~35 MB each on the debug build; `tools/dvdbg.py launch` and `Play.bat` delete the existing ones before each start, since 2026-10-15) |

## Bumping the framework

`docs/framework_pin_history.md` records every bump and the procedure (fetch,
read the upstream log and the title-facing diffs, check out, nested
submodules, rebuild both build dirs, boot + state + cheat checks). Regenerate
`generated/` only when `recompiler/` or the emitters changed upstream.

## Debugging a running game

The debug build listens on `127.0.0.1:4370` (JSON lines; see
`psxrecomp/docs/TCP_COMMANDS.md`). Two clients:

```bash
# project helper (launches muted and detached, skips the launcher)
python tools/dvdbg.py launch            # OpenGL on 4370
python tools/dvdbg.py launch --sw       # software renderer on 4371
python tools/dvdbg.py launch --pad      # let the runtime open a connected gamepad (scripted runs hide pads since 2026-10-15: a resting stick that reads full-left walks the hero out of every scene)
python tools/dvdbg.py launch --show     # show the game window (scripted runs keep it hidden since 2026-10-16: PSX_HIDDEN_WINDOW, so the player's fullscreen setting never covers the screen; the in-game menu, host_key, present_shot and screenshot all work hidden)
python tools/dvdbg.py press start cross down:8
python tools/dvdbg.py shot out.png      # guest display
python tools/dvdbg.py pad               # SIO pad: connected / analog / type
python tools/dvdbg.py health            # unknown dispatches + segment misses (must stay 0)
python tools/dvdbg.py raw video_info
python tools/dvdbg.py kill

# framework client (every command, key=value pass-through)
python psxrecomp/tools/debug_client.py --port 4370 frame
python psxrecomp/tools/debug_client.py --port 4370 press 0xFFF7 10      # Start, active-low word
python psxrecomp/tools/debug_client.py --port 4370 screenshot C:\tmp\a.png
python psxrecomp/tools/debug_client.py --port 4370 pad_status
python psxrecomp/tools/debug_client.py --port 4370 accurate_blending on=0
python psxrecomp/tools/debug_client.py --port 4370 debug_key key=1      # cycle internal scale
```

Useful facts:
- PS1 pad words are active-low: Start `0xFFF7`, Cross `0xBFFF`, Down `0xFFBF`.
- `ping` is answered by the io-thread variant without counters; use `frame`,
  `unknown_dispatch_log`, `segment_misses`, `pad_status`.
- `debug_key` only reaches the 0-9 / F10-F12 video toggles. The F key (FPS
  readout) is keyboard-only; `PSX_FPS_TELEMETRY=1` turns it on at start.
- `psxrecomp/tools/window_capture.py --title DragonValor --series 6` gives a
  LIVE/FROZEN verdict from what the monitor shows (screenshots come from VRAM
  and can lie about a frozen window).

### Save states

The runtime's save states (`savestate op=save|load slot=N`) are the shortcut
around the ~5 min intro, and **every automated test that is not about the
boot or the menus starts from slot 0** (slot 10 for the spell tests):

```bash
python tools/dvdbg.py launch --state 0  # boot, then load slot 0 and wait for it
python tools/dvdbg.py state load 0      # same thing on a running instance
python tools/dvdbg.py state save 0      # overwrite the slot from the running game
python tools/dvdbg.py state keep 0      # copy the slot into tests/states/ (committed)
```

Slot 0 is the first screen of gameplay (New Game → prologue → opening
cutscene → first enemy in Brize Forest; HP 030/030, MP 020/020). The runtime
reads slots from `saves/openbios/` (gitignored); the committed copy lives in
[`tests/states/`](../tests/states/README.md) and `state load` restores it
from there when the local slot is missing, so a fresh clone needs nothing
else. The state carries the loaded `ACTION.BIN`, so no part of the menu
route is replayed. States are whole-machine state: same BIOS (OpenBIOS),
same disc, and a runtime update that changes the format means re-taking
them (`state save`, then `state keep`).

A second committed state, **slot 10** (menu slot 11; `launch --state 10`),
is a town stage of area 1 / stage 7 with Fire selected (level 1) and an
enemy of 15 HP in front of the hero (HP 38/38, MP 38/38, Earrings 0, Val
1014): the start point for anything that needs a spell or a magic kill
(`tests/test_scroll_cheat.py`). The third, `slot09` (menu slot 10, since
2026-10-16), is the player's chapter-2 state: Kodel in the hold of a ship,
area 3 / stage 1, HP 36/36, MP 31/31, Heal selected, three pirates (see
*Character and weapon*): the start point for anything past chapter 1. Slots
the tools write: menu slot 1 (`slot00`), menu slot 10 (`slot09`) and menu
slot 11 (`slot10`), all restored from `tests/states/` by `launch --state N`
when missing or older, and menu slot 12 (`slot11`, `tests/test_menu.py`,
restored afterwards). Keep personal states in slots 2 to 9;
`tests/states/README.md` has the table.

### Known RAM addresses (USA EXE)

Found by pattern search over `read_ram` and confirmed by `write_ram` from slot
0 (the HUD and the Status screen follow the new values). psxdatacenter lists
the same HP/MP addresses as GameShark codes for SLUS-01092 (`8008A5AE 03E7`,
`8008A5B0 03E7`, `8008A5B2 03E7`).

| Address | Width | Meaning | Referenced by |
|---|---|---|---|
| `0x8008A5AE` | u16 | HP current (HUD shows 3 digits) | `func_800389F4`, `func_80038F0C` |
| `0x8008A5B0` | u16 | HP max | |
| `0x8008A5B2` | u16 | MP current | |
| `0x8008A5B4` | u16 | MP max | `func_80034908` |
| `0x8008A5B6` | u16 | STR as shown on the Status screen: a derived value, recomputed by `func_800358A8` as the sum of the five attack entries below and **never read by combat** (see *How damage is computed*) | `func_800358A8` (W) |
| `0x80086324`‥`2C` | u16 ×5 | Attack-power table of the current hero (new game: 6, 9, 12, 15, 18). A hit's power is the entry at index `0x8008632E`; a gem adds one unit (1, 1.5, 2, 2.5, 3, as two alternating byte rows at `0x80011598`) | `func_80040DCC` (R, pc `0x80041798`), `func_80034908` (gem pickup), `func_8003A7C4` (new game) |
| `0x8008632E` | u8 | Index into the attack table for the current hit; written 0 at every attack start (`func_800260FC`, pc `0x80026630`). Which move uses entries 1‥4 is unverified (a chained combo is the guess) | |
| `0x8008631E` / `0x80086320` | u16 ×2 | HP / MP mirrors kept by the action code (HUD bars) | ACTION.BIN |
| `0x800A2EDC` / `0x800A2EDE` / `0x800A2EE0` | u16 ×3 | Gems picked up / scrolls picked up / both, stepped by the floor-pickup handler; the drop rule stops at 8 gems, 5 scrolls, 10 of both (see *How drops are decided*) | `func_80034908` (W), `func_800187D4` (R) |
| `0x800A2DB0` / `0x800A2DB4` | u32 ×2 | Sword kills / magic kills since the last gem / scroll: stepped by the kill credit, compared with the drop tables, reduced by the met threshold when the gem / scroll is picked up | `func_80025650` (W), `func_800187D4` (R), `func_80034908` (W) |
| `0x800A2DA0` / `0x800A2DA4` | u32 ×2 | Lifetime sword / magic kills of the hero (stepped with the above, read by nothing) | `func_80025650` |
| `0x8008633C` | u8 | Non-zero suppresses every drop | `func_800187D4` (R), pc `0x800377C8` (W) |
| `0x8008633E` | u16 | MP cost of the spell being cast: the spell's script stores it through native handler 30 (`func_80029BAC`: cost − Earrings count) and spends it through handler 31 (`func_80029BDC`: MP −= it). The setter does not clamp at 0 | `func_80029BAC` (W), `func_80029BDC` (R) |
| `0x80086336` | u16 | +20 per **Bracelet** obtained (−20 on the undo, `func_80029098`); the game's own text for the Bracelet is "Reduces fatigue". Read by nothing in the EXE or the five BIN modules, and no data on disc 1 holds the address, so whatever fatigue is, the action module does not drive it from here. (Attributed to Pepper until 2026-10-15 through the one-slot-off item table below.) | `func_80028940` pc `0x80028F88`, `FloorPickup` pc `0x80035434` |
| `0x80086318` | struct | Stage / HUD state block the rows above belong to: `+0x00` hero index (u8, `table[area]`, see *Character and weapon*: 0 Clovis, 2 Kodel), `+0x06` / `+0x08` HP / MP mirrors, `+0x0C`‥`+0x14` attack table, `+0x1A` the 1200-frame timer of pickup 9, `+0x1E` the Bracelet (fatigue) counter, `+0x22` a mode byte (0 in play, 1 once the HUD's enemy counter is up), `+0x24` drop flag, `+0x26` spell cost | |
| `0x800A2D99` | u8 | Selected spell of the hero (0xFF = none; slot 0 has none and every level 0). `func_80037D94` SetCurrentSpell copies it into the HUD icon state (`0x800A2C0C` = 1, `0x800A2C10` = (3 × spell + level) × 2 + 15) | `func_80037D94`, ACTION.BIN `0x800BFFE4` |
| `0x8009FAF8` | struct | Spell ring of the action HUD (ACTION.BIN `0x800BFFE4`, pointer at `0x800C5E48`): `+0` current spell, `+1` state, `+2` ring index (u16; the D-pad moves it, action `0x100` confirms, action `0x80` selects), `+0xC` countdown | ACTION.BIN |
| `0x800A2F64` / `0x800A2F68` / `0x800A2F80` | u32 ×3 | Pad words: held / newly pressed / repeat. High half = raw pad bits (D-pad 0x10 Up, 0x20 Right, 0x40 Down, 0x80 Left; 0x1000 Triangle … 0x8000 Square), low half = game actions after the button configuration. Slot 0's configuration, measured with `press` on 2026-10-15: Square 0x01 (attack), Circle 0x08, Cross 0x82 (0x80 = magic), Triangle 0x120, L1 / L2 0x10, R1 / R2 0x04, Start 0x40, Select nothing | ACTION.BIN, MAP.BIN |
| `0x800A2D20`‥`0x800A2D5F` | u8 ×64 | **Save event flags**, the head of the save block (`0x800A2D20` is the base the code builds; selected spell is `+0x79`, play time `+0x7C`, Val `+0xB0`). A pickup object whose `+0x24C` is non-zero sets byte `[+0x24C]` to 1 when taken (`func_80034908`, pc `0x80034A94`‥`AB8`, then `+0x238` ← 0 retires the object); door scripts check these flags, not the key counts (2026-10-15, player's castle save: Gold Key object flag 1, a hand-written count opened nothing). Cleared with the save block by the routine at `0x80038B3C`‥`78`. | `func_80034908` (W) |
| `0x800A6A98` | struct ×32 | Pickup pool, 32 objects of 0x250 bytes (entries 32‥45 hold unrelated data; the `li t7, 46` loop at `0x80033E4C` counts something else): `+0x238` active (counts down to 0 = free slot, the drop vanishes), `+0x244` pickup id, `+0x24C` save-flag index set on pickup (0 = none; coins carry one too) | `func_80034024` (spawn), `func_80034908` (pickup), `func_80035A5C` |
| `0x800A2DB8`‥ | u16 ×N | Pickup counters by id (0 gem, 2 scroll, 12 one Val, 13 ten Val, 14 HP max up, 15 MP max up, 16 attack up, 17 DEF up; index 12 is the Val field itself); ids from 14 on are the inventory list below, 13 is the ten-Val coin and 20‥27 count the spell books | `func_80034908`, `func_80028940` |
| `0x8008A5B8` | u16 | DEF | `func_80038094`, `func_80038274` |
| `0x800A2D9C` | u32 | Play time in frames (Load list shows it as h:mm; the GameFAQs save holds 5,594,735 = 25:54) | |
| `0x800A2DD0` | u16 | Val (gold; Status shows 4 digits, 10000 reads 0000) | `func_8004BF84` |
| `0x800A2E06`‥`0D` | u8 ×8 | Magic levels: Heal, Defense, Fire, Ice, Thunder, Mine, Stealth, Vortex (0 = not learned, 1‥3 = LEVEL) | |
| `0x800A2E18`‥`1A` | u8 ×3 | Three flags that are 0 in slot 0 and 1 in the post-clear GameFAQs save (gallery / cleared-path candidates, unconfirmed) | |

| `0x800A2DD4`‥`0x800A2DDE`, `0x800A2DF0`‥`0x800A2E04` | u16 ×17 | Inventory counts in the Status screen's grid order: Pendant, Lithograph, Ring, Talisman, Bracelet, Earrings (`0x800A2DD4`‥`DE`), then Pepper, Wine, Horn, Perfume, Tea, Mask, Tiara, Harp, Gold Key, Silver Key, Copper Key (`0x800A2DF0`‥`0x800A2E04`); `0x800A2DE0`‥`EE` between them count the eight spell books (ids 20‥27). Verified 2026-10-15 by setting each slot to 1 and reading the Status screen, which also gave the item texts the in-game menu shows (accessories, trade goods, keys; **no weapons**). Until that day the table started at `0x800A2DD2` and named the slots one off, so the menu's Items page edited the wrong count for everything but Earrings; Almar's CodeBreaker list had it right. | |
| `0x800A2F18` / `0x800A2F1A` | u16 ×2 | Area and stage id of the stage to load (DEMO.BIN writes 0/6‥9 and 7/31‥34 before calling `func_8003A9B0`); slot 0 is 0/2 | `func_80025264` (W), `func_80038094`, … |
| `0x8007D8D0` | struct | Stage-start record: `func_80038094` copies the hero's numbers into it when a stage starts, and the CONTINUE YES branch's `func_80038274` copies them back. `+0xF6` (`0x8007D9C6`, u16) is the stage id: `sh` at pc `0x80038158` stores `0x800A2F1A` there (skipped in areas 10‥12 below stage 29), `sh` at pc `0x80038298` writes it back to `0x800A2F1A`. The area is not in it, which is why only the area survived a Continue. The menu's Stage row writes it. | `func_80038094` (W), `func_80038274` (R) |
| `0x8008634C` / `0x8008634D` | u8 ×2 | Game mode and sub-state: 8 = action stage, 9 = GAME OVER (sub 2 = the CONTINUE prompt), 10 = loading, 17 = attract demo and the CONTINUE reload | `func_8004BF84`, `func_80025264`, … |
| `0x8009FAF8` | struct | Work area of the current screen; in mode 9 / sub 2 it is the CONTINUE prompt (ACTION.BIN `0x800C51F0`, jump table `0x800BC1F4`): `+0` state (0 GAME OVER fades in, 1 CONTINUE? fades in, 2 waits for a button, 3 blinks the choice, 4 acts on it), `+0x0C` frames left in state 3, `+0x10` text brightness (128 = full), `+0x1A` cursor (s16, 0 = YES). State 2 tests `0x800A2F68` bit `0x80` (Cross or Circle; Start does nothing) and then writes cursor-blink 128 / timer 30 / state 3 (`0x800C58CC`); YES in state 4 (`0x800C55DC`) takes 50 Val, refills HP and sets mode 17. Sub-state 1 resets `+0` to 0. | ACTION.BIN (`tools/bin_listing.py`) |
| `0x8008A4E0` | struct | Player object; the stats above are its `+0xCE`‥`+0xD8` fields. Objects are 0x250 bytes apart: slot 0 is the hero, enemies sit in later slots with the same layout (slot 0's first screen: the enemy is slot 5, `0x8008B070`, HP 15/15 at `+0xCE`). Enemy fields of the drop rule: `+0x7F` drop class (0 = never drops), `+0xBC` kind of the hit that last touched it (0‥4 sword, 5‥27 magic, copied from the hit object's `+0xC0`; 255 before any hit), `+0xC` kill credit (2 sword, 1 magic, 3 none) | |

`write_ram` writes one byte per call (`addr=`, `val=`). The GameShark codes
circulating on GameRevolution are correct for Val (`800A2DD0 2700`) and the
magic levels (`300A2E06`‥`0D 0003`); its HP/MP codes (`800A5AE0 0064`, …) are
the addresses above with one hex digit dropped. The "beat the first boss,
sell items, pause, press Up Down Left Right Select" cheat from the same page
has no button-sequence table in the EXE or the BIN modules and did nothing
when tried from slot 0; its precondition (first boss beaten) was not met, so
it stays unverified rather than disproved.

**Public cheat codes (checked 2026-10-15).** DuckStation's cheat database
(`duckstation/chtdb`, the source of its `cheats.zip`) has no file for
SLUS-01092 or SLUS-01164, so the backlog idea of shipping its codes as
declarative patches had no source. The CodeBreaker list on Almar's Guides
(NTSC-U) and the three GameShark codes on PSX Data Center agree with the
table above for HP/HPmax/MP/MPmax (`8008A5AE`‥`B4`), STR/DEF
(`8008A5B6`/`B8`), Val (`800A2DD0`), the gem/scroll counters (`800A2DB8`,
`800A2DBC`; it calls id 1 at `800A2DBA` the Chalice, a pickup not seen yet),
the spell levels (`300A2E06`‥`0D`) and **every item** (`800A2DD4`‥`DE`,
`800A2DF0`‥`800A2E04`): the Status screen confirmed its item layout on
2026-10-15 and corrected ours, which had started one slot early (*Pickup
ids and what they do*). "Kill bosses in one hit" writes 0 to
`8008B13E`, object slot 5's HP (`0x8008B070 + 0xCE`): the code assumes the
boss sits in that slot. Every one of them is a per-frame RAM write
(GameShark types 80/30), which a package `[[patch]]` cannot express (it
patches EXE bytes before boot); all are already in-game menu rows except
the one-hit kill, the *attack ×N / one-hit* idea in `BACKLOG.md`. No code
list is reproduced in the repository.

#### How damage is computed (2026-10-14)

The player reported that a very high STR changed nothing in play, while a
gem (dropped after a run of physical kills) raised both the number and the
damage. The live EXE and ACTION.BIN were dumped from RAM and scanned for
every access to the hero block, including base-register ones the static
listing cannot attribute:

- `0x8008A5B6` (STR) is written by exactly one function in the whole EXE,
  `func_800358A8`, which zeroes it and adds the five u16 at `0x80086324`;
  the only readers are the Status screen and the HUD digit painter in
  ACTION.BIN. Nothing in combat reads it.
- `func_80040DCC` (hero combat) takes a hit's power from
  `attack[0x8008632E]` at pc `0x80041798` (or, for non-sword hits, from the
  hit object's own byte). Damage = power `>>` the target's resistance entry
  (`+0x15F` table of the target object, 0 for the first enemy), plus an
  optional table term, at least 1; there is no DEF subtraction on that path.
  `func_800260FC` applies it to the target's HP (pc `0x80026D04`).
- Floor pickups are applied by `func_80034908` (write-traced on 2026-10-15;
  see *How drops are decided*): id 0 (gem) adds a byte row of `0x80011598`
  to the five entries, alternating `1,2,2,3,3` and `1,1,2,2,3` by gem
  count, i.e. one unit of the 1 : 1.5 : 2 : 2.5 : 3 progression the
  new-game values (`func_8003A7C4`: 6, 9, 12, 15, 18) already follow; id 2
  (scroll) adds 8 to MP max (cap 100); ids 14‥17 are the HP max / MP max /
  attack / DEF consumables. `func_80028940` holds the same per-id effects
  behind the jump table at `0x8001131C`, but it is not the floor-pickup
  path and its trigger was not identified.
- Hit test from slot 0 (`tools/ramscan/hit_test.py`; the scans above are
  `tools/ramscan/scan_fields.py` and `taint_scan.py` over `dump_ram.py` images): with the five
  entries written to 500 the first connecting swing took the enemy from
  15 to 0 HP; from a fresh slot 0 the first connecting swing took it from
  15 to 9, i.e. entry 0 (6) verbatim.

The in-game menu's STR row therefore writes the five entries (spread in the
game's proportion, the last one absorbing the rounding) and the STR field;
`func_800358A8` later recomputes the same total. A written STR survives a
save because the save path copies the entries (`func_80038094` /
`func_80038274`). DEF is read on the hero-takes-damage path (`lh +0xD8` in
`func_80042A54` / `func_80042FF0`), so the DEF editor was already real.

Useful test route: writing 0 to HP gives GAME OVER → CONTINUE? in ~5 s, and
Cross on YES reloads the current stage from disc (stage id restored from the
stage-start record `0x8007D9C6`, so `0x800A2F1A` is overwritten). Area values 1‥3 written
before CONTINUE leave the game black in mode 10 (CD-ROM status polling, no
crash); 0 and 7 reload normally.

#### How drops are decided (2026-10-15)

The player also noticed that a scroll turns up after a run of magic kills
and a gem after a run of sword kills. That is the game's rule, not luck.

When an enemy dies, `func_80025650` (kill credit) reads the kind of the hit
that killed it (byte `+0xBC` of the enemy, copied from the hit object's
`+0xC0` by the hero combat code): kinds 0‥4 are sword moves and set the
credit `+0xC` = 2 and step the sword-kill counter `0x800A2DB0`; kinds 5‥27
are spells and set credit 1 and step the magic-kill counter `0x800A2DB4`;
anything else (28 and up, or the 255 an object starts with) leaves credit 3
and counts nowhere. `0x800A2DA0` / `0x800A2DA4` are lifetime copies of the
two counters that nothing reads.

`func_800187D4` (death effects, called from the per-frame object loop
`func_800208D8`) then chooses the drop of every dying enemy, in this order:

1. Nothing while the flag `0x8008633C` is set, and nothing from an enemy
   whose drop class (byte `+0x7F`) is 0.
2. If no gem or scroll is lying on the floor (`func_80035A5C` scans the 32
   pickup objects at `0x800A6A98` for id 0 or 2):
   - credit 2, sword kills ≥ `gemTable[gems]`, gems < 8 and gems + scrolls
     < 10 → **gem** (pickup id 0). `gemTable` at `0x8006CAA8` = 15, 22, 28,
     36, 48, 70, 105, 200;
   - else credit 1, magic kills ≥ `scrollTable[scrolls]`, scrolls < 5 and
     gems + scrolls < 10 → **scroll** (id 2). `scrollTable` at `0x8006CAB8`
     = 2, 4, 7, 12, 20.
3. Otherwise the random table: roll 0 with 50 %, 1 with 25 %, 2 and 3 with
   12.5 % each; item = `dropTable[4 × class + roll]` at `0x8006CA74` (13
   classes × 4 bytes, value = pickup id + 1, 0 = nothing). The first enemy
   of slot 0 is class 2 = {nothing, Val coin, Val coin, nothing}. Ids in the
   table: 3‥8 restores, 12 one Val, 13 ten Val, 9 and 10 unidentified (9
   starts a 1200-frame timer at `0x80086332`; 10 queues 5 HP of damage and
   gives 2 MP).

Spawning is `func_80034024(x, y, z, +0xE0 of the enemy, id)`. Picking the
gem or scroll up (`func_80034908`, the floor-pickup handler ACTION.BIN calls
every frame) subtracts the threshold that was just met from the counter, so
the tables are kills *between* drops and the surplus carries over: 15 sword
kills for the first gem, 22 more for the second, … 200 for the eighth; 2,
4, 7, 12 and 20 magic kills for the five scrolls; at most ten of both for
one hero (`func_80027D58` zeroes the counters and the gem / scroll counts
when a chapter starts). The gem then adds one row of `0x80011598` to the
attack entries (STR +9 for the first), the scroll adds 8 to MP max.

Verified from slot 0 (`tools/ramscan/drop_test.py`): with `0x800A2DB0`
written to 15 the first sword kill left a gem on the floor (pool slot 0,
id 0), the enemy read credit 2 / kind 1, and walking onto the gem took the
counter 16 → 1, gems 0 → 1 and the entries 6/9/12/15/18 → 7/10/14/17/21
(STR 60 → 69); with the counter untouched the same enemy left a Val coin
(id 12) and the counter read 1. The scroll path was confirmed by the player
on the pad the same day (debug build, write traces on the counters): two
enemies killed with magic stepped `0x800A2DB4` 0 → 1 → 2 (pc `0x800256F0`,
called from `func_800260FC` right after the HP write; the sword counter
untouched), the second kill left a scroll, and picking it up ran
`func_80034908`: counter 2 → 0, scrolls 0 → 1, both 1 → 2, MP max 21 → 29.
Casting a spell from a script works from the second test state (solved
2026-10-15 with the player's save, `tests/states/` slot 10 = menu slot 11,
`launch --state 10`): a town stage of area 1 / stage 7, Fire selected at
level 1 (Mine 1 is the only other spell), HP 38/38, MP 38/38, Earrings 0,
an enemy of 15 HP in front of the hero, out of the tutorial. There `press
circle:8` (action bit `0x08`, the game's default Magic button) casts at
once: the cost field `0x8008633E` takes the spell's cost minus the
Earrings count (Fire I costs 3) and MP drops by it within 0.3 s; one or
two casts kill the enemy in object slot 5. Why
slot 0 never cast: its hero knows no spell (`0x800A2D99` = 0xFF, every
level 0) and it sits inside the first stage's tutorial (mode 8 sub 5),
where even a forced level + selection + cast state (`0x800A2C0C` = 1,
icon `0x800A2C10`) did nothing on Cross (`0x80`, the magic-type select) or
Circle. Two things to know when measuring: the cheats package's
`infinite_mp` tops MP up to the maximum every VBlank, so with it on the
deduction is invisible (run the measurement with a bare `mods/state.toml`,
as `scratchpad cast_probe7.py` does), and the top-up never clamps, so an MP
above the maximum stays. `tests/test_scroll_cheat.py on|off` is the
magic-kill counterpart of `test_drop_cheat.py`: from slot 10 it casts Fire
until the enemy dies and reads the pickup pool (2026-10-15: on, the
counter `0x800A2DB4` read 6 = scrollTable[2] − 1 before the kill and a
scroll, id 2, lay on the floor after it; off, 0 → 1 and no scroll; 5/5
each). Pad action bits measured on slot 0's configuration:
Square `0x01` attack, Cross `0x82` (`0x02` jump + `0x80` magic-type
select), Circle `0x08` cast, Triangle `0x120`, L1/L2 `0x10`, R1/R2 `0x04`,
Start `0x40`; spell-ring struct `0x8009FAF8` (`+0` current, `+1` state,
`+2` index, `+0xC` pointer).

**Cheat.** `dragonvalor.cheats` 1.3.0 has `gem_every` / `scroll_every`
(launcher Mods page and the in-game Cheats page): each VBlank the plugin
keeps the matching kill counter at least `threshold − N`, so the game's own
code drops on the N-th kill of that kind; 0 leaves the rule alone and the
8 / 5 / 10 caps and the one-on-the-floor rule stay (previous section,
*This project's package*).

**Pickup ids and what they do** (the two effect routines; amounts read from
the code, not timed in play): 0 gem, 2 scroll, 3 / 4 / 5 HP +10 / +30 /
full, 6 / 7 / 8 MP +10 / +30 / full, 9 a 1200-frame timer at `0x80086332`
(effect unidentified), 10 queues 5 HP of damage and gives 2 MP, 11
nothing, 12 Val +1, 13 Val +10, 13‥38 also step a counter at
`0x800A2DB8 + 2 × id` (13 the ten-Val coin, 14 Pendant, 15 Lithograph, 16
Ring, 17 Talisman, 18 Bracelet, 19 Earrings, 20‥27 the spell books, 28
Pepper, 29 Wine, 30 Horn, 31 Perfume, 32 Tea, 33 Mask, 34 Tiara, 35 Harp,
36 Gold Key, 37 Silver Key, 38 Copper Key: the Status screen's grid order,
confirmed 2026-10-15), 20‥27 raise the matching spell level by one (Heal …
Vortex, max 3). Any pickup object with a non-zero `+0x24C` also sets save
flag byte `0x800A2D20 + that index` to 1: keys carry one, and the locked
door of the stage checks the flag rather than the key count, so a key
count written by hand opens nothing. The in-game menu's key rows therefore
"take" the stage's key objects of that kind (flag set, object retired)
when the count goes up (confirmed in play, 2026-10-15: the castle's
locked door opened after the Gold Key was raised in the menu). Some inventory items
carry a permanent bonus applied **when they are obtained** through
`func_80028940` (reached through the function pointer at `0x8006FFF4`,
presumably from the field module MAP.BIN): Pendant (14) HP max +1 (cap
100), Lithograph (15) MP max +1 (cap 100), Ring (16) one row of
`0x800115A4` (0,0,0,1,1 / 0,1,1,0,0, alternating) on the attack entries,
Talisman (17) DEF +1 (cap 999), Bracelet (18) +20 to `0x80086336` (the
game calls it "Reduces fatigue"; no reader in the EXE or the five BIN
modules, no data on the disc holds the address). The four bonuses match
the Status screen's own texts word for word (Increases H.P. by 1,
Increases M.P. by 1, Increases attack power, Increases defense by 1); the
trade goods (Pepper, Wine, Horn, Perfume, Tea, Mask, Tiara, Harp) and the
keys carry no bonus. The action-stage pickup handler `func_80034908` gives its HP max
/ MP max pickups +8 (cap 100) instead. Earrings (19) are the one count read
live: every spell's script stores its MP cost through native handler 30
(`func_80029BAC`: cost − Earrings count → `0x8008633E`) and spends it
through handler 31 (`func_80029BDC`: MP −= that), so each Earring makes
every cast 1 MP cheaper, as the GameFAQs guide says. The setter does not
clamp, and the u16 underflow is real (measured 2026-10-15 on the first
version of test state 10, a cave stage with Fire at level 3, cheats off):
Earrings 0 → cost 4, MP 37 → 33; 1 → 3; 3 →
1; 4 → cost 0, MP unchanged; 5 → cost 65535, MP 37 → **38**; 50 → cost
65490, MP 37 → **83** and 129 after a second cast, above the 37 maximum
and never clamped (the HUD bar draws it as full). So with Earrings ≥ a
spell's cost every cast *adds* MP. Editing a count on the menu's Items page
therefore changes the count and nothing else (Earrings aside, where 5 or
more turn Fire into a free MP source).

**Object scripts and native handlers.** Every object runs a bytecode
script from `func_800260FC` ObjectUpdate; one opcode calls a native handler
out of the 32-entry table at `0x80011170` (copied to the stack at entry,
dispatched at pc `0x80026D54` with the object and a pointer to the script's
arguments): 0 / 1 `func_80024BA4`, 2 `func_80024F18`, 3 `func_80025264`,
4 `func_80025650` EnemyKillCredit, 5 `func_800250B4`, … 30 `func_80029BAC`
SpellCostSet, 31 `func_80029BDC` SpellMpDeduct. The scripts live in the
stage data, so a grep of the modules never shows who uses a handler;
`func_80028940` / `func_80029098` (pickup apply / undo) are reached through
the pointers at `0x8006FFF4` / `F8` the same way.

### Character and weapon (2026-10-14 status)

Not swappable by a RAM poke yet. Chapter 1's hero (Clovis) is decided by the
game's chapter progress, the hero's model and the Status portrait are loaded
from `DATA.CDR` at stage start, and the weapon is part of the hero: the
inventory holds no weapons, and STR is the only weapon-related number. Nothing
in the stats struct, the inventory block or the area/stage pair changes the
hero, and the field that selects the hero had not been identified (2026-10-14).

**Chapter-2 reference point (2026-10-16).** The player's save state at the
start of chapter 2 (their menu slots 3 and 12, copied to slot 4 and backed up
out of tree; area 3 / stage 1, the hold of a ship with three pirates; hero
**Kodel**, HP 36/36, MP 31/31, STR 73, DEF 0, Val 433; Heal 2, Defense 1,
Fire 2, Mine 1, Stealth 1; Pendant ×2, Lithograph ×4, Ring, Mask, Harp; every
save event flag 0, cleared at the chapter start), diffed against slot 0 over
`0x800A2D00`‥`0x800A3200`, `0x80086300`‥`0x800863FF`, the hero object and
the model-selection block:

- The hero index is `0x80086318` (+0x00 of the stage block above) and it is
  **derived from the area**: `table[area]` with the byte table at
  `0x8006CC70` = 0 0 1 2 3 4 5 6 7 8 7 6 5 (areas 0‥12), stored at stage
  start by `func_80027D58` (pc `0x80027D8C`); slot 0 (area 0) reads 0, Kodel
  (area 3) reads 2. `func_800282E8` stores 8 there while summing the save
  block (an epilogue / gallery path, unverified). The byte is not the
  switch: poking it (0 on Kodel, 2 on Clovis, before or during the GAME
  OVER prompt) changes nothing visible. A stage script also stores it
  (`func_800260FC`, the `sb v0, 0(a2)` at pc `0x80026630`, a script "store
  byte" native): stage 0/2 stored 0 when the hero died; on Kodel's stage
  nothing rewrote it through a death and CONTINUE, so a value poked before
  the death survives the reload.
- **The hero follows the area** (verified 2026-10-16, cheats off, through
  GAME OVER → CONTINUE): Kodel's save with `0x800A2F18` = 0 written before
  or during the prompt reloads area 0 / stage 1, the prologue, with
  **Clovis's model** and Kodel's numbers (HP 36/36, MP 31/31, Heal
  selected). Clovis's save with area 3 asks for area 3 / stage 2, a short
  scene-type stage (20 files, a large image first; `tools/datacdr.py stage
  3 2`), and the reload stays black in mode 10 (the earlier "areas 1‥3 go
  black" note was this). The **stage id cannot be changed this way**: whatever is
  written to `0x800A2F1A` before the prompt, the reload comes back with the
  stage the game recorded at stage start (3/2 → 3/1, 0/1 → 0/2): the YES
  branch calls `func_80038274`, which copies the stage-start record's
  `+0xF6` (`0x8007D9C6`) back over it at pc `0x80038298` (an earlier `wtrace`
  missed that write; its dump then returned only the 256 oldest entries).
  **Stage warp (2026-10-16):** writing the area to `0x800A2F18` and the
  stage to `0x8007D9C6` (the record alone is enough), then HP 0 and YES,
  loads that stage. From the Brize Forest test state (0/2), 0/3 loaded a
  playable action stage (mountain path, HP 30/30, hero in control 4 s
  later). 0/5 is the prologue's closing story scene: it loads and plays
  (Cross pages through it, about 35 pages) but every line of its text is
  garbage glyphs, and when it ends the story moves on by itself (the
  chapter narration "Clovis headed for Raxis", then Raxis Border, area 1).
  The glyphs are real characters in the wrong order, which points at the
  text being indices into a glyph sheet that the normal way into a stage
  loads and a Continue (which normally reloads the same stage, sheet
  still in VRAM) does not (open: BACKLOG L). The in-game menu's
  Experimental section has **Area (next Continue)** and **Stage (next
  Continue)** rows for this, and **Go now** makes the trip; by hand: write them, let HP
  reach 0 (or the HP editor), YES on CONTINUE. Beware: the area sits in the save
  block (`+0x1F8`), so a Memory Card save made afterwards keeps it.
- Save block: only the play time, two kill counters (`0x800A2DA4` = 19,
  `0x800A2DA8` = 16), Val, the inventory, the spell levels, area / stage,
  `0x800A2F74` (0x16F → 0xA4) and 8 bytes at `0x800A2F78` differ. There is no
  separate chapter field.
- Stats block: the HP / MP mirrors, the attack table (7, 11, 14, 19, 22, index
  byte 2 instead of 1), `0x80086310` = 1 (0 in slot 0; 18 references in
  `0x80038F44`‥`0x800393F0`, unexplored) and `+0x3B` = 2.
- The block at `0x8009E840` is not a hero selector: it holds the render
  lists every model is drawn from (see *DATA.CDR layout and the hero*).

Widescreen on that stage: native-wide engaged (mode 2, 128 columns), real
scenery in the extra width, no blemish seen in the hold (`ws_nw`, two
window shots, health 0 / 0).

What it took to get here:

1. A second reference point: a save (memory card or save state) in a later
   chapter with another hero, then a RAM diff against slot 0 around
   `0x800A2Dxx`‥`0x800A31xx` and `0x80086xxx` to find the hero/chapter field.
   The only public save, the GameFAQs DexDrive card at
   <https://gamefaqs.gamespot.com/ps/197154-dragon-valor/saves> ("All Gallery
   Mode Images Unlocked", 131 KB, uploaded by RPGer800, download link
   `/saves/23784`), was loaded on 2026-10-14 and does **not** help: its one
   1-block save (`BASLUS-0109214`, Load list entry 14 "Prolog Clovis 25:54")
   is a post-clear prologue. Loading it replays the prologue text and the
   opening cutscene (Start skips both) and lands in Brize Forest with the
   same Clovis, HP 30/30, MP 20/20, STR 60, DEF 1, Val 0. A RAM diff against
   slot 0 over `0x800A2D00`‥`0x800A3200` differs only in the play-time
   counter and three flag bytes (table above). So a later-chapter save still
   has to come from the human play session, or from a GameShark-style search
   while playing.

   Memory-card notes from that test: the `.gme` is a 3904-byte DexDrive
   header followed by the raw 128 KB card, which is exactly the runtime's
   `saves/cardN.mcd` format (strip the header, or export with MemcardRex).
   **Load Game only scans memory card 1**: with the card in `card2.mcd` the
   screen says "Dragon Valor data not found"; in `card1.mcd` it lists the
   save. The Load list has 15 numbered entries, one per card block; the
   entry number is the two digits after the product code in the file name
   (`BASLUS-01092` + `14`). The save block (8 KB, `SC` + 3 icon frames) is
   laid out as: `+0x200`‥`+0x380` three 128-byte records with an identical
   48-byte head; `+0x380` a 16-byte hash, then play time as hours/minutes
   u16 (`+0x394`, `+0x396`); `+0x3A0` onward a byte copy of RAM
   `0x800A2D98`‥`0x800A2EF8` (play time, Val, inventory, magic, flags);
   `+0x500` a short record (`06 09 0C 0F 12`, `0100`) and at `+0x524` the
   persistent stats HP/HPmax/MP/MPmax/STR/DEF as u16; `+0x560` to the end a
   table of u32 values `0x000806BA`, `+1`, `+2`, … (1704 entries). No
   readable "Clovis"/"Prolog" string exists in the EXE, the BINs or the
   disc, so the hero and chapter names are drawn from ids through an
   encoded font; the id is somewhere in the head records or the `+0x500`
   record, and a second save with another hero is what settles it.
2. Confirm by writing the field and reloading through GAME OVER → CONTINUE.
3. For a cosmetic weapon/hero change without game logic: HD texture
   replacement (OpenGL, `psx.enhancement.hd-textures` dumps the originals) or
   a `[[overlay]]` on the model data inside `DATA.CDR`.

### DATA.CDR layout and the hero (2026-10-16)

Traced from the CD read log (`cd_read_log` with `tail` = the transfers of
one reload) of three GAME OVER → CONTINUE reloads (Clovis 0/2, Kodel 3/1,
Kodel's save forced to area 0 = stage 0/1) and the render lists in RAM.
`tools/datacdr.py` reads the same structures from the player's disc
(`areas`, `stage A S`, `hero A S`).

- **Disc layout.** `DATA.CDR` sector 0 is the area table (13 × [min sec
  frame 0] offsets of the area headers, +1; disc 1 holds areas 0‥6, disc 2
  areas 7‥12, its table restarts at 0). An area header is a container whose
  section 1 is the stage table (offsets of each stage index from the
  header; area 0 has stages 1‥9, the others 1‥39). A stage index is a
  container whose section 3 lists the start of every file of the stage
  ([m s f 0] from the index); a file runs to the next start and the game
  reads exactly that many sectors. A stage is an **action stage** when it
  carries an action pack (below); the others are scene-type stages (story
  scenes, maps) and they interleave differently in every area, e.g. area 1:
  odd stages are action, even ones scenes (`tools/datacdr.py kinds`). Files are mostly containers (u32 count,
  count + 1 u32 offsets, data) with sections tagged `VAB_CODE` (sound bank),
  `TIM_CODE` (a TIM with that text in place of the 0x10 magic) or
  `SEQ_CODE`. The hero byte's address appears nowhere in this data.
- **Load order of a stage.** ACTION.BIN to `0x800BBEF0`; the area header and
  the stage index to `0x8007CE50`; the stage's first file (sound bank +
  TIMs) to `0x800C5E98`, uploaded and then overwritten by the second file,
  the **action pack**, which stays at `0x800C5E98` for the whole stage
  (146‥153 sectors, 149‥157 entries); then sound banks, sequences, more
  TIMs and two model containers (at `0x80127698` / `0x80178698` in stage
  0/2).
- **The hero is in the action pack** (pointers checked in RAM): entry 0
  holds the render nodes (the hero's 14 at `e0+0x10`, 0x70 bytes each, node
  `+0x68` → body-part packets, `+0x6C` → its vertices; the hero object at
  `0x8008A4E0` points at entry 2), entry 1 the hero's sound bank (the same
  for Clovis in areas 0 and 1), entry 2 skeleton / animation data (one per
  area group: identical in stages 0/1 and 0/2, different in castle stage
  1/19 with the same Clovis), entries 3‥16 the 14 body parts as **GPU
  packet templates** (UV, CLUT, texture page; the game rewrites them every
  frame, so RAM never matches the disc), entries 17‥30 their **vertex
  data** (read-only), and four more hero-only entries (35, 59, 115, 116;
  the rest of 31‥150 is common to every stage). The hero's texture page is
  the TIM at VRAM (384, 0) with CLUTs at (384, 245‥255) in the stage's
  first file; Kodel adds a small page at (464, 0) / CLUT (448, 0). The
  prologue hero of stage 0/1 (purple hair, armour) and Clovis of 0/2 share
  entries 1 and 2 and differ in 3‥30 and the page, so the look is entries
  3‥30 plus the page.
- **The render lists** at `0x8009E840`: 0x800-byte lists of node pointers
  with their counts at `+0x1000`; list 0 in stage 0/2 = 14 stage-object
  nodes, then the hero's 14. `func_800213C8` walks them (generic model
  renderer; per-primitive-type drawers `func_80022604`, `func_80022770`,
  `func_80022920`, …).
- **Swap experiments** on a scratch copy of the disc image (the runtime
  checks no sector EDC/ECC, so edited user data loads; a mod would ship the
  same bytes as a `disc_user` `[[overlay]]`), reloaded through GAME OVER →
  CONTINUE with cheats off:

  | Swap | Result |
  |---|---|
  | Kodel's entries 1, 2, 19, 35, 59, 115, 116 into Clovis's 0/2 | runs; Clovis unchanged (entry 2 is not the look) |
  | Clovis 0/2's entries 3‥30 and page into Kodel's 3/1 | hero drawn as long spikes, game keeps running |
  | the same plus entries 1, 2, 35, 59, 115, 116 | guest crash (address-error exception loop in the BIOS handler) or full-screen garbage; hero byte 0 does not help |
  | same area: 0/2's entries 3‥30 into the prologue 0/1 | loads, then memory corruption about 4 s later (the same warp on the stock disc runs on into stage 0/2) |

  So the hero's look is located, but those entries are bound to something
  else per stage: likely the 0x110-byte table at the head of each packet
  template (16-byte records whose high halves differ per stage), or stage
  scripts that touch hero parts by offset. Not a plain data swap. Next steps
  in `BACKLOG.md`, section F.

### Modding: what the framework offers

`psxrecomp/docs/MOD_PACKAGES.md` is the reference. A `.psxmod` package
(`mods/preloaded/packages/<id>/<version>/manifest.toml`, bundled by the build,
or installed by the player through the launcher's Mods page) can carry:

- **`[[patch]]`** — guarded byte patches on `main_exe` (guest address,
  expected bytes, replacement; bounded-integer options encode into them).
  Right for one-shot changes of EXE code or data (e.g. starting Val/levels).
- **`[[overlay]]`** — file-backed replacement of disc ranges by LBA with
  SHA-256 guards (`disc_sha256` of the stock image required). Right for asset
  swaps inside `DATA.CDR` (models, textures, text).
- **`[[plugin]]`** — a C plugin statically linked into our exe and registered
  by id: function-entry hooks on functions listed in `[recompiler]
  mod_function_entry_funcs` (regenerate afterwards), a deterministic VBlank
  callback (per-frame RAM writes = GameShark-style cheats or a hero/weapon
  override), guarded instruction callbacks. This is how a "choose hero" mod
  would be done once the field is known.
- **HD texture packs** and **string translation** are built-in features with
  their own docs.

#### This project's package: `dragonvalor.cheats`

`mods/preloaded/packages/dragonvalor.cheats/1.3.0/manifest.toml` (format 7)
declares **one feature, `cheats`, on by default with every option off**, backed
by `mods/plugins/dragonvalor_cheats.c`. The feature stays in the session's
plan so the in-game menu can switch its options live; with every option off
the plugin does nothing. The package carries no native code, only the plugin
id; the C file is linked by the `target_sources` line in `CMakeLists.txt`.

| Option (launcher group "Cheats") | Each VBlank, while HPmax ≠ 0 |
|---|---|
| `infinite_hp` | HP ← HPmax |
| `infinite_mp` | MP ← MPmax |
| `gem_every` (0‥15, 0 = off) | while gems < 8 and gems + scrolls < 10: sword-kill counter `0x800A2DB0` ← max(itself, `gemTable[gems]` − N), so the game's own drop code (*How drops are decided*) leaves a gem on the N-th sword kill; after the pickup subtracts the threshold the floor is raised again |
| `scroll_every` (0‥5, 0 = off) | the same for the magic-kill counter `0x800A2DB4`, `scrollTable[scrolls]` and scrolls < 5 |

One-shot edits are not options: the in-game menu writes Val, max HP/MP,
STR, DEF, the eight spell levels and the seventeen inventory counts straight
into RAM (next section). A "Val floor" option existed in 1.1.0 and was
dropped: a value that keeps resetting is not what a player wants from a gold
cheat.

The plugin reads the options at activation and again in its option-changed
callback (`psx_mod_register_option_changed_plugin`), which is what makes a
change from the in-game menu (`PsxMenuHost.mod_set` →
`psx_mod_set_option_live`) take effect the same frame. The build stages the
package into `<build>/mods/bundled/dragonvalor.cheats`; the launcher's Mods
page and the in-game menu both write `<build>/mods/state.toml`. Verified
2026-10-14 from slot 0 on build-debug: with the options on, HP/MP written to
5/3 read back 30/20 within half a second; with them off the written values
stayed; toggled from the menu, HP refilled at once. The drop options were
verified the same way on 2026-10-15 (`tests/test_drop_cheat.py on|off` for
the gem from slot 0, `tests/test_scroll_cheat.py on|off` for the scroll from
slot 10, a Fire kill): with
`gem_every = 1` the counter read 14 before the first sword kill of slot 0 and a
gem lay on the floor after it; with 0 the counter read 1 and a Val coin
dropped. Limitation
shared with GameShark codes: the refill runs once per frame, so a single hit
of at least HPmax still kills (writing HP = 0 produced GAME OVER).

To add an option: read it in `dv_cheats_read_options`, apply it in the VBlank
callback, declare the `[[option]]` in the manifest (and a row in the menu
provider if it should be switchable in-game), rebuild both build dirs.
`tests/test_manifests.py` accepts a default-on feature only while every
option defaults to off. Scripted test without the launcher: set the option
values under the `cheats` feature in `<build>/mods/state.toml` (format 2;
values are strings under `[feature.values]`), then `tools/dvdbg.py launch
--state 0`, `write_ram` the stat and read it back (`tests/test_cheats.py
on|off` does exactly that; `tests/test_drop_cheat.py on|off` kills the first
enemy of slot 0 and looks for the gem).

#### In-game menu (`mods/plugins/dragonvalor_menu.cpp`)

Escape, or Select+Start on the pad, opens a menu over the game; the guest
and audio pause while it is up (the "Pause in menu" row turns that off).
Keyboard: arrows, Enter / Space, Backspace, Escape. Pad: D-pad, Cross,
Circle, Start closes. **Mouse (2026-10-15):** the provider feeds SDL mouse
events to ImGui while the menu is open (coordinates scaled from window to
framebuffer), so hovering selects, a click activates a row or a section, the
-/+ buttons step, the wheel scrolls and the right button goes back; closed,
the mouse is not touched. Checked with the debug server's `host_mouse x= y=`
(`scratchpad mouse_probe.py`): a click on System entered the page, two
clicks on the State slot `+` stepped 1 → 3, clicks on Audio and Display
switched pages. Row descriptions were shortened the same day on the
player's request, and the Magic and Items rows carry the game's own texts
(read off the Status screen with every spell at level 3 and every item at
1, `scratchpad status_all.py` / `status_items2.py`). Sections and rows:

| Section | Rows | Applied through |
|---|---|---|
| Display | Fullscreen, Game dimming, Menu opacity | `PsxMenuHost.video_apply` (live; also saved to `settings.toml`); the backdrop rows are the menu's own |
| Graphics | Linear filter, Texture filtering (Nearest / Bilinear / xBR), Scanlines, Dynamic resolution | `video_apply`, live |
| Audio | Volume | `host_volume_set` |
| System | Resume, State slot (1‥12), Save state, Load state, Pause in menu, Return to launcher, Quit | `savestate_request_save/load` (slot − 1), `request_exit`. Return to launcher asks for `PSX_MENU_EXIT_RELAUNCH` in builds without the netplay lobby client (ours): the framework's in-process return re-enters the launcher only inside its lobby-client block and otherwise ends the program (player report 2026-10-16; still so on upstream master 379565a6), while a relaunch restarts the executable with its own arguments, which opens the launcher as at boot. Builds with `PSX_NETPLAY` keep `PSX_MENU_EXIT_TO_FRONTEND`. |
| Cheats | Infinite HP, Infinite MP, Gem every N sword kills, Scroll every N magic kills (`mod_set` on `dragonvalor.cheats` / `cheats` → live option change, saved to `mods/state.toml`; the two counts are integer options, 0 = the game's rule); Val, Max HP, Max MP, STR, DEF | the five editors show the value in RAM and write it at once (`psx_mod_write_half`), greyed out until a hero is loaded; STR is spread over the five attack entries at `0x80086324` (the shown STR is their sum, see *How damage is computed*) |
| Magic | Heal, Defense, Fire, Ice, Thunder, Mine, Stealth, Vortex: Not learned / Level 1‥3, each with the game's description (Heal "Recovers H.P. & damages undead", Defense "Increases defense & weapon range", Fire "Fireball attack or ignite devices", Ice "Ice attack or freeze flame platforms", Thunder "4 Thunder attacks or electrify devices", Mine "3 Controlled Mine attacks", Stealth "Invisibility deters enemy attacks", Vortex "Controlled enemy entrapment"; level-3 costs 14 / 5 / 4 / 5 / 8 / 5 / 12 / 7 MP) | bytes `0x800A2E06`‥`0D`, written at once |
| Items | Pendant, Lithograph, Ring, Talisman, Bracelet, Earrings, Pepper, Wine, Horn, Perfume, Tea, Mask, Tiara, Harp, Gold Key, Silver Key, Copper Key: 0‥99, in the Status screen's order and with its texts (the first five are bonuses applied when found, Earrings cut every cast by 1 MP, the rest are trade goods and keys; raising a key count also takes that kind of key in the stage's pickup pool, see *Pickup ids*) | u16 counts `0x800A2DD4`‥`DE` and `0x800A2DF0`‥`0x800A2E04`, written at once |
| Game | Skip intro movie, Insert disc 1 / 2 | `mod_set` feature switch (next start), `disc_insert` (the runtime's tray policy) |
| Experimental (the last section; Area, Stage and Go now moved here from Cheats on 2026-10-16 at the player's request, because the warp has known limits: garbage text in story scenes, skipped stages' items and keys) | Area (next Continue: `0x800A2F18`, the area the next GAME OVER → CONTINUE loads, see *Character and weapon*; areas of the current disc only) and Stage (next Continue: the stage-start record `0x8007D9C6`, which the Continue copies over `0x800A2F1A`; added 2026-10-16). Both only offer action stages, from the bitmask table `tools/datacdr.py kinds --c` prints, because a scene-type or empty stage left the screen black until the watchdog closed the game. The first version had no Stage row and kept the stage number, so a late stage offered few areas (stage 21 is an action stage only in areas 3 and 6, the player's report); now the Area row moves the stage to the nearest action stage of the new area when needed (stage 1 is one in every area), and the Stage row steps over scene stages. Stages that open with a story scene play it with garbage text after the jump (see *Character and weapon*); **Go now (Game Over)**, an action: writes HP 0, holds Infinite HP off (`dv_cheats_hold_hp`, shared with the cheats plugin, 60 s safety net from the GAME OVER) and closes the menu. HP 0 makes ACTION.BIN (`0x800C40E0`) set mode 9 at once, with no death animation: the hero stays on its feet and the world keeps running under GAME OVER, so the first version, which pressed Cross on the prompt every 800 ms, made the hero jump (player report 2026-10-16; the prompt ignores buttons until its state 2). Now a title pad transform (`psx_mod_set_pad_transform`, player 0; offline only) is the per-frame hook: as soon as the prompt starts (mode 9, sub 2, prompt state ≤ 2 at `0x8009FAF8`) it writes what the prompt's confirm branch writes (cursor YES, brightness 128, timer 30, state 3), and it hands the guest a neutral pad until the action stage runs again, so nothing the player presses moves the hero. Before the GAME OVER the pad stays the player's: the game holds the death back while a dialogue or story scene runs (still mode 8; the Brize Forest tutorial delays it by about 8 s), and a scene that waits for Cross would otherwise never end (up to 5 min). Then it writes back the Val the Continue took (50) and lets Infinite HP act again. The transform returns the stock pad outside a trip and is detached the next time the menu opens. 8‥12 s per trip in `tests/test_area_row.py`, 15 s from the tutorial. Besides Val, a Continue changes `0x800A2DA8` (+1, probably a death / continue counter), `0x800A2EFC` and `0x800A2F08`‥`0F` (unknown) and the play time | Area and Stage are RAM rows like the editors (`0x800A2F18`, the record `0x8007D9C6`), restricted to action stages; Go now is an action with a pad transform |

How it is built: psxrecomp f9c287f5 ships the host side of an in-game menu
(`psx_menu_host.h`: pause, live video, mod changes, disc tray, exit) and
calls a `PsxMenuProvider` the title registers with `psx_menu_set_provider`
before the game window opens; recomp-ui ships the menu model and its Dear
ImGui presentation (`recomp_runtime_ui.h`: `recomp_runtime_ui_create_standard`
with the standard rows plus our extras, `recomp_runtime_ui_handle_input`
with semantic inputs, `recomp_runtime_ui_render_imgui`). The provider is the
glue: it creates the model and an ImGui context with the OpenGL3 backend
(`#version 330`, the runtime's context; font `assets/fonts/LatoLatin-Regular.ttf`),
sees every SDL event first (`event`), maps Escape / arrows / Enter /
Backspace and the pad's D-pad / Cross / Circle / Start to the model, and
draws from the runtime's overlay draw callback (GL thread) under one mutex
with the event path (main thread). No SDL platform backend for ImGui is
used: the model navigates itself, so ImGui is only ever touched by `draw`.
Two things cost time: the model closes itself on BACK at the top level, so
the provider re-syncs its open flag (and the host pause) after every input;
and the model keeps its rail position between opens, so the provider creates
a fresh one on every open (the menu always starts on Display).

The look is recomp-ui's shared design system (the `psx` theme tokens:
accent colours, row height, radius), drawn by its `runtime_ui_imgui.cpp`;
the provider draws nothing itself. Two things that drawer lacked at pin
41c88ff are patched locally (`patches/recomp-ui/0001-…`, applied by
`CMakeLists.txt` at configure time, see `patches/README.md`): the page did
not scroll to follow keyboard/pad navigation, so rows past the eighth of the
Items page were unreachable by eye, and the selected row only had the subtle
header fill; the patch scrolls the selected row into view and frames it with
an accent outline plus a left bar. A second patch (`0002-…`, 2026-10-15)
wraps a row's description inside the row and lets the row grow, after the
player saw the longer cheat descriptions run past the drawer's edge. Both
are meant to go upstream as one PR.

The file compiles to nothing when the recomp-ui launcher is off
(`RECOMP_LAUNCHER`). Rows that need a restart (internal resolution,
renderer) are left out on purpose; the launcher owns them. The game's own
Start pause still exists. Scripted test: `tests/test_menu.py` (keys through
the debug server's `host_key`, screenshots through `present_shot`, since
native screenshots never contain the overlay).

**What each menu row persists to (2026-10-15).** Three stores, none of
them this file's choice except the last:

| Rows | Where | How |
|---|---|---|
| Fullscreen, Linear filter, Texture filtering, Scanlines, Dynamic resolution | `settings.toml` (the launcher's) | `PsxMenuHost.video_apply` posts the change; the main thread applies it and calls `save_user_settings` (checked live: two steps on Texture filtering rewrote the key) |
| Cheats, gem/scroll every N, Skip intro | `mods/state.toml` | `PsxMenuHost.mod_set` records it in the catalogue and saves |
| Magic levels, Items, Val, stats | nowhere | one-shot RAM writes; the game's own save carries them |
| State slot | nowhere | a session choice |
| **Volume**, **Pause in menu**, **Menu dim**, **Menu opacity** | `dragonvalor_settings.toml` next to the exe (ours) | see below |

The runtime keeps the host master volume in `host_osd.c`
(`host_volume_get/set`); the launcher seeds its Settings stepper from it and
re-applies it when it returns, but `settings.toml` has no key for it
(`config_loader.cpp` writes `[audio] frequency` / `spu_hq` only), so every
start came back at 100 whatever the player had set in the launcher or in
this menu; the menu's own rows lived in the provider's struct. The provider
now keeps the four in `dragonvalor_settings.toml` (`[audio] volume`, `[menu]
pause_in_menu / dim / opacity`): read and applied by its static initializer,
before `main`, so the launcher's stepper is seeded with the volume; written
whenever a value is seen to differ, by the rows themselves, at attach (the
launcher stepper) and at detach (the numpad hotkeys). No file = the
defaults, and nothing is written until a value moves. The path comes from
`SDL_GetBasePath()`, resolved once and copied: under SDL3 (this build) the
string is SDL's and must not be freed; the first version freed it and the
heap damage surfaced as a guest `DISPATCH FATAL: misaligned target` a second
later. Round trips checked on build-debug (`scratchpad volume_probe.py`,
`settings_probe.py`): file 37 → the Volume row shows 37 at the next start,
two steps → 47 in the file, relaunch → 47; file dim 20 / opacity 80 / pause
off → the menu opens with that backdrop and Pause in menu Off, two steps on
Menu dim (step 10) → `dim = 40`, Return on Pause in menu → `true`, relaunch keeps
them. The proper fix for the volume is an upstream `[audio] volume` key in
the settings file (`BACKLOG.md`).

#### This project's package: `dragonvalor.skip-intro`

`mods/preloaded/packages/dragonvalor.skip-intro/1.0.0/manifest.toml`: one
feature, off by default, that presses Start for the player as soon as the
intro movie starts, the skip the game itself offers, so power-on reaches the
title with no button. Plugin: `mods/plugins/dragonvalor_skip_intro.c`.

How the boot sequences (intro module at `0x800BBEF0`, mode byte
`0x8008634C`, sub-state `0x8008634D`, traced with `wtrace_arm`):

| Mode | What | Transition |
|---|---|---|
| 0 | boot | |
| 1 sub 0‥5 | Namco logo: loads, fade in, hold, hold, fade out (~6 s from frame ~490) | the fade-out handler does `mode++; sub = 0` when its stage timer (`*(0x800D8DD0)+4`) runs out |
| 2 | intro movie `MOVIE/MOVIE1.STR` (~31 s) | Start → `func_800BD038` tears the movie down and does `mode++` |
| 3 sub 0‥1 | title logo animation | by itself after ~26 s, or earlier |
| 5 sub 0 → 17 sub 0 → 5 sub 2 | title still (~5 s) → menu load → **menu ready** (New Game / Load Game / Configuration) | |
| 17 sub 1 → 8 sub 1‥ / 4 sub 16‥13 → 17 → 8 | story demo and attract gameplay (DEMO.BIN) | back to the menu later |
| 6 | New Game (prologue text) | from the ready menu |

Start cannot be injected from a VBlank callback: the game runs its own SIO
pad exchange inside its per-frame pad routine (`func_80069F0C`, driver
`func_8006A330`) and only then copies the response (pad struct `+0x3C`,
`0x800BBC78` for pad 1: id `0x73`, `0x5A`, buttons active-low, sticks) into
its pad struct (`+0x30`, `0x800A2F20`; buttons at `0x800A2F22`, held word
`0x800A2F66`) through `func_8006C250`. That copy is listed in `game.toml`
`[recompiler] mod_function_entry_funcs`, which makes the recompiler emit
`psx_mod_function_entry()` at its entry (one line in
`generated/SLUS_010.92_full_34.c`; regenerate after editing the list), and
the plugin's function-entry hook clears the Start bit in the response (a0 =
pad struct) there. Details that cost a day:

- The movie player reacts to a press **edge**: a response that always
  carries Start never skips. The plugin presses 4 frames every 24.
- Without a controller the SIO response is not refreshed every frame, so
  the plugin also writes Start *released* in its off phases and during the
  first 60 frames of mode 3.
- The Namco logo is not skippable: ending it early (poking `mode = 2`, or
  expiring its stage timers from sub 2 or 3) sends the title into the story
  demo; cutting only sub 4 stalls the title animation. Its stages evidently
  cover loads the title depends on.
- After the skip the title behaves exactly as after a manual Start: in
  some runs it goes title still → menu, in others it plays the story demo
  first (`17/1 → 8/1`, or `4/16 → … → 17/0 → 8/1`) and reaches the menu
  afterwards. Which one happens looks like a race in the game's own title
  logic (same with a debug-server press); worth watching during the human
  session.
- On the ready menu (mode 5 sub 2) Start = New Game (mode 6). A Start on the
  title still (5 sub 0) or on the animation starts the story demo instead.

`tests/test_skip_intro.py` (fresh boot, feature enabled in `state.toml`)
checks that the title animation starts within 14 s and that the title then
moves on; `--off` checks the movie keeps playing.

#### This project's package: `dragonvalor.enhancement.widescreen` (2026-10-15)

Native-wide widescreen for the stages, **off by default**; the feature's
description says it is experimental (it carried `channel = "experimental"`
and the launcher's amber EXP tag until 2026-10-15, dropped on the player's
request). On PSX the
framework offers no widescreen switch of its own: `psxrecomp/docs/WIDESCREEN.md`
(*Config reference*) says a title's package selects the view at activation,
and `tomba.enhancement.widescreen` / `r4.enhancement.widescreen` are the
worked examples this one follows.

- **Renderer.** Native-wide (`[widescreen] native_wide = true`, the default):
  the game's projection is untouched and the GPU rasterises the columns past
  the 4:3 frame into a wider surface, so the extra width is real scenery.
  `[video] aspect_ratio` stays `"4:3"`; with the package off every build is
  the stock picture (`tests/test_widescreen.py off`: ws mode 0, window 4:3).
- **Plugin** `mods/plugins/dragonvalor_widescreen.c`, id
  `dragonvalor.widescreen`: at activation it reads the `aspect` option
  (`16:9` default, `21:9`, `32:9`, `Fit` = 16:9 then follow the window,
  no upper limit) and calls `psx_mod_set_fixed_display_aspect` (+
  `psx_mod_set_adaptive_display_aspect(0, 0)` for Fit). The window opens at
  that aspect (960×540 for 16:9 against the 4:3 960×720). Changing the
  option takes effect at the next start (the view changes culling and
  projection mid-session, as R4 notes).
- **Scene classification.** `[widescreen] gte_game_mode = true`: a frame
  that projects vertices through the GTE is gameplay (wide); the Namco logo,
  the intro movie, the title, its menus and story movies project nothing and
  stay 4:3 pillarboxed. The pause screen also projects nothing (the stage is
  frozen), so 0.75 s after Start the picture snapped to 4:3; the plugin's
  world-scene predicate (`psx_mod_set_world_scene_predicate`) therefore
  reports gameplay for every frame with the mode byte `0x8008634C == 8` (the
  action module; sub-state 5 in play, 7 paused), sampled once per VBlank and
  re-read after a save-state load. The status screen (Triangle) keeps its
  panel 4:3-centred with the wide stage behind it. FMV detection is separate
  (`mdec_recently_active`) and unaffected.
- **Numbers.** The stage is 384 pixels wide (`0x180`, `video_info
  display_w`): 16:9 reveals 128 extra columns (64 per side, `ws_nw
  nw_extra`), 21:9 reveals 288. Debug-server knobs used for the A/B without
  the package: `display_aspect num= den= [adaptive=1]`, `window_size w= h=`,
  `ws_nw` (mode 2 = native-wide engaged), `gpu_state` → `ws.*`
  (`game_mode`, `present_native_43`, `x_margin`), `present_shot path=` for
  the window image (`shot` is the canonical 4:3 VRAM only).
- **HUD** stays at its 4:3 positions (HP/MP bars 64 columns in from the
  left edge at 16:9, enemy counter the same from the right). The
  framework's `nw_hud_corners` thirds shift is left off: ENHANCEMENTS.md
  records composite tears (dialogue boxes split) on the title that tried it,
  and this game's tutorial boxes span the full width.
- **Culling: left stock** (widened at 14:00 on 2026-10-15, withdrawn the
  same evening). `psxrecomp-analyze SLUS_010.92 --scan-widescreen
  --emit-ws-sites` (`cmake --build build-recompiler --target
  psxrecomp-analyze`) finds two functions with the width+height signature:
  the mesh renderer's per-vertex `sltiu 385` at `0x8001FE1C` and
  `func_800401D8`'s object cull (`slti 385` at `0x80040334` /
  `0x8004034C`, `|object − camera| < 385`), plus the overlay copies
  `0x800BECB8` / `0x800C2B80`; `auto_screen_x = true` widened them all (a
  regen of 11 s whose diff is exactly the substitutions). The player then
  found a 54 × 22 block of texture noise at the right edge of the castle
  corridor (area 1 / stage 19). `ws_census` + `wtrace` traced it: shaded
  textured quads at screen X 381‥462, written by the mesh packet writer
  `func_80023130` (pc `0x80023234`) for a model that `func_800213C8`
  draws from its single call site `0x80021094`; texture page (640, 0),
  CLUT (640, 245), which holds no texture in that stage. It follows the
  camera. **Correction 2026-10-16:** `func_800213C8` is the generic model
  renderer of the render lists at `0x8009E840` (stage objects, enemies and
  the hero; see *DATA.CDR layout and the hero*), not a hero-only renderer,
  and the hero's 14 body meshes use the page at (384, 0) in that stage
  (`tools/datacdr.py hero 1 19`), so the parked part is not one of them. The
  parts table at `0x8006DE70` points into the hero's skeleton entry, so a
  hero-attached piece is still possible; the owner is unverified. The
  block is still there with every cull back to stock, so it is not a cull
  artefact: the game parks an unused model part some 220‥300 px right of the
  hero, where the 4:3 draw area clips it on a console and the wide surface
  does not. The widening therefore bought nothing visible (the first
  forest and town captures already drew to both edges: the stock object
  cull reaches 193 px past the 4:3 edge, enough for 16:9 and 21:9) and the
  tree was regenerated back to stock culls (`[widescreen.cull]
  auto_screen_x = false`, no sites). What would hide the block: pushing
  the parked part further out from a hook on `0x800213C8` once its owner
  is known, or an upstream rule that skips textured
  primitives outside the canonical frame whose texture page was never
  uploaded in the stage (`BACKLOG.md`). The stale-cache note still applies:
  every regen moves the cache tag, and an old `cache/gcc/<tag>` directory
  next to the exe makes the loader log `OVERLAY CACHE HASH MISMATCH`, a
  false alarm while the static shard serves every overlay; delete it.
- **Checked** on build-debug, OpenGL, 1080p, PGXP on, from save state 0:
  `tests/test_widescreen.py on` 5/5 (mode 2, 128 columns, still wide with
  the pause up, window 16:9) and `off` 4/4; 0 unknown dispatches, the
  static shard still serves every overlay (no runtime compiles), re-run
  after the regen together with `tests/test_menu.py`. Not checked: later
  chapters, the field module (`MAP.BIN`) scenes, in-stage movies, the
  software renderer, a real resize with Fit.

#### Video and audio defaults (measured 2026-10-14)

`game.toml` ships `texture_lod = "mipmap"` with `anisotropic_filtering = 4`
and keeps `[audio] buffer_ms` at the 180 ms default; `antialiasing_mode`
stays off and the CD Speed mod stays off. The picture filters are written
out too (the player's choice, 2026-10-14): `antialiasing = true`, linear
filtering of the final image (the in-game menu's *Linear filter* row), and
`texture_filtering = "nearest"`, the game's own textures keep their texels
(bilinear and xBR are one row away in the menu, Graphics > Texture filtering,
for the run). Both are the framework defaults; `Play.bat` no longer forces
xBR. How the rest was decided, on build-debug from slot 0:

- **Mipmap + anisotropic** (live `texture_lod mode=1 aniso=8` at 1440p):
  60.0 fps kept; the far-ground edge energy in a static Brize Forest shot
  was unchanged (27.4 → 27.5), i.e. this side-on stage barely minifies its
  textures. The option only touches proven 3D world primitives and acts
  against shimmer in motion, so it ships on; `PSX_TEXTURE_LOD=0` turns it
  off for one run.
- **FXAA** (`post_aa mode=1`): 60.0 fps, edge energy 27.4 → 25.2, but it
  also softens the HUD and the window already downsamples a 1440p image, so
  it stays off. Try `PSX_AA_MODE=fxaa`.
- **Audio buffer**: `audio_stats` reported 0 pump underruns and 0 output
  underruns at 180, 90 and 60 ms over ~90 s of gameplay plus a stage reload,
  muted (SDL dummy driver). That test is worthless for the real device: with
  90 ms the player heard choppy sound on the title screen the same day, so
  `buffer_ms` is back at the 180 ms default. Lower it only with the audio
  device playing and `audio_stats` watched.
- **CD Speed**: CONTINUE → gameplay after GAME OVER takes 3.5 s with the
  mod off and at 2×, 4× and 8× (0 unknown dispatches each time): the reload
  is paced by the game's own fades, not by the drive. No reason to ship it on.

#### PGXP (on by default since 2026-10-14)

The runtime is built as the **PGXP hook flavour** (`PGXP` in
`psxrecomp_add_game_runtime`): the generated C already carries the hook
sites, so `generated/` did not change, but the one executable now tracks GTE
precision through every vertex path. On the base flavour the
`psx.enhancement.pgxp` mod corrected almost nothing on this title (only
`swc2` is shadowed there). PGXP is armed by the mod, which ships **on** through
`mods/preloaded/packages/psx.enhancement.pgxp/1.0.0/manifest.toml` (the
builtin manifest verbatim with `default_enabled = true`, shadowing the
framework copy under the same id and version) plus `[video] pgxp_mod_only =
true` in `game.toml`, so the Mods page row is the one switch and netplay runs
with it off (ENHANCEMENTS.md G1.11/G1.12).

A/B on build-debug from slot 0 (`scratchpad pgxp_ab.py`: save slot 11, load
it twice, `pgxp geometry= texture=` live, `screenshot_hires`,
`geom_correction` census): with PGXP on the census counted 214,218 vertex
lookups, 141,168 dataflow hits (66 %), 73,050 native, 2,160 value
mismatches; 36.5 % of the 1920×1200 pixels moved between the two shots (the
whole 3D scene shifts by sub-pixel amounts, the HUD does not); no cracks or
missing polygons in Brize Forest; frame rate while walking 60.0 → 60.0
frames/s. Save states are flavour-specific: `tests/states` was re-taken on
this flavour with `tests/retake_state0.py`. Precise culling and CPU-mode
propagation stay off (the manifest's defaults).

#### HD texture packs on this game

`psx.enhancement.hd-textures` (bundled) works here, OpenGL only. Dumping
captures texture **uploads**, so a session that starts from a save state
captures nothing until the game uploads again: force a stage reload (GAME
OVER → CONTINUE, or any stage change). One reload of Brize Forest gave
67 PNGs (740 KB; 4-bit paletted parts of 64×64 and smaller: ground tiles,
tree trunks, the hero and the first enemy) in
`<build>/mods/texture-packs/SLUS-01092/dumps/` after a clean exit (the
`quit` debug command answers "emu busy" but does exit, and the PNGs are
written on exit). Replacements go into `replacements/` under the same file
names, enlarged by an integer factor. No Dragon Valor pack exists publicly;
the first one would be drawn from these dumps. Reference:
`psxrecomp/docs/HD_TEXTURE_PACKS.md`.

Enabling the mod with an empty `replacements/` changes nothing on screen:
the package ships no artwork and does no upscaling of its own. Replacement
verified 2026-10-14 with a demo set (the 67 dumps at 2x, tinted magenta,
left in `build-release/.../replacements/` with a README): after a stage
reload the `hd_textures` command reported 46,107 of 46,314 matched draws
applied, and `present_shot` (the composed OpenGL window) showed the tinted
forest while the native `screenshot` kept the original artwork. The
dump/replacement file names are hashes of the upload data, so dumps taken
on one build match on the other.

A human session from the title into Brize Forest (2026-10-14, release build,
dump on) produced 563 PNGs (7.7 MB): 474 4-bit paletted (`P4`), 72
semi-transparent 4-bit (`STP4`), 16 8-bit (`P8`), 1 `STP8`; the commonest
sizes are 64×64 (117), 16×32 (43) and 16×16 (36). Besides tiles and sprites
it holds the title artwork, the Namco card, the prologue frame borders, the
sword icon and **pre-rendered text atlases**: item names ("STR UP", "H.P.
MAX+6", "POISON", the Tone items…), the enemy roster ("ORC", "SKULL
KNIGHT", "ROCK MONSTER"…), "Prologue", "Dragon Valor". That is why no
readable string exists on the disc: the game's text is artwork, so a text
mod is a texture replacement, not a string patch.

Without any pack, the renderer's own options already sharpen the picture:
`game.toml` now ships `internal_resolution = "1080p"` as the per-title
default (the launcher's Settings → Display row and the player's
`settings.toml` outrank it). Texture filtering ships as nearest (the
player's choice, `game.toml`); xBR is a per-run choice: the in-game menu's
Graphics > Texture filtering row, or `PSX_TEXTURE_FILTER=3` in the
environment (read after the launcher). It cannot be made to stick through
the launcher, which clamps the filter to nearest/bilinear on every start and
persists that to `settings.toml` (`Play.bat` forced the env override until
2026-10-14). OpenGL only; it acts wherever a texel covers two or more output
pixels. Compared 2026-10-14 at 1080p with
the live `texture_filter` command (modes 0/1/3) and `present_shot`: nearest
keeps the blocky texels, bilinear blurs them, xBR keeps flat areas crisp and
removes the staircase on diagonals. Dumps keep accumulating across normal
play sessions, so a texture pack can grow progressively instead of
requiring a dedicated full playthrough; the attract demo (stages 6‥9 and
31‥34) adds a few more areas unattended.

Identification tooling: `tools/dvdbg.py` + `read_ram`/`write_ram` for RAM
probes, a data-xref listing of the EXE (address → function, width,
read/write), `psxrecomp/tools/overlay_xref.py` for code that lives in the BIN
modules (the pages the runtime has executed only; `tools/bin_listing.py OUT
[MAP ACTION …]` reads the whole modules from disc 1 and writes listings at
`0x800BBEF0`, which is how MAP.BIN was searched on 2026-10-15),
`symbols.toml` to name functions as they are understood.


### Play sessions

Start `PlayDebug.bat`, then `python tools/session_monitor.py <out_dir>`: every
30 s it logs frame, fps, unknown dispatches, segment misses and pad mode to
`monitor.csv`, dumps the unknown-dispatch tail whenever a new address appears
(seed material), screenshots every 5 min, and stops when the game closes.

## Test conventions

- **Start from slot 0** (slot 10 for spell tests). `python tools/dvdbg.py launch --state 0` (see *Save
  states* above). Replaying the intro is only for boot/menu tests.
- **Run muted.** `SDL_AUDIO_DRIVER=dummy` (+ `SDL_AUDIODRIVER=dummy`) — the
  SPU still runs. `PSX_HOST_MUTE=1` is documented but not read by this runtime.
- **One instance per port.** Several instances are fine with `--debug-port N`;
  the OpenGL instance tends to settle at internal scale 1x when sharing the GPU.
- **Comparing renderers frame-exact.** Instances started from the same Start
  press run deterministically, offset by a constant frame count (difference of
  the frames at which Start was pressed). Sample `screenshot` every second,
  name files by `frame`, and pair GL/SW by that offset; PIL luma timelines find
  transitions quickly. This is how the dissolve finding in `HISTORY.md` was made.
- **Attract route.** Boot → Start once (skips the FMV) → leave the title alone:
  story movie → main menu → gameplay demo (dungeon, dragon arena), looping.
  A second Start on the title goes to the menu; a Start on the menu starts a
  New Game.
- **Committed tests** (`tests/test_manifests.py`, `test_cheats.py`, `test_drop_cheat.py`,
  `test_scroll_cheat.py`, `test_menu.py`, `test_area_row.py`, `test_skip_intro.py`, `test_widescreen.py`).
  `python tests/test_manifests.py` checks every mod
  manifest statically (ids, default-off features, option bounds, plugin ids
  registered in `mods/plugins/*.c`); `python tests/test_cheats.py on|off`
  exercises the cheat plugins on a running debug build from slot 0 (see *This
  project's package* for the `state.toml` setup); `python
  tests/test_skip_intro.py [--off]` checks the intro skip on a fresh boot;
  `python tests/test_menu.py` drives the in-game menu from slot 0 (it launches
  the build itself with its own `mods/state.toml`, Infinite HP off, the
  player's file backed up and restored, because the menu persists every
  toggle there; `--attach` uses the game already running; the last step
  picks Return to launcher twice and checks that the program restarts with
  the same command line); `python tests/test_area_row.py [--reloads]`
  checks that the Area and Stage rows only offer action stages (the Area
  row moving the stage when the new area lacks it, the Stage row never
  touching the running stage), that Go now reaches the chosen area and
  stage (0/2 → 0/3) with no input (with
  and without Infinite HP, Val given back) while the guest pad stays
  neutral, even with Cross held on the debug server's host layer during
  the GAME OVER, and, with `--reloads`, reloads
  Kodel's slot 9 into every
  allowed area (2026-10-16: areas 0‥6 at stage 1 all come back running,
  each with its own hero).

## Project layout (what is ours)

```
CMakeLists.txt        psxrecomp_add_game_runtime(...), plugin sources, config staging target
codegen_setup.c/.h    launcher-side codegen hooks (display name, generated marker)
game.toml             identity, both discs, [prepare_disc] digests, [video]/[controller] defaults
game_options.toml     optional runtime options staged beside the exe
.recomp.json          recomp.fyi status card;  VERSION  release stamp (rewritten by the packager)
catalog_identity.json, disc_probe.json  Retro-Catalog identity / probe output (scaffold)
framework_pins.txt    scaffold snapshot of the submodule SHAs (gitlinks are authoritative)
generated/            recompiled game C + static AOT shard (committed)
aot/overlays.json     AOT profile for the five BIN/*.BIN modules (docs/AOT.md)
seeds/ghidra_funcs.txt  recompiler seeds (grow as discovery finds more)
symbols.toml          progressive symbol map -> tools/sync_symbols.py -> psx_symbols.h
launcher_assets/img/  boxart (libretro, see BOXART_SOURCE.txt);  assets/  app icon
mods/preloaded/       bundled mod packages (dragonvalor.cheats, dragonvalor.skip-intro, dragonvalor.enhancement.widescreen; the psx.enhancement.pgxp override)
mods/plugins/         C plugins behind those packages' [[plugin]] ids; dragonvalor_menu.cpp = in-game menu provider
tools/dvdbg.py        debug-server helper;  tools/session_monitor.py  long-run sampler
tools/sync_symbols.py framework copy of the symbol sync;  tools/mipsdis.py  listing from generated C
tools/bin_listing.py  BIN/*.BIN from the disc -> listings at 0x800BBEF0 (whole modules, not only executed pages)
tools/datacdr.py      DATA.CDR areas / stages / files, action stages vs scenes (`kinds`), a stage's hero entries (read-only)
tools/ramscan/        RAM-dump listing, base-register scan and the live drop/pepper probes (README inside)
tests/states/         committed save states slot 0 and slot 10 (+ README);  tests/test_*.py  checks
scripts/package_release.sh  local release zip (wraps the framework packager)
Play.bat, PlayDebug.bat     player / debug launch
DISC.md               which dumps work (ships at the zip root);  LICENSE  (MIT, our files)
CONTROLS.md           pads, keyboard binds, the game's default buttons, host hotkeys
THIRD_PARTY_NOTICES.md  what ships and under which licence (framework, launcher, toolchain, art)
docs/STATUS.md        state, open items;  docs/DEVELOPMENT.md (this file);
docs/AOT.md, docs/HISTORY.md, docs/BACKLOG.md, docs/PLAYTESTING.md (tester guide), docs/framework_pin_history.md
RELEASE_NOTES.md      shipped with every release zip (platforms, what was verified, known issues)
.github/workflows/release.yml  bundled release CI (framework template)
.github/ISSUE_TEMPLATE/  bug-report form (asks for psx_last_run_report.json) + links
patches/              local submodule patches applied at configure time (README inside)
psxrecomp/, recomp-ui/  framework and launcher submodules (pinned gitlinks)
```
