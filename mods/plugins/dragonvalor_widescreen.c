/*
 * Dragon Valor (USA) "Widescreen": trusted static plugin behind
 * mods/preloaded/packages/dragonvalor.enhancement.widescreen.
 *
 * The picture itself comes from psxrecomp's native-wide renderer
 * (psxrecomp/docs/WIDESCREEN.md): the game's projection is left alone and
 * the GPU rasterises the columns beyond the 4:3 frame into a wider surface,
 * so the extra width is real scenery, not a stretch. On PSX the framework
 * offers no widescreen switch of its own; a title's mod package selects the
 * view at activation (psx_mod_set_fixed_display_aspect and, for Fit to
 * Window, psx_mod_set_adaptive_display_aspect). That is all this plugin does,
 * plus one game-specific rule:
 *
 *   - game.toml [widescreen] gte_game_mode classifies a frame as gameplay
 *     when it projects vertices through the GTE, which keeps the title,
 *     menus and movies at 4:3. The pause screen projects nothing (the stage
 *     is frozen), so 0.75 s after Start the picture snapped back to 4:3. The
 *     world-scene predicate below says "gameplay" for every frame of the
 *     action module (mode byte 0x8008634C == 8, docs/DEVELOPMENT.md *Known
 *     RAM addresses*), pause included; every other mode falls back to the
 *     GTE detector.
 *
 *   - The noise block right of the 4:3 area (castle corridor, area 1 /
 *     stage 19; seen 2026-10-15, explained 2026-11-16 from the decomp,
 *     github victor-takai/dragon-valor-decomp src/main/r2A_drawers.c). The
 *     model drawer of prim record 16 (ModelDrawR16, 0x80023A24) draws lens
 *     sprites: each POLY_FT4 redraws the frame buffer under it (15-bit
 *     texture page at the buffer's VRAM row), 24 of them in a ring around
 *     the red orbs of that corridor. It clamps the rectangle's right edge to
 *     x 383 but never tests the left edge, so for a sprite whose rectangle
 *     lies wholly right of the frame (left >= 384; the GTE clamps the centre
 *     at 1023, so this holds for every orb anywhere to the right) the quad
 *     comes out inverted, from x 382 out to left + 1, and its texture page
 *     (left >> 6) starts at VRAM x >= 384: texture data, not the picture,
 *     read as 15-bit colour. The console's 384-pixel draw area shows two
 *     columns of it; the wide surface shows the rest. The hook below runs at
 *     the entry of ModelWeldReset (0x80021CE4), which FrameDraw calls right
 *     after ModelRender and ModelWeldVertices and before DrawOTag, and
 *     collapses those quads (texture page column 6 or more) to zero width,
 *     so the GPU draws nothing for them. Lens sprites inside the frame are
 *     left alone. ModelWeldReset is listed in game.toml [recompiler]
 *     mod_function_entry_funcs.
 *
 *   - Its mirror on the left: the smear of horizontal streaks in the left
 *     margin (the town of 1/7, seen 2026-11-17 in a play test). For a
 *     sprite whose centre is left of the frame the drawer reads x unsigned,
 *     clamps the left edge to 0 and leaves the right one where it is, so a
 *     sprite wholly left of the frame comes out inverted, from x 1 out to
 *     its (negative) right edge - 1, texture page column 0, u from 0 to the
 *     right edge's low byte. The console draws at most column 0 of it; the
 *     wide surface shows the whole quad: the frame's first columns
 *     stretched across the margin. The same hook collapses those too
 *     (top-left corner at x 1, top-right corner left of it).
 *
 *   - Screens drawn for 4:3 only (seen 2026-11-17 in a play test). The
 *     magic / item menu (mode 7, Triangle) is a 384 x 240 panel over the
 *     stage, which keeps being drawn and projected, so the GTE detector kept
 *     the frame wide and the stage showed at both sides of the panel. The
 *     screen transitions (g_Transition 0x800A2EF8, src/main/r2B_transitions.c
 *     in the decomp) freeze, move and fade copies of the frame buffer's
 *     384 columns, so in a wide frame they ran inside the 4:3 area while
 *     the live stage went on at the sides. The motion blur some event
 *     scripts switch on (g_Render.blurOn, FrameDraw) is the same kind of
 *     copy: a lighter 4:3 rectangle in a wide frame. A native-scene
 *     predicate (gpu_ws_set_native_scene_predicate: nonzero = present this
 *     frame at 4:3 with side bars) says so for mode 7, while a transition
 *     runs (g_Transition.state != 0) and while the blur is on in an event
 *     scene (sub-mode 5), sampled once per VBlank like the world predicate.
 *
 *   - The blur in play (side bars during a special attack, player's report
 *     2026-11-17). The hero's script (action pack entry 2, two moves) and
 *     some enemy groups switch the blur on in the middle of the action:
 *     blurOn 1, tint 0x9B, no line offset, so the two sprites draw the
 *     4:3 frame over itself about 10 % lighter while the game runs at 20
 *     frames a second. Bars that come and go in a fight look like a bug,
 *     and the lighter part cannot reach the margins (they are not in VRAM),
 *     so outside event scenes the frame stays wide and the hook below takes
 *     the blur's four packets out of this frame's ordering table (the
 *     table is rebuilt every frame; the packets themselves are untouched).
 *     The slow motion stays.
 *
 *   - The dialogue window's top border (src/main/exeE_msgwin.c): six
 *     identical 64 x 8 sprites from x 0 to 383 along the top of the black
 *     band (the band itself, a 384-wide TILE, is already extended by the
 *     renderer), so the border stopped at the 4:3 edges. The hook below
 *     tags the leftmost sprite of the buffer about to be drawn as a
 *     repeating rectangle (gpu_ws_tag_repeat_rect, period 64): the renderer
 *     repeats it into the side margins only.
 *
 * The only guest RAM written is the lens packets of the buffer about to be
 * drawn and, while a blur runs in play, one link of the ordering table being
 * built. Everything is inert unless the package is enabled: activation is
 * the only thing that sets s_enabled, and the predicates check it first;
 * the hook runs only while the package's plan is active.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "cpu_state.h"
#include "mod_plugins.h"

#define DV_WS_PKG       "dragonvalor.enhancement.widescreen"
#define DV_WS_FEATURE   "widescreen"
#define DV_WS_PLUGIN    "dragonvalor.widescreen"

#define DV_MODE         0x8008634Cu   /* u8 game mode: 8 = action stage */
#define DV_MODE_ACTION  8u
#define DV_MODE_MENU    7u            /* magic / item menu (Triangle) */
#define DV_TRANSITION   0x800A2EF8u   /* g_Transition: s32 state (0 = off) */
#define DV_BLUR_ON      0x8009F8A8u   /* u8 g_Render.blurOn (g_Render + 0x1068) */
#define DV_SUBMODE      0x8008634Du   /* u8 sub-mode: 5 = event script, 7 = play */
#define DV_SUB_EVENT    5u

/* Renderer services of psxrecomp's gpu.h (runtime/include/gpu.h) that
 * mod_plugins.h does not re-export; mod activation clears the native-scene
 * predicate, so it belongs to this session's plugin like the world one. */
void gpu_ws_set_native_scene_predicate(int (*predicate)(void));
void gpu_ws_tag_repeat_rect(uint32_t prim, int32_t period);

static int s_enabled;
/* The predicate runs on every primitive and GTE projection, so the mode byte
 * is sampled once per VBlank (as RidgeRacerType4Recomp's plugin does); a
 * save-state load invalidates the sample, and an invalid one reads live. */
static int s_scene_valid, s_scene_value, s_native_value;

static int dv_ws_scene_live(void)
{
    return s_enabled && psx_mod_read_byte(DV_MODE) == DV_MODE_ACTION;
}

/* The blur of an event scene (sub-mode 5): present at 4:3. Anywhere else in
 * the stage (the hero's specials, enemy moves) the frame stays wide and the
 * hook below leaves the blur out. */
static int dv_ws_event_blur(void)
{
    return psx_mod_read_byte(DV_BLUR_ON) != 0u && psx_mod_read_byte(DV_SUBMODE) == DV_SUB_EVENT;
}

static int dv_ws_native_live(void)
{
    return s_enabled && (psx_mod_read_byte(DV_MODE) == DV_MODE_MENU ||
                         psx_mod_read_word(DV_TRANSITION) != 0u ||
                         dv_ws_event_blur());
}

static int dv_ws_scene(void)
{
    if (s_scene_valid) return s_scene_value;
    return dv_ws_scene_live();
}

static int dv_ws_native(void)
{
    if (s_scene_valid) return s_native_value;
    return dv_ws_native_live();
}

static void dv_ws_vblank(void)
{
    s_scene_value = dv_ws_scene_live();
    s_native_value = dv_ws_native_live();
    s_scene_valid = 1;
}

static void dv_ws_savestate_loaded(void)
{
    s_scene_valid = 0;
}

/* g_Render (0x8009E840): two lists of 0x200 model-node pointers, their
 * counts at +0x1000, the buffer being built (0 / 1) at +0x100C. A node's
 * prim table pointer is at +0x68: 17 records of 0x10 bytes (u16 ordinal,
 * u16 count, s32 packet offset, s32 vertex offset, s32 extra offset), the
 * offsets relative to the table. Record 16 packets are POLY_FT4 (0x28
 * bytes), two per sprite (buffer 0, buffer 1): xy0 +0x08, xy1 +0x10,
 * tpage +0x16, xy2 +0x18, xy3 +0x20 (corners left/top, right/top,
 * left/bottom, right/bottom). */
#define DV_RENDER             0x8009E840u
#define DV_RENDER_LIST_SLOTS  0x200u
#define DV_RENDER_COUNTS      (DV_RENDER + 0x1000u)
#define DV_RENDER_FRAME       (DV_RENDER + 0x100Cu)
#define DV_NODE_PRIMS         0x68u
#define DV_PRIM_R16           16u
#define DV_FT4_SIZE           0x28u
#define DV_MODEL_WELD_RESET   0x80021CE4u
#define DV_FB_TPAGE_COLUMNS   6u   /* frame buffer = VRAM x 0..383 = texture page columns 0..5 */
#define DV_LENS_LEFT_X        1    /* left edge clamped to 0, corner inset by 1 */

static int dv_ws_ram_ptr(uint32_t p)
{
    return p >= 0x80000000u && p < 0x80200000u && !(p & 3u);
}

/* g_EventVm (0x8009F998): MsgWinWork pointer at +0x7C, the window's display
 * list per buffer at +0x124 (0 while no window is up). MsgWinWork +0x0C
 * points at the border sprites: 6 pairs (one per buffer) of a DR_TPAGE
 * merged with a SPRT, 0x1C bytes each: tag, E1 word, nop, the 0x64 command
 * at +0x0C, xy, uv / CLUT, wh. The renderer's tag takes the address of the
 * word before the command. */
#define DV_EVENTVM_MSGWORK    0x8009FA14u
#define DV_EVENTVM_WINLIST    0x8009FABCu
#define DV_MSGWIN_EDGE        0x0Cu
#define DV_MW_SPRT_SIZE       0x1Cu
#define DV_MW_BORDER_PERIOD   64

static void dv_ws_tag_dialogue_border(uint32_t frame)
{
    uint32_t work, edge, sprt;
    if (psx_mod_read_word(DV_EVENTVM_WINLIST + frame * 4u) == 0u) return;
    work = psx_mod_read_word(DV_EVENTVM_MSGWORK);
    if (!dv_ws_ram_ptr(work)) return;
    edge = psx_mod_read_word(work + DV_MSGWIN_EDGE);
    if (!dv_ws_ram_ptr(edge)) return;
    sprt = edge + frame * DV_MW_SPRT_SIZE;   /* sprite 0 (x 0) of this buffer */
    if ((psx_mod_read_word(sprt + 0x0Cu) >> 24) != 0x64u) return;
    if ((psx_mod_read_word(sprt + 0x10u) & 0xFFFFu) != 0u) return;   /* x 0 */
    if ((psx_mod_read_word(sprt + 0x18u) & 0xFFFFu) != (uint32_t)DV_MW_BORDER_PERIOD) return;
    gpu_ws_tag_repeat_rect(sprt + 0x08u, DV_MW_BORDER_PERIOD);
}

/* FrameDraw (decomp src/main/exeA_frame.c) links the blur at ot[blurOtz] of
 * g_FrameBuf[frame].ot: ot -> tpage[1] -> sprite[1] -> tpage[0] -> sprite[0]
 * -> what was there; whatever is linked later into that slot comes before
 * tpage[1]. Unlinking the four leaves the packets and the next frame alone. */
#define DV_FRAMEBUF           0x80086358u   /* g_FrameBuf[2], 0x2070 bytes each */
#define DV_FRAMEBUF_SIZE      0x2070u
#define DV_FRAMEBUF_OT        0x70u
#define DV_OT_LEN             0x800
#define DV_BLUR_TPAGE         (DV_RENDER + 0x109Cu)   /* DR_TPAGE blurTpage[2][2], 8 bytes */
#define DV_BLUR_SPRT          (DV_RENDER + 0x10BCu)   /* SPRT blur[2][2], 0x14 bytes */
#define DV_BLUR_OTZ           (DV_RENDER + 0x110Eu)   /* s16 blurOtz */

static void dv_ws_hide_play_blur(uint32_t frame)
{
    uint32_t p, tp1, s0, after, k;
    int16_t otz;
    if (psx_mod_read_byte(DV_BLUR_ON) == 0u || psx_mod_read_byte(DV_MODE) != DV_MODE_ACTION ||
        psx_mod_read_byte(DV_SUBMODE) == DV_SUB_EVENT || psx_mod_read_word(DV_TRANSITION) != 0u)
        return;
    otz = (int16_t)psx_mod_read_half(DV_BLUR_OTZ);
    if (otz < 0 || otz >= DV_OT_LEN) return;
    tp1 = DV_BLUR_TPAGE + (frame * 2u + 1u) * 8u;
    s0 = DV_BLUR_SPRT + frame * 2u * 0x14u;
    after = psx_mod_read_word(s0) & 0x00FFFFFFu;
    p = DV_FRAMEBUF + frame * DV_FRAMEBUF_SIZE + DV_FRAMEBUF_OT + (uint32_t)otz * 4u;
    for (k = 0; k < 4096u; k++) {
        uint32_t w = psx_mod_read_word(p), next = w & 0x00FFFFFFu;
        if (next == 0x00FFFFFFu) return;
        if ((next | 0x80000000u) == tp1) {
            psx_mod_write_word(p, (w & 0xFF000000u) | after);
            psx_mod_counter_add("dv.ws.play_blur_hidden", 1);
            return;
        }
        p = next | 0x80000000u;
        if (!dv_ws_ram_ptr(p)) return;
    }
}

static void dv_ws_weld_reset_entry(struct CPUState* cpu, uint32_t address)
{
    uint32_t frame, list, i, k, hidden = 0;
    (void)cpu;
    (void)address;
    if (!s_enabled) return;
    frame = psx_mod_read_byte(DV_RENDER_FRAME) & 1u;
    dv_ws_tag_dialogue_border(frame);
    dv_ws_hide_play_blur(frame);
    for (list = 0; list < 2u; list++) {
        uint32_t count = psx_mod_read_word(DV_RENDER_COUNTS + list * 4u);
        if (count > DV_RENDER_LIST_SLOTS) continue;
        for (i = 0; i < count; i++) {
            uint32_t node = psx_mod_read_word(DV_RENDER + (list * DV_RENDER_LIST_SLOTS + i) * 4u);
            uint32_t prims, rec, n, pk;
            if (!dv_ws_ram_ptr(node)) continue;
            prims = psx_mod_read_word(node + DV_NODE_PRIMS);
            if (!dv_ws_ram_ptr(prims)) continue;
            rec = prims + DV_PRIM_R16 * 0x10u;
            n = psx_mod_read_half(rec + 2u);
            if (n == 0u || n > 0x400u) continue;
            pk = prims + psx_mod_read_word(rec + 4u) + frame * DV_FT4_SIZE;
            if (!dv_ws_ram_ptr(pk) || pk + n * 2u * DV_FT4_SIZE > 0x80200000u) continue;
            for (k = 0; k < n; k++, pk += 2u * DV_FT4_SIZE) {
                int16_t x0 = (int16_t)psx_mod_read_half(pk + 0x08u);
                int16_t x1 = (int16_t)psx_mod_read_half(pk + 0x10u);
                int right_of_frame = (psx_mod_read_half(pk + 0x16u) & 0xFu) >= DV_FB_TPAGE_COLUMNS;
                int left_of_frame = x0 == DV_LENS_LEFT_X && x1 < DV_LENS_LEFT_X;
                if (!right_of_frame && !left_of_frame) continue;
                psx_mod_write_word(pk + 0x10u, psx_mod_read_word(pk + 0x08u));
                psx_mod_write_word(pk + 0x20u, psx_mod_read_word(pk + 0x18u));
                hidden++;
            }
        }
    }
    if (hidden) psx_mod_counter_add("dv.ws.lens_offframe_hidden", hidden);
}

/* "16:9" (default), "21:9" and "32:9" are fixed views; "Fit" opens at 16:9
 * and then follows the window with no upper aspect limit (native 4:3 is the
 * floor). Anything else (an old or hand-edited state.toml) is 16:9. */
static void dv_ws_activate(void)
{
    char choice[16];
    uint32_t num = 16u, den = 9u;
    int fit = 0;

    s_enabled = 1;
    s_scene_valid = 0;
    if (!psx_mod_option_value(DV_WS_PKG, DV_WS_FEATURE, "aspect", choice, sizeof choice))
        strcpy(choice, "16:9");
    if (!strcmp(choice, "21:9")) num = 21u;
    else if (!strcmp(choice, "32:9")) num = 32u;
    else if (!strcmp(choice, "Fit")) fit = 1;

    if (!psx_mod_set_fixed_display_aspect(num, den))
        fprintf(stderr, "dragonvalor.widescreen: display aspect %u:%u refused\n", num, den);
    if (fit && !psx_mod_set_adaptive_display_aspect(0u, 0u))
        fprintf(stderr, "dragonvalor.widescreen: adaptive display aspect refused\n");
    psx_mod_set_world_scene_predicate(dv_ws_scene);
    gpu_ws_set_native_scene_predicate(dv_ws_native);
}

PSX_MOD_CONSTRUCTOR(dv_register_widescreen)
{
    int ok = psx_mod_register_activation_plugin(DV_WS_PLUGIN, dv_ws_activate)
          && psx_mod_register_vblank_plugin(DV_WS_PLUGIN, dv_ws_vblank)
          && psx_mod_register_savestate_plugin(DV_WS_PLUGIN, dv_ws_savestate_loaded)
          && psx_mod_register_function_entry_plugin(DV_WS_PLUGIN, DV_MODEL_WELD_RESET,
                                                    dv_ws_weld_reset_entry);
    if (!ok) fprintf(stderr, "dragonvalor.widescreen: plugin failed to register\n");
}
