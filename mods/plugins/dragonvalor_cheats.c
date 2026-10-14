/*
 * Dragon Valor (USA) cheats: trusted static plugins.
 *
 * Selected by mods/preloaded/packages/dragonvalor.cheats/<version>/manifest.toml
 * through its [[plugin]] ids. A package never carries native code; the
 * implementation lives here, linked into the game executable, and each
 * callback runs only while the launcher's resolved mod plan activates its id
 * (psxrecomp/docs/MOD_PACKAGES.md, "Trusted static plugins").
 *
 * Addresses come from docs/DEVELOPMENT.md, "Known RAM addresses (USA EXE)".
 * VBlank callbacks run from the emulated GPU VBlank, in guest time, so they
 * behave like a GameShark "every frame" code and are part of save states and
 * replays like any other guest-side change.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "mod_plugins.h"

#define DV_HP       0x8008A5AEu   /* u16 current HP            */
#define DV_HP_MAX   0x8008A5B0u   /* u16 maximum HP            */
#define DV_MP       0x8008A5B2u   /* u16 current MP            */
#define DV_MP_MAX   0x8008A5B4u   /* u16 maximum MP            */
#define DV_VAL      0x800A2DD0u   /* u16 Val (gold), HUD shows 4 digits */
#define DV_VAL_CAP  9999u
#define DV_STAT_CAP 999u          /* HUD shows three digits    */

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

static void dv_infinite_hp(void)
{
    if (dv_hero_loaded()) dv_top_up(DV_HP, DV_HP_MAX);
}

static void dv_infinite_mp(void)
{
    if (dv_hero_loaded()) dv_top_up(DV_MP, DV_MP_MAX);
}

/* Keeps Val at or above the feature's "amount" option: shopping still works,
 * the purse just never drops below the floor the player chose. The option is
 * read once per session, at activation (the plan is committed by then), not
 * on every VBlank. */
static uint16_t g_val_floor = DV_VAL_CAP;

static void dv_val_floor_activate(void)
{
    char text[16];
    unsigned long floor = DV_VAL_CAP;

    if (psx_mod_current_option_value("amount", text, sizeof text))
        floor = strtoul(text, NULL, 10);
    if (floor > DV_VAL_CAP) floor = DV_VAL_CAP;
    g_val_floor = (uint16_t)floor;
}

static void dv_val_floor(void)
{
    if (!dv_hero_loaded()) return;
    if (psx_mod_read_half(DV_VAL) < g_val_floor) psx_mod_write_half(DV_VAL, g_val_floor);
}

static void dv_register(const char* id, int ok)
{
    if (!ok) fprintf(stderr, "dragonvalor.cheats: plugin %s failed to register\n", id);
}

PSX_MOD_CONSTRUCTOR(dv_register_cheat_plugins)
{
    dv_register("dragonvalor.infinite-hp",
                psx_mod_register_vblank_plugin("dragonvalor.infinite-hp", dv_infinite_hp));
    dv_register("dragonvalor.infinite-mp",
                psx_mod_register_vblank_plugin("dragonvalor.infinite-mp", dv_infinite_mp));
    dv_register("dragonvalor.val-floor",
                psx_mod_register_activation_plugin("dragonvalor.val-floor", dv_val_floor_activate)
                && psx_mod_register_vblank_plugin("dragonvalor.val-floor", dv_val_floor));
}
