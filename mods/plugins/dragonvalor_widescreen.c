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
 * The only guest RAM written is those packets of the buffer about to be
 * drawn. Everything is inert unless the package is enabled: activation is
 * the only thing that sets s_enabled, and the predicate checks it first;
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

static int s_enabled;
/* The predicate runs on every primitive and GTE projection, so the mode byte
 * is sampled once per VBlank (as RidgeRacerType4Recomp's plugin does); a
 * save-state load invalidates the sample, and an invalid one reads live. */
static int s_scene_valid, s_scene_value;

static int dv_ws_scene_live(void)
{
    return s_enabled && psx_mod_read_byte(DV_MODE) == DV_MODE_ACTION;
}

static int dv_ws_scene(void)
{
    if (s_scene_valid) return s_scene_value;
    return dv_ws_scene_live();
}

static void dv_ws_vblank(void)
{
    s_scene_value = dv_ws_scene_live();
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

static int dv_ws_ram_ptr(uint32_t p)
{
    return p >= 0x80000000u && p < 0x80200000u && !(p & 3u);
}

static void dv_ws_weld_reset_entry(struct CPUState* cpu, uint32_t address)
{
    uint32_t frame, list, i, k, hidden = 0;
    (void)cpu;
    (void)address;
    if (!s_enabled) return;
    frame = psx_mod_read_byte(DV_RENDER_FRAME) & 1u;
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
                if ((psx_mod_read_half(pk + 0x16u) & 0xFu) < DV_FB_TPAGE_COLUMNS) continue;
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
