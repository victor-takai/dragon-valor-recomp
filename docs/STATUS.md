# Dragon Valor (USA) Recompiled — status

Last updated: 2026-11-24. Framework: psxrecomp @ f9c287f5 (submodule, bumped from 54abcb70 the same day; see [`framework_pin_history.md`](framework_pin_history.md)), recomp-ui @ 41c88ff, OpenBIOS only.
The project started on the PAL release (SCES-01705, 50 Hz) and moved to this
USA version on 2026-10-14; the PAL tree was deleted. What that bring-up
established (controller, dissolve rendering, first play session, framework
quirks) is in [`HISTORY.md`](HISTORY.md).

## Identity

| | |
|---|---|
| Discs | SLUS-01092 (disc 1), SLUS-01164 (disc 2) — USA, NTSC, 2 tracks each |
| Boot EXE | `SLUS_010.92` / `SLUS_011.64`, identical bytes (sha256 `ab40d44b…`) |
| Entry / load / text | `0x80012EA0` / `0x80010000` / `0x67800` |
| Generated C | 35 shards under `generated/`, committed, byte-identical across runs |
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
- **Menu flow (USA)**: the Namco logo ignores Start; Start during the intro
  FMV (from ~f1000) skips to the title, and the title screen *is* the main
  menu (New Game / Load Game / Configuration, New Game highlighted — Start
  there starts a New Game: prologue text → opening cutscene). Left idle, the
  title cycles title → gameplay demo → story movie.
- **Configuration** (reached by injected input, 2026-10-14): *Controller
  Setup* — in-game remap of L1/L2 Dash, R1/R2 Duck, △ Status, ○ Magic, × Jump,
  □ Attack, D-pad Move, Start Pause, Select = default, and **Vibration Setup
  ON/OFF (ON by default)**, so the game drives DualShock rumble; *Adjust
  Screen* — move the picture with the D-pad, Start confirms, △ default;
  *Audio Setup* (not photographed yet).
- Release build: window LIVE, title bar `[Game 60 fps 1.00x]`, NTSC-U detected
  (59.94 Hz pacing).
- **Disc 2** (`--disc` override, SLUS-01164): boots, 59.9 fps, 0 unknown
  dispatches at 80 s; its five `BIN/*.BIN` are byte-identical to disc 1's, so
  the static shard serves both discs (only `SYSTEM.CNF` and the EXE name differ).
- **Release zip**: `scripts/package_release.sh build-release windows-x64`
  produces `dist/DragonValorRecompiled-0.1.0-windows-x64.zip` (26 MB,
  unsigned; `dvus-…` before the rename). The local zip of 2026-10-16 also
  carried the 3227 textures dumped from `build-release` play sessions
  (`mods/texture-packs/`, 42 MB of game imagery): the framework stages the
  build's `mods/` whole. Since 2026-10-17 the wrapper stages, drops that
  folder and zips itself (409 files; CI zips come from a clean build and
  never had it; BACKLOG, upstream row). Unpacked
  elsewhere it boots at 60 fps with the bundled OpenBIOS and the tcc-based
  overlay autocompile (no compiler on the player's machine). `game.toml` names
  the discs by Redump file name only and `DISC.md` ships at the zip root.
  CI: the release workflow's rehearsal (`publish=false`, 2026-10-17, the
  third dispatch after two Windows fixes) builds all four platforms. The
  zips are 31 MB (Windows), 52 MB (Linux) and 28 MB for each macOS build.
  The player played the CI Windows build from a clean folder, first-run
  screen included. **v0.1.0 was published on 2026-10-21** as a GitHub
  pre-release with the four zips (tag `v0.1.0` at `362efef`). The
  repository is private, so only its owner sees it. Before it goes public
  the history was rewritten (test states out, noreply email) and the
  repository re-created with the same release and zips (DEVELOPMENT).
  Public since 2026-10-21, and submitted to the Retro Launcher catalog the
  same day (Retro-Catalog issue #54, awaiting approval; BACKLOG).
  **v0.2.0 was published on 2026-11-19** (pre-release, four zips: Windows
  31 MB, Linux 54 MB, macOS 29 MB each), from the merge of PR #1
  (`hero-swap`: Play as, the new menu, widescreen HUD and transitions,
  render thread). The re-created repository had never registered its
  workflow (`gh workflow run` answered 404, the API listed 0 workflows),
  so the release was started by pushing the tag `v0.2.0` (the `push: tags`
  trigger reads the workflow from the tagged commit); the release text
  was then replaced with the short `RELEASE_NOTES.md` of `89bf7a9`
  (`gh release edit --notes-file`), the zips keep the tag's longer notes.
  **v0.3.0 was published on 2026-11-23** (pre-release, four zips of the
  same sizes as 0.2.0), with PR #2 (Warp > Cut prologue scene), PR #3
  (Cheats > Unlock gallery) and PR #4 (HD texture packs: portraits, and
  the HUD and font after a state load); PR #5 wrote its notes and VERSION.
  Started the same way, by pushing the tag `v0.3.0` at `c010ba4` (the run
  took 12 min). The workflow's `generate_release_notes` appends a list of
  the merged PRs to the release text; it was replaced with
  `RELEASE_NOTES.md` alone, as for 0.2.0. The Windows zip was checked:
  `psx_game_version.txt` 0.3.0, cheats package 1.4.0, the new plugin code
  in the exe, no `texture-packs/` folder.
  **v0.4.0 was published on 2026-11-23** (pre-release, four zips about
  5 KB larger than 0.3.0's), with PR #8 (the wide event blur, the 4:3
  chapter title cards, `tools/make_bezel.py`); PR #10 wrote its notes and
  VERSION. Tag `v0.4.0` at `fcb6ad6`, the run took 14 min; the release
  text was replaced with `RELEASE_NOTES.md`, as before. The Windows zip
  was checked: `psx_game_version.txt` 0.4.0, `make_bezel.py` at its root
  (the same bytes as `tools/`), the event blur's counter in the exe, no
  `texture-packs/` folder.
  **v0.5.0 was published on 2026-11-24** (pre-release, four zips about
  25 KB larger than 0.4.0's), with PR #12 (nine story characters to play
  as, with their trails, voices and moves; three weapons; Skip Intro
  straight to the menu with the fast boot of `patches/psxrecomp/0005`);
  PR #14 wrote its notes and VERSION. Before the tag, a
  `workflow_dispatch` run with `publish=false` on master `9e65378` built
  the four platforms (12 min), the first build of that code on Linux and
  macOS. Tag `v0.5.0` at `d148273`, the run took 8 min; the release text
  was replaced with `RELEASE_NOTES.md`, as before. The Windows zip was
  checked: `psx_game_version.txt` 0.5.0, the heroswap 2.0.0, skip-intro
  1.0.0 and cheats 1.4.0 manifests the same bytes as the repo's, the new
  plugin code in the exe (the Moves label "Clovis (Young)", Fast boot's
  `psx_mod_hold_acceleration`), no `texture-packs/` folder.
- **Widescreen** (2026-10-15): `dragonvalor.enhancement.widescreen`, off by
  default, experimental. Native-wide: the 384-pixel stage gains 128 real
  columns at 16:9 (288 at 21:9), the pause screen stays wide (mode-byte
  predicate), title / menus / movies stay 4:3, the HUD sits at the window
  edges (2026-11-18: HP / MP, spell, gauge and keys left, counter and the
  pick-up banner right, also on the black screen of a stage reload); the
  game's own screen-X rejects are left stock (widening
  them bought nothing visible). The block of texture noise at the right
  edge of the castle corridor (1/19) is fixed (2026-11-16): off-frame lens
  sprites of prim record 16, collapsed by the plugin (`BACKLOG.md`,
  section F). After the player's play test (2026-11-17): the magic / item
  menu and the screen transitions present at 4:3 with side bars (they only
  cover the 4:3 frame), and so did the event motion blur until 2026-11-23
  (in play, where the hero's specials switch it on, the frame stays wide
  and the blur is left out), and the dialogue
  window's top border runs to both edges; the streaks in the left margin
  (town of 1/7) were the left-side twin of the noise block (off-frame
  lens sprites drawn inverted) and are collapsed by the same hook. Black
  wedges where a room's mesh ends stay (no geometry there; a sliver of
  them shows at the 4:3 edge on a console). Since 2026-11-19 the scene
  changes (effects 0, 1, 2 and 7) run at full width, HUD at the edges: the
  plugin tags their frame-buffer copies and the renderer continues them
  into the margins (psxrecomp PR #692 + a fix of ours, local patches
  `psxrecomp/0002`, `0003`; 0003 also makes the GL 127/128 fades reach
  black). The melt (soft reset, attract demo) and the ending title's
  shatter stay 4:3. The fade between two rooms of a stage keeps the HUD at
  the edges too (its tags are renewed every VBlank: they had gone stale
  while the next room loaded). Since 2026-11-23 the event scenes' blur (the
  prologue's red flashback, the dragon fight's trails; the player saw black
  bars there) stays wide too: its two sprites draw the previous frame, and
  the plugin tags them as frame-buffer copies like the transitions'. The
  chapter title cards (stage 0 of each area) present at 4:3 instead of
  wide with black margins, so that side-bar art frames them (below).
  `tests/test_widescreen.py on|off` 11/11 and 4/4 (test
  state 10);
  chapter 2's first stage (the player's Kodel save, 2026-10-16) is wide and
  clean too; see
  `DEVELOPMENT.md`, *This project's package:
  dragonvalor.enhancement.widescreen*.
- **Side-bar art** (2026-11-23, the player's request): with widescreen on,
  the screens made for 4:3 (title cards, menus, world map, movies) keep
  side bars, which the framework's Bezel Artwork (launcher, Mods) fills
  with a picture. `tools/make_bezel.py` (standard-library Python, shipped
  at the zip root) cuts two 1920×1080 pictures from the paintings of the
  illustration gallery (`ILLUST/ILLUST_U.CDR`, disc 1), the heroes or the
  dragons, read from the player's disc, and `--install` turns the bezel on
  with one of them. The same disc gives the same bytes on every run. The
  pictures are the game's art and stay on the player's machine (`/bezel/`
  is gitignored); README, *Art in the side bars*.
- **Bilinear seam fixed** (2026-11-17): with bilinear texture filtering the
  OpenGL renderer drew a line at x 256, two thirds across the screen (a
  coloured stripe in the transitions and the blur, a dark line on the title
  screen): its bilinear neighbours wrapped to the far side of the texture
  page before the prim clamp. A local psxrecomp patch
  (`patches/psxrecomp/0001-…`, the same change as the open upstream PR #896
  for issue #719) clamps first; `tests/test_bilinear_seam.py` 3/3. The
  game-side workaround of the same day (`dragonvalor.fixes`) is gone. The
  white-out of effect 0 on GL is a separate, older renderer issue.
- **Chapter 2 reached** (2026-10-16): the player finished chapter 1 (Clovis)
  on this build and plays chapter 2 as Kodel at 60 fps; their save state at
  its first stage (area 3 / stage 1) is the third test state
  (`tests/states/`, menu slot 10) and gave the hero finding in
  `DEVELOPMENT.md`, *Character and weapon*: the hero follows the area, so
  the Cheats page gained an **Area (next Continue)** row that reloads
  another area's stage (Kodel's save + area 0 = Clovis in the prologue).
- **Launcher Mods page cleaned up** (2026-11-18, branch `hero-swap`): two
  groups (Game, Graphics), names a player understands (Faster loading,
  Steady 3D (PGXP), ...), a one-line summary per row instead of the package
  name and version (`patches/recomp-ui/0005-...`), current descriptions;
  title overrides of the framework's cd-speed, hd-textures and bezel
  packages for the texts.
- **Unlock gallery** (2026-11-21, the player's request): a Cheats option
  (`dragonvalor.cheats` 1.4.0, launcher and in-game menu) that shows the
  title menu's fourth item, the illustration gallery the game only offers
  after an ending, with all 95 pictures; the save's ending flags are not
  touched. `tests/test_gallery.py` 5/5, `--off` 2/2.
- **HD texture packs keep the HUD after a save state** (2026-11-22): the
  HUD's art is uploaded once, by the title, and a loaded state restores VRAM
  without its uploads, so the HD Texture Packs mod drew the original HUD
  until the title came back. The cheats plugin now sends the title's three
  HUD images again at the first action frame after a state load (the same
  words, nothing changes on screen without a pack), and the title's 12×12
  font sheet with them (the menus' text, the hero's name, the dialogue).
  The third image's CLUT block (the floor items' palettes) lies inside the
  first image and is sent again last: without it the coins and the chalice
  lost their icons after a state load (player report). `tests/test_hd_hud.py`
  10/10. What a pack for this game needs
  (MaxVRAMWriteSplits, ConvertCopiesToWrites, no nested duplicate
  rectangles): `DEVELOPMENT.md`, *HD texture packs on this game*.
- **HD texture packs reach the portraits** (2026-11-22, the player's
  request): the framework refused textures whose 8-bit page runs past VRAM's
  right edge, where every portrait lives (the magic menu's hero, the
  dialogue speakers); `patches/psxrecomp/0004` checks only the texels a draw
  reads (a unit test fails without it). A computed portrait name needs its
  semi-transparent twin too: without it the framework drew a story scene's
  semi-transparent portrait with the plain image, opaque, and a black
  silhouette sat in the prologue's dragon scene (player report on 0.3.0
  with the local pack, 2026-11-23; `DEVELOPMENT.md`).
- **Cut prologue scene** (2026-11-21, branch `secret-stages`, the
  player's request): Warp > Cut prologue scene takes the hero to 0/5, a
  scene cut from the prologue that nothing in the game leads to (the
  decomp's docs/SECRETS.md). The cheats plugin calls the game's own
  ModeTransition at a quiet frame, as a stage exit would: no Game Over,
  about a second. Its lines, left in Japanese, were read and translated
  (the decomp's SECRETS.md) and show in English. The first version also
  offered the corridor 1/24 (with its Celia draft in English) and the
  Japanese world maps 1/14 / 1/16; the player had them taken out.
  `tests/test_secret_stages.py` 6/6 (it loads a save state after the
  trip: the first version's expansion-memory allocation changed the
  save-state layout and refused older states).
- **In-game menu redesigned** (2026-11-18, branch `hero-swap`, the player's
  request): the title draws its own menu (`dragonvalor_menu_view.cpp`: dark
  panel with a gold accent, the hero, HP / MP and stage on top, one line
  per row, the selected row's help underneath, key caps) instead of
  recomp-ui's generic one; sections Game (Resume, states, discs, leaving),
  Hero, Cheats, Magic, Items, Warp, Video, Options; a dialog asks before
  loading or overwriting a state, changing discs, the warp, leaving the
  game and applying a Play as / Weapon choice (picked first, applied
  together), and a notice at the top right says what happened.
  F7 and Game > Save / Load state open a save-state screen in the same
  style (12 slots with pictures and dates, Save and Load tabs) instead of
  the runtime's 8 x 8-font picker; loading a state made with another hero
  than Play as makes the save's hero and weapon the choice (a notice says so).
  `tests/test_menu.py` 9/9 (now from test state 10, no longer slot 0),
  `test_heroswap.py arlen --menu kodel,stage` 19/19, `default
  --menu-weapon igunis` 9/9.
- **Play as** (2026-11-17, branch `hero-swap`, experimental): pick the hero
  of every action stage in the launcher or the in-game menu
  (`dragonvalor.heroswap` 2.0.0; on by default since 2026-11-18 at the
  neutral choices, so a menu choice applies at once: off, the first choice
  only took effect after restarting the game). The plugin reads the heroes from the disc
  at start (cached afterwards; a disc-2 hero needs disc 2 started once) and
  rebuilds each stage's files in RAM as they load (hooks on
  `StageBuffersAlloc` and `PackInstall`), model, moves, sounds, portrait and
  name; the C rebuild gives the decomp tool's bytes. `tests/test_heroswap.py`
  passes for all eight heroes in Clovis's 1/7 and 1/9. Story scenes follow
  too: a scene's own copy of the stage's hero takes the chosen one's parts,
  and the dialogues name the chosen hero (3/1's opening as Clovis, 0/1's
  prologue as Kodel); the world map's walker, the Status screen and the
  save follow it too, and a choice in the in-game menu restarts the stage
  with the new hero at once. The remaining scenes were checked the same
  day: the twins of 5/19‥5/24 and 12/29 (their texture variants no longer
  overwrite the chosen hero's), 5/20 and 5/24's copies of the plain Phillip,
  12/29's ending portrait, and disc 2 after a disc change in the same
  session (read from its image; its heroes no longer need disc 2 started
  once). The chapter epilogues name the chosen hero too (the only other
  texts that name a hero; the character profiles stay). Save states keep
  the hero they were taken with (the swap happens when a stage loads): a
  state taken as another hero plays on as it was and an on-screen notice
  says the choice applies from the next stage; a state taken with a swapped
  hero keeps its scenes on that hero after the load. The hero can hold any
  of the game's nine weapon models (option `weapon`, menu row Weapon,
  2026-11-17/18; found by scanning the hero's node 2 in all 226 action
  stages, the magic swords named as the game's dialogues name them: Azos,
  Soul, Kadum, Igunis (the three last campaigns' sword), then Felippe's
  first and Mihael's, Anna's and Anita's own weapons of those campaigns,
  Anita's in its purple and its turquoise colours):
  the model and its texture, moved into the hero's
  texture page (the C port matches the decomp tool byte for byte;
  `tests/test_heroswap.py --weapon / --menu-weapon`). Before it
  (2026-11-16): one stage per folder written by the decomp's
  `tools/heroswap.py`. Open: long play, 9/10's scene not watched, a scene
  pack installed right after a save-state load not watched
  (`DEVELOPMENT.md`, *This project's package: dragonvalor.heroswap*).
  **Weapons of other characters** (2026-11-23, the player's request:
  "Raimun's sword"): a scan of every object of both discs with the heroes'
  body (decomp `heroswap.py weapons`) found five more weapons; four are
  were offered: Raimun's sword (chapter 1's boss, 1/20), Jeanne's crystal
  sword, Mihael's broadsword (Anna's chapter, disc 2; out again: in play it
  looks like Azos, the player) and Kodel's cutlass (3/1's opening scene);
  the grown Clovis's long sword (3/17) draws from his whole
  texture sheet and fits no hero. Raimun's needs more CLUTs than a hero has
  free: the hero's CLUT block grows up into the page's unused rows. Each
  matches the decomp tool byte for byte (`test_heroswap.py arlen --weapon
  raimun / jeanne / cutlass / broadsword`, Dragon Valor with Raimun's).
  **Trails and voices** (2026-11-24, the player's request): each body
  swings with its own sword-trail colours, and Raimun, the grown Clovis and
  Lodonya shout with their own voices (attack, hurt, jump and death cries,
  their roles read from the scripts after the player's first listen); both
  match the decomp tool byte for byte. At the player's word they cry only
  on the standing combo's third hit and the charged attack (the other
  attacks are silent), the grown Clovis also on his strong attack and his
  jumps back, and the jump, hurt and death cries stay.
  **Other looks** (2026-11-24, the player's request): Mihael's green look
  of area 10 and Gerome's look of area 12 are Play as choices of their own,
  each with that area's kit (moves, weapon, portrait); a red Mihael picked
  in area 10 is now swapped in too (by the code; area 10 not played yet).
  Each matches the decomp tool byte for byte.
  **Moves** (2026-11-24, the player's request): the bodies can wear any
  hero's moves and voice (Hero > Moves; Anna's or Anita's for Shalya,
  Fannah and Jeanne; the row is locked while a hero is picked), and the
  Status screen names the body played instead of Clovis. Each matches the
  decomp tool byte for byte. The grown Clovis's strong attack takes his own
  strong-attack cry, his own sound on the jump back and, after his special
  (the strong attack: the same animations), his tired end with his breath;
  the skeleton, which has no voice, is silent. With Moves > Clovis (Old)
  (2026-11-24, the player's request; shown when he is picked) he has his
  own moves: his charged attack is the double slash he fights Kodel with as
  a boss (his own animations and reach; tried first as the combo's end,
  which the player did not like) and his special ends tired; with the other
  Moves he moves as that kit. His guard is the heroes' own (standing still,
  a hit from the front or a side is blocked). His energy shot is on L2
  (2026-11-25, the player's request: "the fire attack"): his own wind-up,
  blink and recovery, his cry, and his ball of fire flying from his hand,
  hitting the enemies it meets (power 8), held back by walls; its
  projectile takes the last spell object slot (with spell 4 or 5 at level
  3 selected, L2 does nothing until the next stage), and its own two
  sounds are left out so that he still fits in every stage he did.
  **More bodies** (2026-11-23, the player's request): a skeleton, Raimun,
  Lodonya, Shalya, Fannah, Jeanne and Phillip with Kadum on his back (5/19's
  scene; first misnamed the Guardian; a Felippe with shield left, the
  hero's own look) are Play as choices too, all on the prologue Clovis's kit (no
  shield: the chapter-1 Clovis's moves held a raised empty hand), the hero
  page baked from each body's texture file; each matches the decomp tool
  byte for byte (`test_heroswap.py NAME --ref`).
  **Old Clovis** (2026-11-23, the player's request): the grown Clovis of
  chapter 3 (3/17) is the 11th choice, his body and long sword on Clovis's
  kit (`test_heroswap.py oldclovis --ref` 20/20). The plugin's cache now
  lives beside the executable: started from a link, the game ran in another
  folder and lost the disc-2 weapons (the player saw them all as Clovis's).
  **The Dragon Valor** (2026-11-23, the player's request): the dragon slayer
  of the prologue's opening scene (0/1) is a 10th Play as choice: his body
  (a scene object with a hero's 14 parts, found by the decomp), texture and
  portrait on Clovis's kit, so Clovis's moves; the dialogues and epilogues
  say Dragon Valor, the Status screen and the save Clovis (no hero of the
  game). `tests/test_heroswap.py dragonvalor --ref ...` 20/20 (the decomp
  tool's `--body dragonvalor` bytes), and 3/1's opening scene with him.
- **Menu fixes from the player's reports** (2026-10-16): Return to launcher
  restarts the program, which opens the launcher (the framework's own
  return only works in netplay builds and closed the game; upstream
  report pending); the Area row only steps to areas where the current
  stage number is an action stage on the same disc (a scene left the
  screen black until the watchdog closed the game), and from stage 1 it
  reaches every chapter's hero (`tests/test_area_row.py --reloads`, 7/7);
  action rows no longer show "OPEN" (`patches/recomp-ui/0003`). A **Go
  now (Game Over)** row below Area makes the trip by itself: Game Over,
  Continue answered for the player, Infinite HP held off meanwhile, the
  Continue's 50 Val given back (8‥12 s; `tests/test_area_row.py` 11/11).
  Since the player saw the hero jump during it, the row no longer presses
  Cross: it writes the CONTINUE prompt's own state (`0x8009FAF8`) and
  gives the game a neutral pad until the stage runs again.
  The blue status line now replaces the key hint in the footer and wraps
  before Resume instead of running under it (`patches/recomp-ui/0004`).
- **Names** (2026-10-16, the player's request): the project folder is
  now `Dragon Valor Recomp` and the game is "Dragon Valor Recompiled"
  (window, launcher, release zips); the exe is `DragonValorRecompiled.exe`.
  On 2026-10-17 the launch scripts followed: `Play.bat` and `PlayDebug.bat`
  are now `DragonValor.bat` and `DragonValorDebug.bat`.
  Builds reconfigured, `generated/` regenerates byte-identical, the
  player's Memory Card and texture-pack paths re-pointed; tests pass
  (`test_menu` 9/9, `test_area_row` 16/16, `test_manifests`).
- **App icon** (2026-10-17, the player's choice): the framework's
  controller gave way to our own pixel-art sword on crimson with a cyan
  glint, after the idea of the game's memory-card save icon (drawn anew;
  `tools/make_app_icon.py`). Both builds embed it; the debug build starts
  from slot 0 with it (no unknown dispatches).
- **Fan-project notice** (2026-10-17, the player's request before a first
  GitHub release): README (top and *Legal*), `RELEASE_NOTES.md`, `DISC.md`
  and `THIRD_PARTY_NOTICES.md` say the project is unofficial and
  non-commercial, unaffiliated with Bandai Namco or Sony, and name the two
  things of the game a download does carry (the recompiled program and the
  launcher's cover art); "nothing from the game is included" was too broad.
  The GitHub repository description says the same in one line.
- **Stage warp** (2026-10-16): after the player asked why the Area row
  only offered areas 3 and 6 (their stage number, 21, is an action stage
  only there), the stage the Continue reloads was found in the stage-start
  record (`0x8007D9C6`), and the menu gained a **Stage (next
  Continue)** row: Area and Stage pick any action stage of the disc, Go
  now goes there (`tests/test_area_row.py` 16/16, `tests/test_menu.py`
  9/9). Known limit: a stage that opens with a story scene shows the
  scene's text as garbage after the jump (the scene still plays). The
  warp does not give a free hero choice (the hero still comes with the
  area, BACKLOG M), so at the player's request the three rows moved to
  a last menu section, **Experimental**, with a save-state warning. (Play
  as, 2026-11-17, now gives any hero anywhere; since the menu's redesign
  of 2026-11-18 these rows are its Warp section.)
- **In-game menu with the mouse** (2026-10-15): hover, click, -/+ buttons,
  wheel, right button = back. Shorter row texts; Magic and Items rows carry
  the game's own descriptions; the Items page now edits the right slots
  (the table was one off for everything but Earrings), and raising a key
  count also takes the stage's key objects, which is what doors check
  (confirmed in play: the castle door opened).
- **Every menu row is remembered** (2026-10-15): video rows through the
  launcher's `settings.toml`, mods through `mods/state.toml`; the volume
  (which the framework never saved) and the menu's own rows (Pause in menu,
  dim, opacity) through `dragonvalor_settings.toml` next to the exe.
- Tester material: [`PLAYTESTING.md`](PLAYTESTING.md), the GitHub bug-report
  form, [`CONTROLS.md`](../CONTROLS.md), [`THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md).

## Open items

1. Human play session: the player finished chapter 1 and played into
   chapter 2 (Kodel, 2026-10-16); still open are Load Game after a restart, reaching disc 2
   in-game, and whether the Xbox pad actually rumbles with Vibration ON. (The
   "story demo race" after the intro skip noted here until 2026-11-24 was
   the attract demo, which starts after 25 s idle on the menu: the decomp
   shows mode 5 is the demo, not the menu.)
2. Known from the PAL bring-up and not re-checked here: dissolve brightness at
   high internal scale (see [`HISTORY.md`](HISTORY.md)).
3. Play as (hero and weapon, done 2026-11-17/18, above) is still open on
   long play, on 9/10's scene (not watched) and on a scene pack installed
   right after a save-state load (not watched). It needed no
   hero-selecting field after all: the hero follows the area, and the
   plugin rebuilds each stage's files as they load.
4. The framework's freeze watchdog writes a ~35 MB `psx_freeze_dump_*.json`
   beside the exe on every boot (false positive on statically compiled code).
   Upstream PR #646 fixes it (merged 2026-10-19, after the pin of
   2026-10-15); until a bump brings it in, `DragonValor.bat` and
   `tools/dvdbg.py launch` delete the existing dumps before each start (no
   config switch exists; 58 of them, 2 GB, had piled up in one afternoon of
   scripted runs).
5. Widescreen follow-ups: a check of chapters 3+ and the field module
   (chapter 2's first stage is fine), a real Fit resize, the light centre
   of the next-stage transition (effect 0) in the GL renderer; see
   `BACKLOG.md`, section F.
6. Hitches where many enemies are on screen (player, 2026-11-18): the frame
   ran at its 16.7 ms limit with the GL calls on the emulation thread; the
   render thread is now on by default (`game.toml`), which measured about
   1.5x the headroom at the spot (`DEVELOPMENT.md`, *Run*). An install whose
   `settings.toml` says `render_thread = false` keeps it off until that line
   is changed.

## Backlog

A read-only review on 2026-10-14 (code, docs, config, and a survey of other
psxrecomp titles) is recorded in [`BACKLOG.md`](BACKLOG.md), which marks what
has been applied since.

## Tooling

`DragonValor.bat`, `DragonValorDebug.bat` (`PlaySmooth.bat` was dropped on 2026-10-14: frame
generation adds nothing at 60 Hz), `tools/dvdbg.py`, `tools/session_monitor.py`. See [`DEVELOPMENT.md`](DEVELOPMENT.md).

Automated tests start from save state slot 0 (first screen of gameplay) or
slot 10 (a town stage with Fire selected and an enemy in front, for spell
and magic-kill tests); both are kept in `tests/states/` (local, out of git since 2026-10-21) and restored
into `saves/openbios/` by `python tools/dvdbg.py launch --state N`. RAM addresses for HP/MP/STR/DEF, Val and
magic levels (verified cheat map) and the save-state workflow are in [`DEVELOPMENT.md`](DEVELOPMENT.md#save-states).

Mods: the project ships `dragonvalor.cheats` 1.4.0 (Infinite HP, Infinite MP,
gem / scroll every N kills, added 2026-10-15; Unlock gallery, 2026-11-21) and `dragonvalor.skip-intro` (power-on straight to the title menu since
2026-11-24: the Namco logo keeps only the title-data read it covers, the
movie and the title's logo flight get the game's own Start skip, all from
a function-entry hook on the pad copy; the menu in 3.8 s instead of 9.8 s;
Fast boot then runs the black boot faster than real time through a mod API
of `patches/psxrecomp/0005`: menu ~3.4 s sooner from the process start) as bundled
packages backed by plugins in `mods/plugins/`, verified on and off
(2026-10-14). The cheats feature is on by default with every option off, so
the **in-game menu** (Escape / Select+Start; `mods/plugins/dragonvalor_menu.cpp`
on psxrecomp's menu host + recomp-ui's runtime UI) can switch them live; the
menu also pauses, changes texture filtering / scanlines / fullscreen /
volume, saves and loads states, changes disc and returns to the launcher.
Its STR editor writes the game's real attack table (`0x80086324`, five
entries; the shown STR is only their sum, found 2026-10-14 after the player
saw a huge STR change nothing; `DEVELOPMENT.md`, *How damage is computed*).
The gem and scroll drops the player noticed are a rule, not luck: a gem
after 15 sword kills (then 22, 28, …), a scroll after 2 magic kills (then
4, 7, 12, 20), both confirmed live on 2026-10-15, the gem by script and
the scroll by the player on the pad (`DEVELOPMENT.md`, *How drops
are decided*). Items (code reading, 2026-10-15): Earrings take 1 MP off
every cast each, the Bracelet feeds a fatigue counter nothing in the action module reads, and Pendant / Lithograph / Ring / Talisman give their bonus when obtained, not while carried
(`DEVELOPMENT.md`, *Pickup ids and what they do*).
The Namco logo stays: shortening it breaks the title's loads. Title defaults
measured the same day: mipmapped 3D textures with 4 anisotropic taps ship in `game.toml` (a
90 ms audio buffer was tried and reverted: choppy sound on the title screen); FXAA and CD Speed do not help here
(see `DEVELOPMENT.md`, *Video and audio defaults*); linear filtering of the
final image and nearest texture filtering are the written-out defaults (the
player's choice; `DragonValor.bat` no longer forces xBR). The runtime is the PGXP
hook flavour and PGXP ships on by default (66 % of vertices corrected, 60
fps kept; `DEVELOPMENT.md`, *PGXP*). The
framework's HD texture dumper works on this game once a stage reloads
(563 PNGs from a session from the title into Brize Forest). See [`DEVELOPMENT.md`](DEVELOPMENT.md#modding-what-the-framework-offers).
