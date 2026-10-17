/*
 * Dragon Valor (USA) in-game menu: the title's PsxMenuProvider.
 *
 * psxrecomp owns the contract (psx_menu_host.h): the runtime pauses the guest,
 * applies video settings live, records mod changes and runs disc changes; the
 * provider only draws a menu and translates input. recomp-ui owns the menu
 * model and its Dear ImGui presentation (recomp_runtime_ui.h): sections, rows,
 * navigation, status line. This file is the glue: it registers the provider
 * (static initializer, before the game window opens), opens on Escape or
 * Select+Start, routes keyboard/gamepad input into the model, feeds the mouse
 * to ImGui (hover selects, click activates, right button = back, wheel
 * scrolls) and maps the rows to the host.
 *
 * Threads: attach/event/is_open/set_open run on the main thread, draw on the
 * thread that owns the GL context (the main thread unless the render thread
 * is on). The model is used from both, under one mutex; the ImGui context is
 * touched by draw only (no platform backend: the model navigates itself).
 */
#if defined(RECOMP_LAUNCHER)

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

#include "psx_sdl.h"
#include "imgui.h"
#include "imgui_impl_opengl3.h"

#include "recomp_runtime_ui.h"
#include "psx_menu_host.h"
#include "host_osd.h"
#include "mod_plugins.h"
#include "savestate.h"
#include "video_texture_filter.h"

namespace {

/* ---- rows of our own, appended to the standard ones ----------------------- */
const char* const kTextureFilterChoices[] = {"Nearest", "Bilinear", "xBR"};
const int kTextureFilterValues[] = {PSX_TEXTURE_FILTER_NEAREST, PSX_TEXTURE_FILTER_BILINEAR,
                                    PSX_TEXTURE_FILTER_XBR};

#define DV_KEY_TEXFILTER   "dv.graphics.texture_filter"
#define DV_KEY_SCANLINES   "dv.graphics.scanlines"
#define DV_KEY_DYNRES      "dv.graphics.dynamic_resolution"
#define DV_KEY_CHEAT_HP    "dv.cheats.infinite_hp"
#define DV_KEY_CHEAT_MP    "dv.cheats.infinite_mp"
#define DV_KEY_GEM_EVERY   "dv.cheats.gem_every"
#define DV_KEY_SCROLL_EVERY "dv.cheats.scroll_every"
#define DV_KEY_SKIP_INTRO  "dv.game.skip_intro"
#define DV_KEY_DISC1       "dv.disc.1"
#define DV_KEY_DISC2       "dv.disc.2"
#define DV_KEY_GO_NOW      "dv.warp.go"

/* ---- live RAM editors: a row shows the value in guest RAM and writes it ----
 * Addresses: docs/DEVELOPMENT.md, "Known RAM addresses (USA EXE)". The rows
 * are enabled only while a hero is loaded (HPmax != 0); the guest is paused
 * while the menu is up, so a plain write is a one-shot cheat, not a per-frame
 * lock. Keyboard/pad activations run on the main thread; a mouse click
 * activates from the draw callback (the GL thread when [video] render_thread
 * is on; off by default), which is fine while "Pause in menu" holds the
 * guest. */
struct RamRow { const char* key; uint32_t addr; int width; };
#define DV_RAM_VAL "dv.ram.val"
const RamRow kRamRows[] = {
    {DV_RAM_VAL, 0x800A2DD0u, 2},        {"dv.ram.hpmax", 0x8008A5B0u, 2},
    {"dv.ram.mpmax", 0x8008A5B4u, 2},    {"dv.ram.str", 0x8008A5B6u, 2},
    {"dv.ram.def", 0x8008A5B8u, 2},
    {"dv.ram.area", 0x800A2F18u, 2},   /* area the next stage load reads (save block +0x1F8) */
    {"dv.ram.stage", 0x8007D9C6u, 2},  /* stage the next Continue loads (stage-start record +0xF6) */
    {"dv.ram.magic0", 0x800A2E06u, 1}, {"dv.ram.magic1", 0x800A2E07u, 1},
    {"dv.ram.magic2", 0x800A2E08u, 1}, {"dv.ram.magic3", 0x800A2E09u, 1},
    {"dv.ram.magic4", 0x800A2E0Au, 1}, {"dv.ram.magic5", 0x800A2E0Bu, 1},
    {"dv.ram.magic6", 0x800A2E0Cu, 1}, {"dv.ram.magic7", 0x800A2E0Du, 1},
    {"dv.ram.item0", 0x800A2DD4u, 2},  {"dv.ram.item1", 0x800A2DD6u, 2},
    {"dv.ram.item2", 0x800A2DD8u, 2},  {"dv.ram.item3", 0x800A2DDAu, 2},
    {"dv.ram.item4", 0x800A2DDCu, 2},  {"dv.ram.item5", 0x800A2DDEu, 2},
    {"dv.ram.item6", 0x800A2DF0u, 2},  {"dv.ram.item7", 0x800A2DF2u, 2},
    {"dv.ram.item8", 0x800A2DF4u, 2},  {"dv.ram.item9", 0x800A2DF6u, 2},
    {"dv.ram.item10", 0x800A2DF8u, 2}, {"dv.ram.item11", 0x800A2DFAu, 2},
    {"dv.ram.item12", 0x800A2DFCu, 2}, {"dv.ram.item13", 0x800A2DFEu, 2},
    {"dv.ram.item14", 0x800A2E00u, 2}, {"dv.ram.item15", 0x800A2E02u, 2},
    {"dv.ram.item16", 0x800A2E04u, 2},
};
const char* const kMagicChoices[] = {"Not learned", "Level 1", "Level 2", "Level 3"};

#define DV_ROW_INT(key, section, label, desc, lo, hi, step) \
    {key, section, label, desc, RECOMP_RUNTIME_UI_INT, lo, hi, step, nullptr, 0, nullptr}
#define DV_ROW_MAGIC(key, label, desc) \
    {key, "Magic", label, desc, RECOMP_RUNTIME_UI_CHOICE, 0, 3, 1, kMagicChoices, 4, nullptr}
#define DV_ROW_ITEM(key, label, desc) \
    DV_ROW_INT(key, "Items", label, desc, 0, 99, 1)

const RecompRuntimeUiItem kExtraItems[] = {
    {DV_KEY_TEXFILTER, "Graphics", "Texture filtering",
     "Nearest is the PS1 look; xBR smooths diagonals at high resolution.",
     RECOMP_RUNTIME_UI_CHOICE, 0, 2, 1, kTextureFilterChoices, 3, kTextureFilterValues},
    {DV_KEY_SCANLINES, "Graphics", "Scanlines", "Draw CRT-style scanlines over the picture.",
     RECOMP_RUNTIME_UI_BOOL, 0, 1, 1, nullptr, 0, nullptr},
    {DV_KEY_DYNRES, "Graphics", "Dynamic resolution",
     "Lower the internal resolution when a frame runs late.",
     RECOMP_RUNTIME_UI_BOOL, 0, 1, 1, nullptr, 0, nullptr},
    {DV_KEY_CHEAT_HP, "Cheats", "Infinite HP", "HP never drops.",
     RECOMP_RUNTIME_UI_BOOL, 0, 1, 1, nullptr, 0, nullptr},
    {DV_KEY_CHEAT_MP, "Cheats", "Infinite MP", "MP never drops.",
     RECOMP_RUNTIME_UI_BOOL, 0, 1, 1, nullptr, 0, nullptr},
    {DV_KEY_GEM_EVERY, "Cheats", "Gem every N sword kills",
     "0 = game rule; N = one every N kills.",
     RECOMP_RUNTIME_UI_INT, 0, 15, 1, nullptr, 0, nullptr},
    {DV_KEY_SCROLL_EVERY, "Cheats", "Scroll every N magic kills",
     "0 = game rule; N = one every N kills.",
     RECOMP_RUNTIME_UI_INT, 0, 5, 1, nullptr, 0, nullptr},
    DV_ROW_INT(DV_RAM_VAL, "Cheats", "Val", "Your gold.", 0, 9999, 100),
    DV_ROW_INT("dv.ram.hpmax", "Cheats", "Max HP", "Maximum HP.", 1, 999, 10),
    DV_ROW_INT("dv.ram.mpmax", "Cheats", "Max MP", "Maximum MP.", 1, 999, 10),
    DV_ROW_INT("dv.ram.str", "Cheats", "STR", "Attack power.", 0, 999, 5),
    DV_ROW_INT("dv.ram.def", "Cheats", "DEF", "Defense.", 0, 999, 5),
    DV_ROW_INT("dv.ram.area", "Cheats", "Area (next Continue)",
               "Area the next GAME OVER > Continue loads (0-1 Clovis, 3 Kodel). Areas of this disc only; if the stage below is not an action stage there, it moves to the nearest one that is. Then use Go now below.", 0, 12, 1),
    DV_ROW_INT("dv.ram.stage", "Cheats", "Stage (next Continue)",
               "Stage the next Continue loads in the area above; only action stages are offered. A stage that opens with a story scene shows that scene's text as garbage after the jump, but the scene plays and can move the story on.", 0, 47, 1),
    {DV_KEY_GO_NOW, "Cheats", "Go now (Game Over)",
     "Ends the stage with a Game Over and answers Continue for you: the area and stage above load in about 10 s. The pad is ignored meanwhile (a dialogue on screen holds the Game Over back until you finish it) and Infinite HP pauses; the 50 Val the Continue costs is given back.",
     RECOMP_RUNTIME_UI_ACTION, 0, 0, 0, nullptr, 0, nullptr},
    DV_ROW_MAGIC("dv.ram.magic0", "Heal", "Recovers HP and damages undead."),
    DV_ROW_MAGIC("dv.ram.magic1", "Defense", "Raises defense and weapon range."),
    DV_ROW_MAGIC("dv.ram.magic2", "Fire", "Fireball; also ignites devices."),
    DV_ROW_MAGIC("dv.ram.magic3", "Ice", "Ice attack; also freezes flame platforms."),
    DV_ROW_MAGIC("dv.ram.magic4", "Thunder", "Four thunder strikes; also electrifies devices."),
    DV_ROW_MAGIC("dv.ram.magic5", "Mine", "Three controlled mines."),
    DV_ROW_MAGIC("dv.ram.magic6", "Stealth", "Invisibility; enemies stop attacking."),
    DV_ROW_MAGIC("dv.ram.magic7", "Vortex", "Traps enemies in a controlled vortex."),
    DV_ROW_ITEM("dv.ram.item0", "Pendant", "Max HP +1 each, applied when found."),
    DV_ROW_ITEM("dv.ram.item1", "Lithograph", "Max MP +1 each, applied when found."),
    DV_ROW_ITEM("dv.ram.item2", "Ring", "Attack power up, applied when found."),
    DV_ROW_ITEM("dv.ram.item3", "Talisman", "Defense +1 each, applied when found."),
    DV_ROW_ITEM("dv.ram.item4", "Bracelet", "Reduces fatigue, applied when found."),
    DV_ROW_ITEM("dv.ram.item5", "Earrings", "Each one makes every spell cost 1 MP less."),
    DV_ROW_ITEM("dv.ram.item6", "Pepper", "Spice, traded at shops."),
    DV_ROW_ITEM("dv.ram.item7", "Wine", "Drink, traded at shops."),
    DV_ROW_ITEM("dv.ram.item8", "Horn", "Handmade item, traded at shops."),
    DV_ROW_ITEM("dv.ram.item9", "Perfume", "Scented herb, traded at shops."),
    DV_ROW_ITEM("dv.ram.item10", "Tea", "Special item, traded at shops."),
    DV_ROW_ITEM("dv.ram.item11", "Mask", "Handmade item, traded at shops."),
    DV_ROW_ITEM("dv.ram.item12", "Tiara", "Jeweled item, traded at shops."),
    DV_ROW_ITEM("dv.ram.item13", "Harp", "Royal instrument, traded at shops."),
    DV_ROW_ITEM("dv.ram.item14", "Gold Key", "Opens locked doors. Raising it also takes this stage's gold keys, which is what the doors check."),
    DV_ROW_ITEM("dv.ram.item15", "Silver Key", "Opens locked doors. Raising it also takes this stage's silver keys, which is what the doors check."),
    DV_ROW_ITEM("dv.ram.item16", "Copper Key", "Opens locked doors. Raising it also takes this stage's copper keys, which is what the doors check."),
    {DV_KEY_SKIP_INTRO, "Game", "Skip intro movie",
     "Press Start for you when the intro movie starts. Takes effect at the next start.",
     RECOMP_RUNTIME_UI_BOOL, 0, 1, 1, nullptr, 0, nullptr},
    {DV_KEY_DISC1, "Game", "Insert disc 1", "Swap to Dragon Valor disc 1.",
     RECOMP_RUNTIME_UI_ACTION, 0, 0, 0, nullptr, 0, nullptr},
    {DV_KEY_DISC2, "Game", "Insert disc 2", "Swap to Dragon Valor disc 2.",
     RECOMP_RUNTIME_UI_ACTION, 0, 0, 0, nullptr, 0, nullptr},
};

#define DV_CHEATS_PKG   "dragonvalor.cheats"
#define DV_CHEATS_FEAT  "cheats"
#define DV_SKIP_PKG     "dragonvalor.skip-intro"
#define DV_SKIP_FEAT    "skip-intro"

struct Menu {
    std::mutex mu;
    const PsxMenuHost* host = nullptr;
    RecompRuntimeUi* ui = nullptr;
    bool imgui = false;
    bool open = false;
    /* host-side policy rows */
    bool pause_in_menu = true;
    int dim = RECOMP_RUNTIME_UI_DEFAULT_DIM_PERCENT, opacity = 100;
    int slot = 1;                  /* 1-based state slot shown; runtime slot = slot - 1 */
    /* mirrors of the mod selection (the catalogue is the host's) */
    int cheat_hp = 0, cheat_mp = 0, skip_intro = 0;
    int gem_every = 0, scroll_every = 0;   /* integer options of the same feature */
    /* gamepad combo */
    bool pad_start = false, pad_back = false, combo_fired = false;
    /* mouse: window coordinates are scaled to the framebuffer ImGui draws in */
    SDL_Window* window = nullptr;
    int fb_w = 0, fb_h = 0;
};

Menu g_menu;

/* ---- persisted title settings ------------------------------------------ */
/* psxrecomp keeps the host master volume in host_osd.c and the launcher's
 * settings.toml has no key for it (config_loader.cpp writes [audio]
 * frequency / spu_hq only), so every start came back at 100 whatever the
 * player set in the launcher or in this menu; the menu's own rows (Pause in
 * menu, backdrop dim and opacity) lived in this struct only. Until upstream
 * persists the volume, the title keeps all four in dragonvalor_settings.toml
 * next to the executable: read by the static initializer at the end of this
 * file (before main, so the launcher's Settings page is seeded with the
 * volume and re-applies it when it returns), written whenever a value is
 * seen to differ: the rows themselves, attach (the launcher stepper), detach
 * (the numpad hotkeys). Missing or unreadable file = the defaults, and
 * nothing is written until a value moves. Video rows go through the host
 * (settings.toml) and mod rows through mods/state.toml, as before. */
struct DvSettings {
    int volume = -1, pause_in_menu = -1, dim = -1, opacity = -1;   /* -1 = absent */
    bool operator==(const DvSettings& o) const {
        return volume == o.volume && pause_in_menu == o.pause_in_menu && dim == o.dim && opacity == o.opacity;
    }
};
DvSettings g_saved;                 /* what the file holds (or held at start) */

/* Resolved once, at the first call (the static initializer below, before any
 * other SDL_GetBasePath user): under SDL3 the returned string is owned by
 * SDL and must not be freed, under SDL2 the caller frees it. */
const std::string& dv_settings_path() {
    static std::string path;
    if (!path.empty()) return path;
    std::string dir;
    if (const char* base = SDL_GetBasePath()) {
        dir = base;
#if !defined(PSX_SDL3)
        SDL_free((void*)base);
#endif
    }
    path = dir + "dragonvalor_settings.toml";
    return path;
}

int dv_clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* `key = value` lines; sections are ignored (the keys are unique). */
DvSettings dv_settings_load() {
    DvSettings d;
    FILE* f = std::fopen(dv_settings_path().c_str(), "rb");
    if (!f) return d;
    char line[256];
    while (std::fgets(line, sizeof line, f)) {
        const char* p = line;
        while (*p == ' ' || *p == '\t') ++p;
        const char* k = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '=') ++p;
        const size_t klen = (size_t)(p - k);
        while (*p == ' ' || *p == '\t') ++p;
        if (*p != '=') continue;
        ++p;
        while (*p == ' ' || *p == '\t') ++p;
        const int n = std::atoi(p);
        const int b = (p[0] == 't') ? 1 : (p[0] == 'f') ? 0 : -1;
        if (klen == 6 && !std::strncmp(k, "volume", 6)) d.volume = dv_clamp(n, 0, 100);
        else if (klen == 13 && !std::strncmp(k, "pause_in_menu", 13)) d.pause_in_menu = b;
        else if (klen == 3 && !std::strncmp(k, "dim", 3)) d.dim = dv_clamp(n, 0, 100);
        else if (klen == 7 && !std::strncmp(k, "opacity", 7)) d.opacity = dv_clamp(n, 0, 100);
    }
    std::fclose(f);
    return d;
}

DvSettings dv_settings_current(const Menu& m) {
    DvSettings d;
    d.volume = host_volume_get();
    d.pause_in_menu = m.pause_in_menu ? 1 : 0;
    d.dim = m.dim;
    d.opacity = m.opacity;
    return d;
}

void dv_settings_sync(const Menu& m) {
    const DvSettings d = dv_settings_current(m);
    if (d == g_saved) return;
    FILE* f = std::fopen(dv_settings_path().c_str(), "wb");
    if (!f) return;
    std::fprintf(f,
                 "# Dragon Valor (USA) Recompiled: title settings the launcher does not keep.\n"
                 "# Written by the in-game menu provider (mods/plugins/dragonvalor_menu.cpp).\n"
                 "[audio]\nvolume = %d\n\n"
                 "[menu]\npause_in_menu = %s\ndim = %d\nopacity = %d\n",
                 d.volume, d.pause_in_menu ? "true" : "false", d.dim, d.opacity);
    std::fclose(f);
    g_saved = d;
}

int opt_int(const char* pkg, const char* feat, const char* opt, int dflt) {
    char text[32];
    if (!psx_mod_option_value(pkg, feat, opt, text, sizeof text)) return dflt;
    if (text[0] == 't') return 1;
    if (text[0] == 'f') return 0;
    return std::atoi(text);
}

/* ---- model callbacks (main thread, under g_menu.mu) ----------------------- */
bool is(const RecompRuntimeUiItem* item, const char* key) { return std::strcmp(item->key, key) == 0; }

const RamRow* ram_row(const RecompRuntimeUiItem* item) {
    for (const RamRow& r : kRamRows)
        if (std::strcmp(item->key, r.key) == 0) return &r;
    return nullptr;
}
bool hero_loaded() { return psx_mod_game_started() && psx_mod_read_half(0x8008A5B0u) != 0; }
int ram_read(const RamRow& r) { return r.width == 1 ? psx_mod_read_byte(r.addr) : psx_mod_read_half(r.addr); }

/* STR is only a display value: the game recomputes 0x8008A5B6 as the sum of
 * the five attack-power entries at 0x80086324 (func_800358A8) and a hit takes
 * its power from that table (func_80040DCC, pc 0x80041798), never from the
 * STR field, so a plain STR write changed the number and not the damage.
 * The editor therefore spreads the new value over the five entries in the
 * game's own 1 : 1.5 : 2 : 2.5 : 3 proportion (a new game starts at
 * 6/9/12/15/18 = 60; every gem adds one such unit) and writes the sum to
 * STR as well, so the Status screen agrees until the game recomputes it to
 * the same total. docs/DEVELOPMENT.md, "How damage is computed". */
/* ---- Area and Stage rows: where the next CONTINUE goes, action stages only.
 * The CONTINUE reload takes the area from 0x800A2F18 and the stage from the
 * stage-start record: func_80038094 copies 0x800A2F1A to 0x8007D9C6 when a
 * stage starts, and the YES branch's func_80038274 copies it back (pc
 * 0x80038298), so the Stage row writes the record, not 0x800A2F1A. Landing on
 * a scene-type stage or an empty table entry left the screen black until the
 * runtime's watchdog closed the game (player report 2026-10-16), so both rows
 * only offer pairs whose stage has an action pack: bit N of an entry = stage
 * N of that area, generated by `python tools/datacdr.py kinds --c` from both
 * discs. Disc 1 holds areas 0..6, disc 2 areas 7..12, so the rows never
 * cross discs. Stage 1 is an action stage in every area. */
#define DV_AREA         0x800A2F18u
#define DV_STAGE_RECORD 0x8007D9C6u
const uint64_t kAreaActionStages[13] = {
    0x00000000000003FEull,   /* area  0 */
    0x00000082B7DAAAAAull,   /* area  1 */
    0x000000001F55AB5Aull,   /* area  2 */
    0x000000001EAB55AAull,   /* area  3 */
    0x00000000075AB55Aull,   /* area  4 */
    0x00000000035DAAAAull,   /* area  5 */
    0x00000000006AAAAAull,   /* area  6 */
    0x00000007BED555AAull,   /* area  7 */
    0x000000003EAB55AAull,   /* area  8 */
    0x000000003ED5556Aull,   /* area  9 */
    0x000000007FAAAAD6ull,   /* area 10 */
    0x000000007FAAAAD6ull,   /* area 11 */
    0x000000007FAAAAD6ull,   /* area 12 */
};
bool area_has_action_stage(int area, int stage, int current_area) {
    if (area < 0 || area > 12 || stage < 1 || stage > 63) return false;
    if ((area <= 6) != (current_area <= 6)) return false;   /* the other disc */
    return ((kAreaActionStages[area] >> stage) & 1u) != 0;
}
/* The next action stage of `area` from `wanted` on in the direction of
 * travel, or -1. */
int next_action_stage(int area, int wanted, int current) {
    const int dir = wanted > current ? 1 : -1;
    for (int s = wanted; s >= 1 && s <= 47; s += dir)
        if (area_has_action_stage(area, s, area)) return s;
    return -1;
}
/* The action stage of `area` nearest to `stage` (the lower one on a tie). */
int nearest_action_stage(int area, int stage) {
    for (int d = 0; d <= 47; ++d) {
        if (area_has_action_stage(area, stage - d, area)) return stage - d;
        if (area_has_action_stage(area, stage + d, area)) return stage + d;
    }
    return -1;
}

/* ---- "Go now": a Game Over that reloads the stage in the chosen area -------
 * The row writes HP 0 and closes the menu. ACTION.BIN (0x800C40E0) turns
 * HP 0 straight into mode 9 (no death animation: the hero stays on its feet
 * and the world keeps running under GAME OVER), and the mode 9 / sub-state 2
 * handler (0x800C51F0) runs the CONTINUE prompt as a small state machine at
 * 0x8009FAF8: 0 GAME OVER fades in, 1 CONTINUE? fades in, 2 waits for
 * Cross/Circle, 3 blinks the choice for a 30-frame timer, 4 acts on it (YES:
 * Val - 50, HP refilled, mode 17 loads the area and stage the rows above
 * chose). Pressing Cross there also made the standing hero jump, and
 * the prompt ignored it until state 2, so the trip writes what the prompt's
 * confirm branch writes (0x800C58CC: cursor on YES, fade 128, timer 30,
 * state 3) as soon as the prompt starts, and no button is pressed at all.
 * A title pad transform (mod_plugins.h; offline only, never under netplay)
 * is the per-frame hook: from the GAME OVER on it does those writes and hands
 * the guest a neutral pad until the stage runs again, so nothing the player
 * presses meanwhile moves the hero. Before that the pad stays the player's:
 * the game holds the death back while a dialogue or story scene runs (still
 * mode 8), and that needs Cross to go on. The cheats plugin's Infinite HP is held off
 * (dv_cheats_hold_hp, mods/plugins/dragonvalor_cheats.c) so the HP stays at
 * 0. Outside a trip the transform returns the stock pad; it is detached the
 * next time the menu opens. Timing is wall-clock because the transform may
 * run more than once per guest frame. */
extern "C" volatile int dv_cheats_hold_hp;
#define DV_MODE        0x8008634Cu
#define DV_SUB         0x8008634Du
#define DV_HP          0x8008A5AEu
#define DV_GO_STATE    0x8009FAF8u   /* u32 CONTINUE prompt state, 0..4 (above) */
#define DV_GO_TIMER    0x8009FB04u   /* u32 +0x0C frames left in state 3       */
#define DV_GO_FADE     0x8009FB08u   /* u32 +0x10 text brightness, 128 = full  */
#define DV_GO_CURSOR   0x8009FB12u   /* s16 +0x1A 0 = YES, 1 = NO              */
struct Warp {
    int phase = 0;            /* 0 idle, 1 dying, 2 reloading */
    uint64_t pressed_ms = 0;  /* Go now */
    uint64_t started_ms = 0;  /* the GAME OVER (the last frame of mode 8 before it) */
    uint64_t since_ms = 0;    /* the last frame that was not mode 8 */
    uint16_t val = 0;         /* Val before the trip: CONTINUE costs 50 */
    bool registered = false;
};
Warp g_warp;

void warp_finish() { g_warp.phase = 0; dv_cheats_hold_hp = 0; }

void neutral_pad(PSXModPadOutput* out) {
    out->buttons = 0xFFFFu;
    out->lx = out->ly = out->rx = out->ry = 0x80u;
    out->negcon_i = out->negcon_ii = out->negcon_l = 0;
}

int warp_transform(const PSXModPadFrame* frame, PSXModPadOutput* out) {
    (void)frame;
    if (!g_warp.phase) return 0;
    const uint64_t now = SDL_GetTicks();
    const uint8_t mode = psx_mod_read_byte(DV_MODE), sub = psx_mod_read_byte(DV_SUB);
    if (g_warp.phase == 1 && mode == 8) {
        if (now - g_warp.pressed_ms > 300000u) warp_finish();   /* the Game Over never came */
        else g_warp.started_ms = now;   /* a dialogue holds the death back: the player finishes it */
        return 0;
    }
    if (now - g_warp.started_ms > 60000u) { warp_finish(); return 0; }   /* never got there */
    if (g_warp.phase == 1) {
        /* Sub-state 1 resets the prompt state to 0 before it hands over to 2. */
        if (mode == 9 && sub == 2 && psx_mod_read_word(DV_GO_STATE) <= 2u) {
            psx_mod_write_half(DV_GO_CURSOR, 0);
            psx_mod_write_word(DV_GO_FADE, 128);
            psx_mod_write_word(DV_GO_TIMER, 30);
            psx_mod_write_word(DV_GO_STATE, 3);
            g_warp.phase = 2;
        }
        neutral_pad(out);
        return 1;
    }
    if (mode != 8) { g_warp.since_ms = now; neutral_pad(out); return 1; }   /* still reloading */
    if (now - g_warp.since_ms > 1000) {                            /* the stage runs again */
        psx_mod_write_half(0x800A2DD0u, g_warp.val);   /* the trip is not a real death: no Val toll */
        warp_finish();
    }
    return 0;
}

const PSXModPadTransform kWarpTransform = {
    sizeof(PSXModPadTransform),
    PSX_MOD_PAD_TYPE_BIT(PSX_MOD_PAD_DIGITAL) | PSX_MOD_PAD_TYPE_BIT(PSX_MOD_PAD_DUALSHOCK) |
        PSX_MOD_PAD_TYPE_BIT(PSX_MOD_PAD_JOGCON) | PSX_MOD_PAD_TYPE_BIT(PSX_MOD_PAD_NEGCON),
    PSX_MOD_PAD_DUALSHOCK,
    warp_transform,
};

bool warp_possible() { return !g_warp.phase && hero_loaded() && psx_mod_read_byte(DV_MODE) == 8; }

#define DV_ATTACK_TABLE 0x80086324u
#define DV_STR          0x8008A5B6u
void write_str(int v) {
    static const int twice_weight[5] = {2, 3, 4, 5, 6};   /* sum 20 */
    int sum = 0;
    for (int i = 0; i < 5; ++i) {
        int part = (i == 4) ? v - sum : (v * twice_weight[i] + 10) / 20;
        if (part < 0) part = 0;
        psx_mod_write_half(DV_ATTACK_TABLE + 2u * (uint32_t)i, (uint16_t)part);
        sum += part;
    }
    psx_mod_write_half(DV_STR, (uint16_t)v);
}
/* Keys. A door script does not look at the key count: when the hero walks
 * over a key the pickup handler (func_80034908, pc 0x80034A94..0x80034AB8)
 * sets the save event flag byte 0x800A2D20 + obj[+0x24C] to 1 and clears the
 * object (obj[+0x238] = 0), and the door checks that flag (found 2026-10-15
 * with the player's save: a hand-written count opened nothing, the real
 * pickup changed exactly the count and that byte). So a key row that goes up
 * also "takes" every key object of that kind in the stage's pickup pool
 * (32 entries of 0x250 bytes at 0x800A6A98: +0x238 active, +0x244 pickup id,
 * +0x24C flag index), which is what opens this stage's locked doors. Keys of
 * other stages keep needing their own pickup. */
#define DV_PICKUP_POOL   0x800A6A98u
#define DV_PICKUP_COUNT  32u
#define DV_OBJ_STRIDE    0x250u
#define DV_SAVE_FLAGS    0x800A2D20u   /* 64 event bytes at the head of the save block */
int key_pickup_id(uint32_t count_addr) {
    switch (count_addr) {
        case 0x800A2E00u: return 36;   /* Gold Key */
        case 0x800A2E02u: return 37;   /* Silver Key */
        case 0x800A2E04u: return 38;   /* Copper Key */
        default: return 0;
    }
}
int take_stage_keys(int pickup_id) {
    int taken = 0;
    for (uint32_t i = 0; i < DV_PICKUP_COUNT; ++i) {
        const uint32_t obj = DV_PICKUP_POOL + i * DV_OBJ_STRIDE;
        if (psx_mod_read_word(obj + 0x238u) == 0) continue;
        if ((int)psx_mod_read_word(obj + 0x244u) != pickup_id) continue;
        const uint32_t flag = psx_mod_read_word(obj + 0x24Cu);
        if (flag != 0 && flag < 64u) psx_mod_write_byte(DV_SAVE_FLAGS + flag, 1);
        psx_mod_write_word(obj + 0x238u, 0);
        ++taken;
    }
    return taken;
}
void ram_write(const RamRow& r, int v) {
    if (r.addr == DV_STR) { write_str(v); return; }
    if (r.width == 1) psx_mod_write_byte(r.addr, (uint8_t)v);
    else psx_mod_write_half(r.addr, (uint16_t)v);
}

int cb_get_value(void* ctx, const RecompRuntimeUiItem* item, int* out) {
    Menu& m = *static_cast<Menu*>(ctx);
    PsxHostVideoSettings v{};
    if (is(item, RECOMP_RUNTIME_UI_KEY_FULLSCREEN)) { m.host->video_get(&v); *out = v.fullscreen != 0; return 1; }
    if (is(item, RECOMP_RUNTIME_UI_KEY_LINEAR_FILTER)) { m.host->video_get(&v); *out = v.present_linear != 0; return 1; }
    if (is(item, DV_KEY_TEXFILTER)) {
        m.host->video_get(&v);
        *out = v.texture_filter == PSX_TEXTURE_FILTER_XBR ? PSX_TEXTURE_FILTER_XBR
             : v.texture_filter == PSX_TEXTURE_FILTER_NEAREST ? PSX_TEXTURE_FILTER_NEAREST
             : PSX_TEXTURE_FILTER_BILINEAR;
        return 1;
    }
    if (is(item, DV_KEY_SCANLINES)) { m.host->video_get(&v); *out = v.scanlines != 0; return 1; }
    if (is(item, DV_KEY_DYNRES)) { m.host->video_get(&v); *out = v.dynamic_resolution != 0; return 1; }
    if (is(item, RECOMP_RUNTIME_UI_KEY_VOLUME)) { *out = host_volume_get(); return 1; }
    if (is(item, RECOMP_RUNTIME_UI_KEY_MENU_DIM)) { *out = m.dim; return 1; }
    if (is(item, RECOMP_RUNTIME_UI_KEY_MENU_OPACITY)) { *out = m.opacity; return 1; }
    if (is(item, RECOMP_RUNTIME_UI_KEY_PAUSE_IN_MENU)) { *out = m.pause_in_menu; return 1; }
    if (is(item, RECOMP_RUNTIME_UI_KEY_STATE_SLOT)) { *out = m.slot; return 1; }
    if (is(item, DV_KEY_CHEAT_HP)) { *out = m.cheat_hp; return 1; }
    if (is(item, DV_KEY_CHEAT_MP)) { *out = m.cheat_mp; return 1; }
    if (is(item, DV_KEY_GEM_EVERY)) { *out = m.gem_every; return 1; }
    if (is(item, DV_KEY_SCROLL_EVERY)) { *out = m.scroll_every; return 1; }
    if (is(item, DV_KEY_SKIP_INTRO)) { *out = m.skip_intro; return 1; }
    if (const RamRow* r = ram_row(item)) {
        const int v = hero_loaded() ? ram_read(*r) : 0;
        *out = v > item->maximum ? item->maximum : v < item->minimum ? item->minimum : v;
        return 1;
    }
    return 0;
}

/* Returns what set_value should return. Mod and video rows return 1 (the model
 * only records a toggle it was told succeeded; recomp-ui then overwrites the
 * status with "Saved"); RAM rows return 0 and keep their own text. */
int apply_video(Menu& m, const PsxHostVideoSettings& v) {
    const unsigned bits = m.host->video_apply(&v);
    if (bits & PSX_MENU_RESTART_AFTER_MATCH) recomp_runtime_ui_set_status(m.ui, "Applies after the match");
    else if (bits) recomp_runtime_ui_set_status(m.ui, "Restart the game to apply");
    return 1;   /* recomp-ui then shows "Saved"; the restart note is kept in the row's own status while bits were set */
}

void set_mod(Menu& m, const char* pkg, const char* feat, const char* opt, const char* value) {
    const int live = m.host->mod_set(pkg, feat, opt, value);
    recomp_runtime_ui_set_status(m.ui, live ? "Applied" : "Saved; takes effect at the next start");
}

int cb_set_value(void* ctx, const RecompRuntimeUiItem* item, int value) {
    Menu& m = *static_cast<Menu*>(ctx);
    PsxHostVideoSettings v{};
    char text[96];
    if (is(item, RECOMP_RUNTIME_UI_KEY_FULLSCREEN)) {
        m.host->video_get(&v); v.fullscreen = value ? (v.fullscreen ? v.fullscreen : 1) : 0; return apply_video(m, v);
    }
    if (is(item, RECOMP_RUNTIME_UI_KEY_LINEAR_FILTER)) { m.host->video_get(&v); v.present_linear = value ? 1 : 0; return apply_video(m, v); }
    if (is(item, DV_KEY_TEXFILTER)) { m.host->video_get(&v); v.texture_filter = value; return apply_video(m, v); }
    if (is(item, DV_KEY_SCANLINES)) { m.host->video_get(&v); v.scanlines = value ? 1 : 0; return apply_video(m, v); }
    if (is(item, DV_KEY_DYNRES)) { m.host->video_get(&v); v.dynamic_resolution = value ? 1 : 0; return apply_video(m, v); }
    if (is(item, RECOMP_RUNTIME_UI_KEY_VOLUME)) { host_volume_set(value); dv_settings_sync(m); return 1; }
    if (is(item, RECOMP_RUNTIME_UI_KEY_MENU_DIM)) { m.dim = value; recomp_runtime_ui_set_backdrop(m.ui, m.dim / 100.0f, m.opacity / 100.0f); dv_settings_sync(m); return 1; }
    if (is(item, RECOMP_RUNTIME_UI_KEY_MENU_OPACITY)) { m.opacity = value; recomp_runtime_ui_set_backdrop(m.ui, m.dim / 100.0f, m.opacity / 100.0f); dv_settings_sync(m); return 1; }
    if (is(item, RECOMP_RUNTIME_UI_KEY_PAUSE_IN_MENU)) {
        m.pause_in_menu = value != 0;
        if (m.open) (void)m.host->set_paused(m.pause_in_menu ? 1 : 0);
        dv_settings_sync(m);
        return 1;
    }
    if (is(item, RECOMP_RUNTIME_UI_KEY_STATE_SLOT)) { m.slot = value; return 1; }
    if (is(item, DV_KEY_CHEAT_HP)) { m.cheat_hp = value ? 1 : 0; set_mod(m, DV_CHEATS_PKG, DV_CHEATS_FEAT, "infinite_hp", value ? "true" : "false"); return 1; }
    if (is(item, DV_KEY_CHEAT_MP)) { m.cheat_mp = value ? 1 : 0; set_mod(m, DV_CHEATS_PKG, DV_CHEATS_FEAT, "infinite_mp", value ? "true" : "false"); return 1; }
    if (is(item, DV_KEY_GEM_EVERY)) { m.gem_every = value; std::snprintf(text, sizeof text, "%d", value); set_mod(m, DV_CHEATS_PKG, DV_CHEATS_FEAT, "gem_every", text); return 1; }
    if (is(item, DV_KEY_SCROLL_EVERY)) { m.scroll_every = value; std::snprintf(text, sizeof text, "%d", value); set_mod(m, DV_CHEATS_PKG, DV_CHEATS_FEAT, "scroll_every", text); return 1; }
    if (is(item, DV_KEY_SKIP_INTRO)) { m.skip_intro = value ? 1 : 0; set_mod(m, DV_SKIP_PKG, DV_SKIP_FEAT, nullptr, value ? "true" : "false"); return 1; }
    if (const RamRow* r = ram_row(item)) {
        if (!hero_loaded()) { recomp_runtime_ui_set_status(m.ui, "No hero loaded yet"); return 0; }
        if (r->addr == DV_AREA) {
            const int current = ram_read(*r);
            if (value == current) return 0;
            const int stage = psx_mod_read_half(DV_STAGE_RECORD);
            const int to = nearest_action_stage(value, stage);
            if (to < 0 || (value <= 6) != (current <= 6)) {
                std::snprintf(text, sizeof text, "%s", current <= 6 ? "Areas 7-12 are on disc 2" : "Areas 0-6 are on disc 1");
            } else {
                psx_mod_write_half(DV_AREA, (uint16_t)value);
                if (to != stage) psx_mod_write_half(DV_STAGE_RECORD, (uint16_t)to);
                if (to != stage)
                    std::snprintf(text, sizeof text, "Area %d, stage %d (no action stage %d there): use Go now", value, to, stage);
                else
                    std::snprintf(text, sizeof text, "Area %d, stage %d: use Go now below", value, to);
            }
            recomp_runtime_ui_set_status(m.ui, text);
            return 0;
        }
        if (r->addr == DV_STAGE_RECORD) {
            const int current = ram_read(*r);
            if (value == current) return 0;
            const int area = psx_mod_read_half(DV_AREA);
            const int to = next_action_stage(area, value, current);
            if (to < 0) {
                std::snprintf(text, sizeof text, "No action stage %s %d in area %d", value > current ? "above" : "below", current, area);
            } else {
                psx_mod_write_half(DV_STAGE_RECORD, (uint16_t)to);
                std::snprintf(text, sizeof text, "Area %d, stage %d: use Go now below", area, to);
            }
            recomp_runtime_ui_set_status(m.ui, text);
            return 0;
        }
        const int before = ram_read(*r);
        ram_write(*r, value);
        const int key_id = key_pickup_id(r->addr);
        if (key_id && value > before) {
            const int taken = take_stage_keys(key_id);
            std::snprintf(text, sizeof text, taken ? "Written: %d; %d key object(s) of this stage taken" : "Written: %d (no such key object in this stage)", value, taken);
        } else {
            std::snprintf(text, sizeof text, "Written: %d", value);
        }
        recomp_runtime_ui_set_status(m.ui, text);
        return 0;   /* 1 would let recomp-ui replace the status with "Saved"; the row re-reads RAM */
    }
    return 0;
}

void set_open_locked(Menu& m, bool open);

int cb_run_action(void* ctx, const RecompRuntimeUiItem* item) {
    Menu& m = *static_cast<Menu*>(ctx);
    char text[64];
    if (is(item, RECOMP_RUNTIME_UI_KEY_RESUME)) { set_open_locked(m, false); return 1; }
    if (is(item, RECOMP_RUNTIME_UI_KEY_SAVE_STATE)) {
        const int ok = savestate_request_save(m.slot - 1);
        std::snprintf(text, sizeof text, ok ? "Saving state to slot %d" : "Save state refused (slot %d)", m.slot);
        recomp_runtime_ui_set_status(m.ui, text); return 0;
    }
    if (is(item, RECOMP_RUNTIME_UI_KEY_LOAD_STATE)) {
        if (!savestate_slot_exists(m.slot - 1)) {
            std::snprintf(text, sizeof text, "Slot %d is empty", m.slot);
            recomp_runtime_ui_set_status(m.ui, text); return 0;
        }
        const int ok = savestate_request_load(m.slot - 1);
        if (ok) set_open_locked(m, false);
        else { std::snprintf(text, sizeof text, "Load state refused (slot %d)", m.slot); recomp_runtime_ui_set_status(m.ui, text); }
        return ok ? 1 : 0;
    }
    if (is(item, RECOMP_RUNTIME_UI_KEY_RETURN_TO_LAUNCHER)) {
#if defined(PSX_HAS_LOBBY_CLIENT)
        m.host->request_exit(PSX_MENU_EXIT_TO_FRONTEND);
#else
        /* psxrecomp (pin f9c287f5 and master 379565a6) re-enters the launcher
         * after a session only inside its netplay lobby-client block
         * (main.cpp, soft_return_lobby); a build without PSX_NETPLAY falls
         * through to SDL_Quit and the program closes. Restarting the
         * executable with its own arguments opens the launcher as at boot.
         * Upstream report: docs/BACKLOG.md, section F. */
        m.host->request_exit(PSX_MENU_EXIT_RELAUNCH);
#endif
        return 1;
    }
    if (is(item, RECOMP_RUNTIME_UI_KEY_QUIT)) { m.host->request_exit(PSX_MENU_EXIT_QUIT); return 1; }
    if (is(item, DV_KEY_GO_NOW)) {
        if (m.host->netplay_active()) { recomp_runtime_ui_set_status(m.ui, "Not during netplay"); return 0; }
        if (!warp_possible()) { recomp_runtime_ui_set_status(m.ui, "Only in an action stage"); return 0; }
        if (!g_warp.registered) {
            if (!psx_mod_set_pad_transform(0, &kWarpTransform)) {
                recomp_runtime_ui_set_status(m.ui, "The pad hook was refused");
                return 0;
            }
            g_warp.registered = true;
        }
        dv_cheats_hold_hp = 1;
        g_warp.val = (uint16_t)psx_mod_read_half(0x800A2DD0u);
        g_warp.pressed_ms = g_warp.started_ms = g_warp.since_ms = SDL_GetTicks();
        g_warp.phase = 1;
        psx_mod_write_half(DV_HP, 0);
        set_open_locked(m, false);
        return 1;
    }
    if (is(item, DV_KEY_DISC1) || is(item, DV_KEY_DISC2)) {
        const int n = is(item, DV_KEY_DISC1) ? 1 : 2;
        const int rc = m.host->disc_insert(n);
        std::snprintf(text, sizeof text, rc == 0 ? "Changing to disc %d" : rc == -1 ? "No disc %d in the roster" : "Refused during netplay", n);
        recomp_runtime_ui_set_status(m.ui, text);
        if (rc == 0) set_open_locked(m, false);
        return 0;
    }
    return 0;
}

int cb_is_enabled(void* ctx, const RecompRuntimeUiItem* item) {
    Menu& m = *static_cast<Menu*>(ctx);
    if (is(item, DV_KEY_DISC1) || is(item, DV_KEY_DISC2)) {
        const int n = is(item, DV_KEY_DISC1) ? 1 : 2;
        return m.host->disc_count() >= n && m.host->disc_mounted() != n;
    }
    if (ram_row(item)) return hero_loaded() ? 1 : 0;
    if (is(item, DV_KEY_GO_NOW)) return warp_possible() && !m.host->netplay_active() ? 1 : 0;
    return 1;
}

void cb_save(void*) { /* the host persists settings.toml and mods/state.toml itself */ }

/* ---- open/close ------------------------------------------------------------ */
/* The model closes itself on BACK at the top level; keep the host's pause in
 * step with what it did. */
void sync_open_locked(Menu& m) {
    if (m.open && !recomp_runtime_ui_is_open(m.ui)) {
        m.open = false;
        (void)m.host->set_paused(0);
    }
}

bool create_ui(Menu& m);

void set_open_locked(Menu& m, bool open) {
    if (!m.ui) return;
    if (open && !m.open) {
        /* A fresh model each time: the menu always opens on the first section
         * with nothing selected, instead of wherever it was left. */
        recomp_runtime_ui_destroy(m.ui);
        m.ui = nullptr;
        if (!create_ui(m)) return;
        recomp_runtime_ui_open(m.ui);
        if (m.imgui) ImGui::GetIO().ClearInputMouse();   /* no stale button or hover from last time */
        if (g_warp.registered && !g_warp.phase) {        /* a finished trip: give the pad back */
            (void)psx_mod_set_pad_transform(0, nullptr);
            g_warp.registered = false;
        }
        m.open = true;
        if (m.pause_in_menu && !m.host->set_paused(1))
            recomp_runtime_ui_set_status(m.ui, "Running: pause refused during netplay");
    } else if (!open && m.open) {
        recomp_runtime_ui_close(m.ui);
        if (m.imgui) ImGui::GetIO().ClearInputMouse();   /* a click that closed the menu never gets its release */
        m.open = false;
        (void)m.host->set_paused(0);
    }
}

bool create_ui(Menu& m) {
    RecompRuntimeUiStandardConfig cfg{};
    cfg.menu.title = "Dragon Valor";
    cfg.menu.subtitle = "PlayStation";
    cfg.menu.theme = "psx";
    cfg.menu.accept_label = "Enter / Cross";
    cfg.menu.back_label = "Esc / Circle";
    cfg.menu.callbacks.context = &m;
    cfg.menu.callbacks.get_value = cb_get_value;
    cfg.menu.callbacks.set_value = cb_set_value;
    cfg.menu.callbacks.run_action = cb_run_action;
    cfg.menu.callbacks.is_enabled = cb_is_enabled;
    cfg.menu.callbacks.save = cb_save;
    cfg.features = RECOMP_RUNTIME_UI_STANDARD_FULLSCREEN | RECOMP_RUNTIME_UI_STANDARD_LINEAR_FILTER |
                   RECOMP_RUNTIME_UI_STANDARD_VOLUME | RECOMP_RUNTIME_UI_STANDARD_RESUME |
                   RECOMP_RUNTIME_UI_STANDARD_STATE_SLOT | RECOMP_RUNTIME_UI_STANDARD_SAVE_STATE |
                   RECOMP_RUNTIME_UI_STANDARD_LOAD_STATE | RECOMP_RUNTIME_UI_STANDARD_PAUSE_IN_MENU |
                   RECOMP_RUNTIME_UI_STANDARD_MENU_BACKDROP |
                   RECOMP_RUNTIME_UI_STANDARD_RETURN_TO_LAUNCHER | RECOMP_RUNTIME_UI_STANDARD_QUIT;
    cfg.state_slot_count = SAVESTATE_SLOTS;
    cfg.extra_items = kExtraItems;
    cfg.extra_item_count = sizeof kExtraItems / sizeof kExtraItems[0];
    m.ui = recomp_runtime_ui_create_standard(&cfg);
    if (!m.ui) return false;
    recomp_runtime_ui_set_backdrop(m.ui, m.dim / 100.0f, m.opacity / 100.0f);
    return true;
}

/* ---- provider -------------------------------------------------------------- */
int p_attach(void* ctx, void* sdl_window, void* gl_context, const PsxMenuHost* host) {
    Menu& m = *static_cast<Menu*>(ctx);
    (void)gl_context;
    std::lock_guard<std::mutex> lk(m.mu);
    if (!host || host->abi_version != PSX_MENU_ABI_VERSION) return 0;
    m.host = host;
    m.window = static_cast<SDL_Window*>(sdl_window);
    dv_settings_sync(m);               /* the launcher's volume stepper may have moved it */
    if (!m.ui && !create_ui(m)) return 0;
    /* the committed selection is what the launcher left; mirror it */
    m.cheat_hp = opt_int(DV_CHEATS_PKG, DV_CHEATS_FEAT, "infinite_hp", 0);
    m.cheat_mp = opt_int(DV_CHEATS_PKG, DV_CHEATS_FEAT, "infinite_mp", 0);
    m.gem_every = opt_int(DV_CHEATS_PKG, DV_CHEATS_FEAT, "gem_every", 0);
    m.scroll_every = opt_int(DV_CHEATS_PKG, DV_CHEATS_FEAT, "scroll_every", 0);
    m.skip_intro = opt_int(DV_SKIP_PKG, DV_SKIP_FEAT, "movie", 0) ? 1 : 0;   /* 0 unless the feature is in the plan */
    if (!m.imgui) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        if (host->assets_dir) {
            const std::string font = std::string(host->assets_dir) + "/fonts/LatoLatin-Regular.ttf";
            if (FILE* f = std::fopen(font.c_str(), "rb")) {
                std::fclose(f);
                io.Fonts->AddFontFromFileTTF(font.c_str(), 22.0f);
            }
        }
        if (!ImGui_ImplOpenGL3_Init("#version 330")) {
            ImGui::DestroyContext();
            return 0;
        }
        m.imgui = true;
    }
    return 1;
}

void p_detach(void* ctx) {
    Menu& m = *static_cast<Menu*>(ctx);
    std::lock_guard<std::mutex> lk(m.mu);
    dv_settings_sync(m);               /* numpad +/- volume changes since attach */
    if (m.open) { recomp_runtime_ui_close(m.ui); m.open = false; }
    if (m.imgui) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui::DestroyContext();
        m.imgui = false;
    }
    if (m.ui) { recomp_runtime_ui_destroy(m.ui); m.ui = nullptr; }
    m.host = nullptr;
}

int p_is_open(void* ctx) {
    Menu& m = *static_cast<Menu*>(ctx);
    std::lock_guard<std::mutex> lk(m.mu);
    return m.open ? 1 : 0;
}

void p_set_open(void* ctx, int open) {
    Menu& m = *static_cast<Menu*>(ctx);
    std::lock_guard<std::mutex> lk(m.mu);
    set_open_locked(m, open != 0);
}

/* Semantic input from a key scancode or a gamepad button; -1 = not a menu key. */
int key_to_input(int scancode) {
    switch (scancode) {
        case SDL_SCANCODE_UP: return RECOMP_RUNTIME_UI_INPUT_UP;
        case SDL_SCANCODE_DOWN: return RECOMP_RUNTIME_UI_INPUT_DOWN;
        case SDL_SCANCODE_LEFT: return RECOMP_RUNTIME_UI_INPUT_LEFT;
        case SDL_SCANCODE_RIGHT: return RECOMP_RUNTIME_UI_INPUT_RIGHT;
        case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: case SDL_SCANCODE_SPACE:
            return RECOMP_RUNTIME_UI_INPUT_ACCEPT;
        case SDL_SCANCODE_BACKSPACE: return RECOMP_RUNTIME_UI_INPUT_BACK;
        default: return -1;
    }
}

int pad_to_input(int button) {
#if defined(PSX_SDL3)
    switch (button) {
        case SDL_GAMEPAD_BUTTON_DPAD_UP: return RECOMP_RUNTIME_UI_INPUT_UP;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return RECOMP_RUNTIME_UI_INPUT_DOWN;
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return RECOMP_RUNTIME_UI_INPUT_LEFT;
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return RECOMP_RUNTIME_UI_INPUT_RIGHT;
        case SDL_GAMEPAD_BUTTON_SOUTH: return RECOMP_RUNTIME_UI_INPUT_ACCEPT;
        case SDL_GAMEPAD_BUTTON_EAST: return RECOMP_RUNTIME_UI_INPUT_BACK;
        default: return -1;
    }
#else
    switch (button) {
        case SDL_CONTROLLER_BUTTON_DPAD_UP: return RECOMP_RUNTIME_UI_INPUT_UP;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return RECOMP_RUNTIME_UI_INPUT_DOWN;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return RECOMP_RUNTIME_UI_INPUT_LEFT;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return RECOMP_RUNTIME_UI_INPUT_RIGHT;
        case SDL_CONTROLLER_BUTTON_A: return RECOMP_RUNTIME_UI_INPUT_ACCEPT;
        case SDL_CONTROLLER_BUTTON_B: return RECOMP_RUNTIME_UI_INPUT_BACK;
        default: return -1;
    }
#endif
}

int p_event(void* ctx, const void* sdl_event) {
    Menu& m = *static_cast<Menu*>(ctx);
    const SDL_Event* ev = static_cast<const SDL_Event*>(sdl_event);
    if (!ev) return 0;
    std::lock_guard<std::mutex> lk(m.mu);
    if (!m.ui) return 0;
    switch (ev->type) {
#if defined(PSX_SDL3)
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        const bool down = ev->type == SDL_EVENT_KEY_DOWN;
        const int sc = ev->key.scancode;
        const bool repeat = ev->key.repeat != 0;
#else
    case SDL_KEYDOWN:
    case SDL_KEYUP: {
        const bool down = ev->type == SDL_KEYDOWN;
        const int sc = ev->key.keysym.scancode;
        const bool repeat = ev->key.repeat != 0;
#endif
        if (sc == SDL_SCANCODE_ESCAPE) {
            if (down && !repeat) {
                if (m.open) {
                    (void)recomp_runtime_ui_handle_input(m.ui, RECOMP_RUNTIME_UI_INPUT_BACK, 1, 0);
                    sync_open_locked(m);
                } else {
                    set_open_locked(m, true);
                }
            }
            return 1;
        }
        if (!m.open) return 0;
        const int input = key_to_input(sc);
        if (input >= 0) (void)recomp_runtime_ui_handle_input(m.ui, (RecompRuntimeUiInput)input, down, repeat);
        sync_open_locked(m);
        return 1;   /* the game never sees keys while the menu is up */
    }
#if defined(PSX_SDL3)
    case SDL_EVENT_MOUSE_MOTION:
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    case SDL_EVENT_MOUSE_WHEEL: {
        const bool motion = ev->type == SDL_EVENT_MOUSE_MOTION;
        const bool wheel = ev->type == SDL_EVENT_MOUSE_WHEEL;
        const bool button = ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN || ev->type == SDL_EVENT_MOUSE_BUTTON_UP;
        const bool down = ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        const float mx = motion ? ev->motion.x : button ? ev->button.x : ev->wheel.mouse_x;
        const float my = motion ? ev->motion.y : button ? ev->button.y : ev->wheel.mouse_y;
        const int which = button ? ev->button.button : 0;
        const float wx = wheel ? ev->wheel.x : 0.0f, wy = wheel ? ev->wheel.y : 0.0f;
#else
    case SDL_MOUSEMOTION:
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
    case SDL_MOUSEWHEEL: {
        const bool motion = ev->type == SDL_MOUSEMOTION;
        const bool wheel = ev->type == SDL_MOUSEWHEEL;
        const bool button = ev->type == SDL_MOUSEBUTTONDOWN || ev->type == SDL_MOUSEBUTTONUP;
        const bool down = ev->type == SDL_MOUSEBUTTONDOWN;
        int ix = 0, iy = 0;
        if (wheel) SDL_GetMouseState(&ix, &iy);
        const float mx = motion ? (float)ev->motion.x : button ? (float)ev->button.x : (float)ix;
        const float my = motion ? (float)ev->motion.y : button ? (float)ev->button.y : (float)iy;
        const int which = button ? ev->button.button : 0;
        const float wx = wheel ? (float)ev->wheel.x : 0.0f, wy = wheel ? (float)ev->wheel.y : 0.0f;
#endif
        /* The game never sees the mouse while the menu is up; closed, it is
         * not ours (the runtime's own mouse policy applies). */
        if (!m.open || !m.imgui) return 0;
        ImGuiIO& io = ImGui::GetIO();
        int ww = 0, wh = 0;
        if (m.window) SDL_GetWindowSize(m.window, &ww, &wh);
        const float sx = (ww > 0 && m.fb_w > 0) ? (float)m.fb_w / (float)ww : 1.0f;
        const float sy = (wh > 0 && m.fb_h > 0) ? (float)m.fb_h / (float)wh : 1.0f;
        io.AddMousePosEvent(mx * sx, my * sy);
        if (button) {
            if (which == SDL_BUTTON_LEFT) io.AddMouseButtonEvent(0, down);
            else if (which == SDL_BUTTON_RIGHT && down) {
                /* right button = Back, like Escape / Circle */
                (void)recomp_runtime_ui_handle_input(m.ui, RECOMP_RUNTIME_UI_INPUT_BACK, 1, 0);
                sync_open_locked(m);
            } else if (which == SDL_BUTTON_MIDDLE) io.AddMouseButtonEvent(2, down);
        }
        if (wheel) io.AddMouseWheelEvent(wx, wy);
        return 1;
    }
#if defined(PSX_SDL3)
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP: {
        const bool down = ev->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
        const int button = ev->gbutton.button;
        const bool is_start = button == SDL_GAMEPAD_BUTTON_START;
        const bool is_back = button == SDL_GAMEPAD_BUTTON_BACK;
#else
    case SDL_CONTROLLERBUTTONDOWN:
    case SDL_CONTROLLERBUTTONUP: {
        const bool down = ev->type == SDL_CONTROLLERBUTTONDOWN;
        const int button = ev->cbutton.button;
        const bool is_start = button == SDL_CONTROLLER_BUTTON_START;
        const bool is_back = button == SDL_CONTROLLER_BUTTON_BACK;
#endif
        if (is_start) m.pad_start = down;
        if (is_back) m.pad_back = down;
        if (m.pad_start && m.pad_back && !m.combo_fired) {
            m.combo_fired = true;
            set_open_locked(m, !m.open);
            return 1;
        }
        if (!m.pad_start && !m.pad_back) m.combo_fired = false;
        if (!m.open) return 0;
        if (is_start && down) { set_open_locked(m, false); return 1; }
        const int input = pad_to_input(button);
        if (input >= 0) (void)recomp_runtime_ui_handle_input(m.ui, (RecompRuntimeUiInput)input, down, false);
        sync_open_locked(m);
        return 1;
    }
#if defined(PSX_SDL3)
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
#else
    case SDL_CONTROLLERAXISMOTION:
#endif
        return m.open ? 1 : 0;
    default:
        return 0;
    }
}

void p_draw(void* ctx, int width, int height) {
    Menu& m = *static_cast<Menu*>(ctx);
    std::lock_guard<std::mutex> lk(m.mu);
    if (!m.ui || !m.imgui) return;
    if (!m.open && !recomp_runtime_ui_toast_visible(m.ui)) return;
    if (width <= 0 || height <= 0) return;
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)width, (float)height);
    io.DeltaTime = 1.0f / 60.0f;
    m.fb_w = width; m.fb_h = height;
    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();
    recomp_runtime_ui_render_imgui(m.ui);
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

const PsxMenuProvider g_provider = {
    PSX_MENU_ABI_VERSION, "Dragon Valor menu", &g_menu,
    p_attach, p_event, p_is_open, p_set_open, p_draw, p_detach,
};

struct Registrar {
    Registrar() {
        (void)psx_menu_set_provider(&g_provider);
        const DvSettings d = dv_settings_load();
        if (d.volume >= 0) host_volume_set(d.volume);
        if (d.pause_in_menu >= 0) g_menu.pause_in_menu = d.pause_in_menu != 0;
        if (d.dim >= 0) g_menu.dim = d.dim;
        if (d.opacity >= 0) g_menu.opacity = d.opacity;
        g_saved = dv_settings_current(g_menu);   /* no file, or a partial one: write only once a value moves */
    }
} g_registrar;

}  // namespace

#endif  /* RECOMP_LAUNCHER */
