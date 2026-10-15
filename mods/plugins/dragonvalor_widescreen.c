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
 * Nothing is written to guest RAM. Everything is inert unless the package is
 * enabled: activation is the only thing that sets s_enabled, and the
 * predicate checks it first.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

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
          && psx_mod_register_savestate_plugin(DV_WS_PLUGIN, dv_ws_savestate_loaded);
    if (!ok) fprintf(stderr, "dragonvalor.widescreen: plugin failed to register\n");
}
