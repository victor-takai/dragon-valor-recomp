/*
 * Dragon Valor (USA) cheats: trusted static plugin.
 *
 * Selected by mods/preloaded/packages/dragonvalor.cheats/<version>/manifest.toml
 * through its [[plugin]] id. A package never carries native code; the
 * implementation lives here, linked into the game executable, and runs only
 * while the launcher's resolved mod plan activates the "cheats" feature
 * (psxrecomp/docs/MOD_PACKAGES.md, "Trusted static plugins").
 *
 * The feature is on by default with every option off, so the plugin is
 * always in the plan and the in-game menu can switch the options live
 * through the option-changed callback (psx_mod_set_option_live). Each
 * option is one GameShark-style "every frame" write from the VBlank callback.
 * One-shot edits (Val, stats, magic levels, items) are not options: the
 * in-game menu writes them straight into RAM (mods/plugins/dragonvalor_menu.cpp).
 * Addresses come from docs/DEVELOPMENT.md, "Known RAM addresses (USA EXE)"
 * and "How drops are decided".
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "mod_plugins.h"

#define DV_HP       0x8008A5AEu   /* u16 current HP            */
#define DV_HP_MAX   0x8008A5B0u   /* u16 maximum HP            */
#define DV_MP       0x8008A5B2u   /* u16 current MP            */
#define DV_MP_MAX   0x8008A5B4u   /* u16 maximum MP            */
#define DV_STAT_CAP 999u          /* HUD shows three digits    */

/* Drop rule: an enemy killed by the sword leaves a gem once the sword-kill
 * counter reaches the gem table's entry for the gems already collected; the
 * magic-kill counter and the scroll table work the same way for scrolls.
 * Picking the item up subtracts that threshold, so the counters count kills
 * between drops. The game also stops at 8 gems, 5 scrolls, 10 of both, and
 * never drops a second one while one lies on the floor. */
#define DV_SWORD_KILLS   0x800A2DB0u  /* u32 sword kills since the last gem    */
#define DV_MAGIC_KILLS   0x800A2DB4u  /* u32 magic kills since the last scroll */
#define DV_GEMS          0x800A2EDCu  /* u16 gems collected                    */
#define DV_SCROLLS       0x800A2EDEu  /* u16 scrolls collected                 */
#define DV_GEMS_SCROLLS  0x800A2EE0u  /* u16 both                              */
#define DV_GEM_TABLE     0x8006CAA8u  /* u16[8] 15, 22, 28, 36, 48, 70, 105, 200 */
#define DV_SCROLL_TABLE  0x8006CAB8u  /* u16[5] 2, 4, 7, 12, 20                */
#define DV_GEM_CAP       8u
#define DV_SCROLL_CAP    5u
#define DV_BOTH_CAP      10u

#define DV_PLUGIN_ID "dragonvalor.cheats"

static int g_infinite_hp, g_infinite_mp, g_gem_every, g_scroll_every;

/* The in-game menu's "Go now" (mods/plugins/dragonvalor_menu.cpp) sets this
 * while it takes the hero through a Game Over to reload the stage in the
 * chosen area: Infinite HP must leave the HP at 0 meanwhile. The menu clears
 * it when the reload is done; the VBlank count is a safety net (60 s). */
volatile int dv_cheats_hold_hp = 0;
static uint32_t g_hold_vblanks;

static int dv_opt_bool(const char* id)
{
    char text[8];
    if (!psx_mod_current_option_value(id, text, sizeof text)) return 0;
    return text[0] == 't' || text[0] == '1';
}

static int dv_opt_int(const char* id)
{
    char text[16];
    if (!psx_mod_current_option_value(id, text, sizeof text)) return 0;
    return (int)strtol(text, NULL, 10);
}

static void dv_cheats_read_options(void)
{
    g_infinite_hp  = dv_opt_bool("infinite_hp");
    g_infinite_mp  = dv_opt_bool("infinite_mp");
    g_gem_every    = dv_opt_int("gem_every");
    g_scroll_every = dv_opt_int("scroll_every");
}

/* Session start: the committed plan's option values. */
static void dv_cheats_activate(void)
{
    dv_cheats_read_options();
}

/* In-game menu: psx_mod_current_option_value already answers the new value. */
static int dv_cheats_option_changed(const char* option_id, const char* value)
{
    (void)option_id; (void)value;
    dv_cheats_read_options();
    return 1;
}

/* The stats block is zero until a hero is loaded (boot, title, menus, FMV),
 * so a zero maximum keeps every cheat inert outside gameplay. */
static int dv_hero_loaded(void)
{
    return psx_mod_game_started() && psx_mod_read_half(DV_HP_MAX) != 0;
}

static void dv_top_up(uint32_t cur_addr, uint32_t max_addr)
{
    uint16_t max = psx_mod_read_half(max_addr);
    if (max == 0 || max > DV_STAT_CAP) return;
    if (psx_mod_read_half(cur_addr) < max) psx_mod_write_half(cur_addr, max);
}

/* Keep a kill counter at least (threshold - every), so the game's own drop
 * code fires on the every-th kill of that kind. After the pickup subtracts
 * the threshold the floor is raised again on the next frame. The game's
 * caps are left in force: past them the table is not consulted any more. */
static void dv_drop_floor(uint32_t counter, uint32_t table, uint32_t collected,
                          uint32_t cap, int every)
{
    if (every <= 0 || collected >= cap) return;
    if (psx_mod_read_half(DV_GEMS_SCROLLS) >= DV_BOTH_CAP) return;
    uint32_t threshold = psx_mod_read_half(table + 2u * collected);
    if (threshold == 0 || threshold > 1000u) return;   /* not the table we know */
    int32_t floor = (int32_t)threshold - every;
    if (floor < 0) floor = 0;
    if ((int32_t)psx_mod_read_word(counter) < floor)
        psx_mod_write_word(counter, (uint32_t)floor);
}

static void dv_cheats_vblank(void)
{
    if (!dv_cheats_hold_hp) g_hold_vblanks = 0;
    else if (++g_hold_vblanks > 3600u) dv_cheats_hold_hp = 0;
    if (!(g_infinite_hp || g_infinite_mp || g_gem_every || g_scroll_every)) return;
    if (!dv_hero_loaded()) return;
    if (g_infinite_hp && !dv_cheats_hold_hp) dv_top_up(DV_HP, DV_HP_MAX);
    if (g_infinite_mp) dv_top_up(DV_MP, DV_MP_MAX);
    if (g_gem_every)
        dv_drop_floor(DV_SWORD_KILLS, DV_GEM_TABLE, psx_mod_read_half(DV_GEMS),
                      DV_GEM_CAP, g_gem_every);
    if (g_scroll_every)
        dv_drop_floor(DV_MAGIC_KILLS, DV_SCROLL_TABLE, psx_mod_read_half(DV_SCROLLS),
                      DV_SCROLL_CAP, g_scroll_every);
}

PSX_MOD_CONSTRUCTOR(dv_register_cheat_plugins)
{
    int ok = psx_mod_register_activation_plugin(DV_PLUGIN_ID, dv_cheats_activate)
          && psx_mod_register_vblank_plugin(DV_PLUGIN_ID, dv_cheats_vblank)
          && psx_mod_register_option_changed_plugin(DV_PLUGIN_ID, dv_cheats_option_changed);
    if (!ok) fprintf(stderr, "dragonvalor.cheats: plugin failed to register\n");
}
