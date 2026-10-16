# Backlog — review of 2026-10-14

Findings of a read-only review (three parallel reviewers over our code, docs
and config, plus a survey of other psxrecomp titles). Rows marked **✔** were
applied on 2026-10-14 (commits ef29247, 2691d05 and the following ones); the
rest are still open, each with the file to touch and why. Priority:
**H** (wrong or misleading today), **M** (worth doing soon), **L** (tidy).
Verified-correct things are listed at the end so they are not re-checked.

## A. Build, release and legal

| P | Item | Where | Why / how |
|---|---|---|---|
| H | ✔ README promises a release CI that does not exist, and embeds `.github/raid-discord.png` | `README.md` (Releases paragraph, image), `scripts/package_release.sh` comments | No `.github/` directory. Either run `sh psxrecomp/tools/generate_ci.sh` to install `.github/workflows/release.yml` from the framework template (GAME_PROJECT_SETUP.md "CI workflow template"), or reword to "local packaging only" and use `windows-x64` in the example. **[done: `.github/workflows/release.yml` generated, README reworded]** |
| H | ✔ README badges point at `RetroPortingToolKit/DragonValorUSRecomp` | `README.md` top | Scaffold default slug; the remote is `victor-takai/dragon-valor-recomp`. Fix the three URLs or drop the block until a release exists. **[done]** |
| H | ✔ No LICENSE for our code | repo root, `README.md` "Legal" | Framework is PolyForm Noncommercial, recomp-ui and OpenBIOS MIT, the cheats manifest says MIT. Add a LICENSE for our files and one paragraph on what the noncommercial framework licence means for the built exe. **[done: MIT `LICENSE` + README "Licensing"]** |
| M | ✔ `.recomp.json` says `"status": "exploring"` | `.recomp.json` | Reality is boot-to-gameplay at 60 fps on both discs and a packaged zip. `in-progress` now, `playable` after the human play session (GAME_PROJECT_SETUP.md release checklist). **[done: `in-progress`]** |
| M | ✔ `catalog_identity.json` and `disc_probe.json` are stale and disc-1-only | both files | Pre-Redump names, an absolute dead path, `track_counts = [2]` for one disc. Re-run `psxrecomp/tools/new_project_layout/probe_disc.py --write-catalog` (or `update_disc_set.py`) on both renamed cues. **[done: re-probed (identity stays disc-1 based, the tool's format)]** |
| M | ✔ `[prepare_disc]` digests cover disc 1 only | `game.toml` | `psxrecomp_cli.py verify-disc` fails for disc 2; append its size/md5/sha1 (DISC.md has the size). **[done]** |
| L | ✔ Player-facing name is the scaffold token | `game.toml [game] name`, CMake `WINDOW_TITLE`, `package_release.sh --display-name/--disc-hint`, `codegen_setup.c display_name`, `DISC.md` | "DragonValorUS Recompiled" vs "Dragon Valor (USA) Recompiled" vs "Dragon Valor Recompiled". Pick one; if the title changes, pass `EXE_NAME DragonValorUS_Recompiled` so the exe keeps its name (the exe name is duplicated in Play.bat, PlayDebug.bat, dvdbg.py, package_release.sh, codegen_setup.c). **[partly: packager display name / disc hint and DISC.md say "Dragon Valor (USA) Recompiled"; `WINDOW_TITLE` kept (exe name)]** |
| L | ✔ Setup wizard left OFF | `CMakeLists.txt` | Framework layout ships it ON as the first-run disc picker; browsing for a disc works without it. Enable and test once, or record the decision. **[done: decision recorded in CMakeLists.txt]** |
| L | ✔ `package_release.sh` side effects undocumented | `scripts/package_release.sh` header | The packager rewrites the tracked `VERSION`; `chmod +x` is unnecessary (it is run through `bash`). **[done]** |

## B. Player experience (config)

| P | Item | Where | Why / how |
|---|---|---|---|
| H | ✔ xBR filtering cannot be set through the launcher | docs | `recomp-ui` clamps `texture_filter` to nearest/bilinear on every start and the runtime persists that to `settings.toml`, so the hand-edit recipe lasts one Play. Done on 2026-10-14: `Play.bat` sets `PSX_TEXTURE_FILTER=3` (read after the launcher). Still to do: fix the recipe in `docs/DEVELOPMENT.md`, and watch for a recomp-ui bump with an xBR row. **[done: Play.bat override + DEVELOPMENT recipe; superseded 2026-10-14: the player chose nearest textures + linear present as the shipped defaults (`game.toml`), the override left `Play.bat`, xBR stays a per-run choice in the in-game menu]** |
| H | ✔ No per-title internal resolution default | `game.toml [video]` | Fresh installs render at native 240 lines. `internal_resolution = "1080p"` (or `"display"`) is the documented per-game default; check `resolution_reference_lines` against this title's `video_info`. **[done: `internal_resolution = "1080p"`]** |
| H | ✔ PGXP is offered but the exe is the base flavour | `CMakeLists.txt` (`PGXP` option of `psxrecomp_add_game_runtime`) | Without the hook variant the mod covers almost nothing (ENHANCEMENTS.md G1.11). Needs the A/B the doc mandates (`geom_correction` census + `screenshot_hires`), costs ~7 % CPU when off, and save states of the two flavours differ (slot 0 would be re-taken). **[done 2026-10-14: hook flavour built, state re-taken, census 66 % dataflow hits, default-on override + pgxp_mod_only]** |
| M | ✔ `default_mode = "dualshock"` is not a recognised value | `game.toml [controller]` | Parser accepts `analog`/`digital` and falls back to analog, which is why it works. Write `analog`. Do not migrate to `[runtime] controller`: parsed but never read. **[done: `analog`]** |
| M | ✔ Deprecated `psx.enhancement.fast-loading` ships in the catalog | `CMakeLists.txt` | Its own manifest says deprecated; `EXCLUDE_BUILTIN_MODS psx.enhancement.fast-loading` is the documented pattern; CD Speed is the supported accelerator. **[done: excluded]** |
| M | ✔ Audio latency left at the 180 ms default | `game.toml [audio] buffer_ms` | Action title; measure 60‥90 ms for crackle and set it (a developer choice, not a player setting). **[done: 90 ms was clean muted but choppy on the device at the title screen; reverted to 180 ms in game.toml]** |
| L | ✔ OpenGL quality opt-ins never evaluated | `game.toml [video]` | `texture_lod = "mipmap"` + `anisotropic_filtering` (shimmer at high scale), `fmv_chroma_smoothing = true` (MDEC movies), `antialiasing_mode = "fxaa"`. A/B with `present_shot`, ship the winners. **[done: mipmap + aniso 4 shipped; FXAA measured and left off; chroma smoothing not evaluated]** |
| L | ✔ Load acceleration unconfigured | `game.toml [runtime] idle_skip`, `warm_cd_routes`; cd-speed mod | Per-game opt-in, evidence first; at least test cd-speed 2x/4x and note the safe value. **[done: 2×/4×/8× clean but the stage reload stays 3.5 s; left off]** |
| L | ✔ Stale comment on `p1_device = "auto"` | `game.toml` | The "auto" branch is unconditional in the runtime; the macro story is outdated. **[done]** |
| L | `[game] discs` holds file names, not paths | `game.toml`, `DISC.md` | Works through the stem match but is off the documented "array of .cue paths" contract; keep the rename rule prominent. |
| L | ✔ Dev leftovers in the player build dir | `build-release/mods/state.toml`, `build-release/psx_freeze_dump_*.json` | HD dump on and eight freeze dumps; not shipped, but clear before timing runs. **[done: freeze dumps deleted (the player's state.toml is theirs)]** |

## C. Tooling and code

| P | Item | Where | Why / how |
|---|---|---|---|
| H | ✔ `session_monitor.py` loses screenshots with a relative out dir | `tools/session_monitor.py:10,37` | Path is handed unresolved to the runtime, whose cwd is the build dir; `.resolve()` it and check the reply's `ok`. **[done]** |
| H | ✔ `dvdbg.py launch` cannot tell a new instance from an old one | `tools/dvdbg.py:103-112` | A taken port makes the runtime log "bind FAILED" and keep running; the readiness poll is answered by the old process and `--state` loads into it. Probe the port before `Popen` and refuse. **[done: port probe before launch]** |
| M | ✔ `sync_symbols.py --check` fails out of the box | `tools/sync_symbols.py:97` | Default label is the directory name, the committed header says `DragonValorUS`; take the label from `game.toml [game] name`. Also: our copy is byte-identical to the framework's, uses `Path.cwd()`, and `psx_symbols.h` is included by nothing. **[done: label from game.toml, repo-relative paths]** |
| M | ✔ `PSX_DEBUG_SERVER=1` is dead | `PlayDebug.bat:11`, `tools/dvdbg.py:91` | Nothing reads it; the server is compiled in with `PSX_DEBUG_TOOLS=ON`. Same class as `PSX_HOST_MUTE`. **[done]** |
| M | ✔ `Play.bat -dbg` and `PlayDebug.bat` drifted | both | Same exe, different env and pause policy. Make PlayDebug call `Play.bat -dbg` after setting its env, or drop `-dbg`. `Play.bat` also swallows extra arguments (`%*`). **[done: PlayDebug calls Play.bat -dbg; args forwarded]** |
| M | ✔ `state load` lets a stale local slot shadow a re-taken committed state | `tools/dvdbg.py:160-163` | Restores `tests/states/` only when the local file is missing; compare size/mtime or add `--fresh`. On failure, hint that the state may predate the runtime (`savestate_status` carries no reason). **[done: size/mtime refresh + hint on failure]** |
| M | ✔ `session_monitor.py` duplicates `dvdbg.rpc`, hard-codes 4370, rewrites the CSV header on each start | `tools/session_monitor.py` | Import from dvdbg, write the header only for a new file, document the 2 h cap. **[done]** |
| M | ✔ The cheat test lives outside the repo | `docs/DEVELOPMENT.md` (scratchpad references) | Commit `test_cheats.py` as `tests/test_cheats.py` (needs only dvdbg + a `state.toml` edit) and drop the scratch `refs.json` mention or commit it. **[done: `tests/test_cheats.py`]** |
| M | ✔ Machine-specific disc default and duplicated entry PC | `tools/dvdbg.py:41,44` | Check the disc exists before `Popen` and say to pass `--disc`/`DVDBG_DISC`; read `entry_pc` from `game.toml`. **[done: disc check, entry PC from game.toml]** |
| L | ✔ Tracebacks on user mistakes | `tools/dvdbg.py` (`press strat`, `raw` with no command, flags without values, zero-padded ints) | A `flag_value` helper, a clear "unknown button" exit, a "…truncated" marker on `raw` output. **[done]** |
| L | ✔ Cheats plugin tidy | `mods/plugins/dragonvalor_cheats.c:60-70` | Cache `amount` in an activation callback instead of parsing the option every VBlank; report a failed registration. Not a bug. Optionally declare `save_compatibility = "shared"` in the manifest for parity with builtins. **[done: activation-time option read, registration check, `save_compatibility`]** |
| L | ✔ `codegen_setup.c` comment and marker duplication | `codegen_setup.c:1-2,16` | Compiled whenever recomp-ui is on, not only with the wizard; `.gen_marker_relpath` duplicates `GEN_MARKER`. **[done: comment]** |
| L | ✔ CMake scaffold residue | `CMakeLists.txt:14-25` | Dangling sentence and twice-commented netplay/wizard blocks. **[done]** |
| L | ✔ `dvdbg.py` log handle never closed; Windows-only while the docstring says any shell | `tools/dvdbg.py:96,114` | `with open(...)` around `Popen`, drop the no-op flush, say "Windows". **[done]** |
| L | ✔ `*.pst` has no gitattributes rule | `.gitattributes` | Add `*.pst binary`. The state is incompressible (1.7 MB per re-take); consider LFS before the second re-take. **[done (LFS not adopted)]** |
| L | ✔ `symbols.toml` is still the scaffold stub | `symbols.toml` | Docs name ~10 identified functions (HP writer, Val, stage loader…); add `[[func]]` rows with `emit = false`, `status = "confirmed"` (SYMBOLS.md: no raw hex in host hooks). **[done: 8 functions catalogued, header regenerated]** |

## D. Docs and repo hygiene

| P | Item | Where | Why / how |
|---|---|---|---|
| H | ✔ README says the PAL project lives in `../DragonValorRecomp` | `README.md` | Deleted on 2026-10-14; point at `docs/HISTORY.md`. **[done]** |
| H | ✔ `docs/AOT.md` names the PAL base `0x800BC268` for the USA check words | `docs/AOT.md:43-44` | The profile pins `0x800BBEF0`. **[done]** |
| M | ✔ Two divergent build recipes | `README.md` "Quick start (dev)", `docs/DEVELOPMENT.md` "Build" | README's raw cmake recipe lacks the toolchain exports and fails on a fresh machine; keep one recipe in DEVELOPMENT and a pointer in README. **[done]** |
| M | ✔ Disc location explained three ways | `README.md`, `docs/DEVELOPMENT.md:31`, `tools/dvdbg.py:44` | State one convention (sibling folders with Redump names; override with `DVDBG_DISC`/`--disc`). **[done]** |
| M | ✔ STATUS "Open items" carries done and duplicated entries | `docs/STATUS.md` items 2, 5; line 88 | Move the release-zip fact to "What works", keep the freeze-dump note in one place, update the HD dump count (563 now). **[done]** |
| M | ✔ HISTORY duplicates live pitfalls | `docs/HISTORY.md` "Framework quirks met" | Narrate only what the PAL bring-up established; link DEVELOPMENT/AOT for current pitfalls. **[done]** |
| M | ✔ Shard/line/dispatch counts duplicated and not reproducible | `docs/STATUS.md:16`, `docs/DEVELOPMENT.md:58-59` | Keep "35 shards" or say how the numbers were measured. **[done]** |
| M | ✔ "Project layout (what is ours)" is incomplete | `docs/DEVELOPMENT.md` | Missing DISC.md, AOT.md, HISTORY.md, aot/overlays.json, tests/states, scripts/, the .bat files, session_monitor.py, codegen_setup.*, catalog/probe JSON, VERSION, framework_pins.txt. **[done]** |
| M | ✔ Portuguese echo strings in an English repo | `Play.bat`, `PlayDebug.bat` | Translate. **[done]** |
| L | ✔ `mods/preloaded/README.md` points players at a doc that does not ship | `mods/preloaded/README.md` | It is staged as `<exe>/mods/README.md`; use a GitHub URL. **[done]** |
| L | ✔ Dissolve pointer, Start wording, Pillow prerequisite, unreferenced images, framework paths without `psxrecomp/` prefix | `docs/DEVELOPMENT.md:366,11`, `README.md:39`, `docs/HISTORY.md:41`, `game.toml:1,46`, `CMakeLists.txt:28` | Point at HISTORY for the dissolve; "press Start during the intro movie"; Pillow is optional; embed or drop `docs/assets/*.png`; prefix the paths. **[done (images embedded in HISTORY)]** |
| L | ✔ `mods/preloaded/packages/.gitkeep` | repo | Remove, the directory has content now. **[done]** |

## F. Added since 2026-10-14

| P | Item | Where | Why / how |
|---|---|---|---|
| M | Propose `patches/recomp-ui/0001` and `0002` upstream | RetroPortingToolKit/recomp-ui, `runtime_ui_imgui.cpp` | Scroll-to-selection, the selected-row frame and wrapped row descriptions; drop the local patches once the pin includes them. Needs the user's GitHub account for the PR. |
| M | Title flow after an early movie skip | game / framework | The title animation sometimes runs > 2 min before the menu, sometimes jumps to the story demo; a Start on the still title can restart the opening. Seen with and without the skip mod. Watch in the human session; if it reproduces by hand, bisect against the pre-bump pin. |
| L | ✔ Live scroll-drop test | `tools/ramscan/drop_test.py`, `tests/test_scroll_cheat.py` | Done by the player on the pad, 2026-10-15: two magic kills stepped `0x800A2DB4` to 2, the second left a scroll, the pickup took the counter back to 0 (traces in `DEVELOPMENT.md`, *How drops are decided*). **[closed the same day: the player's slot 11 (town stage, Fire, enemy in front) became test state 10; `tests/test_scroll_cheat.py on|off` casts Fire, kills the enemy and checks the scroll drop, 5/5 both ways. Slot 0 never cast because its hero knows no spell and sits in the tutorial.]** |
| L | ✔ Earrings underflow | `docs/DEVELOPMENT.md` *Pickup ids and what they do* | **[confirmed 2026-10-15 on slot 10, cheats off: Fire costs 4; Earrings 4 → free, 5 → +1 MP, 50 → +46 MP per cast, MP 83/37 then 129/37 with no clamp. Documented; left as the game's behaviour.]** |
| L | ✔ Drop cheat option | `dragonvalor.cheats` 1.3.0, `docs/DEVELOPMENT.md` *How drops are decided* | `gem_every` / `scroll_every` (0 = game rule, N = the next drop after N kills of that kind): the plugin keeps the kill counter at threshold − N each frame; caps and the one-on-the-floor rule stay. Menu rows + `tests/test_drop_cheat.py`. **[done 2026-10-15]** |
| L | Pickup ids 9 and 10; the second effect routine | `func_80028940`, jump table `0x8001131C` | Id 9 starts a 1200-frame timer at `0x80086332`, id 10 queues 5 HP of damage and gives 2 MP; `func_80028940` applies effects per id without the kill-counter bookkeeping and its trigger is unknown (not the floor pickup). |
| M | ✔ Widescreen: widen the game's 4:3 reject (done, then withdrawn the same evening: it bought nothing visible, see `DEVELOPMENT.md` *Culling*) | `game.toml [widescreen.cull] auto_screen_x` + the scan's immediates, `psxrecomp_cli.py generate` | **[done 2026-10-15: regen diff = 3 EXE compares (shards 05/17) + 2 in the static overlay shard, all `x < 385` → `psx_ws_cull_slti(u)`; both builds rebuilt, widescreen/menu tests re-run, static shard still serves every overlay. Pop-in at the far edges was never caught on camera before or after (`ws_probe3.py` series), so the change is justified by the scan, not by a visible fix.]** |
| L | Widescreen: HUD at the window edges | `nw_hud_corners` (framework thirds shift) or HUD packet tagging as R4 does (`psx_mod_tag_hud_primitive` from entry hooks on the HUD producers) | The bars sit 64 columns in at 16:9. The thirds shift tore composite boxes elsewhere and this game's tutorial boxes span the full width, so tagging per producer is the safer route; needs the HUD draw functions identified first. |
| L | Widescreen: later chapters, field module, software renderer, Fit resize | play session with the package on | Only chapter 1 (forest, town, castle corridor) was checked. Watch the 2D field scenes (`MAP.BIN`): with `gte_game_mode` they should pillarbox; if they project a few vertices they would widen a flat image. |
| M | Upstream: persist the host volume in `settings.toml` | psxrecomp `recompiler/src/config_loader.cpp` (`UserSettings`, reader, writer), `runtime/src/main.cpp` (seed / save of `ls.volume`) | The launcher stepper and the in-game Volume row both end in `host_volume_set`, which nothing persists, so every start is 100. Worked around in `mods/plugins/dragonvalor_menu.cpp` with `dragonvalor_settings.toml` (2026-10-15); drop the workaround when the pin has the key. |
| L | Upstream: `psx_window_icon.cpp` frees `SDL_GetBasePath()` under SDL3 | `psxrecomp/runtime/src/psx_window_icon.cpp` (non-Windows branch) | SDL3 owns the returned string; `SDL_free` on it corrupts the heap (seen in our plugin before the fix). Windows never reaches that branch (`GetModuleFileNameW` wins). |
| M | Controller shortcuts for more actions | launcher Controls page, "Controller shortcuts" (recomp-ui `assist_pad_bind`: Rewind, Save-state menu, Fast forward, Fast-forward toggle; Select + button, or a two-button chord); keyboard hotkeys `[KeyMap]` (fullscreen, pause, turbo, volume, perf, reset …) have no pad capture | Player's request 2026-10-15: bind pad chords to the keyboard hotkeys as well. Upstream work in recomp-ui's hotkey catalog, or a title plugin that watches pad chords and calls the host (fullscreen / volume are reachable through `PsxMenuHost.video_apply` and `host_volume_set`; the in-game menu already opens on Select+Start). |
| L | ✔ Items page edited the wrong slots | `mods/plugins/dragonvalor_menu.cpp`, `DEVELOPMENT.md` RAM table | **[fixed 2026-10-15: the inventory starts at `0x800A2DD4` and skips `0x800A2DE0`‥`EE` (spell-book counters); confirmed on the Status screen, which also supplied the row descriptions.]** |
| L | Keys from the menu: confirm the door opens | player's castle save (menu slots 1‥3, area 1 / stage 13), `mods/plugins/dragonvalor_menu.cpp` `take_stage_keys` | The real pickup sets save flag 1 and retires the key object; the menu now does the same when a key count rises. Not yet seen: the door actually opening (the scripted walks never found the door's trigger). The player can confirm; then automate from a state in front of the door. |
| M | Widescreen: the hero's parked part at the right edge | `func_800213C8` (hero model renderer, single caller `0x80021094`), parts table `0x8006DE70`, selection block `0x8009E840`; texture page (640, 0) / CLUT (640, 245) | A 54 × 22 noise block at screen X 381‥462 in stages where that texture page is not loaded (castle corridor, area 1 / stage 19; not the forest or the town). Hide it by moving the parked part further out from an entry hook once the model layout is decoded, or ask upstream for a rule that skips textured primitives outside the canonical frame whose texture page was never uploaded in the stage. Player report 2026-10-15 with screenshot; reproduced from their slot 2. |

## G. Review round of 2026-10-15 (evening)

Three read-only reviewers (code, docs/config, release readiness) over the
day's 16 commits. Applied the same evening: F7 is the save-state slot picker
(not a direct save) in README / CONTROLS / PLAYTESTING; the zip now ships
CONTROLS.md, THIRD_PARTY_NOTICES.md, RELEASE_NOTES.md, LICENSE and
BOXART_SOURCE.txt; player docs say "run the executable" (the zip has no
Play.bat); `.recomp.json` project/game fields; release name token,
`body_path: RELEASE_NOTES.md` and a manifest-test step in the workflow;
DISC.md Redump digests; the widescreen texts name the real blemish; STATUS /
BACKLOG / DEVELOPMENT stale lines. Still open:

| P | Item | Where | Why / how |
|---|---|---|---|
| H | Dry-run the release workflow before the first release | `.github/workflows/release.yml` (`workflow_dispatch`, `publish=false`) | Never run: 0 workflow runs, 0 tags. It builds four platforms, configures `-DPSX_SETUP_WIZARD=ON` (local builds keep it OFF, so the shipped first-run wizard was never exercised), applies `patches/recomp-ui/*` with `git apply` inside MSYS2 and runs the new manifest step. Dispatch once with `publish=false`, unpack the Windows zip in a clean folder and walk the wizard; then align the CMakeLists comment and DISC.md with what the wizard shows. |
| H | First version number | `VERSION` = 0.1.0, no tags | A dispatch with an empty `version` auto-bumps to v0.1.1. Pass `version=0.1.0` or push the tag `v0.1.0` first. Do not hand-upload `dist/dvus-0.1.0-windows-x64.zip`: it is a stale 2026-10-14 stage (old game.toml, no dragonvalor.* packages). |
| M | Repository is private | GitHub settings | README badges, the PLAYTESTING / issue-template links and the Retro-Catalog submission need a public repository; a private one also bills the four-OS matrix minutes. Make it public before the first dispatch, or accept both until then. |
| L | `.recomp.json` multi-serial form | `.recomp.json` (`original.serial` = SLUS-01092 only) | Check the recomp.fyi schema for listing disc 2 (SLUS-01164). |
| L | CONTROLS / README hotkeys vs `config.ini` | `psxrecomp/runtime/src/host_keymap.c` | The defaults documented now (F6 scanlines, F7 picker, F8 rewind, F9 turbo toggle, Numpad +/- volume, Ctrl+F fullscreen) come from the code; a player's `config.ini [KeyMap]` can change them, which the docs say. Re-check after a framework bump. |

## E. Ideas from other psxrecomp titles

Survey of 2026-10-14 over ~60 repositories (GitHub, retroportingtoolkit.com,
recomp.fyi). Nothing adopted yet.

**Where the framework is.** The canonical repo is now
<https://github.com/RetroPortingToolKit/psxrecomp> (our `.gitmodules` URL
`mstan/psxrecomp` still redirects). Our pin `54abcb70` (2026-10-13) is 53
commits behind master `d3db61b1` (2026-10-14); landed since: the native
in-game overlay menu series (#598‥#603, host pause, live mod options), cdrom
SetLoc/XA fixes (#657/#658), debug-server accept timeout (#659), rollback
fixes. Open PRs that matter to us: **#646** freeze-heartbeat false positives on
statically compiled code (our STATUS open item 4: reference it, do not file a
duplicate), #652 in-game disc menu + game-driven disc switching, #645 AOT
prepared images / disc sets, #591 graphics presets with hardware detection,
#590 idle_skip, #593 Steam Deck Game Mode. Open issues: #371 wizard mojibake on
non-UTF-8 code pages (pt-BR Windows is cp1252: test accented paths), #443
licensing provenance of code ported from Beetle/DuckStation, #368/#571
widescreen gaps. Community: Discord <https://discord.gg/Ad9BwSzctP>, docs
<https://retroportingtoolkit.com/docs> (guides: port, mod, translate,
widescreen, release), contact hello@retroportingtoolkit.com. Dragon Valor is
in no catalog (Retro-Catalog, recomp.board) and has no other recomp.

| P | Idea | Source | Notes |
|---|---|---|---|
| H | ✔ Install the framework CI template and ship checksums | `psxrecomp/tools/generate_ci.ps1` → `.github/workflows/release.yml`; MMX4/R4/Hercules `SHA256SUMS.txt`; Tomba per-asset `.sha256` | Four platform zips, auto-bump, zip verification. Check whether the template emits checksums (uncertain). **[done: workflow installed (checksums: the template's own business)]** |
| H | Get listed once a release exists | Retro-Catalog submission form <https://retroportingtoolkit.github.io/Retro-Catalog/submit/>, retroportingtoolkit.com "Submit a recomp", recomp.board | Our `catalog_identity.json` exists but is stale (section A). Retro Launcher then installs/updates the title for players, so no custom auto-updater. |
| H | ✔ Bump the framework pin and re-validate | upstream master; Tomba `docs/framework_pin_history.md` | Gains the in-game menu and cdrom fixes; keep a pin-history table (date, SHAs, why, what was verified) and warn that save states may not survive a bump. **[done 2026-10-14: `f9c287f5`, `docs/framework_pin_history.md`; the state survived. The in-game menu provider was then written on that contract (`mods/plugins/dragonvalor_menu.cpp`, 2026-10-14)]** |
| M | ✔ Cheats from the DuckStation cheat DB as declarative patches | Hellnight `hellnight.cheats` manifest, sfex2p-es | **[done 2026-10-15: no entry for either disc in `duckstation/chtdb`; the public GameShark/CodeBreaker lists are per-frame RAM writes (not expressible as `[[patch]]`) and every one is already an in-game menu row, except "boss in one hit" (= the attack ×N idea). Addresses cross-checked in `DEVELOPMENT.md`, *Public cheat codes*.]** |
| M | ✔ Default-off "Skip intro" package | Tomba `tomba.enhancement.skip-fmv` (option to keep title/ending FMVs), Hercules `hercules.skip-intro`, Simpsons entry hooks | Namco logo + intro FMV; via `mod_function_entry_funcs` hooks. **[done 2026-10-14: `dragonvalor.skip-intro` skips the movie through a hook on the pad copy; the logo cannot be shortened (title loads behind it)]** |
| M | ✔ Widescreen first step | Valkyrie `game.toml [widescreen]`; R4/Tomba `*.enhancement.widescreen` packages | **[done 2026-10-15: `dragonvalor.enhancement.widescreen` 1.0.0 (native-wide, off by default, experimental; 16:9 / 21:9 / 32:9 / Fit), plugin predicate keeps the pause wide, `tests/test_widescreen.py`. Follow-ups in section F: cull regen, HUD edges, later chapters.]** |
| M | ✔ Cheap tests in ctest | R4 `tests/test_r4_defaults.py` (manifest defaults), Ape `tests/test_preloaded_mods.cpp` + `test_release_overlay_staging.py`, MMX4 `tests/test_release_config.py`, Tomba `tools/boot_smoke.py` | Pure-Python checks of every preloaded manifest plus a boot smoke test beside our save-state harness. **[done: `tests/test_manifests.py` (standalone, not wired into ctest)]** |
| M | ✔ Playtest support | MMX4 `docs/PLAYTESTING.md`; Disruptor/Hellnight `.github/ISSUE_TEMPLATE/bug_report.yml` | **[done 2026-10-15: `docs/PLAYTESTING.md` (setup, what to test, which files to send) and `.github/ISSUE_TEMPLATE/bug_report.yml` + `config.yml`. Not done: a "Playtest Diagnostics" mod; the exit report and `tools/session_monitor.py` cover it.]** |
| M | Doc set other titles carry | Tomba/Ape README shape, `ISSUES.md`, `CONTROLS.md` (Hercules), `DISC.md` with TOC fingerprint + EXE sha (R4), `THIRD_PARTY_NOTICES.md` (MMX4), `NOTICE.md` with takedown contact, `RELEASE_NOTES.md` | **[partly done 2026-10-15: `CONTROLS.md`, `THIRD_PARTY_NOTICES.md`, `docs/PLAYTESTING.md`.]** Still open: `ISSUES.md` (STATUS open items serve for now), `NOTICE.md` with a takedown contact (needs the owner's address), `RELEASE_NOTES.md` with the first release, the EXE sha in `DISC.md`. |
| M | ✔ Multi-disc UX | upstream PR #652 (disc menu + trusted plugin reporting the requested disc); Valkyrie confirms mid-session swap is otherwise unimplemented | Needs the RAM flag of the "insert disc 2" prompt; our RAM map is the start. **[partly done 2026-10-14: the in-game menu's Game page has Insert disc 1 / 2 through the runtime's tray policy; automatic switching on the game's prompt still open]** |
| L | Linux AppImage + Steam Deck page | `psxrecomp/tools/package_appimage.sh`; R4 `docs/STEAM_DECK.md` | Tomba/Ape/MMX4 ship one. |
| L | pt-BR localization path | ddr-1st-recomp (redrew VRAM text artwork, shipped EN + pt-BR); Tomba HD pack example as a release asset; Tomba2 `mods/preloaded_ita/` | Our text is pre-rendered artwork, so string patches do not apply: an HD texture pack replacing the text textures is the route. |
| L | PGO profile committed | Simpsons `pgo/windows.profdata` + `tools/pgo_train.py` | Framework: never set `PSX_PGO` in CI. |
| L | Release-note conventions | Tomba v0.19, Einhander v0.1.3, retroportingtoolkit.com "release a port" | Platforms table, disc serials, "no BIOS needed", what was verified, save-state warnings, "A/B against the previous version" request, test from a clean directory, verify a wrong disc is rejected. |
| L | Damage follow-ups | `docs/DEVELOPMENT.md`, *How damage is computed* | Which move uses attack entries 1‥4 (`0x8008632E` stays 0 on single swings; a chained combo is the guess); a per-frame "attack ×N" or one-hit-kill option in `dragonvalor.cheats` if wanted (the per-hit power is `attack[index]`, min 1 after the target's resistance shift). |
| L | Field-tested pitfalls to keep in mind | alexbeavs (paths with spaces break MinGW; exe-name drift), Hellnight (developing on a non-Redump dump rejected real Redump discs), Tomba (states refused after bumps) | Our `known_md5` lists one digest per disc; check #474 (TinyCC licence notice in bundled zips) against our zip's `licenses/`. |

Not found: another Dragon Valor recomp; a reusable standalone in-game menu
(YuGiOh's lived in a fork and that project moved to a decomp; upstream now has
its own); an upstream CHANGELOG or forum; the origin of the `medievil.fr`
counter prefix.

## Verified correct (no action)

- Cheats manifest matches MOD_PACKAGES.md; `[[target]] game_id` is matched against `[game] id`, so the package also applies on disc 2.
- Multi-disc roster (`discs` / `disc_serials` / `required_disc_fps`, `require_cue`, `required_tracks = 2`) and the `[netplay]` block are as the schema asks; `overlay_cache = true`, `openbios`/`bios_hle` defaults, no deprecated turbo keys, `MAX_PLAYERS 1`.
- `hide_hidden_mod_features` not needed (8 MB RAM is hidden and irrelevant); rewind is a player setting; Vulkan stays unoffered until validated; widescreen is per-title GTE work, not a flag.
- README keyboard table matches the runtime key maps; DISC.md sizes and disc-1 SHA-256 match the dumps, `aot/overlays.json` and the AOT audit; seeds count, submodule pin and all markdown anchors check out; `.gitignore` covers disc/, saves/, dist/, build-*/; no tracked file over 6 MB.
- Freeze dumps have no config switch (heartbeat is unconditional); an upstream issue, as STATUS says.
