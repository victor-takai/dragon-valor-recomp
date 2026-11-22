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
  `DragonValorRecomp`. The launch scripts are `DragonValor.bat` and
  `DragonValorDebug.bat` (`Play.bat` and `PlayDebug.bat` until 2026-10-17,
  renamed at the player's request; other psxrecomp titles ship no script
  at all, their players run the exe, and neither script is in the release
  zip). The exe name is repeated in `DragonValor.bat`,
  `tools/dvdbg.py`, `tests/test_menu.py`, `scripts/package_release.sh` and
  `codegen_setup.c`; the release workflow takes the zip prefix from the
  packager (it was renamed in place: `generate_ci.py --force` would drop
  this project's manifest-test step and release-notes body).
- App icon (2026-10-17, the player's choice of three candidates): a
  pixel-art sword on crimson with a cyan glint, after the idea of the
  game's memory-card save icon (16x16, 3 frames, the same for every save;
  read from `saves/card1.mcd`) but drawn anew: no pixel of it is used.
  It replaced the framework's default controller. `tools/make_app_icon.py`
  writes `assets/psxrecomp.ico` with hand-sized 16, 24 and 32 px frames
  (48 = the 24 doubled, 64/128/256 = the 32 enlarged, so pixels stay
  square), `assets/psxrecomp.png` (512 px, the 32 x16; the runtime sets it
  as the window icon and recomp-ui as the launcher's) and the `.svg`. The
  names are the framework's, so nothing else changes; a rebuild embeds the
  `.ico` (checked: all seven frames are in both exes). Explorer caches icons
  and can show the old one for a while (`ie4uinit.exe -show` refreshes it).
- Release CI (first runs 2026-10-17): the Windows job compiles in MSYS2
  with a minimal PATH. So the workflow installs MSYS2's `git` (CMakeLists
  applies `patches/recomp-ui/` with it) and sets `core.autocrlf=false`
  before checkout: `recomp-ui` has no `.gitattributes`, and a CRLF checkout
  made the LF patches fail. Rehearse with
  `gh workflow run release.yml -f version=X.Y.Z -f publish=false` and fetch
  a zip with `gh run download <run> -n DragonValorRecompiled-windows-x64`.
  Windows takes about 18 min, the others 1 to 4. 0.x versions publish as
  pre-releases.
- `RELEASE_NOTES.md` is the GitHub release text (and ships in the zip).
  Keep it short: what it is, how to run, features, known issues; details
  live in `DISC.md`, `CONTROLS.md` and `docs/PLAYTESTING.md`. Write one
  line per paragraph or bullet. GitHub renders every newline of a release
  body as a line break, so the 75-column wrap of the other docs broke
  v0.1.0's page mid-sentence (fixed 2026-10-21 with `gh release edit
  v0.1.0 --notes-file`; the 0.1.0 zip keeps the long notes).
- History rewrite (2026-10-21, before the repository went public):
  `tests/states/*.pst` were removed from every commit, since a state holds
  the game's RAM, VRAM and sound RAM. The author and committer email became
  the GitHub noreply address, now this repository's `user.email`. The
  repository was then re-created from that history, and the first one,
  which held the old commits, was deleted the same day. So
  commit ids cited in messages written before 2026-10-21 (such as
  "Correction to b962176") are pre-rewrite ids and resolve nowhere here.
  A force-pushed `v*` tag starts the release workflow (`push: tags`):
  disable it first (`gh workflow disable release.yml`) when moving a tag.

## Run

- `DragonValor.bat` — player launch (`build-release`). `DragonValor.bat -dbg` uses `build-debug`.
  The executable is a Windows-subsystem program (the framework links Release
  builds with `-mwindows`, `runtime.cmake`), so it opens no console of its
  own and double-clicking it is enough; the script starts it detached and
  closes its window at once (2026-10-15). `PLAY_WAIT=1` runs it in the
  foreground instead (`DragonValorDebug.bat` does, to keep a window with the exit
  code). The runtime never attaches to a parent console, so its stdout is
  only seen when redirected (`tools/dvdbg.py launch` writes `dvdbg_4370.log`);
  the exit report `psx_last_run_report.json` is the channel otherwise.
- `DragonValorDebug.bat` — debug build with the debug server and the FPS readout on.
- Frame generation (`[video] frame_generation`, `PSX_FRAME_GEN=1`) is of no
  use on this 60 Hz title and its launcher script was removed on 2026-10-14.
- **Render thread on by default** (`[video] render_thread = true` in
  `game.toml`, 2026-11-18; psxrecomp `docs/RENDER_THREAD.md`): the OpenGL
  calls run on their own thread, so a frame costs max(emulation, GL)
  instead of the sum. The player felt hitches where many enemies are on
  screen. Measured on the town street of 1/7 with the red orbs (the
  player's state, only loaded), 1440p, i7-10700K / RTX 4070 Ti, debug
  build: the frame ran at the 16.7 ms limit, and where the scene grew from
  ~720 to ~1075 primitives it fell to 0.6-0.8x for about a second. The
  host sampler showed about half the frame outside the executable (the GL
  driver on the emulation thread), the rest the recompiled game,
  cycle accounting and the GTE (`host_profile`, `phase_profile`: static
  code ~95 %, the interpreter ~1 %). Not the cause: internal resolution
  (1080p hitched the same) and texture dumping (no new texture was dumped
  in those runs). Unthrottled at that spot (`turbo`), 6 interleaved runs:
  thread off 0.93-1.31x, on 1.70-2.05x, Vulkan 0.47-1.34x; the paced walks
  with the thread on had no dip. `test_menu`, `test_widescreen` and
  `test_heroswap` pass with it on (`PSX_RENDER_THREAD=1`), and the in-game
  menu's own GL textures (the save-state thumbnails) draw. A
  `settings.toml` that already has `render_thread = false` keeps it off
  (the runtime writes the key back on every launch); our recomp-ui pin
  has no launcher row for it, so an existing install is switched by
  editing that line. The measuring script is not kept (scratch
  `perf_walk.py`: hold the pad, sample `frame` at 10 Hz, `host_profile`
  when a 0.5 s window runs under 0.85x).
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
| Exit report / watchdog | `psx_last_run_report.json` (unknown dispatches, segment misses, overlay loader counters, CPU state at exit), `psx_freeze_heartbeat.json`, boot-time `psx_freeze_dump_*.json` (false positives, ~35 MB each on the debug build; `tools/dvdbg.py launch` and `DragonValor.bat` delete the existing ones before each start, since 2026-10-15) |

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
python tools/dvdbg.py state keep 0      # copy the slot into tests/states/ (local, not in git)
```

Slot 0 is the first screen of gameplay (New Game → prologue → opening
cutscene → first enemy in Brize Forest; HP 030/030, MP 020/020). The runtime
reads slots from `saves/openbios/` (gitignored); the test copy lives in
[`tests/states/`](../tests/states/README.md) and `state load` restores it
from there when the local slot is missing. Those copies are not in git
(since 2026-10-21: a state holds the game's RAM and VRAM, so it may not
be published), and a fresh clone has to take them again. The state carries the loaded `ACTION.BIN`, so no part of the menu
route is replayed. States are whole-machine state: same BIOS (OpenBIOS),
same disc, and a runtime update that changes the format means re-taking
them (`state save`, then `state keep`).

A second test state, **slot 10** (menu slot 11; `launch --state 10`),
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
| `0x800A2D99` | u8 | Selected spell of the hero (0xFF = none; slot 0 has none and every level 0). `func_80037D94` SetCurrentSpell queues the spell's two stage files ((3 × spell + level) × 2 + 15 and + 16) in load row 3 of the stage loader: `0x800A2C0C` / `0x800A2C10` are that row's state (1 = requested) and file number, not a HUD icon | `func_80037D94`, ACTION.BIN `0x800BFFE4` |
| `0x8009FAF8` | struct | Work area of the Triangle magic / item menu (mode 7; ACTION.BIN `0x800BFFE4`, pointer at `0x800C5E48`), the first 0x138 bytes of the per-screen work area: `+0` the spell selected when the menu opened, `+1` focus (0 spell grid, 1 item grid), `+2` cursor (u16; the D-pad moves it, action `0x100` confirms, action `0x80` selects), `+8` state, `+0xC` countdown | ACTION.BIN |
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
| `0x8008634C` / `0x8008634D` | u8 ×2 | Game mode and sub-state: 7 = the Triangle magic / item menu, 8 = action stage, 9 = GAME OVER (sub 2 = the CONTINUE prompt), 10 = MAP.BIN's world map (travel, Status, Save), 17 = loading and the hand-off between modules (the CONTINUE reload and the start of the attract demo pass through it) | `func_8004BF84`, `func_80025264`, … |
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
- `func_80040DCC` (the collision and hit resolution of every object, the
  hero's attacks among them) takes a hit's power from
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
`func_80038274`). DEF is read where the hero takes damage, inside `func_80040DCC`
(`lh +0xD8` at pc `0x80042D70` for enemy melee and `0x800434E0` for harmful
particles; `func_80042A54` / `func_80042FF0` are places in that function,
not functions of their own): damage = power − DEF − a power-up term
(`0x8008631B`), at least 1. So the DEF editor was already real.

Useful test route: writing 0 to HP gives GAME OVER → CONTINUE? in ~5 s, and
Cross on YES reloads the current stage from disc (stage id restored from the
stage-start record `0x8007D9C6`, so `0x800A2F1A` is overwritten). Area values 1‥3 written
before CONTINUE leave the game black in mode 10 (MAP.BIN's world map mode,
waiting on the CD-ROM; no crash); 0 and 7 reload normally.

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
level 0) and it sits inside the first stage's tutorial, an event script
(mode 8 sub 5 runs the event-script interpreter `func_8004BF84`), where even
a forced level + selection + a load-row request (`0x800A2C0C` = 1, file
`0x800A2C10`: load row 3, see the spell row above) did nothing on Cross (`0x80`, the magic-type select) or
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

**Cheat.** `dragonvalor.cheats` (1.3.0 on) has `gem_every` / `scroll_every`
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
  3 2`), and the reload stays black in mode 10, the world map mode (the earlier "areas 1‥3 go
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
  Warp section (Experimental until 2026-11-18) has **Area** and **Stage (next
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
- **Solved (2026-11-16)** from the decomp (github
  victor-takai/dragon-valor-decomp, `docs/targets/hero-swap.md`): the action
  pack's entry 0 is an install program (`PackInstall`, 0x80035E48) and the
  hero is its first ops: the object of op 3 (behaviour script = entry 2, the
  14 parts, **200 animation slots naming about 80 entries**), the weld lists,
  the sound bank and the shadow; the swaps above moved the parts but left the
  other hero's animations under them. Entries hold no absolute address (the
  install relocates entry numbers), so a pack can be rebuilt freely. The
  decomp's `tools/heroswap.py` does that and the `dragonvalor.heroswap`
  package below serves the result: Kodel and Mihael play Clovis's 1/7.

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

**The launcher's Mods page (2026-11-18).** The player found it cryptic: a
row read "CD Speed / CD Speed 1.0.0" or "Cheats / Dragon Valor Cheats
1.3.0", and nine features sat in seven groups (Cheats, Display,
Experimental, Convenience, Advanced, Quality of Life, Visual). Now:

- two groups, **Game** (Cheats, Faster loading, Play as, Skip intro movie)
  and **Graphics** (Bezel artwork, HD texture packs, Steady 3D (PGXP),
  Widescreen); the list sorts by group, then name;
- every feature description opens with a short sentence the row shows as
  its summary ("Infinite HP and MP, more drops.", "Shorter loading from the
  CD."), then the details; "Disabled by default" / "On by default in this
  title" openings are gone (the checkbox says it), and the texts are
  current (Widescreen's HUD at the edges, Play as through the Hero section);
- the framework packages shown on the page get title overrides like the
  PGXP one (same id and version under `mods/preloaded/packages`, the
  builtin manifest verbatim but for the feature's name, description and
  group): `psx.enhancement.cd-speed` ("Faster loading", options group
  Speed, still off by default), `psx.enhancement.hd-textures` and
  `psx.presentation.bezel`; `psx.enhancement.8mb-ram` is hidden by the
  framework already;
- the row and detail layout is recomp-ui's: `patches/recomp-ui/0005-...`
  (name + summary rows; package, version, author and source at the detail
  pane's foot; no short "EXP" channel tag before a row's name, which pushed
  it out of line: the detail pane still shows EXPERIMENTAL and its blurb);
- the launcher keeps 511 bytes of a description (recomp-ui's 512-byte
  buffers): Play as's was cut mid-sentence, so it was shortened and
  `tests/test_manifests.py` now fails a longer one.

Checked in the debug launcher (window capture, `PrintWindow`; a scratch
build opened it on the Mods page with Play as selected, since clicks posted
to the window do not reach SDL): the two groups, the summaries, the
options, the EXPERIMENTAL tag and blurb in the detail pane and the
"Package: ..." foot.

#### This project's package: `dragonvalor.cheats`

`mods/preloaded/packages/dragonvalor.cheats/1.4.0/manifest.toml` (format 7)
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
| `unlock_gallery` (1.4.0, 2026-11-21) | not a gameplay cheat: only in the title (mode 3, with OPENNING.BIN in memory, its first word 4). The title's main menu offers a fourth item, the illustration gallery, only when one of the three ending flags of the save data in memory is set (`g_SaveBlock.areaFlags[10‥12]`, `0x800A2E18`‥`1A`; ENDING sets `areaFlags[10 + ending]`); each flag unlocks its own list of pictures, two or more unlock 43‥95, and flags 0 and 1 together lock picture 9 again (the decomp's `src/openning/titlemenu.c`: TitleMainInit, GalleryUnlockInit, GalleryUnlockBonus). The plugin leaves the flags alone and writes the module's results instead: `g_TitleHasClearData` (`0x80157248`) ← 1, the item count the menu reads every pass, and, in the gallery (`g_TitleWork.screen` `0x80156F32` = 5, past its Init), `g_GalleryUnlocked[1‥95]` (`0x80158520`, s16) ← 1 (picture 0, the first shown, stays as the game has it). The player asked for it after seeing the illustrations among the textures. `tests/test_gallery.py` (fresh boot: 4 items, R1 visits all 95 pictures; `--off`: 3 items) |

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

#### In-game menu (`mods/plugins/dragonvalor_menu.cpp`, `dragonvalor_menu_view.cpp`)

Escape, or Select+Start on the pad, opens a menu over the game; the guest
and audio pause while it is up (Options > Pause in menu turns that off).
Keyboard: arrows, Enter / Space, Backspace, Escape, Tab / Shift+Tab (or
Page Down / Up) for the next / previous section. Pad: D-pad or left stick,
Cross, Circle, L1 / R1 for the sections, Start closes. **Mouse (2026-10-15):** the provider feeds SDL mouse
events to ImGui while the menu is open (coordinates scaled from window to
framebuffer): moving the pointer over a row or a section selects it, a click
activates (on a value's arrows it steps that way), the wheel scrolls and the
right button goes back; closed, the mouse is not touched. The Magic and
Items rows carry the game's own texts (read off the Status screen with every
spell at level 3 and every item at 1).

**Redesign (2026-11-18, the player: "System could sit better in the list, a
real confirmation somewhere, like an alert when picking the hero and sword;
overall it is a bit ugly").** The menu is no longer recomp-ui's standard
builder plus extras with its generic drawer:

- *Order.* `kItems` lists every row in the order shown (recomp-ui makes a
  section where its first row is): the pause menu's own business first
  (Game), then what the game offers (Hero, Cheats, Magic, Items, Warp),
  then settings (Video, Options). The standard rows keep recomp-ui's keys
  (`RECOMP_RUNTIME_UI_KEY_*`), so the callbacks are unchanged.
- *Look.* `dragonvalor_menu_view.cpp` draws the model (through
  `recomp_runtime_ui_internal.h`, as recomp-ui's own drawer does): a dark
  panel with a gold accent, the game's name and the hero playing (name, HP,
  MP, area and stage, from RAM) on top, the sections on the left, one line
  per row on the right (a switch for on/off rows, arrows around a value, a
  chevron on an action, crimson for the two rows that leave the game, a
  muted note such as "saved" / "empty" for the state slot or "in the drive"
  for a disc), the selected row's description in a help box under the rows
  (descriptions used to sit under every row and made the old Experimental
  page a wall of text), key caps in the footer, the status on the right.
  Lato Regular and Bold from the launcher's assets at four sizes scaled by
  the window height (ImGui 1.91 bakes glyphs at one size, so the atlas is
  rebuilt when the scale moves by a 5 % step).
- *Dialogs.* Load state, Save state over a saved slot, Insert disc, Go now,
  Return to launcher and Quit ask first in a dialog over the menu (Cancel on
  the left, the action on the right; Left / Right move between them, Enter
  presses, Escape takes the dialog away; leaving the game focuses Cancel).
  The dialog's action calls `cb_run_action` again with `m.confirmed` set.
- *Hero.* Play as and Weapon are picked with Left / Right ("not applied"
  beside them) and applied together from a dialog: Enter on either row or
  on Apply asks "Play as Arlen with Kadum?" with what happens ("This stage
  restarts from its beginning ..." in an action stage, "... when the next
  stage loads" elsewhere, or that Play as is off in the launcher). Closing
  the menu with a choice not applied asks too, with Discard as the other
  button. In an action stage the menu closes and the stage restarts at once
  (`dv_heroswap_restart_request`, carried out by the heroswap plugin's
  VBlank callback, no Game Over).
- *Notices.* What an action did shows for about 3 s at the top right (the
  game's HUD is on the left), with the menu open or closed: "State saved /
  Slot 3", "State loaded", "Arlen, holding Kadum / The stage restarts from
  its beginning", "Warping / Area 4, stage 1 in about 10 seconds",
  "Changing discs".
- *Pad (2026-11-18, the player: "it has to be easy to navigate with a pad
  too").* The footer shows the buttons of the device last used: keys, or
  the pad's (SDL3 `SDL_GetGamepadButtonLabelForType` on the south button:
  Cross / Circle shapes and L1 / R1 on a PlayStation pad, A / B and LB / RB
  on an Xbox or unknown pad, B / A and L / R on a Nintendo one), and the
  "Press ... to apply" status names that button. The shoulder buttons step
  through the sections (entering the next one when inside one). SDL does
  not repeat pad buttons, so a held D-pad direction (or the left stick past
  half way, let go under 35 %) repeats from the draw callback: first after
  380 ms, then every 110 ms down to 35 ms the longer it is held; a held
  direction stops at the first / last row instead of wrapping around.
  Checked by turning F13..F20 into pad events in a scratch build (the debug
  server injects keys and mouse clicks, not pad events): R1 x4 highlighted
  Items, Cross entered it, holding Down 2 s ran to Copper Key and stopped,
  R1 entered Warp, a short stick push moved one row, L1 x3 entered Cheats,
  Circle twice closed the menu; mouse clicks (`host_mouse`) entered Cheats
  and switched Infinite HP on and off.
- *Save states (2026-11-18, the player: "the save state's style is ugly
  too, can it match?").* F7 (the runtime's `SaveStateMenu` binding, matched
  with `host_keymap_match_event`) and Game > Save state / Load state open a
  save-state screen in the menu's frame instead of the runtime's own picker
  (`psx_savestate_menu.c`, a 640 x 480 panel in an 8 x 8 font, which the
  provider now never lets see F7): a Save and a Load tab (Tab, L1 / R1, or
  a click) over the 12 slots in a 6 x 2 or 4 x 3 grid (whichever gives the
  larger cards in the window; the view reports it so the arrows move on the
  same grid), each card with the slot's thumbnail (`savestate_read_thumb`,
  128 x 96, uploaded as a GL texture through the `imgl3w` entry points
  ImGui's OpenGL3 backend loaded; bound texture and unpack state put back),
  its date (`savestate_slot_mtime`) or Empty, and "Other build" when
  `savestate_slot_compatible` refuses it. Enter saves into an empty slot at
  once and asks before overwriting one; on the Load tab it asks before
  loading; the slot is re-read (date, picture) once `savestate_pending()`
  says the save landed. Opened by F7, Back closes the menu; from the Game
  section, Back returns to it. **Select + R1** (the pad): the runtime polls
  its `[hotkeys] save_state_menu_pad` (default 2040 = Select + R1) straight
  from the pad after the provider has seen the events and opened its old
  picker anyway (the player got it on 2026-11-18). A title cannot claim it
  and the menu host's launcher-settings calls carry no shortcuts, so the
  provider's static initializer, before `main()` reads `settings.toml`,
  rewrites that key to 0 while it holds the default (the launcher then
  shows the shortcut unbound; a binding the player changed is kept; with no
  `settings.toml` yet nothing is written), and `p_event` opens the screen on
  Select + R1 itself (again closes it). Checked with pad events made from
  keys in a scratch build: the key read 0 after the start, Select + R1
  opened and closed the screen.
- *A state made with another hero.* The heroswap plugin's resync after a
  state load (see its section) used to put up an OSD line, "Save made as
  Clovis: Kodel plays from the next stage", and the player read the
  Clovis portrait and name in the next scene as the swap failing (test
  state 10, made as Clovis, again on 2026-11-18). It now sets
  `dv_heroswap_mismatch` with what the save holds, and the menu makes the
  save's hero and weapon the Play as choice (`adopt_save_choice`, through
  `mod_set` like a Hero choice; the save's area hero becomes "each stage's
  own", its weapon "the hero's own" or a model) and says so in a notice:
  "Playing as Clovis / As in this save: Play as follows it". A first version
  asked in a dialog (restart with the old choice, or keep the save); the
  player preferred no question at every load. The OSD line is only left for
  builds without the menu (`dv_menu_present`). `tests/test_heroswap.py`
  checks that a load asks nothing (the game keeps running), that state.toml
  follows the save, and then picks its hero again in the Hero rows (the
  stage restarts with it) before the rest of its checks.
- *Arrows.* ImGui's anti-aliased convex fill feathers the edge outward for
  one winding only; the mirrored (left) arrows and the diamonds had the
  other one and came out smaller and softer (the player saw one arrow
  smaller than the other). Every filled shape in the view now lists its
  points in the same order.
- *Labels in buttons* (same day: A and B sat off centre in their circles).
  A label used to be centred by the font's line box, which also holds room
  for accents and descenders; now `put_centered` / `put_mid` place it by the
  ink of its glyphs horizontally and the capitals' height (glyph `H`)
  vertically, for the pad letters, key caps, the save-state tabs and the
  dialog buttons, and the hint texts beside them line up the same way.
- *D-pad icon* (same day, the player asked for a better one than the plus):
  a cross with rounded arm ends and a dimpled centre, the left and right
  arms lit (they change a value). Dark arrows embossed in the arms were
  tried first and dropped: too busy at footer size (the player's call).

| Section | Rows | Applied through |
|---|---|---|
| Game | Resume, Save state, Load state (both open the save-state screen above; Load shows how many slots are saved), Insert disc 1 / 2, Return to launcher, Quit game | `savestate_request_save/load` (slot − 1), `disc_insert` (the runtime's tray policy), `request_exit`. Return to launcher asks for `PSX_MENU_EXIT_RELAUNCH` in builds without the netplay lobby client (ours): the framework's in-process return re-enters the launcher only inside its lobby-client block and otherwise ends the program (player report 2026-10-16; still so on upstream master 379565a6), while a relaunch restarts the executable with its own arguments, which opens the launcher as at boot. Builds with `PSX_NETPLAY` keep `PSX_MENU_EXIT_TO_FRONTEND`. |
| Hero (experimental) | Play as, Weapon, Apply | the `hero` and `weapon` options of `dragonvalor.heroswap` (see its section), written by `mod_set` when the dialog is confirmed; off in the launcher, the first choice switches the feature on for the next start |
| Cheats | Infinite HP, Infinite MP, Gem every N sword kills, Scroll every N magic kills, Unlock gallery (`mod_set` on `dragonvalor.cheats` / `cheats` → live option change, saved to `mods/state.toml`; the two counts are integer options, 0 = the game's rule); Val, Max HP, Max MP, STR, DEF | the five editors show the value in RAM and write it at once (`psx_mod_write_half`), greyed out until a hero is loaded; STR is spread over the five attack entries at `0x80086324` (the shown STR is their sum, see *How damage is computed*) |
| Magic | Heal, Defense, Fire, Ice, Thunder, Mine, Stealth, Vortex: Not learned / Level 1‥3, each with the game's description (Heal "Recovers H.P. & damages undead", Defense "Increases defense & weapon range", Fire "Fireball attack or ignite devices", Ice "Ice attack or freeze flame platforms", Thunder "4 Thunder attacks or electrify devices", Mine "3 Controlled Mine attacks", Stealth "Invisibility deters enemy attacks", Vortex "Controlled enemy entrapment"; level-3 costs 14 / 5 / 4 / 5 / 8 / 5 / 12 / 7 MP) | bytes `0x800A2E06`‥`0D`, written at once |
| Items | Pendant, Lithograph, Ring, Talisman, Bracelet, Earrings, Pepper, Wine, Horn, Perfume, Tea, Mask, Tiara, Harp, Gold Key, Silver Key, Copper Key: 0‥99, in the Status screen's order and with its texts (the first five are bonuses applied when found, Earrings cut every cast by 1 MP, the rest are trade goods and keys; raising a key count also takes that kind of key in the stage's pickup pool, see *Pickup ids*) | u16 counts `0x800A2DD4`‥`DE` and `0x800A2DF0`‥`0x800A2E04`, written at once |
| Warp (experimental; Area, Stage and Go now moved out of Cheats on 2026-10-16 at the player's request, because the warp has known limits: garbage text in story scenes, skipped stages' items and keys; the section was called Experimental until 2026-11-18) | Area (next Continue: `0x800A2F18`, the area the next GAME OVER → CONTINUE loads, see *Character and weapon*; areas of the current disc only) and Stage (next Continue: the stage-start record `0x8007D9C6`, which the Continue copies over `0x800A2F1A`; added 2026-10-16). Both only offer action stages, from the bitmask table `tools/datacdr.py kinds --c` prints, because a scene-type or empty stage left the screen black until the watchdog closed the game. The first version had no Stage row and kept the stage number, so a late stage offered few areas (stage 21 is an action stage only in areas 3 and 6, the player's report); now the Area row moves the stage to the nearest action stage of the new area when needed (stage 1 is one in every area), and the Stage row steps over scene stages. Stages that open with a story scene play it with garbage text after the jump (see *Character and weapon*); **Go now (Game Over)**, an action: writes HP 0, holds Infinite HP off (`dv_cheats_hold_hp`, shared with the cheats plugin, 60 s safety net from the GAME OVER) and closes the menu. HP 0 makes ACTION.BIN (`0x800C40E0`) set mode 9 at once, with no death animation: the hero stays on its feet and the world keeps running under GAME OVER, so the first version, which pressed Cross on the prompt every 800 ms, made the hero jump (player report 2026-10-16; the prompt ignores buttons until its state 2). Now a title pad transform (`psx_mod_set_pad_transform`, player 0; offline only) is the per-frame hook: as soon as the prompt starts (mode 9, sub 2, prompt state ≤ 2 at `0x8009FAF8`) it writes what the prompt's confirm branch writes (cursor YES, brightness 128, timer 30, state 3), and it hands the guest a neutral pad until the action stage runs again, so nothing the player presses moves the hero. Before the GAME OVER the pad stays the player's: the game holds the death back while a dialogue or story scene runs (still mode 8; the Brize Forest tutorial delays it by about 8 s), and a scene that waits for Cross would otherwise never end (up to 5 min). Then it writes back the Val the Continue took (50) and lets Infinite HP act again. The transform returns the stock pad outside a trip and is detached the next time the menu opens. 8‥12 s per trip in `tests/test_area_row.py`, 15 s from the tutorial. Besides Val, a Continue changes `0x800A2DA8` (+1, probably a death / continue counter), `0x800A2EFC` and `0x800A2F08`‥`0F` (unknown) and the play time. **Cut prologue scene** (2026-11-21, the player asked for an easy way into the stages nothing in the game leads to, found by the decomp's `tools/stages.py`, its docs/SECRETS.md; the first version offered all four of disc 1 through a Secret stage choice row and a Go to secret stage action, and the marble corridor 1/24 and the Japanese release's chapter 1 world maps 1/14 / 1/16 were taken out the same day at the player's request, leaving the one action): the scene cut from the prologue, stage 0/5. No Game Over: the action, asked in a dialog, sets `dv_trip_area` / `dv_trip_stage` and `dv_trip_request` and closes the menu; the cheats plugin's entry hook on ModelWeldReset (`0x80021CE4`, FrameDraw, after the frame's objects and before DrawOTag) waits for a quiet frame (mode 8 sub-mode 7, no pause, no event script, area and stage phases 0, load rows idle, the same test as Play as's restart) and calls the game's own ModeTransition (`0x80025264`, object native 3) through `psx_mod_call_guest`, with { kind, area delta, stage delta } in `g_CdUnused8768` (`0x80078768`, 16 bytes of the CD object's .bss that nothing touches, the decomp's docs/OBJECTS.md section 9; zeroed after the call): kind 0 (kind 1 inside the area; a world map would need kind 3, into MAP.BIN). Not `psx_mod_alloc_guest_memory`: the first version put the four bytes there, and an allocation changes the save-state header's layout cookie (`psx_mod_memory_layout_cookie`, `boot_state.c`), so every state saved before the first trip of a session was refused afterwards (found before release; `tests/test_secret_stages.py` loads test state 9 after the trip). The game resets the stage, loads it, and asks for disc 1 itself when the player is on disc 2. 0/5 plays its scene (about a minute) and goes on to chapter 1. Its 22 messages, left in the Japanese release's code (kana where the US font has its accented letters, kanji of a sheet no longer on the disc), are shown in English: while the room's message table (`g_EventVm.messages`, `0x8009FA18`) still points at the Japanese message 0, the cheats plugin's VBlank encodes the English lines (`kCutScene`, the US font's glyph code; the encoder also knows the speed, pen, colour and question controls) at `0x801E0000` and points the table at them. That RAM is above the stage's heap: the stage buffers end where the second arena starts (GS_ARENA2, `0x800A2CA0`, where the message window builds), the RAM stack only uses the top of RAM, and `0x801D1698`‥`0x801FF698` stayed zero in 1/24, the largest of the secret stages; the text is written only while GS_ARENA2 is 64 KB or more below it. The first version wrote the lines over 0/5's description bank (a Japanese leftover there); the corridor's English translation (an early draft of Clovis finding Princess Celia, 1/24 room 1) needed the RAM above the heap, since its bank is the English one the magic menu reads, and left with the corridor (git history: `5c65d83`; the Japanese and the English stay in the decomp's SECRETS.md). Main RAM only, so a state saved during the scene keeps the English. A loaded state cancels a pending trip; the row is greyed out without the Cheats mod | Area and Stage are RAM rows like the editors (`0x800A2F18`, the record `0x8007D9C6`), restricted to action stages; Go now is an action with a pad transform, asked in a dialog first; Cut prologue scene is an action asked in a dialog and made by `dragonvalor_cheats.c` |
| Video | Fullscreen, Smooth picture (the linear filter of the final image), Texture filtering (Nearest / Bilinear / xBR), Scanlines, Dynamic resolution | `PsxMenuHost.video_apply` (live; also saved to `settings.toml`) |
| Options | Volume, Pause in menu, Skip intro movie, Game dimming, Menu opacity | `host_volume_set`; Skip intro is a `mod_set` feature switch (next start); the rest are the menu's own (`dragonvalor_settings.toml`, below) |

How it is built: psxrecomp f9c287f5 ships the host side of an in-game menu
(`psx_menu_host.h`: pause, live video, mod changes, disc tray, exit) and
calls a `PsxMenuProvider` the title registers with `psx_menu_set_provider`
before the game window opens; recomp-ui ships the menu model
(`recomp_runtime_ui.h`: `recomp_runtime_ui_create` with our rows,
`recomp_runtime_ui_handle_input` with semantic inputs). The provider is the
glue: it creates the model and an ImGui context with the OpenGL3 backend
(`#version 330`, the runtime's context), sees every SDL event first
(`event`), maps Escape / arrows / Enter / Backspace and the pad's D-pad /
Cross / Circle / Start to semantic inputs and sends them, and the view's
mouse clicks, through one `dispatch` (the dialog, the Hero rows' Enter and
the close request are handled there before the model), and draws from the
runtime's overlay draw callback (GL thread) under one mutex with the event
path (main thread). No SDL platform backend for ImGui is used: the model
navigates itself, so ImGui is only ever touched by `draw`. The model closes
itself on BACK only through `dispatch` now (the top-level BACK is the close
request), and the provider still creates a fresh model on every open, so
the menu always starts on Game.

recomp-ui's drawer (`runtime_ui_imgui.cpp`, the `psx` theme) is no longer
used by the game. The two local patches to it (`patches/recomp-ui/0001-…`
scrolls the selected row into view and frames it, `0002-…` wraps a long
description inside its row; see `patches/README.md`) stay for the launcher
build and the planned upstream PR.

The files compile to nothing when the recomp-ui launcher is off
(`RECOMP_LAUNCHER`). Rows that need a restart (internal resolution,
renderer) are left out on purpose; the launcher owns them. The game's own
Start pause still exists. Scripted tests: `tests/test_menu.py` (keys through
the debug server's `host_key`, from test state 10; screenshots through
`present_shot`, which carries the overlay), `tests/test_heroswap.py ...
--menu` / `--menu-weapon` (the Hero dialog), `tests/test_area_row.py` (Warp).

**What each menu row persists to (2026-10-15).** Three stores, none of
them this file's choice except the last:

| Rows | Where | How |
|---|---|---|
| Fullscreen, Smooth picture, Texture filtering, Scanlines, Dynamic resolution | `settings.toml` (the launcher's) | `PsxMenuHost.video_apply` posts the change; the main thread applies it and calls `save_user_settings` (checked live: two steps on Texture filtering rewrote the key) |
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
  re-read after a save-state load. FMV detection is separate
  (`mdec_recently_active`) and unaffected.
- **4:3 screens inside the action module** (2026-11-17, from the player's
  play test: "the open menu shows the sides", "the stage-change effects
  only run in the original area"). The magic / item menu (mode 7,
  Triangle) is a 384 × 240 panel over a stage that keeps being drawn, so
  the GTE detector kept it wide with the stage at both sides. The screen
  transitions (`g_Transition` `0x800A2EF8`; decomp
  `src/main/r2B_transitions.c`) freeze, move and fade copies of the frame
  buffer's 384 columns, so in a wide frame they ran inside the 4:3 area
  while the live stage went on at the sides; the motion blur some event
  scripts switch on (`g_Render.blurOn`, `0x8009F8A8`) is the same kind of
  copy and showed as a lighter 4:3 rectangle. The plugin registers a
  native-scene predicate (`gpu_ws_set_native_scene_predicate` from the
  runtime's `gpu.h`, not re-exported by `mod_plugins.h`; nonzero = present
  this frame at 4:3 with side bars; activation clears it, so it belongs to
  the session's plugin) that says so for mode 7, while
  `g_Transition.state != 0` and while the blur is on in an event scene
  (sub-mode 5, `0x8008634D`), sampled per VBlank like the world predicate.
  The pause screen (Start) stays wide: its darkening tile is extended by
  the renderer. The world map and its Status screen (modes 10 / 13) were
  already 4:3 (2D, no projection).
- **The blur in play** (2026-11-17, the player's report: "with the special
  (hold X, press Square) the black bars appear at the sides"). The blur is
  not only an event effect: every action pack's hero script (entry 2) has
  two moves (animations `0x2036` and `0x204E`) that write `blurOn = 1` and
  the tint `0x9B` through object-script space 4 (`g_Render`; `SET_B`
  index `0x1068`), and some enemy-group files do the same (a scan of
  every stage file of disc 1 for the instruction words
  `03 07 04 00 68 10 00 00` + value). There `blurY` is 0, so the two sprites draw
  the 4:3 frame over itself about 10 % lighter while the game runs at 20
  frames a second; with the predicate above, the bars came and went in
  the middle of a fight. The lighter part cannot be carried into the
  margins (they are not in VRAM), so the predicate now asks for 4:3 only
  in event scenes, and in play the `ModelWeldReset` hook takes the blur's
  four packets (`blurTpage[f][1]`, `blur[f][1]`, `blurTpage[f][0]`,
  `blur[f][0]`, linked in that order at `ot[blurOtz]` of
  `g_FrameBuf[f]`, `0x80086358 + f × 0x2070 + 0x70`) out of the ordering
  table being built: one link rewritten, the table is cleared again next
  frame, the packets stay as they are. The slow motion stays. Checked with
  `blurOn` held at 1 in play from test state 10: wide in all 103 samples,
  `dv.ws.play_blur_hidden` 80 frames, no lighter rectangle; in the 1/7
  event scene (sub-mode 5) the blur still presents at 4:3;
  `tests/test_widescreen.py on` 9/9. The special's exact input was not
  reproduced here (the script tests the held Attack and Magic actions and
  a pressed Jump).
- **Dialogue border** (2026-11-17, "the dialogue bar does not span the
  screen"). The message window's black band is a 384-wide TILE the renderer
  already extends; its top border is six identical 64 × 8 sprites from x 0
  to 383 (decomp `src/main/exeE_msgwin.c`, `MsgWinWork +0x0C`), so the
  border stopped at the 4:3 edges. The `ModelWeldReset` entry hook tags the
  leftmost border sprite of the buffer about to be drawn as a repeating
  rectangle (`gpu_ws_tag_repeat_rect`, `gpu.h`, period 64; the tag is the
  address of the word before the `0x64` command of the E1 + SPRT packet,
  only while `g_EventVm.winList[frame]` is set), and the renderer repeats it
  into the side margins only.
- **Numbers.** The stage is 384 pixels wide (`0x180`, `video_info
  display_w`): 16:9 reveals 128 extra columns (64 per side, `ws_nw
  nw_extra`), 21:9 reveals 288. Debug-server knobs used for the A/B without
  the package: `display_aspect num= den= [adaptive=1]`, `window_size w= h=`,
  `ws_nw` (mode 2 = native-wide engaged), `gpu_state` → `ws.*`
  (`game_mode`, `present_native_43`, `x_margin`), `present_shot path=` for
  the window image (`shot` is the canonical 4:3 VRAM only).
- **HUD at the edges** (2026-11-18; until then it stayed at its 4:3
  positions, the HP / MP block 64 columns in from the left edge at 16:9,
  and the player asked for it at the edge). The framework's
  `nw_hud_corners` thirds shift stays off (ENHANCEMENTS.md records
  composite tears on the title that tried it, and this game's tutorial
  boxes span the full width); instead the plugin names the HUD's own
  primitives. The decomp rebuilt the HUD (`src/main/r2A_hud.c`,
  `include/dv/r2A_hud.h`): 61 elements built once by `HudInit`, pointers in
  `XbHudState.prim[]` at `0x8009FAF8 + 0x0C`, two copies each (one per
  display buffer, back to back), eight groups each opened by a DR_OFFSET
  and hung on `g_Transition.ot` by `HudLinkGroups`. Every frame, in the
  `ModelWeldReset` hook (inside `FrameDraw`, after the mode handler's
  `HudUpdate` and before the HUD is drawn), each element of a corner group
  is tagged with `gpu_ws_tag_hud_prim` (valid for that frame and packet
  content; the address is the word before the drawing command, so a
  T-SPRT's E1 word is skipped: tagging the packet's head left those
  pieces behind and split the HP bar in the first try) with its edge: left
  for the spell icon and name, HP / MP and the enemy gauge (bottom left)
  and the keys, right for the enemies-left counter, the pop-up flashes by
  where they sit (left of x 128, right of x 300, in place between: those
  are the ones a flying banner icon drops on its way); the title card stays
  centred. Seen in a 16:9 capture of test state 10: the spell frame at x 60
  of 1834 instead of 286, the HP / MP block whole; 60 fps;
  `tests/test_widescreen.py` 9/9. The counter, gauge and key icons were not
  on screen in that capture.

  **The pick-up banner** (same day, the player: its icon did not reach the
  corner either) goes to the right edge too: frame, item name and count.
  Its item icon flies there from where the pickup was in the world
  (`HudShowPickupBanner`: `bannerPos += bannerStep`, 1/16 of the way a
  frame, then a snap to (319, 24)), so it takes the edge's offset bit by
  bit: it is tagged right and (1 - progress) x the offset
  (`gpu_ws_configured_x_reveal`) is taken off the x of its corners in the
  packet drawn this frame (the game rewrites them every frame), so it starts
  over the pickup and lands in the shifted frame. The name that rises over
  the pickup stays in the world. Checked by starting the banner by hand
  (the XbHudState fields `HudShowPickupBanner` sets: group on, relink,
  timer 70, corners and steps; scratchpad `ws_banner.py`): the icon flew
  from mid-screen into the frame at the right edge.

  **Stage reloads and transitions** (same day, the player: in some screen
  changes the HP / MP block and the item banner went to their 4:3 places
  for a moment). Two cases, both captured:
  - *A reload with no transition effect* (mode 17, a black screen: the
    hero-swap restart writes mode 17 / sub 0 / stagePhase 1, and so do some
    stage changes): the HUD stays linked on `g_Transition.ot` and is drawn
    every frame, but the world-scene predicate only said "gameplay" for mode
    8, so these frames fell to the GTE detector, which sees no 3D and
    presents 4:3: HUD at its 4:3 place until the stage ran. Fixed: the
    predicate also holds while the HUD is linked (`dv_ws_hud_linked`: state
    1 and a chain head equal to a group's DR_OFFSET element + b x 0x0C; the
    state word alone is reused by the CONTINUE prompt). Captured from a
    player's state of 1/15 (`saves/openbios` file slot04, only loaded) with
    the reload written by hand: before, the HUD at its 4:3 place on the
    black screen; after, at the edge throughout. `tests/test_widescreen.py` checks it
    (writes the reload, expects `present_native_43` 0).
  - *A transition effect* (`g_Transition`; the player still saw it on
    2026-11-19 after the fix above): the effects were presented at 4:3 (the
    copies of the previous picture covered only the 384 columns), so the
    whole picture, HUD included, jumped into a pillarboxed 4:3 frame. Fixed
    for effects 0, 1, 2 and 7, which are built only from full-width 15-bit
    sprite copies: the plugin keeps them wide and tags those copies
    (`dv_ws_tag_transition`, in the ModelWeldReset hook: the chain on
    `g_Transition.ot[frame]`, sprites of a 15-bit page at least 128 wide),
    and the renderer continues each into the margins from the source
    buffer's wide surface: psxrecomp PR #692 as local patch
    `patches/psxrecomp/0002`, plus `0003`. Why the HUD comes out right:
    the 4:3 frame buffer does not hold the HUD at its 4:3 place; read back
    from the GL VRAM (`gl_fbo_peek`) in wide play it holds the HUD already
    moved to the edge and cut at x 0, so centre (4:3 buffer) plus margins
    (wide surface) are exactly the previous wide picture. (An earlier note
    here guessed the HUD would show twice; the VRAM read said otherwise.)
    Three things `0003` fixes, all measured on effect 2 from RAM over test
    state 10, brightness of the left margin against the centre: PR #692
    copied nothing for Dragon Valor (both buffers at base_x 0 share one wide
    surface, and the PR skipped a source equal to the target: margins stayed
    the live world); its band shader rounded an rgb 127 pass back up, so the
    margins brightened (42 to 61) while the centre faded; and the centre
    itself stopped at a dark grey (~17 of 255 with bilinear, ~50 with
    nearest; effect 7 with nearest did not darken at all, as the decomp's
    r2B_transitions.c had noted for the GL renderer) because `TEX_FS` kept
    the modulated 15-bit texel unrounded. After: effect 2 fades centre and
    margins to black together. Effect 7 (CONTINUE) darkens from the top and
    bottom in the margins as on the PS1, while with bilinear filtering the
    centre darkens everywhere a little earlier (the filter blurs every
    feedback pass); both end black. Effect 0 (next stage): the margins fade
    to black, the centre turns light under the sparkles, as it already did
    at 4:3 in the GL renderer (not chased). The right copies are 129 wide;
    their last column, VRAM x 384, is texture memory (clipped on a console)
    and drew a coloured line at the 4:3 edge that the margin's own copy
    carried on, so the plugin narrows them to 128 (same 4:3 picture). The
    melt (3), the shatter (9) and the unused 4, 5, 6, 8 still present at
    4:3. `tests/test_widescreen.py` checks effect 2 stays wide with its
    copies tagged and effect 3 goes 4:3 (11/11, also with the render
    thread). Captures: scratch `ws_trans.py` / `ws_trans_series.py` (start
    an effect from RAM, shoot, measure the regions).
  - *A room change inside a stage* (the one the player meant all along:
    their slot of 1/7 on 2026-11-19, walk left: the picture fades to black
    and the next room fades in, mode 8 / sub 7 throughout, no
    `g_Transition`): the frame stayed wide (`present_native_43` 0), but the
    renderer's HUD-tag counters (`ws_tag_stats`) showed every lookup
    *stale* from the fade's start to the next room (hits frozen, `stale`
    climbing by ~1000 a second), so the HP / MP block drew untagged, at its
    4:3 place. A tag holds for 2 VBlanks (`WS_HUD_ANCHOR_FRESH_FRAMES`); the
    plugin tags in the ModelWeldReset hook, before FrameDraw's wait for the
    frame (`while (g_FrameWait > 0)`), and while the room loads that wait
    is longer. Fixed by tagging again at every VBlank (`dv_ws_refresh_tags`
    from `dv_ws_vblank`: the same HUD and transition tags, nothing new
    written; the HUD only while its chain is linked), so the tag is fresh
    whenever the draw comes. Same walk after: 0 stale lookups, HUD at the
    edge through the black (also with the render thread). The fade itself
    is the game's own (FrameDraw's fade tile, which the renderer already
    widens).
- **Culling: left stock** (widened at 14:00 on 2026-10-15, withdrawn the
  same evening). `psxrecomp-analyze SLUS_010.92 --scan-widescreen
  --emit-ws-sites` (`cmake --build build-recompiler --target
  psxrecomp-analyze`) finds two functions with the width+height signature:
  `sltiu 385` at `0x8001FE1C` and `func_800401D8`'s `slti 385` at
  `0x80040334` / `0x8004034C`, read then as the mesh renderer's per-vertex
  bound and an object cull (`|object − camera| < 385`). **Correction
  2026-11-15**, from the asm: neither is a screen test. The first is the
  spark bound of the screen transition `func_8001F6CC` (sparks kept at
  x < 385); the second is floor contact, the largest height gap accepted
  between an object and the floor under it (385 above, 259 below). Plus the
  overlay copies
  `0x800BECB8` / `0x800C2B80`; `auto_screen_x = true` widened them all (a
  regen of 11 s whose diff is exactly the substitutions). The player then
  found a 54 × 22 block of texture noise at the right edge of the castle
  corridor (area 1 / stage 19), and it stayed with every cull back to
  stock, so it was not a cull artefact (its cause: *The noise block*
  below; the 2026-10-15 trace that blamed a parked model part with an
  unloaded texture page is superseded). The widening bought nothing
  visible (the first forest and town captures already drew to both edges:
  the stock object cull reaches 193 px past the 4:3 edge, enough for 16:9
  and 21:9) and the tree was regenerated back to stock culls
  (`[widescreen.cull] auto_screen_x = false`, no sites). The stale-cache
  note still applies:
  every regen moves the cache tag, and an old `cache/gcc/<tag>` directory
  next to the exe makes the loader log `OVERLAY CACHE HASH MISMATCH`, a
  false alarm while the static shard serves every overlay; delete it.
- **The noise block** (fixed 2026-11-16). The player's screenshot of
  2026-10-15 is the side-view stretch of the castle corridor (1/19) with
  the spike holes, not the forward-view stretch their later quick save
  sits in (the reason a check on 2026-11-14 saw nothing); walking from
  that save through the door and right brings it back on every frame.
  `ws_census` there: the quads at the block (y 124‥148) are POLY_FT4
  (`0x2E`, semi-transparent) with one edge at x 382 and the other at
  638‥982, in the packets of prim record 16 of three nodes (the
  decomp's `tools/live.py owner`). Record 16 is `ModelDrawR16`
  (`0x80023A24`; github victor-takai/dragon-valor-decomp
  `src/main/r2A_drawers.c`): lens sprites, each a quad that redraws the
  frame buffer under it (15-bit texture page at the buffer's VRAM row),
  24 in a ring around each red orb of that corridor. It clamps the
  rectangle's right edge to x 383 and never tests the left one, so a
  sprite wholly right of the frame (left ≥ 384, and the GTE clamps the
  centre at 1023, so every orb anywhere to the right qualifies) comes out
  inverted, from x 382 to left + 1, with its texture page at
  `left >> 6` ≥ column 6 (VRAM x 576 / 704 / 896 seen): texture data read
  as 15-bit colour. The console's 384-column draw area shows two columns
  of it; the wide surface shows the rest. Fix: `dragonvalor.widescreen`
  registers a function-entry hook on `ModelWeldReset` (`0x80021CE4`,
  added to `[recompiler] mod_function_entry_funcs`; the regen changed one
  line of `generated/SLUS_010.92_full_06.c`), which the frame draw calls
  right after `ModelRender` and `ModelWeldVertices` and before
  `DrawOTag`. The hook walks the two render lists and, for the buffer
  being built (`0x8009F84C`), collapses every record-16 packet whose
  texture page column is 6 or more to zero width (xy1 = xy0, xy3 = xy2).
  Checked on build-debug with the player's corridor save: the same walk
  that showed the block in all six captures shows none, the on-screen
  orbs keep their lens, the counter `dv.ws.lens_offframe_hidden`
  (`mod_counters`) climbs about 24 per off-frame orb per frame, and with
  the package off the hook does nothing (ws mode 0, no counter).
- **The left-margin smear** (fixed 2026-11-17, the player's save in the
  town of 1/7, menu slot 4, walking up and then left along the wall):
  horizontal streaks over the lower left margin. `gpu_frame_dump` of one
  frame, polygons under the streaks: POLY_FT4 `0x2E` quads with texture
  page `0x100` (the frame buffer, 15-bit), top-left corner at x 1 and
  top-right corner at x −156‥−734, 2‥14 lines tall, written by
  `ModelDrawR16` (`wtrace` PCs `0x80023C14‥0x80023C6C`). The mirror of the
  noise block: for a sprite whose centre is left of the frame the drawer
  reads x unsigned, clamps the left edge to 0 and keeps the (negative)
  right one, so a sprite wholly left of the frame comes out inverted from
  x 1 to right − 1, u from 0 to the right edge's low byte. The console
  draws at most column 0 of it; the wide surface draws the frame's first
  columns stretched across the margin. The same `ModelWeldReset` hook now
  also collapses record-16 packets with xy0.x = 1 and xy1.x < 1. Checked:
  the walk that showed the streaks has none of those quads in the GP0
  ring and a clean margin; `tests/test_widescreen.py on` 9/9.
- **Not fixed, not a bug: black wedges in the margin** where a room's
  mesh simply ends (the same save, walking left in the garden: a black
  wedge at the lower left). No polygon covers it (the PGXP triangle ring
  rasterised over one frame leaves exactly that wedge empty), the only
  cull before drawing is the ±89° object view cone (wide enough), and on
  a console a sliver of the same hole shows at the 4:3 edge. Tried and
  dropped the same day: exact NCLIP for the model drawers' backface tests
  (`[widescreen] precise_nclip`, `[widescreen.cull] nclip_exact_sites`
  for the eight `ModelDraw*` branches, plus the plugin's
  `psx_mod_set_native_wide_nclip_sites` / projection correction). It
  changed a quarter of the GT3 / GT4 decisions but nothing visible in an
  on / off comparison of the same frames (`nclip_stats exact=0|1`), and
  the projection correction never engaged (`projection_vertices` 0).
- **Checked** on build-debug, OpenGL, 1080p, PGXP on, from save state 0:
  `tests/test_widescreen.py on` 5/5 (mode 2, 128 columns, still wide with
  the pause up, window 16:9) and `off` 4/4; 0 unknown dispatches, the
  static shard still serves every overlay (no runtime compiles), re-run
  after the regen together with `tests/test_menu.py`. Not checked: later
  chapters, the field module (`MAP.BIN`) scenes, in-stage movies, the
  software renderer, a real resize with Fit.
  2026-11-17: the test now starts from test state 10 (`--state 9` also
  works; it refuses slot 0, the player's quick save) and also checks the
  menu and a transition at 4:3 and the stage wide again after each: `on`
  9/9, `off` 4/4. The transition is started from RAM and the state
  reloaded afterwards: an effect allocates its packets from
  `g_GameState.arena`, the stage's work area, so a stage cannot resume
  after an effect it did not start (the emulation stops on an unknown
  dispatch from `0x800C3FB0` a few frames later; a real scene change
  loads the next stage first). Composed frames (`present_shot`) of the
  menu, GAME OVER → CONTINUE (effect 7) into 3/1's opening scene with its
  dialogues, effect 2, the pause, the blur and the world map of area 1
  match the description above.

#### The bilinear page-edge seam (2026-11-17, a local psxrecomp patch)

- **What the player saw.** With `texture_filtering = "bilinear"` (the
  launcher's other choice; the project ships nearest), a coloured vertical
  stripe at x 256, two thirds across the screen, during the screen
  transitions and the motion blur ("a stripe in the middle of the screen at
  some moments"), and a dark line at the same column through the title
  screen; 4:3 and wide alike, OpenGL only. Every such picture is drawn as
  two prims meeting at a 256-texel page edge: the transitions and the blur
  copy the frame buffer from the pages at x 0 and 256, the title draws its
  picture in a 256 and a 128 half.
- **Cause** (psxrecomp, not the game). The plain bilinear path fetches the
  neighbour texels with `fetch_texel`, which wraps u / v to 8 bits before it
  clamps them to the prim's UV bounds: a neighbour left of u 0 becomes 255
  and is clamped to the prim's right edge, one past 255 becomes 0, so a
  prim's edge column blends the far side of its own texture. The feedback
  transitions grow that column into the stripe (VRAM column 256 bright and
  coloured while 255 had faded to black; `vram_peek` and `gl_fbo_peek`
  agree). The framework's `stable_texel` already clamps first; the plain
  path did not. Upstream: issue RetroPortingToolKit/psxrecomp#719 (another
  title, the same line on its title screen), PR #896 open; master 31130e90
  still has the old code.
- **Fix.** `patches/psxrecomp/0001-bilinear-clamp-neighbours-before-wrap.patch`
  (applied at configure time like the recomp-ui patches; `patches/README.md`
  allows psxrecomp patches since 2026-11-17 for bugs with an upstream issue
  or PR): a neighbour is clamped to the prim's bounds inside the base
  texel's 256-texel page before it wraps, in the OpenGL shader, the Vulkan
  shader and the software renderer, the same helper and calls as PR #896.
  It replaced a game-side workaround of the same day (a `dragonvalor.fixes`
  plugin that moved the transitions' and the blur's screen copies one page
  column left with u + 64), which only covered those copies.
- **Left as is.** Effect 0 (the dissolve of the next stage of an area)
  turns white on GL instead of fading to black: the renderer's modulation
  of those copies, not this seam (*Comparing renderers* below;
  `PLAYTESTING.md` item 6).
- **Checked:** `tests/test_bilinear_seam.py` (bilinear for the run, effect 2
  from test state 10, VRAM columns 255 / 256 of both buffers one second
  in): 3/3, largest step 2 colour levels (20 before). The title screen with
  bilinear has no line any more (column profile as with nearest), and
  composed frames of effects 0, 2, 7 and the blur show no stripe.

#### This project's package: `dragonvalor.heroswap` "Play as" (2026-11-17, experimental)

Every action stage loads with the hero the player picks. Package 2.0.0,
feature `play-as` (channel experimental, on by default since 2026-11-18 at its neutral choices, see below), option `hero`
(`stage` = each stage's own, or `clovis` … `anita`); the in-game menu's
Hero section has the same choice as **Play as**, confirmed in a dialog (the
first choice also switches the feature on, for the next start, when it is
off). The choice applies when a
stage loads, and a choice made in the in-game menu restarts the stage being
played as soon as the menu closes (2026-11-17): the plugin's VBlank callback
waits for plain play (mode 8 / sub-mode 7, the game's pause off, no event
script, no load busy) and writes what a stage exit or the CONTINUE prompt
writes, `g_GameFlow` mode 17 / sub-mode 0 and `g_GameState.stagePhase = 1`
(0x800A2D18): the stage loads again from its start with the new hero, with no
GAME OVER, Val and HP kept; the stage's doors and keys reset as with
Continue. Until 2026-11-18 the feature was off by default, so a player's
first choice in the menu only saved it for the next start: the plugin was
not running, the stage did not restart and a GAME OVER / Continue changed
nothing (the player's report on the release build). It is now on by default
with the neutral choices, where the plugin does nothing to the game (no
stage rebuild: hero and weapon equal the stage's own; map and epilogue
swaps only run with a hero chosen) beyond reading the disc once at the
first start and checking save-state loads, so a choice in the menu applies
at once (`tests/test_manifests.py` accepts it through `INERT_CHOICES`;
`tests/test_heroswap.py default --menu-weapon turquoise`, a state.toml that
does not name the package: 1/7 loads untouched, then the menu's Weapon
restarts it with Clovis holding the turquoise sword, 9/9). A player who
switched the feature off in the launcher gets the old behaviour.
The hero
is data, not code: the decomp (github victor-takai/dragon-valor-decomp,
`docs/targets/hero-swap.md`) found it, and its `tools/heroswap.py` is the
reference this plugin's C rebuild is checked against.

What the plugin (`mods/plugins/dragonvalor_heroswap.c`) does:

- **Kits, at start.** It reads `DATA.CDR` from the mounted disc
  (`psx_mod_read_disc_file`, the whole file: 438 MB on disc 1, 500 MB on
  disc 2, freed again) and keeps one kit per hero of that disc, taken from
  the first stage of the hero's first area (Clovis 1/1, Arlen 2/1, … Anita
  9/1): the hero's install ops (op 3 object, op 31 weld lists, op 52 kind 0
  sound bank, op 7 shadow) with the entries they name (animations no slot
  uses left out, the decomp tool's `--prune`), the VAB of the stage's file
  0, its hero-page TIMs (VRAM 384..447 × 0..255) and its portrait (960, 432,
  40 × 64, CLUT row (0, 500)). With them a table of the disc's action stages
  (108 on disc 1, 118 on disc 2) and the sizes a rebuild needs. Kits and
  tables go to `mods/cache/dragonvalor.heroswap/` beside the executable, so
  later starts read nothing. The other disc's heroes (Anna, Mihael, Anita on
  disc 2; Clovis to Phillip on disc 1; Gerome's kit comes from disc 1) come
  from the other disc's image (see *Disc change* below); before 2026-11-17
  that disc had to be started once with the feature on.
- **`StageBuffersAlloc` (0x80035AB8), every stage load.** For an action
  stage whose own hero is not the chosen one, it works out the rebuilt
  sizes and raises row 0 (`g_StageBufferSizes[0]`, 0x80078B20: file 0 and
  then the action pack, whole sectors) and sound slot 1 (`g_SoundBodySize[1]`,
  0x80077DBC: the VAB body rounded up to 64) when needed, unless the stage
  buffers would then end past 0x801EE69C or the sound slots take more than
  476096 bytes (the largest layouts the game itself uses: that stage keeps
  its hero). Otherwise it gives back the hero index a swap replaced.
- **`PackInstall` (0x80035E48, a0 = the file just read).** In a swapped
  stage, file 0 and the action pack, both read to ACTION's heap (0x800C5E98),
  are rebuilt in RAM before the install: file 0 takes the kit's VAB,
  hero-page TIMs (in the places of the stage's) and portrait; the pack keeps
  the stage's own ops and entries after the kit's (tools/heroswap.py's
  merge, byte for byte). With the pack it writes `g_Hero.index` (0x80086318,
  the name in the menus and the save; the game writes it only at a chapter
  start). In any other pack it replaces copies of the stage hero's portrait
  by the kit's: frame 0 of each event image set (op 66), uploaded back at a
  dialogue's end. The CD reads are untouched.

**Story scenes (2026-11-17).** Most scenes play the hero object itself, with
motions the event pack writes into its slots (op 23, 231 packs: made for the
stage's hero, played on the chosen hero's parts; 1/23's boss intro with
Kodel looks right). Six event packs (0/1 file 88, 3/1, 4/13, 5/19 and 12/29
file 80, 9/10 file 216) install their own copy of the stage's hero instead:
a 14-part object whose vertex entries are the hero's byte for byte, with its
own script, motions and texture page. In every pack of a swapped stage the
`PackInstall` hook now also:

- swaps such a copy's parts for the kit's (whose packets use the hero page)
  and its weld lists for the kit's (the old ones are packet offsets into the
  old parts); the table notes, per stage, the copy's part and list bytes, so
  `StageBuffersAlloc` grows the warp row (`g_StageBufferSizes[8]`) by the
  difference;
- replaces the stage hero's name by the chosen hero's in the messages (op 62
  entries, glyph-code text): the speaker label ("Kodel:") and whole-word
  copies in the lines; both names come from ACTION.BIN's g_HeroNames
  (0x800C5E14), the same glyph codes;
- replaces a reload of the stage hero's texture (0/1's file 81, after the
  opening) by the kit's.

A rebuilt pack is written only if it fits before the next stage buffer (the
warp row runs over the enemy groups' memory, as the game's own files do).
Checked with screenshots every 1.5 s through the scenes: 3/1's opening as
Clovis (Clovis on screen, "Clovis:" labels), 0/1's prologue as Kodel (the
village scene with Kodel, his texture, "Kodel:" labels), 1/23's boss intro
and fight as Kodel. A save made while playing as another hero records that
hero's name.

**Chapter epilogues (2026-11-17).** A scan of every entry of every stage of
both discs for the nine name glyph strings (ACTION.BIN's g_HeroNames): 305
op-62 messages (the dialogues, above) and 19 op-20 text banks, all on
MAP.BIN screens: the chapter epilogues (mode 11: 1/35, 1/37, 2/29, 3/29,
3/31, 4/27, 5/26, 6/23, 7/30, 8/30, 9/30) and the character profiles after
them (mode 12). No action stage's bank (the magic menu's texts) and no
narration page names a hero. The `PackInstall` hook now rewrites an
epilogue's text (program [30 1, 20 2, 71 11 0]): the area hero's name
becomes the chosen hero's, whole words (the lines are centred when drawn;
0xFCCC colour codes skipped), if the file still fits its buffer. The names
are a table in the plugin, because ACTION.BIN is not in RAM in MAP.BIN's
modes. The profiles stay as they are: each is a character's page with its
picture. Checked: 1/35 as Kodel ("once Kodel rescued Princess Celia",
"Kodel was coerced into becoming Princess Celia's personal bodyguard";
"His name was Arlen" stays), 3/31 as Felippe (five names, the file 20
bytes longer).

**Twins, the area hero's look, disc change (2026-11-17).** The remaining
scenes, read from the data and then watched:

- *Twin variants.* The enemy-group files 77 / 75 / 77 of 5/19, 5/20, 5/24
  upload Phillip's page again with Kadum's blade in its unused corner (units
  416..447 × 64..183) and a CLUT row 243; 12/29's file 77 uploads Anita's
  page with another face and a great sword (rows to 239, CLUT rows
  241..255). They texture a scene copy of the hero (file 80's object 0 /
  object 8), the hero holding the sword the scene gives him. Every other
  hero uses that corner of the page, so the variant painted the chosen hero
  with the stage hero's texture. The copy takes the kit's parts anyway, so
  the variant is now replaced by a two-unit TIM that leaves the kit's
  texture (there since file 0) as it is: the chosen hero takes the sword
  with their own weapon. (A kit's TIM can be bigger than the variant,
  Felippe's and Anna's, more than the enemy group's buffer holds.)
- *The area hero's look.* 5/20 and 5/24 play Phillip with Kadum: 13 of their
  hero's 14 vertex entries differ from 5/1's, while their scene copies are
  the plain Phillip of 5/1 (12 of 14 equal), so the copy was not found and
  showed Phillip's shape in the chosen hero's texture. A copy is now also
  one whose vertex entries match the area hero's kit (table cache version 3
  for the warp-row budget), and a portrait equal to the area hero's kit
  portrait is the hero's too: 12/29's ending shows 9/1's Anita, not its own
  file 0's.
- *Disc change.* The runtime's plugin disc reads (`psx_mod_read_disc_file`,
  `psx_mod_disc_file_extent`) read the disc the session started with; when
  the game asks for disc 2 and the player changes it (`cdrom_disc_select`),
  they still read disc 1, so disc 2's stages found no table and kept their
  hero. The plugin now asks the roster (`cdrom_disc_selected`,
  `cdrom_disc_roster_path`, runtime `include/cdrom.h`) which image is in the
  drive and reads a disc other than the session's straight from its image:
  the .cue's first FILE, MODE2/2352 sectors (2048 bytes at +24), `DATA.CDR`
  in the ISO 9660 root. The same reader takes the other disc's kits at
  start, so a disc 2 hero no longer needs disc 2 started once.
- *Checked* (composed frames, test state 10, GAME OVER > CONTINUE, a room
  reached the way `RoomWarp` does it once no read is in flight): 5/19 as
  Felippe (the Guardian gives Kadum; Felippe in the close-ups, "Felippe:",
  his portrait; the variant neutralised; then 5/20's scene with Jeanne,
  Vilherm and Calvin naming Felippe, the copy swapped), 4/13 as Kodel (the
  Guardian and the sword Soul), 9/10 as Clovis after `disc_select n=2` from
  the disc 1 session (disc 2 read from its image, 118 stages, the stage
  rebuilt; its scene, file 216, is loaded by an event script and was not
  reached), 12/29 as Clovis (Dahaka's end: Clovis, "Clovis:", his
  portrait). A first room warp that ignored a read still in flight (room
  0 prefetches room 1's enemy group into the memory the warp row overlaps)
  crashed the swapped runs and not the stock ones: a test artefact, gone
  once the warp waits for idle rows.

**World map and name (2026-11-17, from the player's play test).** With a
hero chosen, `g_Hero.index` now holds that hero outside the action stages
too (world map, scenes, the save screen), so the menus and the memory card
save name the hero being played; "Each stage's own" gives the game's value
back. (A bug fixed the same day: after several changes the plugin gave back
a previous swap's hero, and a save made after playing Gerome said Phillip.)
The world map's walker (decomp `src/map/mod2_worldmap.c`, MapWalkerDraw) is
an 8-bit sprite on page (832, 0), CLUT (384, 240), 8 frames of 32 px per
direction, in the 192 x 176 sheet each map stage uploads at VRAM (768, 0)
(the left half is the map's own labels and buttons, the walker the right
half, rows 0..143). The disc scan keeps each hero's walker and palette from
a map stage of the hero's area (cache `hero<N>.map`), and the `PackInstall`
hook writes them, with the hero's portrait (the map's Status screen), into
a map stage's file 0 (program [30, 21, 20, 71 10 0]) in place. Checked: 1/2
(Clovis's map) as Kodel shows Kodel walking, as Gerome the Status screen
shows Gerome's portrait and name.

**Save states (2026-11-17, from the player's play test: "in the first
cutscene after the stage of save 11, the name and the picture are still
Clovis's").** The swap happens when a stage loads; a save state brings back
the RAM of its moment, packs included. The player's menu slot 11 is test
state 10 (file slot 10, 1/7, taken as Clovis on 2026-10-15): with Play as
Arlen it loads with `g_Hero.index` 0 and Clovis's packs, so the stage, and
its closing scene (room 5's event pack, file 112: "Carolina: ... Thanks for
saving me again!"), stayed Clovis's; the same scene after a Continue as
Arlen shows "Arlen:" and Arlen's portrait in every line (the plugin swaps 1
portrait and 1 name in that pack). The plugin now registers a save-state
callback and, at the next VBlank, reads the loaded RAM: in an action stage
whose `g_Hero.index` is not the area hero, the stage was rebuilt for that
hero, so the swap state is set for it (the stage's other packs keep
following that hero: dialogue names, portraits, scene copies, texture
reloads) with the area hero's kit as the original (portrait, hero page,
vertex entries; the stage's own file 0 / 1 would mean reading all of
`DATA.CDR`, which on the emulation thread took over 4 s and the runtime's
starvation watchdog stopped the game). When the state's hero is not the
one chosen now, an on-screen notice (`host_osd_push`) says "Save made as
Clovis: Arlen plays from the next stage" (a stage cannot be rebuilt in the
middle; picking another hero in the menu restarts it). Checked in one
session: Continue as Arlen, save to a scratch slot (deleted afterwards),
load test state 10 (the notice), load the scratch slot ("save state 1/7
played as Arlen: its scenes follow"); `tests/test_heroswap.py arlen`
12/12. Not checked: a scene pack installed after such a load (a read
queued by hand into the warp row right after a load stays in its read
state, with or without the plugin; reads the game queues itself after a
load were not affected in play).

**Weapon (2026-11-17, the player's request: "being able to swap the hero's
sword too").** A second option of the feature, `weapon` (`hero` = the
hero's own, else a weapon model), and a menu row Weapon after Play as (a
change restarts the stage the same way). The weapon is node 2 of the hero
object (decomp `docs/targets/hero-swap.md`, *The weapon*: the node a
spell's power-up trades, the one with the sword trail's attach points, in
no weld list); a model only, damage stays STR and the moves the hero's.
The first version offered "each hero's weapon" from the kits (each hero's
first stage), and the player saw that most were the same sword: of the 9
kits, 7 hold one and the same model (Clovis's), only Anna's and Mihael's
differ. A scan of node 2 in all 226 action stages (geometry, and the look
decoded through the CLUTs) found the models (2026-11-18), now the option's
values, each taken from the first stage that has it. The four magic
swords carry the names the game's dialogues give them (the player named
Azos and Igunis; the glyph text of every stage file, decoded with the
decomp's `docs/FORMATS.md` table, agrees): `azos` Azos (1/1; the first
weapon of Clovis, Arlen, Kodel, Phillip, Gerome and Anita, 111 stages;
3/1 "You're stealing the magic sword Azos?", 9/1 Anita: "my sword,
Azos"), `soul` Soul (Felippe from 4/15, 4/13 "The magic sword Soul is
yours to protect now"; Anna's in area 7), `kadum` Kadum (Mihael's, area
8; 5/19 "You deserve to inherit Kadum", that scene's twin holds the same
model with another palette), `igunis` Igunis (10/2 and 46 more stages:
the three last campaigns, areas 10, 11, 12 with Mihael, Anna, Anita,
start with the hero's own weapon and give Igunis, 10/7 and 12/7 "The
magic sword Igunis has chosen you as its master", held at stage 2 and
from stage 9 on); then the other models: `felippe` Felippe's first sword
(4/3..4/13), and the last campaigns' own weapons, `dark` Mihael's (10/1,
10/4, 10/6, 10/7), `red` Anna's (11/1, 11/4, 11/6, 11/7), `purple`
Anita's (12/6, 12/7) and `turquoise` Anita's of 12/1 and 12/4 (the purple
one's design with a turquoise palette, the player's wish; its texture is
on another page of its file 0, VRAM (576, 256): `arm_kit` takes a
weapon's texels from the TIM its prims draw from, page-relative, and
gives the moved prims the hero page's tpage, as the decomp tool's `arm`
does; `tests/test_heroswap.py arlen --weapon turquoise --ref
<decomp>/build/heroswap/2-1_w12-1_to_1-7 --prime-disc2` 19/19, the hero-page
weapons' references rebuild unchanged). The last four have no name in the
game's text. (10/7 also says "Azos, the angel of fire. Soul, the angel of
light. Kadum, the angel of thunder.") Not offered: the prologue's
(0/1..0/5: a few more vertices, Azos's texture, the same sword on screen,
the player's check). The disc scan keeps each model's node-2 prims and
verts and the TIM they draw from (cache `weapon_<area>_<stage>.bin`, so a
renamed value keeps its cache); a disc-2 model is
offered once disc 2 has been read, like a disc-2 hero (that read used to
be tried once at activation, before the disc roster is registered, and
then never again: now the try only counts once the other disc's image
opens). A model that is the hero's own (Arlen with Azos) needs
no rebuild. `arm_kit` builds an armed kit: the hero's kit with the model's node-2
prims and verts, and the texels its hero-page prims use copied into the
hero's page where the hero's other 13 nodes use none (its own weapon's are
free then); prims sharing texels move together, an island keeps its place
if free, else the first free place by rows then 16-bit units; the CLUTs
keep their slot or take the first free one; if needed the image grows to
64 units x the CLUT block's first row (a scan of both discs: nothing but
the hero's own texture uploads into the page). It is the decomp tool's
`arm` (`heroswap.py build ... --weapon A S`) in C, and the swap uses the
armed kit wherever it used the kit (file 0, the pack, scene copies,
texture reloads), so scenes show the weapon too. The swap now also runs
when only the weapon differs (the stage's own hero, rebuilt from its kit
with the other weapon; its portrait and dialogue names are left alone).
Every hero / kit-weapon pair fits (Anna's and Mihael's pages grow for
some). Checked: `tests/test_heroswap.py arlen --weapon kadum --ref
<decomp>/build/heroswap/2-1_w8-1_to_1-7 --menu-weapon flame` 20/20 (pack
head in RAM and hero page image + CLUTs in VRAM equal to `build 2 1 1 7
--prune --weapon 8 1`; the menu row restarts 1/7 with Igunis, then still
called `flame`), `anna --weapon felippe --ref
<decomp>/build/heroswap/7-1_w4-3_to_1-7` 18/18 (`build 7 1 1 7 --prune
--weapon 4 3`), `kodel --menu stage` 14/14, `arlen --weapon soul
--menu-weapon purple` 15/15 (the row's last value), and after the
renaming `arlen --weapon kadum --menu-weapon igunis` 15/15 (the cache
renamed to `weapon_<area>_<stage>.bin` loads, no disc read); close-ups of
Arlen after a Continue into 1/7 with each of the other models, all
textured right (Kadum's split ice-blue blade and ring pommel, Soul's gold,
Igunis's fiery tip, Anna's thin white blade), and earlier Clovis (Each
stage's own) with Soul and Anna with Azos. (The test's GAME OVER answer pressed Cross once after a
fixed wait and sometimes missed the prompt; it now presses while mode 9
lasts.) After a save-state load the weapon is read back too: the hero
object's node 2 (`g_Objects[0]` `0x8008A4E0`, `nodes` at +0x80, node 2's
verts at +0x6C) points at a vertex entry that is byte for byte the hero
kit's (its own) or one model's (an armed stage holds the model's entry
unchanged), so the swap state is set with the armed kit again ("save
state 1/7 played as Arlen with weapon kadum: its scenes follow"; the
stage's own hero with another weapon: "... as Clovis with weapon soul"),
and a state whose weapon is not the chosen one gets the notice "Save made
with the hero's own weapon: Soul from the next stage". Checked in one
session with a scratch slot (deleted), as above.

History: 1.0.0 (2026-11-16) served per-stage folders written by the decomp
tool (`heroswap.py build SRC_A SRC_S DST_A DST_S`); 2.0.0 does the rebuild
itself for every stage.

- **Checked** on build-debug, muted and hidden, with
  `tests/test_heroswap.py HERO --ref FOLDER` (own `mods/state.toml`, the
  player's files restored, the game killed by PID): test state 10 (1/7,
  Clovis), GAME OVER > CONTINUE, then 1/7's file 0 and pack rebuilt, the
  pack in RAM with the same container head as the decomp tool's output for
  the same kit stage (`heroswap.py build 3 1 1 7 --prune` for Kodel), VRAM
  holding the reference's hero texture and portrait byte for byte
  (`vram_peek`), `g_Hero.index`, the Status screen (Triangle), 20 s of play,
  then a second Continue into 1/9, rebuilt for the same hero. Kodel 17/17
  (first start: disc 1 read, 108 stages, six kits); Anita 18/18 with
  `--prime-disc2` (disc 2 started first: 118 stages, three more kits; then
  disc 1 from the cache; row 0 and slot 1 raised). Screenshots: Kodel in
  Raxis City, on the Status screen with his portrait and name, and in 1/9.
  Arlen, Felippe, Phillip, Gerome, Anna and Mihael 17/17 each. Clovis, from
  test state 9 into Kodel's 3/1 (`--state 9 --leave 3/3`): the rebuild, the
  reference head, VRAM and the index pass, but the stage opens with a story
  scene (Kodel's actor on screen, Clovis's portrait in the dialogue) that
  holds the Status screen and the GAME OVER back, so those checks fail
  (14/17): the scene actors above (since handled, see *Story scenes*).
  In-menu change (`--menu felippe` after Kodel): 1/7 restarted as Felippe
  with modes 8 and 17 only (no GAME OVER), Val and HP kept, 22/22. Not
  checked: long play, a whole stage.

#### Video and audio defaults (measured 2026-10-14)

`game.toml` ships `texture_lod = "mipmap"` with `anisotropic_filtering = 4`
and keeps `[audio] buffer_ms` at the 180 ms default; `antialiasing_mode`
stays off and the CD Speed mod stays off. The picture filters are written
out too (the player's choice, 2026-10-14): `antialiasing = true`, linear
filtering of the final image (the in-game menu's *Linear filter* row), and
`texture_filtering = "nearest"`, the game's own textures keep their texels
(bilinear and xBR are one row away in the menu, Graphics > Texture filtering,
for the run). Both are the framework defaults; `DragonValor.bat` no longer forces
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
names, enlarged by an integer factor; `scripts/package_release.sh` leaves
the whole `texture-packs/` folder out of the release zip. No Dragon Valor
pack exists publicly;
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

**The HUD and save states.** The HUD's art (the H.P. / M.P. frames and
gauges, the digits, the spell icons) belongs to no stage: the title uploads
it once from `\TITLE\DB_TITLE.TIP` (115×248 words at (832, 256), then 46×48
at (960, 256), then 64×120 at (896, 280) over part of the first) and it stays
in VRAM for the whole game. The mod knows a texture by the upload that put
it there, and a loaded state restores VRAM without its uploads, so after a
state load the HUD kept its original art until the title came back. The
cheats plugin now sends the same three images again at the first action
frame after a state load (`dragonvalor_cheats.c`, *HD texture packs: the HUD
after a save state*; only while the HD Texture Packs feature is in the plan;
the words are the ones VRAM already holds, so nothing changes on screen).
`tests/test_hd_hud.py` checks it with an empty pack in dump mode: after
state 10 the HUD's two sources are dumped, and with the cheats feature off
(control) they are not. Stage and room textures still need a fresh upload
after a state load (a stage or area change).

**What a pack for this game needs** (learned making a local 4× pack from
4,502 dumps, 2026-11-21/22; the art and the tooling stay local):
- `config.yaml` at the pack root with `MaxVRAMWriteSplits: 64`. The game
  writes over parts of some uploads (the third HUD image over the first, at
  the title as in the plugin; parts of Raxis Castle's walls), and by default
  an upload that is partly overwritten loses its identity: those textures
  were never dumped nor replaced. With splits the untouched parts keep it.
  `DumpTextureWidthThreshold` / `HeightThreshold: 8` also catch the HUD's
  small text.
- No two replacements of one texture, palette and convention where one's
  rectangle lies inside the other's (dump sessions record different used
  areas): two parts covering a draw make the renderer compose them on every
  draw, and Raxis Castle fell from 60 to 42 guest frames/s. Keep the
  biggest.
- Switching replacements with the `hd_textures` debug command rebuilds the
  session and forgets the uploads seen so far, the title's HUD included;
  test from a state load (the plugin re-sends the HUD) or from the title.

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
persists that to `settings.toml` (`DragonValor.bat` forced the env override until
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
`symbols.toml` to name functions as they are understood. Three of its
names are older than what the code was later read to do: `GameFlowMain`
(`0x8004BF84`) is the event-script interpreter (cutscenes and the tutorial,
once per frame in mode 8 sub 5), `LoadStage` (`0x8003A9B0`) resets sound,
render, objects, pickups, textures and the camera before an area / stage
change and reads nothing from the disc, and `HeroCombat` (`0x80040DCC`)
resolves the collisions and hits of every object.


### Play sessions

Start `DragonValorDebug.bat`, then `python tools/session_monitor.py <out_dir>`: every
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
  `test_scroll_cheat.py`, `test_menu.py`, `test_area_row.py`, `test_skip_intro.py`, `test_widescreen.py`,
  `test_bilinear_seam.py`, `test_heroswap.py HERO`).
  `python tests/test_manifests.py` checks every mod
  manifest statically (ids, default-off features unless every option is off or at a value in `INERT_CHOICES`,
  option bounds, plugin ids
  registered in `mods/plugins/*.c`); `python tests/test_cheats.py on|off`
  exercises the cheat plugins on a running debug build from slot 0 (see *This
  project's package* for the `state.toml` setup); `python
  tests/test_skip_intro.py [--off]` checks the intro skip on a fresh boot;
  `python tests/test_menu.py` drives the in-game menu from test state 10 (it launches
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
  each with its own hero); `python tests/test_secret_stages.py`
  takes test state 10 to the cut scene 0/5 through Warp > Cut prologue
  scene (the stage loads and keeps running, the room's messages are the
  English ones, test state 9 still loads after the trip; a picture goes
  to `<build>/test_shots`); `python tests/test_hd_hud.py` checks that the
  HD Texture Packs mod knows the HUD again after a state load (an empty
  temporary pack in dump mode: the HUD's sources are dumped after test
  state 10, and not with the cheats feature off).

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
launcher_assets/img/  boxart (libretro, see BOXART_SOURCE.txt);  assets/  app icon (tools/make_app_icon.py)
mods/preloaded/       bundled mod packages (dragonvalor.cheats, dragonvalor.skip-intro, dragonvalor.enhancement.widescreen, dragonvalor.heroswap; overrides of psx.enhancement.pgxp, cd-speed, hd-textures and psx.presentation.bezel)
mods/plugins/         C plugins behind those packages' [[plugin]] ids; dragonvalor_menu.cpp = in-game menu provider
tools/dvdbg.py        debug-server helper;  tools/session_monitor.py  long-run sampler
tools/sync_symbols.py framework copy of the symbol sync;  tools/mipsdis.py  listing from generated C
tools/bin_listing.py  BIN/*.BIN from the disc -> listings at 0x800BBEF0 (whole modules, not only executed pages)
tools/datacdr.py      DATA.CDR areas / stages / files, action stages vs scenes (`kinds`), a stage's hero entries (read-only)
tools/make_app_icon.py  draws the app icon into assets/ (.ico, .png, .svg; needs Pillow)
tools/ramscan/        RAM-dump listing, base-register scan and the live drop/pepper probes (README inside)
tests/states/         save states 0, 9 and 10, local only (not in git) + README;  tests/test_*.py  checks
scripts/package_release.sh  local release zip (framework packager with --stage-only, minus mods/texture-packs, then its create_release_zip.py; mods/cache, the Play as data read from the disc, is parked outside the build tree for the run: the packager's BIOS guard refused its .bin files on 2026-11-18)
DragonValor.bat, DragonValorDebug.bat  player / debug launch (were Play.bat, PlayDebug.bat)
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
