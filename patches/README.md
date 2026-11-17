# Local patches to the submodules

Small fixes the title needs before upstream has them. `CMakeLists.txt` applies
every `patches/<submodule>/*.patch` with `git apply` at configure time when it
is not applied yet (checked with `git apply --check --reverse`), so a fresh
clone and the release workflow build the same code as this machine. The
submodule's gitlink keeps pointing at the upstream commit; `git status` shows
the submodule as modified while a patch is applied.

Rules: one upstream-shaped change per file, named `NNNN-<what>.patch`, with
the upstream issue or PR in this table. Remove the patch when the pin moves
past the merge. The framework (`psxrecomp/`) is patched too since 2026-11-17
(the owner's decision): only for a bug that already has an upstream issue or
PR, with the change shaped like the upstream fix, so the patch goes away when
the pin moves past the merge.

| Patch | Submodule | What | Upstream |
|---|---|---|---|
| `recomp-ui/0001-runtime-ui-keep-selection-visible-and-frame.patch` | recomp-ui @ 41c88ff | In-game menu drawer (`runtime_ui_imgui.cpp`): scroll the page so the selected row stays visible under keyboard/pad navigation (long pages such as our Items page never scrolled), and give the selected row an accent outline plus a left bar. | to be proposed as a PR to RetroPortingToolKit/recomp-ui |
| `recomp-ui/0002-runtime-ui-wrap-row-descriptions.patch` | recomp-ui @ 41c88ff | In-game menu drawer (`runtime_ui_imgui.cpp`): a row's description is drawn with a wrap width (the row minus its padding and, on integer rows, the -/+ buttons) and the row grows by the extra lines, instead of one line running past the drawer's edge (the player saw the cheat descriptions cut off). One-line descriptions keep the old row height. | to be proposed together with 0001 |
| `recomp-ui/0003-runtime-ui-no-open-label-on-actions.patch` | recomp-ui @ 41c88ff | In-game menu drawer (`runtime_ui_imgui.cpp`): action rows (Resume, Save state, Load state, Return to launcher, Quit, Insert disc) no longer show a fixed "OPEN" on the right; the row's label is already the command (player report 2026-10-16: "OPEN" beside the System commands made no sense). Upstream master (2026-10-16) still prints it. | to be proposed together with 0001 and 0002 |
| `psxrecomp/0001-bilinear-clamp-neighbours-before-wrap.patch` | psxrecomp @ f9c287f | Bilinear texture filter (OpenGL `gpu_gl_renderer.c`, Vulkan `shaders/geo_tex.frag`, software `gpu_sw_renderer.c`): a neighbour texel is clamped to the prim's UV bounds inside the base texel's 256-texel page before the 8-bit wrap. Before, a neighbour left of u 0 wrapped to 255 and was clamped to the prim's right edge (and one past 255 to its left edge), so every picture drawn as two prims meeting at a page edge (the title screen, the screen copies of the transitions, the motion blur) showed a dark or coloured column at x 256 with `texture_filtering = "bilinear"` (player report 2026-11-17). Written to match the upstream PR: the same helper (`neighbour_texel`) and calls. | issue RetroPortingToolKit/psxrecomp#719, open PR #896 (2026-11-12); upstream master 31130e90 still has the old code |
| `recomp-ui/0004-runtime-ui-footer-status-before-resume.patch` | recomp-ui @ 41c88ff | In-game menu drawer (`runtime_ui_imgui.cpp`, `recomp_runtime_ui_internal.h`): the blue status line takes the key hint's place in the footer and wraps before the Resume button (the footer grows by the extra lines), instead of following the hint and running under Resume; the status buffer grows from 48 to 128 bytes, so a provider's sentence ("No area above on this disc has an action stage 2") is no longer cut. Player report 2026-10-16. | to be proposed together with 0001‥0003 |
