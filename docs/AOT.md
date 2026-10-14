# AOT overlay sharding for Dragon Valor (USA)

How the game streams code, what `aot/overlays.json` declares, and how the
static shard in `generated/overlays_static*.c` is produced and checked.
Framework reference: `psxrecomp/docs/AOT_SHARDING.md`.

## How the game loads code

- The boot EXE `SLUS_010.92` (load `0x80010000`, text `0x67800`; disc 2's
  `SLUS_011.64` is the same bytes) clears its BSS from `0x800774A8` to
  `0x800BBEF0` at entry (`0x80012EA0`) and uses `0x800BBEF0` as the heap base
  (`lui/addiu` at `0x80012EA8` and `0x80012EF0`). The PAL release uses the same
  loader with base `0x800BC268` (+0x378).
- Five **mode modules** live in `BIN/` on the ISO and are read whole (their
  header included) into that address, one at a time:

  | File | Size | Header | Static discovery |
  |---|---|---|---|
  | `BIN/MAP.BIN` | 93,388 | `{count=8, ptr[8]}`, absolute pointers | see audit |
  | `BIN/OPENNING.BIN` | 651,924 | `{count=4, ptr[4]}` | see audit |
  | `BIN/ACTION.BIN` | 40,872 | raw | see audit |
  | `BIN/ENDING.BIN` | 425,144 | raw | see audit |
  | `BIN/DEMO.BIN` | 1,714 | raw (`allow_missing`) | static roots only |

  The base is established three independent ways: the EXE constant above, the
  only base at which every `MAP.BIN` / `OPENNING.BIN` header pointer lands on a
  function prologue, and `extract_generic.py`'s jal-fit vote for the raw files.
- Everything else code-like the runtime ever captured sits inside that slot
  (`0x800BC000–0x800CF000`), plus tiny kernel-area patches (`0x80002000`,
  `0x8000D000`) and a 4 KB region at `0x80067000` inside the EXE image.
- `DATA.CDR` (438 MB) and `ILLUST/ILLUST_U.CDR` are packed virtual file
  systems (the EXE holds the path table: `\MACRO\CHAPT.MAB`, `\EMY0\EMY0.DV`,
  `\BG\BG.TMP`, `\BGM\…`, `\DATA\MAGIC1\…`); `ILLUST_E.CDR` starts with a table of
  24-bit big-endian sector offsets. No executable members have been identified
  in them so far (the runtime captured no code outside the slots above).

## The profile (`aot/overlays.json`)

Schema `psxrecomp AOT methods v1`, game `SLUS-01092`:

- `disc_hashes.sha256` of the disc 1 data track
  (`9b41965d…9c59f`), so another dump exits 3 instead of producing a shard.
- `checks[0]` (`words`) pins the two `lui/addiu` pairs that form `0x800BC268`
  in `SLUS_010.92`; a changed loader fails the build loudly.
- `images[0]`: `fixed_address_files` for MAP / ACTION / OPENNING / ENDING at
  `0x800BBEF0`; `images[1]`: `DEMO.BIN` with `allow_missing` (the generic
  detector does not vote on a 1.7 KB file).
- `expected_records = 5`, `strict_bounds = true`,
  `static_output = "generated/overlays_static.c"`.

`CMakeLists.txt` passes the same file as `GAME_OVERLAY_STATIC_C` (keep it
**before** `GEN_FULL_GLOB`, a multi-value argument that would otherwise
swallow it).

## Regenerating the shard

```bash
# verify inventory only
python psxrecomp/tools/aot_overlay_pipeline.py extract --profile aot/overlays.json \
  --game-toml game.toml --disc "../Dragon Valor (Disc 1)/Dragon Valor (Disc 1).cue" \
  --recompiler build-recompiler/psxrecomp-game.exe --work-dir <scratch>/aot-work

# build generated/overlays_static.c + units + AOT_STATIC_AUDIT.json (CPS runtime)
python psxrecomp/tools/aot_overlay_pipeline.py static --profile aot/overlays.json \
  --game-toml game.toml --disc "../Dragon Valor (Disc 1)/Dragon Valor (Disc 1).cue" \
  --recompiler build-recompiler/psxrecomp-game.exe --work-dir <scratch>/aot-static-work \
  --out-dir generated --cps
```

`psxrecomp_cli.py generate` runs the `static` step itself after writing the
game C (it reads `static_output` and the dispatch's CPS mode), so a full
regenerate needs no extra command. The shard is committed with the rest of
`generated/`; release CI links it and never compiles overlays itself.

First build (2026-10-14): 5 recipes, **4630 audited static variants** in 7
files, `all_pairs_valid`, `all_guards_match_known_input_bytes`.

## Validation

Method from the framework guide: run the debug build with
`PSX_OVERLAY_AUTOCOMPILE_OFF=1`, drive to gameplay, inspect
`autocompile_status` (no builds) and `overlay_loader_status` (native dispatch),
and compare fps against the runtime-compile baseline (one 44 fps dip during a
compile burst in the first play session).

Results (2026-10-14, debug build, `PSX_OVERLAY_AUTOCOMPILE_OFF=1`, muted,
Start → title → idle into the attract demo, 335 s): 59.9–60.0 fps in every
gameplay window (three 57–58.5 fps windows during the story movie), 0 unknown
dispatches / segment misses, no runtime compiles, 4217 static entries / 4630
variants registered; the dungeon demo renders from the static shard. Release
build with the shard: LIVE at `[Game 60 fps 1.00x]`.

Disc 2 of the USA set carries the same EXE and byte-identical `BIN/*.BIN`
files (verified 2026-10-14), so the one shard serves both discs; booting with
`--disc` on disc 2 ran at 59.9 fps with 0 unknown dispatches.
