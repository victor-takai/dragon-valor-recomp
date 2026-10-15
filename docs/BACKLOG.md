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
| M | ✔ Audio latency left at the 180 ms default | `game.toml [audio] buffer_ms` | Action title; measure 60‥90 ms for crackle and set it (a developer choice, not a player setting). **[done: 90 ms, 0 underruns muted]** |
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

## F. Added 2026-10-14

| P | Item | Where | Why / how |
|---|---|---|---|
| M | Propose `patches/recomp-ui/0001` upstream | RetroPortingToolKit/recomp-ui, `runtime_ui_imgui.cpp` | Scroll-to-selection and the selected-row frame; drop the local patch once the pin includes it. Needs the user's GitHub account for the PR. |
| M | Title flow after an early movie skip | game / framework | The title animation sometimes runs > 2 min before the menu, sometimes jumps to the story demo; a Start on the still title can restart the opening. Seen with and without the skip mod. Watch in the human session; if it reproduces by hand, bisect against the pre-bump pin. |
| L | Live scroll-drop test | `tools/ramscan/drop_test.py` | The scroll path (`0x800A2DB4` ≥ 2 / 4 / 7 / 12 / 20 with credit 1) is read from the code only: no pad button cast a spell from slot 0 even with every magic level written to 1. Find how a spell is selected and cast (pause-menu Magic page?), then prime the counter and kill with it. |
| L | Drop cheat option | `dragonvalor.cheats`, `docs/DEVELOPMENT.md` *How drops are decided* | "Gem / scroll every kill": hold `0x800A2DB0` / `0x800A2DB4` at the current threshold each frame (the 8 / 5 / 10 caps and the one-on-the-floor rule still apply). Only if the player wants it. |
| L | Pickup ids 9 and 10; the second effect routine | `func_80028940`, jump table `0x8001131C` | Id 9 starts a 1200-frame timer at `0x80086332`, id 10 queues 5 HP of damage and gives 2 MP; `func_80028940` applies effects per id without the kill-counter bookkeeping and its trigger is unknown (not the floor pickup). |

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
statically compiled code (our STATUS open item 5: reference it, do not file a
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
| M | Cheats from the DuckStation cheat DB as declarative patches | Hellnight `hellnight.cheats` manifest, sfex2p-es | One-shot instruction patches need no plugin; record provenance in `author`/`description`. Complements our VBlank cheats. |
| M | ✔ Default-off "Skip intro" package | Tomba `tomba.enhancement.skip-fmv` (option to keep title/ending FMVs), Hercules `hercules.skip-intro`, Simpsons entry hooks | Namco logo + intro FMV; via `mod_function_entry_funcs` hooks. **[done 2026-10-14: `dragonvalor.skip-intro` skips the movie through a hook on the pad copy; the logo cannot be shortened (title loads behind it)]** |
| M | Widescreen first step | Valkyrie `game.toml [widescreen] offer=true full_2d=true` + `[widescreen.cull] auto_backdrop/auto_screen_x`; R4/SFA3 `analysis/widescreen_sites.toml` | Cheap opt-in 16:9 for a GTE title; upstream #368/#571 are the known gaps. |
| M | ✔ Cheap tests in ctest | R4 `tests/test_r4_defaults.py` (manifest defaults), Ape `tests/test_preloaded_mods.cpp` + `test_release_overlay_staging.py`, MMX4 `tests/test_release_config.py`, Tomba `tools/boot_smoke.py` | Pure-Python checks of every preloaded manifest plus a boot smoke test beside our save-state harness. **[done: `tests/test_manifests.py` (standalone, not wired into ctest)]** |
| M | Playtest support | MMX4 "Playtest Diagnostics" mod + `docs/PLAYTESTING.md`; Disruptor/Hellnight `.github/ISSUE_TEMPLATE/bug_report.yml` asking for `psx_last_run_report.json` | Directly useful for the human play session. |
| M | Doc set other titles carry | Tomba/Ape README shape (What this is / Status / Features / Setup / Legal), `ISSUES.md`, `CONTROLS.md` (Hercules), `DISC.md` with TOC fingerprint + EXE sha (R4), `THIRD_PARTY_NOTICES.md` (MMX4), `NOTICE.md` with takedown contact (jajdp), "own code MIT, framework PolyForm-NC" line, an AI-usage notice (Valkyrie) | Pairs with the LICENSE item in section A. |
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
