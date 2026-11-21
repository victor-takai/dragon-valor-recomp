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

#include "cpu_state.h"
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

/* --- trips: the in-game menu's Warp > Secret stage --------------------------
 * The menu sets dv_trip_area / dv_trip_stage / dv_trip_map and then
 * dv_trip_request. At the first quiet frame of an action stage (plain play:
 * mode 8 sub-mode 7, no pause, no event script, no stage or area load, every
 * load row idle) the hook below calls the game's own ModeTransition
 * (0x80025264, object native 3 / event native 2), as a stage exit would:
 * kind 0 (area and stage deltas, the module kept; kind 1, the stage delta
 * alone, inside the area) for an action or scene stage, kind 3 (deltas,
 * MAP.BIN) for a world map. The game then resets the
 * stage, loads the area header and the stage, and asks for the other disc
 * itself when the area needs it (decomp src/main/objscript.c).
 *
 * The call is made from the entry of ModelWeldReset (0x80021CE4, already in
 * game.toml [recompiler] mod_function_entry_funcs: FrameDraw calls it each
 * frame, after the frame's objects ran and before DrawOTag), which is where
 * a script's exit would also have left the frame. The arguments live in four
 * bytes of expansion memory (psx_mod_alloc_guest_memory). A loaded save state
 * cancels a pending trip. */
#define DV_MODEL_WELD_RESET 0x80021CE4u
#define DV_MODE_TRANSITION  0x80025264u
#define DV_MODE             0x8008634Cu   /* g_GameFlow.mode, +1 subMode */
#define DV_PAUSE_STATE      0x8007D826u   /* g_PauseState */
#define DV_EVENT_RUNNING    0x8009F9F8u   /* g_EventVm.running */
#define DV_AREA_PHASE       0x800A2D14u   /* g_GameState.areaPhase */
#define DV_STAGE_PHASE      0x800A2D18u   /* g_GameState.stagePhase */
#define DV_LOAD_ROWS        0x800A2B84u   /* g_GameState.row[10], 0x28 bytes each */
#define DV_STAGE_AREA       0x800A2F18u   /* g_StageSelect.area (s16) */
#define DV_STAGE_STAGE      0x800A2F1Au   /* g_StageSelect.stage (s16) */

volatile int dv_trip_request = 0;   /* set by the menu; cleared when the trip starts */
volatile int dv_trip_area = 0, dv_trip_stage = 0, dv_trip_map = 0;
static uint32_t g_trip_args;        /* guest address of { kind, area delta, stage delta, 0 } */

static int dv_trip_quiet(void)
{
    uint32_t r, c;
    if (psx_mod_read_byte(DV_MODE) != 8 || psx_mod_read_byte(DV_MODE + 1) != 7 ||
        psx_mod_read_byte(DV_PAUSE_STATE) || psx_mod_read_word(DV_EVENT_RUNNING) ||
        psx_mod_read_word(DV_AREA_PHASE) || psx_mod_read_word(DV_STAGE_PHASE) || !dv_hero_loaded())
        return 0;
    for (r = 0; r < 10; r++)
        for (c = 0; c < 2; c++)
            if (psx_mod_read_byte(DV_LOAD_ROWS + r * 0x28 + 0x10 + c)) return 0;
    return 1;
}

static void dv_trip_frame(struct CPUState* cpu, uint32_t address)
{
    int da, ds, kind;
    (void)address;
    if (!dv_trip_request || !dv_trip_quiet()) return;
    if (!g_trip_args) g_trip_args = psx_mod_alloc_guest_memory(4, 4);
    dv_trip_request = 0;
    if (!g_trip_args) {
        fprintf(stderr, "%s: trip: no guest memory for the arguments\n", DV_PLUGIN_ID);
        return;
    }
    da = dv_trip_area - (int16_t)psx_mod_read_half(DV_STAGE_AREA);
    ds = dv_trip_stage - (int16_t)psx_mod_read_half(DV_STAGE_STAGE);
    if (da < -128 || da > 127 || ds < -128 || ds > 127) return;
    kind = dv_trip_map ? 3 : da ? 0 : 1;
    psx_mod_write_byte(g_trip_args, (uint8_t)kind);
    psx_mod_write_byte(g_trip_args + 1, (uint8_t)(int8_t)da);
    psx_mod_write_byte(g_trip_args + 2, (uint8_t)(int8_t)ds);
    psx_mod_write_byte(g_trip_args + 3, 0);
    fprintf(stderr, "%s: trip to %d/%d (ModeTransition kind %d)\n", DV_PLUGIN_ID, dv_trip_area, dv_trip_stage, kind);
    psx_mod_call_guest(cpu, DV_MODE_TRANSITION, cpu->gpr[31], 0, g_trip_args, 0, 0);
}

static void dv_cheats_savestate_loaded(void)
{
    dv_trip_request = 0;
}

PSX_MOD_CONSTRUCTOR(dv_register_cheat_plugins)
{
    int ok = psx_mod_register_activation_plugin(DV_PLUGIN_ID, dv_cheats_activate)
          && psx_mod_register_vblank_plugin(DV_PLUGIN_ID, dv_cheats_vblank)
          && psx_mod_register_option_changed_plugin(DV_PLUGIN_ID, dv_cheats_option_changed)
          && psx_mod_register_savestate_plugin(DV_PLUGIN_ID, dv_cheats_savestate_loaded)
          && psx_mod_register_function_entry_plugin(DV_PLUGIN_ID, DV_MODEL_WELD_RESET, dv_trip_frame);
    if (!ok) fprintf(stderr, "dragonvalor.cheats: plugin failed to register\n");
}
