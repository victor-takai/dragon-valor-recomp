# Local patches to the submodules

Small fixes the title needs before upstream has them. `CMakeLists.txt` applies
every `patches/<submodule>/*.patch` with `git apply` at configure time when it
is not applied yet (checked with `git apply --check --reverse`), so a fresh
clone and the release workflow build the same code as this machine. The
submodule's gitlink keeps pointing at the upstream commit; `git status` shows
the submodule as modified while a patch is applied.

Rules: one upstream-shaped change per file, named `NNNN-<what>.patch`, with
the upstream issue or PR in this table. Remove the patch when the pin moves
past the merge. The framework itself (`psxrecomp/`) is not patched here; its
changes go upstream first.

| Patch | Submodule | What | Upstream |
|---|---|---|---|
| `recomp-ui/0001-runtime-ui-keep-selection-visible-and-frame.patch` | recomp-ui @ 41c88ff | In-game menu drawer (`runtime_ui_imgui.cpp`): scroll the page so the selected row stays visible under keyboard/pad navigation (long pages such as our Items page never scrolled), and give the selected row an accent outline plus a left bar. | to be proposed as a PR to RetroPortingToolKit/recomp-ui |
| `recomp-ui/0002-runtime-ui-wrap-row-descriptions.patch` | recomp-ui @ 41c88ff | In-game menu drawer (`runtime_ui_imgui.cpp`): a row's description is drawn with a wrap width (the row minus its padding and, on integer rows, the -/+ buttons) and the row grows by the extra lines, instead of one line running past the drawer's edge (the player saw the cheat descriptions cut off). One-line descriptions keep the old row height. | to be proposed together with 0001 |
