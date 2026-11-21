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
#include <string.h>

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

static void dv_cut_scene_english(void);

static void dv_cheats_vblank(void)
{
    if (!dv_cheats_hold_hp) g_hold_vblanks = 0;
    else if (++g_hold_vblanks > 3600u) dv_cheats_hold_hp = 0;
    dv_cut_scene_english();
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
 * a script's exit would also have left the frame. The four argument bytes go
 * to g_CdUnused8768 (0x80078768, 16 bytes of the CD object's .bss that no
 * code, data or script touches: decomp docs/OBJECTS.md section 9) and are
 * zeroed after the call. Not expansion memory: an allocation there changes
 * the save-state layout cookie (psx_mod_memory_layout_cookie), and every
 * state saved before it would be refused. A loaded save state cancels a
 * pending trip. */
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
#define DV_TRIP_ARGS        0x80078768u   /* g_CdUnused8768: { kind, area delta, stage delta, 0 } */

volatile int dv_trip_request = 0;   /* set by the menu; cleared when the trip starts */
volatile int dv_trip_area = 0, dv_trip_stage = 0, dv_trip_map = 0;

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
    dv_trip_request = 0;
    da = dv_trip_area - (int16_t)psx_mod_read_half(DV_STAGE_AREA);
    ds = dv_trip_stage - (int16_t)psx_mod_read_half(DV_STAGE_STAGE);
    if (da < -128 || da > 127 || ds < -128 || ds > 127) return;
    kind = dv_trip_map ? 3 : da ? 0 : 1;
    psx_mod_write_word(DV_TRIP_ARGS, (uint32_t)kind | (uint32_t)(uint8_t)(int8_t)da << 8 |
                                     (uint32_t)(uint8_t)(int8_t)ds << 16);
    fprintf(stderr, "%s: trip to %d/%d (ModeTransition kind %d)\n", DV_PLUGIN_ID, dv_trip_area, dv_trip_stage, kind);
    psx_mod_call_guest(cpu, DV_MODE_TRANSITION, cpu->gpr[31], 0, DV_TRIP_ARGS, 0, 0);
    psx_mod_write_word(DV_TRIP_ARGS, 0);
}

/* --- the cut scenes in English ---------------------------------------------
 * Two stages that only Warp > Secret stage reaches play event scripts whose
 * messages are the Japanese release's: kana in the cells where the US font
 * has its accented letters, kanji of a sheet the disc no longer has.
 *   0/5 room 1: a scene cut from the prologue (22 messages, file 80 entries
 *   4..25);
 *   1/24 room 1: an early draft of the scene where Clovis finds Princess
 *   Celia (the game has it as 1/15 room 10; 21 messages, file 80 entries
 *   4..24). It starts when the hero walks to the door at the end of the
 *   corridor (the room pack's object script: object native 10 within 1500
 *   units of (0, -4000)).
 * They were read and translated on 2026-11-21 (the Japanese, rebuilt, is in
 * the decomp's docs/SECRETS.md). The names are the US release's: Celia,
 * Raxis, Volef; the choice of 1/24 is worded as in 1/15.
 *
 * While a room's message table (g_EventVm.messages, op 62) still points at
 * the Japanese message 0, the VBlank below writes the English, in the US
 * font's glyph code (decomp docs/EVENT_SCRIPT.md, "Text": glyph = advance
 * << 10 | column << 5 | row, 21 cells a row; control words 0x8000 new line,
 * 0x8800 speed, 0x9000 pen, 0x9800 colour, 0xA000 a question's answers,
 * 0xFFFF end), at DV_TEXT_HOME and points the table at it. DV_TEXT_HOME is
 * main RAM above the stage's heap: the stage buffers end where the second
 * arena starts (GS_ARENA2, where the message window builds its display
 * list: 0x80188698 in 1/24), and the RAM stack only uses the top of RAM
 * under 0x801FFFF4 (the game runs on the scratchpad stack and switches there
 * for the CD, sound and object-script calls); from 0x801D1698 to 0x801FF698
 * RAM stayed zero in 1/24. The text is written only while the stage's arena
 * starts 64 KB or more below it. Main RAM only, so a save state taken during
 * a scene keeps the English and loads anywhere (no expansion memory: that
 * changes the save-state layout). */
#define DV_EVENT_MESSAGES   0x8009FA18u   /* g_EventVm.messages (u16 **) */
#define DV_EVENT_MSG_COUNT  0x8009FA1Cu   /* g_EventVm.messageCount */
#define DV_ARENA2           0x800A2CA0u   /* g_GameState.row[7].buffer[1] (GS_ARENA2) */
#define DV_TEXT_HOME        0x801E0000u
#define DV_TEXT_END         0x801F0000u   /* 64 KB clear of the RAM stack */

/* Message text: plain lines get {pos 88,188} first (the band's second line)
 * unless they set the pen themselves; "{color n}Name:" is a speaker's name.
 * {speed n} {pos x,y} {instant x,y} {color n} are the game's controls (the
 * decomp's evscript.py spells them the same way). */
static const char* const kCutScene[22] = {
    "...Elena?!",                                                            /* Raimun */
    "Answer me!\nElenaaa!!",
    "Who...\nWho could be so cruel...",
    "Was it you?\nCalling me...",                                            /* the dragon */
    "Wh-what is this thing?!",
    "(Th-this thing?!\nSuch insolence, from a worm...)",
    "I hear the voice of your heart.\n\"Kill, kill, kill...\" it says.\nDo you wish to avenge this girl?",
    "You bastard! What do you know?!",
    "(B-bastard, he calls me...)",
    "They say this girl was killed by\nher own brother. She said so\nbefore she died, I believe...",
    "C-Clovis did?!",
    "Hmm...\nThat was the name, I believe...",
    "......",
    "Hmm... You can't believe it?",
    "Clo...vis...",
    "(He'll make a fine pawn\nfor a while...)",
    "Tell me... Do you wish to\nkill Clovis?",
    "......?!",
    "If that is your heart's desire,\nI could lend you my power.\nI have my own score to settle...",
    "Elena...\nIf it will avenge you...\nI would sell my soul to a demon...",
    "{color 2}Raimun:",                                                      /* speaker names */
    "{color 3}Dragon:",
};

static const char* const kCelia[21] = {
    "......?!{speed 30}\nT{speed 0}hat's strange...\nOnly this door is locked...",                  /* Clovis */
    "{pos 88,176}<What to do?>{speed 30}{instant 120,188}{color 3}B{speed 0}reak down the door.\n"
        "Ignore it and move on.",                                                                     /* the choice */
    "Monsters! It's no use hiding!",                                                                  /* broken down */
    "...Oh, never mind.{speed 30}\nF{speed 0}inding the dragon comes first.",                       /* moved on */
    "......?!",
    "Who is there?\nWhat is all this noise...",                                                      /* the girl */
    "What? Who are you?\nStay in a place like this and\nthe monsters will kill you.",
    "Oh...?!{speed 30}\nY{speed 0}ou are not one of Volef's men?",
    "Then forgive my rudeness.\nI am Celia, first princess of\nthe Kingdom of Raxis.",
    "I know not who you are, but\nI thank you for saving me.",
    "......{speed 30}\n({speed 0}I didn't exactly come here\nto save her...)",
    "I've killed every monster here.\nSorry, but get yourself out.\nI still have work to do...",
    "But you, all alone...{speed 10}\nO{speed 0}ur kingdom's knights will help.\nTogether, let us defeat Volef.",
    "Heh heh heh...{speed 10}\nC{speed 0}an those knights of yours\nbeat a dragon?",
    "......?!",
    "My foe isn't small fry like Volef.\nIt's the dragon in this castle.\n...I can squeeze it out of Volef.",
    "A dragon...{speed 30}\n.{speed 0}..I see.{speed 30}\nT{speed 0}hen, sir swordsman... Good luck.",
    "Yeah. You too...",
    "{color 2}Clovis:",                                                                              /* speaker names */
    "{color 1}Girl:",
    "{color 1}Celia:",
};

/* The rooms, known by their Japanese message 0: {pos 88,188}...Elena?! in
 * kana (0/5) and {pos 88,188}......?! (1/24). */
static const struct {
    int16_t area, stage;
    uint32_t count;
    uint16_t jp0[4];
    const char* const* text;
} kScenes[] = {
    {0, 5, 22, {0x90B2, 0x31EC, 0x3288, 0x320A}, kCutScene},
    {1, 24, 21, {0x90B2, 0x31EC, 0x31EC, 0x2564}, kCelia},
};

/* US font cells of ASCII 0x20..0x7E: {row x 21 + column, advance}; 0xFFFF none. */
static const struct { uint16_t cell; uint8_t advance; } kAscii[95] = {
    {0, 6}, {19, 5}, {14, 7}, {96, 12}, {97, 11}, {98, 12}, {99, 12}, {13, 4},
    {1, 6}, {2, 6}, {31, 9}, {32, 12}, {11, 4}, {33, 12}, {12, 4}, {35, 13},
    {21, 7}, {22, 6}, {23, 8}, {24, 8}, {25, 8}, {26, 8}, {27, 8}, {28, 8},
    {29, 8}, {30, 8}, {15, 4}, {16, 4}, {101, 12}, {34, 12}, {102, 12}, {95, 7},
    {42, 12}, {43, 10}, {44, 9}, {45, 10}, {46, 10}, {47, 9}, {48, 9}, {49, 10},
    {50, 10}, {51, 5}, {52, 7}, {53, 10}, {54, 9}, {55, 12}, {56, 11}, {57, 11},
    {58, 9}, {59, 11}, {60, 9}, {61, 8}, {62, 9}, {63, 10}, {64, 10}, {65, 12},
    {66, 10}, {67, 9}, {68, 9}, {3, 5}, {0xFFFF, 0}, {4, 5}, {18, 6}, {103, 13},
    {0xFFFF, 0}, {69, 8}, {70, 8}, {71, 7}, {72, 8}, {73, 7}, {74, 6}, {75, 8},
    {76, 9}, {77, 5}, {78, 6}, {79, 8}, {80, 5}, {81, 11}, {82, 8}, {83, 7},
    {84, 8}, {85, 8}, {86, 7}, {87, 6}, {88, 6}, {89, 7}, {90, 8}, {91, 10},
    {92, 8}, {93, 9}, {94, 7}, {0xFFFF, 0}, {17, 4}, {0xFFFF, 0}, {36, 12},
};

/* A pen position as the {pos} / {instant} words hold it: (x / 8) << 4 | (y - 176) / 6. */
static unsigned dv_pen(unsigned x, unsigned y)
{
    return (x / 8u & 0x3Fu) << 4 | ((y - 176u) / 6u & 0xFu);
}

/* One message as the game codes it, at addr (0: only measured); the end. */
static uint32_t dv_encode_message(uint32_t addr, const char* s)
{
    uint32_t a = addr, n = 0;
    const int name = strncmp(s, "{color", 6) == 0;
#define DV_PUT(w) do { if (addr) psx_mod_write_half(a, (uint16_t)(w)); a += 2; n++; } while (0)
    if (!name && strncmp(s, "{pos", 4) != 0) DV_PUT(0x90B2);   /* {pos 88,188} */
    for (; *s; s++) {
        const unsigned c = (unsigned char)*s;
        unsigned cell, adv, x = 0, y = 0;
        if (c == '{') {
            const char* close = strchr(s, '}');
            if (!close) break;
            if (sscanf(s, "{speed %u}", &x) == 1) DV_PUT(0x8800 | (x & 0xFFu));
            else if (sscanf(s, "{pos %u,%u}", &x, &y) == 2) DV_PUT(0x9000 | dv_pen(x, y));
            else if (sscanf(s, "{instant %u,%u}", &x, &y) == 2) DV_PUT(0xA000 | dv_pen(x, y));
            else if (sscanf(s, "{color %u}", &x) == 1) DV_PUT(0x9800 | (x & 7u));
            s = close;
            continue;
        }
        if (c == '\n') { DV_PUT(0x8000); continue; }
        if (c < 0x20 || c > 0x7E || kAscii[c - 0x20].cell == 0xFFFF) continue;
        cell = kAscii[c - 0x20].cell;
        adv = (name && c == ':') ? 6u : kAscii[c - 0x20].advance;   /* the names' colon: {w6} */
        DV_PUT(adv << 10 | (cell % 21) << 5 | cell / 21);
    }
    DV_PUT(0xFFFF);
    if (n & 1) DV_PUT(0);                      /* 4-byte entries */
#undef DV_PUT
    return a;
}

static int dv_ram_pointer(uint32_t p) { return (p & 0xFFE00001u) == 0x80000000u; }

static void dv_cut_scene_english(void)
{
    const int16_t area = (int16_t)psx_mod_read_half(DV_STAGE_AREA);
    const int16_t stage = (int16_t)psx_mod_read_half(DV_STAGE_STAGE);
    const uint32_t count = psx_mod_read_word(DV_EVENT_MSG_COUNT);
    const uint32_t scenes = sizeof kScenes / sizeof kScenes[0];
    uint32_t table, m0, arena2, a, need = 0, i, k;
    for (k = 0; k < scenes; k++)
        if (kScenes[k].area == area && kScenes[k].stage == stage && kScenes[k].count == count) break;
    if (k == scenes) return;
    table = psx_mod_read_word(DV_EVENT_MESSAGES);
    if (!dv_ram_pointer(table) || !dv_ram_pointer(m0 = psx_mod_read_word(table))) return;
    for (i = 0; i < 4; i++)
        if (psx_mod_read_half(m0 + 2u * i) != kScenes[k].jp0[i]) return;   /* another room, or done */
    arena2 = psx_mod_read_word(DV_ARENA2);
    if (!dv_ram_pointer(arena2) || arena2 > DV_TEXT_HOME - 0x10000u) return;   /* the stage's heap is that high */
    for (i = 0; i < count; i++) need += dv_encode_message(0, kScenes[k].text[i]);
    if (DV_TEXT_HOME + need > DV_TEXT_END) return;
    for (a = DV_TEXT_HOME, i = 0; i < count; i++) {
        psx_mod_write_word(table + 4u * i, a);
        a = dv_encode_message(a, kScenes[k].text[i]);
    }
    fprintf(stderr, "%s: %d/%d's scene in English (%u bytes at %08X)\n", DV_PLUGIN_ID, area, stage,
            (unsigned)(a - DV_TEXT_HOME), (unsigned)DV_TEXT_HOME);
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
