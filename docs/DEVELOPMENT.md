# Developing DragonValorUSRecomp

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

## Run

- `Play.bat` — player launch (`build-release`). `Play.bat -dbg` uses `build-debug`.
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
| Memory cards | `saves/card1.mcd`, `saves/card2.mcd` (project root, `[runtime] memcard_dir`); raw 128 KB images, a DexDrive `.gme` minus its 3904-byte header. The game's Load Game reads card 1 only. |
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
| `0x800A2D9C` | u32 | Play time in frames (Load list shows it as h:mm; the GameFAQs save holds 5,594,735 = 25:54) | |
| `0x800A2DD0` | u16 | Val (gold; Status shows 4 digits, 10000 reads 0000) | `func_8004BF84` |
| `0x800A2E06`‥`0D` | u8 ×8 | Magic levels: Heal, Defense, Fire, Ice, Thunder, Mine, Stealth, Vortex (0 = not learned, 1‥3 = LEVEL) | |
| `0x800A2E18`‥`1A` | u8 ×3 | Three flags that are 0 in slot 0 and 1 in the post-clear GameFAQs save (gallery / cleared-path candidates, unconfirmed) | |

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

`mods/preloaded/packages/dragonvalor.cheats/1.0.0/manifest.toml` (format 7)
declares three features, all off by default, each backed by a VBlank
callback in `mods/plugins/dragonvalor_cheats.c`. That file is linked into the
exe by the `target_sources` line in `CMakeLists.txt`; the package itself
carries no native code, only the plugin ids.

| Feature (launcher group "Cheats") | Plugin id | Each VBlank, while HPmax ≠ 0 |
|---|---|---|
| Infinite HP | `dragonvalor.infinite-hp` | HP ← HPmax |
| Infinite MP | `dragonvalor.infinite-mp` | MP ← MPmax |
| Val floor | `dragonvalor.val-floor` | Val ← max(Val, `amount`); integer option 0‥9999, default 9999 |

The build stages the package into `<build>/mods/bundled/dragonvalor.cheats`,
the launcher lists the features, and the player's choice lands in
`<build>/mods/state.toml`. Verified 2026-10-14 from slot 0 on build-debug:
with the features on, HP/MP/Val written to 5/3/12 read back 30/20/5000
within half a second; with them off the written values stayed. Limitation
shared with GameShark codes: the refill runs once per frame, so a single hit
of at least HPmax still kills (writing HP = 0 produced GAME OVER).

To add a feature: write the callback in `dragonvalor_cheats.c`, register its
id in the `PSX_MOD_CONSTRUCTOR` block, add `[[feature]]` + `[[plugin]]` (and
`[[option]]`s, read with `psx_mod_current_option_value`) to the manifest,
rebuild both build dirs. Scripted test without the launcher: append the
package and its features with `enabled = true` to `<build>/mods/state.toml`
(format 2; option values are strings under `[feature.values]`), then
`tools/dvdbg.py launch --state 0`, `write_ram` the stat and read it back
(`tests/test_cheats.py on|off` does exactly that).

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
`settings.toml` outrank it), and `Play.bat` sets `PSX_TEXTURE_FILTER=3` for
xBR texture filtering. xBR cannot be chosen in the launcher: recomp-ui clamps
the filter to nearest/bilinear on every start and the runtime persists that
to `settings.toml`, so a hand-edit lasts one Play; the env override is read
after the launcher and wins for the run (OpenGL only; it acts wherever a
texel covers two or more output pixels). Compared 2026-10-14 at 1080p with
the live `texture_filter` command (modes 0/1/3) and `present_shot`: nearest
keeps the blocky texels, bilinear blurs them, xBR keeps flat areas crisp and
removes the staircase on diagonals. Dumps keep accumulating across normal
play sessions, so a texture pack can grow progressively instead of
requiring a dedicated full playthrough; the attract demo (stages 6‥9 and
31‥34) adds a few more areas unattended.

Identification tooling: `tools/dvdbg.py` + `read_ram`/`write_ram` for RAM
probes, a data-xref listing of the EXE (address → function, width,
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
  transitions quickly. This is how the dissolve finding in `HISTORY.md` was made.
- **Attract route.** Boot → Start once (skips the FMV) → leave the title alone:
  story movie → main menu → gameplay demo (dungeon, dragon arena), looping.
  A second Start on the title goes to the menu; a Start on the menu starts a
  New Game.

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
mods/preloaded/       bundled mod packages (dragonvalor.cheats manifest)
mods/plugins/         C plugins behind those packages' [[plugin]] ids
tools/dvdbg.py        debug-server helper;  tools/session_monitor.py  long-run sampler
tools/sync_symbols.py framework copy of the symbol sync
tests/states/         committed save state slot 0 (+ README)
scripts/package_release.sh  local release zip (wraps the framework packager)
Play.bat, PlayDebug.bat     player / debug launch
DISC.md               which dumps work (ships at the zip root);  LICENSE  (MIT, our files)
docs/STATUS.md        state, open items;  docs/DEVELOPMENT.md (this file);
docs/AOT.md, docs/HISTORY.md, docs/BACKLOG.md
.github/workflows/release.yml  bundled release CI (framework template)
psxrecomp/, recomp-ui/  framework and launcher submodules (pinned gitlinks)
```
