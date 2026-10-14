# Developing DragonValorUSRecomp

How this repo is built, run and debugged on the machine it was brought up on
(Windows 11). Project state and known issues live in [`STATUS.md`](STATUS.md).

## Prerequisites

| Need | Where |
|---|---|
| Git for Windows (`bash` on PATH for the framework scripts) | `C:\Program Files\Git\bin` |
| Python 3.12+ (3.13 used) with Pillow (used by the test helpers) | system |
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
by its file-name stem, so dumps must keep the Redump names. On this machine the
dumps live in `F:\dragon-valor-recomp\Dragon Valor (USA) (Disc N)\`; scripted
runs pass `--disc` (`tools/dvdbg.py launch` does, or `DVDBG_DISC=<cue>`).

## Build

```bash
git submodule update --init --recursive        # psxrecomp, recomp-ui, nested libs

# 1. recompiler + BIOS emitters (once per framework bump) -> build-recompiler/
bash psxrecomp/tools/ci/build_emitters.sh

# 2. game C from the disc (only when seeds or the framework pin change) -> generated/
python psxrecomp/psxrecomp_cli.py generate --config game.toml --project-root . \
    --disc "../Dragon Valor (USA) (Disc 1)/Dragon Valor (USA) (Disc 1).cue"

# 3a. player build (no debug server)            -> build-release/DragonValorUS_Recompiled.exe
python psxrecomp/psxrecomp_cli.py rebuild --config game.toml --project-root . \
    --build-dir build-release --no-pgo

# 3b. debug build (TCP debug server on :4370)   -> build-debug/DragonValorUS_Recompiled.exe
python psxrecomp/psxrecomp_cli.py rebuild --config game.toml --project-root . \
    --build-dir build-debug --no-pgo --cmake-extra=-DPSX_DEBUG_TOOLS=ON
```

Notes:
- `rebuild` forces `-DPSX_DEBUG_TOOLS=OFF`; the `--cmake-extra` in 3b is what
  turns the debug server on (later flags win).
- `generated/` is deterministic (35 shards, 1,302,445 lines, 1962 dispatch
  entries). It is committed; regenerate and commit together with the seed or
  pin change that caused it.
- `game.toml` / `game_options.toml` are re-staged next to the exe by the
  always-run `psx-stage-configs` target. After editing only `game.toml`, run
  `ninja -C build-release psx-stage-configs` (and the same for `build-debug`)
  or a full rebuild.
- Only OpenBIOS is linked (no retail BIOS C is generated).
- `generated/overlays_static*.c` is the AOT shard for the `BIN/*.BIN` mode
  modules; `generate` rebuilds it from `aot/overlays.json` (see [`AOT.md`](AOT.md)).

## Run

- `Play.bat` — player launch (`build-release`). `Play.bat -dbg` uses `build-debug`.
- `PlayDebug.bat` — debug build with the debug server and the FPS readout on.
- `PlaySmooth.bat` — release build with `PSX_RENDER_THREAD=1 PSX_FRAME_GEN=1`
  (`[video] render_thread` / `frame_generation`; `PSX_FRAME_GEN_METHOD=redraw|reprojection`).
  NTSC title: the guest runs at 60 Hz, generation only adds presented frames.
- Both open the **recomp-ui launcher** first (disc check, Disc 1/2, pad mode,
  memory cards, Settings, Mods). `--no-launcher` or `PSX_NO_LAUNCHER=1` boots
  straight into the game.

Files the runtime writes, all gitignored:

| What | Where |
|---|---|
| Memory cards | `saves/card1.mcd`, `saves/card2.mcd` (project root, `[runtime] memcard_dir`) |
| Launcher/user settings | `settings.toml` next to the exe (overrides `game.toml` defaults) |
| Keyboard binds | `keybinds.ini` next to the exe (generated on first run) |
| Host hotkeys / pad mapping | `config.ini` next to the exe (`[KeyMap]`, `[controller]`, `[mapping]`) |
| Overlay captures + native shards | `overlay_captures.json(.d)` and `cache/` next to the exe |
| Exit report / watchdog | `psx_last_run_report.json` (unknown dispatches, segment misses, overlay loader counters, CPU state at exit), `psx_freeze_heartbeat.json`, boot-time `psx_freeze_dump_*.json` (false positives, ~35 MB each — delete) |

## Debugging a running game

The debug build listens on `127.0.0.1:4370` (JSON lines; see
`psxrecomp/docs/TCP_COMMANDS.md`). Two clients:

```bash
# project helper (launches muted and detached, skips the launcher)
python tools/dvdbg.py launch            # OpenGL on 4370
python tools/dvdbg.py launch --sw       # software renderer on 4371
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
boot or the menus starts from slot 0**:

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
| `0x8008A5B6` | u16 | STR | |
| `0x8008A5B8` | u16 | DEF | `func_80038094`, `func_80038274` |
| `0x800A2DD0` | u16 | Val (gold; Status shows 4 digits, 10000 reads 0000) | `func_8004BF84` |
| `0x800A2E06`‥`0D` | u8 ×8 | Magic levels: Heal, Defense, Fire, Ice, Thunder, Mine, Stealth, Vortex (0 = not learned, 1‥3 = LEVEL) | |

| `0x800A2DD2`‥ | u16 ×N | Inventory counts, one per item type: Pendant, Lithograph, Ring, Talisman, Wine, Pepper, Earrings, Bracelet, Horn, Perfume, Tea, Mask, Silver Key, Gold Key, Harp, Tiara, Copper Key, … (accessories, trade goods, keys; **no weapons**) | |
| `0x800A2F18` / `0x800A2F1A` | u16 ×2 | Area and stage id of the stage to load (DEMO.BIN writes 0/6‥9 and 7/31‥34 before calling `func_8003A9B0`); slot 0 is 0/2 | `func_80025264` (W), `func_80038094`, … |
| `0x8008634C` / `0x8008634D` | u8 ×2 | Game mode and sub-state: 8 = action stage, 10 = loading, 17 = attract demo | `func_8004BF84`, `func_80025264`, … |
| `0x8008A4E0` | struct | Player object; the stats above are its `+0xCE`‥`+0xD8` fields | |

`write_ram` writes one byte per call (`addr=`, `val=`). The GameShark codes
circulating on GameRevolution are correct for Val (`800A2DD0 2700`) and the
magic levels (`300A2E06`‥`0D 0003`); its HP/MP codes (`800A5AE0 0064`, …) are
the addresses above with one hex digit dropped. The "beat the first boss,
sell items, pause, press Up Down Left Right Select" cheat from the same page
has no button-sequence table in the EXE or the BIN modules and did nothing
when tried from slot 0; its precondition (first boss beaten) was not met, so
it stays unverified rather than disproved.

Useful test route: writing 0 to HP gives GAME OVER → CONTINUE? in ~5 s, and
Cross on YES reloads the current stage from disc (stage id restored from the
game's own record, so `0x800A2F1A` is overwritten). Area values 1‥3 written
before CONTINUE leave the game black in mode 10 (CD-ROM status polling, no
crash); 0 and 7 reload normally.

### Character and weapon (2026-10-14 status)

Not swappable by a RAM poke yet. Chapter 1's hero (Clovis) is decided by the
game's chapter progress, the hero's model and the Status portrait are loaded
from `DATA.CDR` at stage start, and the weapon is part of the hero: the
inventory holds no weapons, and STR is the only weapon-related number. Nothing
in the stats struct, the inventory block or the area/stage pair changes the
hero, and the field that selects the hero has not been identified. What it
takes:

1. A second reference point: a save (memory card or save state) in a later
   chapter with another hero, then a RAM diff against slot 0 around
   `0x800A2Dxx`‥`0x800A31xx` and `0x80086xxx` to find the hero/chapter field.
   One public candidate: GameFAQs hosts a single USA DexDrive card at
   <https://gamefaqs.gamespot.com/ps/197154-dragon-valor/saves> ("All Gallery
   Mode Images Unlocked", 131 KB, uploaded by RPGer800; the download link is
   `/saves/23784` on that page). Its directory holds one 1-block save,
   `BASLUS-0109214`, titled "Dragon Valor 14", so it is at least a cleared
   game. A `.gme` is a 3904-byte DexDrive header followed by the raw 128 KB
   card: strip the header (or export with MemcardRex) and drop the result in
   as `saves/card2.mcd` (both project cards are formatted and empty), boot
   without `--state`, use Load Game and dump RAM. Not loaded or diffed yet;
   the human play session is the other source.
2. Confirm by writing the field and reloading through GAME OVER → CONTINUE.
3. For a cosmetic weapon/hero change without game logic: HD texture
   replacement (OpenGL, `psx.enhancement.hd-textures` dumps the originals) or
   a `[[overlay]]` on the model data inside `DATA.CDR`.

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

Identification tooling: `tools/dvdbg.py` + `read_ram`/`write_ram` for RAM
probes, the scratch `refs.json` (EXE data xrefs: address → function, width,
read/write), `psxrecomp/tools/overlay_xref.py` for code that lives in the BIN
modules, `symbols.toml` to name functions as they are understood.


### Play sessions

Start `PlayDebug.bat`, then `python tools/session_monitor.py <out_dir>`: every
30 s it logs frame, fps, unknown dispatches, segment misses and pad mode to
`monitor.csv`, dumps the unknown-dispatch tail whenever a new address appears
(seed material), screenshots every 5 min, and stops when the game closes.

## Test conventions

- **Start from slot 0.** `python tools/dvdbg.py launch --state 0` (see *Save
  states* above). Replaying the intro is only for boot/menu tests.
- **Run muted.** `SDL_AUDIO_DRIVER=dummy` (+ `SDL_AUDIODRIVER=dummy`) — the
  SPU still runs. `PSX_HOST_MUTE=1` is documented but not read by this runtime.
- **One instance per port.** Several instances are fine with `--debug-port N`;
  the OpenGL instance tends to settle at internal scale 1x when sharing the GPU.
- **Comparing renderers frame-exact.** Instances started from the same Start
  press run deterministically, offset by a constant frame count (difference of
  the frames at which Start was pressed). Sample `screenshot` every second,
  name files by `frame`, and pair GL/SW by that offset; PIL luma timelines find
  transitions quickly. This is how the dissolve finding in `STATUS.md` was made.
- **Attract route.** Boot → Start once (skips the FMV) → leave the title alone:
  story movie → main menu → gameplay demo (dungeon, dragon arena), looping.
  A second Start on the title goes to the menu; a Start on the menu starts a
  New Game.

## Project layout (what is ours)

```
CMakeLists.txt        psxrecomp_add_game_runtime(...) + config staging target
game.toml             identity, both discs, [controller] p1_device/default_mode
generated/            recompiled game C (committed)
seeds/ghidra_funcs.txt  recompiler seeds (grow as discovery finds more)
symbols.toml          progressive symbol map -> tools/sync_symbols.py -> psx_symbols.h
launcher_assets/img/  boxart (libretro, see BOXART_SOURCE.txt)
tools/dvdbg.py        debug-server helper;  tools/sync_symbols.py (framework copy)
docs/STATUS.md        state, open items, pitfalls;  docs/DEVELOPMENT.md (this file)
psxrecomp/, recomp-ui/  framework and launcher submodules (pinned gitlinks)
```
