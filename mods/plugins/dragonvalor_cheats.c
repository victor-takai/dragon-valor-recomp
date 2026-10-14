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
 * Addresses come from docs/DEVELOPMENT.md, "Known RAM addresses (USA EXE)".
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "mod_plugins.h"

#define DV_HP       0x8008A5AEu   /* u16 current HP            */
#define DV_HP_MAX   0x8008A5B0u   /* u16 maximum HP            */
#define DV_MP       0x8008A5B2u   /* u16 current MP            */
#define DV_MP_MAX   0x8008A5B4u   /* u16 maximum MP            */
#define DV_VAL      0x800A2DD0u   /* u16 Val (gold), HUD shows 4 digits */
#define DV_VAL_CAP  9999u
#define DV_STAT_CAP 999u          /* HUD shows three digits    */

#define DV_PLUGIN_ID "dragonvalor.cheats"

static int g_infinite_hp, g_infinite_mp;
static uint16_t g_val_floor;   /* 0 = off */

static int dv_opt_bool(const char* id)
{
    char text[8];
    if (!psx_mod_current_option_value(id, text, sizeof text)) return 0;
    return text[0] == 't' || text[0] == '1';
}

static uint16_t dv_opt_val(const char* id)
{
    char text[16];
    unsigned long v;
    if (!psx_mod_current_option_value(id, text, sizeof text)) return 0;
    v = strtoul(text, NULL, 10);
    return (uint16_t)(v > DV_VAL_CAP ? DV_VAL_CAP : v);
}

static void dv_cheats_read_options(void)
{
    g_infinite_hp = dv_opt_bool("infinite_hp");
    g_infinite_mp = dv_opt_bool("infinite_mp");
    g_val_floor = dv_opt_val("val_floor");
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

static void dv_cheats_vblank(void)
{
    if (!(g_infinite_hp || g_infinite_mp || g_val_floor)) return;
    if (!dv_hero_loaded()) return;
    if (g_infinite_hp) dv_top_up(DV_HP, DV_HP_MAX);
    if (g_infinite_mp) dv_top_up(DV_MP, DV_MP_MAX);
    /* Val floor: shopping still works, the purse just never drops below it. */
    if (g_val_floor && psx_mod_read_half(DV_VAL) < g_val_floor) psx_mod_write_half(DV_VAL, g_val_floor);
}

PSX_MOD_CONSTRUCTOR(dv_register_cheat_plugins)
{
    int ok = psx_mod_register_activation_plugin(DV_PLUGIN_ID, dv_cheats_activate)
          && psx_mod_register_vblank_plugin(DV_PLUGIN_ID, dv_cheats_vblank)
          && psx_mod_register_option_changed_plugin(DV_PLUGIN_ID, dv_cheats_option_changed);
    if (!ok) fprintf(stderr, "dragonvalor.cheats: plugin failed to register\n");
}
