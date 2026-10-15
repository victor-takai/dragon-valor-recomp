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
| `0x8008A5B6` | u16 | STR as shown on the Status screen: a derived value, recomputed by `func_800358A8` as the sum of the five attack entries below and **never read by combat** (see *How damage is computed*) | `func_800358A8` (W) |
| `0x80086324`‥`2C` | u16 ×5 | Attack-power table of the current hero (new game: 6, 9, 12, 15, 18). A hit's power is the entry at index `0x8008632E`; a gem adds one unit (1, 1.5, 2, 2.5, 3, as two alternating byte rows at `0x80011598`) | `func_80040DCC` (R, pc `0x80041798`), `func_80028940` (gem), `func_8003A7C4` (new game) |
| `0x8008632E` | u8 | Index into the attack table for the current hit; written 0 at every attack start (`func_800260FC`, pc `0x80026630`). Which move uses entries 1‥4 is unverified (a chained combo is the guess) | |
| `0x8008631E` / `0x80086320` | u16 ×2 | HP / MP mirrors kept by the action code (HUD bars) | ACTION.BIN |
| `0x800A2EDC` / `0x800A2EDE` / `0x800A2EE0` | u16 ×3 | Gems picked up / scrolls picked up / both, stepped by the pickup handler | `func_80028940` |
| `0x800A2DB8`‥ | u16 ×N | Pickup counters by id (0 gem, 2 scroll, 14 HP max up, 15 MP max up, 16 attack up, 17 DEF up); ids from 13 on are the inventory list below | `func_80028940` |
| `0x8008A5B8` | u16 | DEF | `func_80038094`, `func_80038274` |
| `0x800A2D9C` | u32 | Play time in frames (Load list shows it as h:mm; the GameFAQs save holds 5,594,735 = 25:54) | |
| `0x800A2DD0` | u16 | Val (gold; Status shows 4 digits, 10000 reads 0000) | `func_8004BF84` |
| `0x800A2E06`‥`0D` | u8 ×8 | Magic levels: Heal, Defense, Fire, Ice, Thunder, Mine, Stealth, Vortex (0 = not learned, 1‥3 = LEVEL) | |
| `0x800A2E18`‥`1A` | u8 ×3 | Three flags that are 0 in slot 0 and 1 in the post-clear GameFAQs save (gallery / cleared-path candidates, unconfirmed) | |

| `0x800A2DD2`‥ | u16 ×N | Inventory counts, one per item type: Pendant, Lithograph, Ring, Talisman, Wine, Pepper, Earrings, Bracelet, Horn, Perfume, Tea, Mask, Silver Key, Gold Key, Harp, Tiara, Copper Key, … (accessories, trade goods, keys; **no weapons**) | |
| `0x800A2F18` / `0x800A2F1A` | u16 ×2 | Area and stage id of the stage to load (DEMO.BIN writes 0/6‥9 and 7/31‥34 before calling `func_8003A9B0`); slot 0 is 0/2 | `func_80025264` (W), `func_80038094`, … |
| `0x8008634C` / `0x8008634D` | u8 ×2 | Game mode and sub-state: 8 = action stage, 10 = loading, 17 = attract demo | `func_8004BF84`, `func_80025264`, … |
| `0x8008A4E0` | struct | Player object; the stats above are its `+0xCE`‥`+0xD8` fields. Objects are 0x250 bytes apart: slot 0 is the hero, enemies sit in later slots with the same layout (slot 0's first screen: the enemy is slot 5, `0x8008B070`, HP 15/15 at `+0xCE`) | |

`write_ram` writes one byte per call (`addr=`, `val=`). The GameShark codes
circulating on GameRevolution are correct for Val (`800A2DD0 2700`) and the
magic levels (`300A2E06`‥`0D 0003`); its HP/MP codes (`800A5AE0 0064`, …) are
the addresses above with one hex digit dropped. The "beat the first boss,
sell items, pause, press Up Down Left Right Select" cheat from the same page
has no button-sequence table in the EXE or the BIN modules and did nothing
when tried from slot 0; its precondition (first boss beaten) was not met, so
it stays unverified rather than disproved.

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
- Pickups go through the jump table at `0x8001131C` (`func_80028940`): id 0
  (gem) adds a byte row of `0x80011598` to the five entries, alternating
  `1,2,2,3,3` and `1,1,2,2,3` by gem count, i.e. one unit of the
  1 : 1.5 : 2 : 2.5 : 3 progression the new-game values (`func_8003A7C4`:
  6, 9, 12, 15, 18) already follow; id 2 (scroll) raises MP max; ids 14‥17
  are the HP max / MP max / attack / DEF consumables.
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

`mods/preloaded/packages/dragonvalor.cheats/1.2.0/manifest.toml` (format 7)
declares **one feature, `cheats`, on by default with every option off**, backed
by `mods/plugins/dragonvalor_cheats.c`. The feature stays in the session's
plan so the in-game menu can switch its options live; with every option off
the plugin does nothing. The package carries no native code, only the plugin
id; the C file is linked by the `target_sources` line in `CMakeLists.txt`.

| Option (launcher group "Cheats") | Each VBlank, while HPmax ≠ 0 |
|---|---|
| `infinite_hp` | HP ← HPmax |
| `infinite_mp` | MP ← MPmax |

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
stayed; toggled from the menu, HP refilled at once. Limitation
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
on|off` does exactly that).

#### In-game menu (`mods/plugins/dragonvalor_menu.cpp`)

Escape, or Select+Start on the pad, opens a menu over the game; the guest
and audio pause while it is up (the "Pause in menu" row turns that off).
Sections and rows:

| Section | Rows | Applied through |
|---|---|---|
| Display | Fullscreen, Game dimming, Menu opacity | `PsxMenuHost.video_apply` (live; also saved to `settings.toml`); the backdrop rows are the menu's own |
| Graphics | Linear filter, Texture filtering (Nearest / Bilinear / xBR), Scanlines, Dynamic resolution | `video_apply`, live |
| Audio | Volume | `host_volume_set` |
| System | Resume, State slot (1‥12), Save state, Load state, Pause in menu, Return to launcher, Quit | `savestate_request_save/load` (slot − 1), `request_exit` |
| Cheats | Infinite HP, Infinite MP (`mod_set` on `dragonvalor.cheats` / `cheats` → live option change, saved to `mods/state.toml`); Val, Max HP, Max MP, STR, DEF | the five editors show the value in RAM and write it at once (`psx_mod_write_half`), greyed out until a hero is loaded; STR is spread over the five attack entries at `0x80086324` (the shown STR is their sum, see *How damage is computed*) |
| Magic | Heal, Defense, Fire, Ice, Thunder, Mine, Stealth, Vortex: Not learned / Level 1‥3 | bytes `0x800A2E06`‥`0D`, written at once |
| Items | Pendant, Lithograph, Ring, Talisman, Wine, Pepper, Earrings, Bracelet, Horn, Perfume, Tea, Mask, Silver Key, Gold Key, Harp, Tiara, Copper Key: 0‥99 | u16 counts from `0x800A2DD2`, written at once |
| Game | Skip intro movie, Insert disc 1 / 2 | `mod_set` feature switch (next start), `disc_insert` (the runtime's tray policy) |

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
an accent outline plus a left bar. It is meant to go upstream as a PR.

The file compiles to nothing when the recomp-ui launcher is off
(`RECOMP_LAUNCHER`). Rows that need a restart (internal resolution,
renderer) are left out on purpose; the launcher owns them. The game's own
Start pause still exists. Scripted test: `tests/test_menu.py` (keys through
the debug server's `host_key`, screenshots through `present_shot`, since
native screenshots never contain the overlay).

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
- **Committed tests.** `python tests/test_manifests.py` checks every mod
  manifest statically (ids, default-off features, option bounds, plugin ids
  registered in `mods/plugins/*.c`); `python tests/test_cheats.py on|off`
  exercises the cheat plugins on a running debug build from slot 0 (see *This
  project's package* for the `state.toml` setup); `python
  tests/test_skip_intro.py [--off]` checks the intro skip on a fresh boot;
  `python tests/test_menu.py` drives the in-game menu from slot 0.

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
mods/preloaded/       bundled mod packages (dragonvalor.cheats, dragonvalor.skip-intro)
mods/plugins/         C plugins behind those packages' [[plugin]] ids; dragonvalor_menu.cpp = in-game menu provider
tools/dvdbg.py        debug-server helper;  tools/session_monitor.py  long-run sampler
tools/sync_symbols.py framework copy of the symbol sync;  tools/mipsdis.py  listing from generated C
tests/states/         committed save state slot 0 (+ README);  tests/test_*.py  checks
scripts/package_release.sh  local release zip (wraps the framework packager)
Play.bat, PlayDebug.bat     player / debug launch
DISC.md               which dumps work (ships at the zip root);  LICENSE  (MIT, our files)
docs/STATUS.md        state, open items;  docs/DEVELOPMENT.md (this file);
docs/AOT.md, docs/HISTORY.md, docs/BACKLOG.md
.github/workflows/release.yml  bundled release CI (framework template)
patches/              local submodule patches applied at configure time (README inside)
psxrecomp/, recomp-ui/  framework and launcher submodules (pinned gitlinks)
```
