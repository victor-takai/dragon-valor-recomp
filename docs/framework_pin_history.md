# Framework pin history

One row per deliberate submodule bump: what moved, why, and what was
verified afterwards. The gitlinks are authoritative; `framework_pins.txt`
is the scaffold snapshot of the same SHAs.

| Date | psxrecomp | recomp-ui | Why | Verified |
|---|---|---|---|---|
| 2026-10-13 | `54abcb70` | `41c88ff` | Project scaffold (PAL bring-up, then the USA project on 2026-10-14). | Boot → title → attract demo at 60 fps, 0 unknown dispatches / 0 segment misses, release zip boots standalone. |
| 2026-10-14 | `f9c287f5` (55 commits: in-game menu host contract #598‥#603, cdrom SetLoc/XA fixes #657/#658, debug-server accept timeout #659, restore-edge IRQ check #660, depth24 hold #661, rollback fixes #662, native-wide sprite backgrounds #663; the submodule URL now points at `RetroPortingToolKit/psxrecomp`) | `41c88ff` (already current) | Pick up the cdrom and debug-server fixes and the live mod-option API. The recompiler, emitters and `generated/` did not change, so the game C was not regenerated. | Both builds compile; fresh boot reaches the title (Start at frame ≥ 1500); the committed slot-0 state loads unchanged; gameplay paces at 60.0 frames/s; `tests/test_cheats.py` passes on and off; `tests/test_manifests.py` passes; `generate_ci.py --check` says the workflow is current. Still present: the freeze dumps at boot (upstream PR #646 not merged yet). |

Procedure used (also in `docs/DEVELOPMENT.md`): `git -C psxrecomp fetch`,
read `git log --oneline <old>..origin/master` and the diff of
`RELEASE_NOTES.md`, `docs/`, `runtime/include/mod_plugins.h`, `runtime.cmake`,
`recompiler/` (regenerate `generated/` only if the recompiler or emitters
moved), check out the commit, `git submodule update --init --recursive`
inside `psxrecomp/`, rebuild `build-debug` and `build-release`, then run the
checks above. If `state load 0` is refused, re-take the state
(`state save 0`, `state keep 0`) and note it here.
