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
 *
 * Look and flow (2026-11-18, the player found the generic menu plain and
 * asked for real confirmations): the rows are this file's own list, the
 * pause menu's Game section first; the menu is drawn by
 * dragonvalor_menu_view.cpp instead of recomp-ui's presentation; actions
 * that are hard to undo (load or overwrite a state, change discs, the warp,
 * leaving the game) and the Hero choices ask in a dialog first, and what
 * they did shows as a notice (toast) near the top of the screen. Play as and
 * Weapon are picked first and applied together, from the dialog.
 */
#if defined(RECOMP_LAUNCHER)

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>

#include "psx_sdl.h"
#include "imgui.h"
#include "imgui_impl_opengl3.h"

#include "recomp_runtime_ui.h"
#include "../../recomp-ui/src/common/recomp_runtime_ui_internal.h"
#include "dragonvalor_menu_view.h"
#include "psx_menu_host.h"
#include "host_osd.h"
#include "mod_plugins.h"
#include "savestate.h"
#include "host_keymap.h"
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
#define DV_KEY_SECRET_GO   "dv.warp.secret_go"
/* Warp > Cut prologue scene: stage 0/5, a scene cut from the prologue that
 * nothing in the game leads to (the decomp's tools/stages.py walks every way a
 * stage is picked; its docs/SECRETS.md). Reached by a trip, the game's own
 * ModeTransition called at a quiet frame (dragonvalor_cheats.c, kind 0). The
 * other stages nothing reaches (the corridor 1/24, the Japanese world maps
 * 1/14 and 1/16) were offered too until 2026-11-21 and taken out at the
 * player's request. */
#define DV_SECRET_AREA  0
#define DV_SECRET_STAGE 5
#define DV_KEY_PLAY_AS     "dv.heroswap.hero"
#define DV_KEY_HERO_APPLY  "dv.heroswap.apply"
/* dragonvalor.heroswap's `hero` option: index 0 = each stage's own hero. */
const char* const kPlayAsChoices[] = {"Each stage's own", "Clovis", "Arlen", "Kodel", "Felippe",
                                      "Phillip", "Gerome", "Anna", "Mihael", "Anita"};
const char* const kPlayAsValues[] = {"stage", "clovis", "arlen", "kodel", "felippe",
                                     "phillip", "gerome", "anna", "mihael", "anita"};
#define DV_KEY_WEAPON      "dv.heroswap.weapon"
/* dragonvalor.heroswap's `weapon` option: index 0 = the hero's own, then the
 * weapon models (kWeapons in dragonvalor_heroswap.c). */
const char* const kWeaponChoices[] = {"The hero's own", "Azos", "Soul", "Kadum", "Igunis", "Felippe's first sword",
                                      "Mihael's dark sword", "Anna's red weapon", "Anita's purple sword",
                                      "Anita's turquoise sword"};
const char* const kWeaponValues[] = {"hero", "azos", "soul", "kadum", "igunis", "felippe", "dark", "red", "purple",
                                     "turquoise"};
#define DV_WEAPON_CHOICES 10

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

#define DV_ROW_ACTION(key, section, label, desc) \
    {key, section, label, desc, RECOMP_RUNTIME_UI_ACTION, 0, 0, 0, nullptr, 0, nullptr}
#define DV_ROW_BOOL(key, section, label, desc) \
    {key, section, label, desc, RECOMP_RUNTIME_UI_BOOL, 0, 1, 1, nullptr, 0, nullptr}

/* Every row, in the order shown: a section is listed where its first row is.
 * The pause menu's own business comes first (the player, 2026-11-18: System
 * sat between Audio and Cheats), then what the game offers, then settings.
 * Descriptions are one or two lines: the view shows only the selected row's,
 * under the rows. The standard keys (RECOMP_RUNTIME_UI_KEY_*) are recomp-ui's
 * names for the same host settings the callbacks below handle. */
const RecompRuntimeUiItem kItems[] = {
    DV_ROW_ACTION(RECOMP_RUNTIME_UI_KEY_RESUME, "Game", "Resume", "Close the menu and play."),
    DV_ROW_ACTION(RECOMP_RUNTIME_UI_KEY_SAVE_STATE, "Game", "Save state",
                  "Keep this moment in one of 12 slots, with a picture of it. F7 opens the same screen."),
    DV_ROW_ACTION(RECOMP_RUNTIME_UI_KEY_LOAD_STATE, "Game", "Load state",
                  "Go back to a moment kept in a slot. F7 opens the same screen."),
    DV_ROW_ACTION(DV_KEY_DISC1, "Game", "Insert disc 1", "Swap to disc 1: areas 0 to 6, the first chapters."),
    DV_ROW_ACTION(DV_KEY_DISC2, "Game", "Insert disc 2", "Swap to disc 2: areas 7 to 12, the last chapters."),
    DV_ROW_ACTION(RECOMP_RUNTIME_UI_KEY_RETURN_TO_LAUNCHER, "Game", "Return to launcher",
                  "Close the game and open the launcher. Progress since your last save is lost."),
    DV_ROW_ACTION(RECOMP_RUNTIME_UI_KEY_QUIT, "Game", "Quit game",
                  "Close the game. Progress since your last save is lost."),

    {DV_KEY_PLAY_AS, "Hero", "Play as",
     "The hero of every action stage: model, moves, voice, portrait and name, story scenes included. "
     "Pick one with Left / Right, then confirm it.",
     RECOMP_RUNTIME_UI_CHOICE, 0, 9, 1, kPlayAsChoices, 10, nullptr},
    {DV_KEY_WEAPON, "Hero", "Weapon",
     "The sword the hero holds (its look: damage and moves stay the hero's). Kadum, Igunis and the last "
     "four are read from disc 2 once it has been started. Pick one, then confirm it.",
     RECOMP_RUNTIME_UI_CHOICE, 0, DV_WEAPON_CHOICES - 1, 1, kWeaponChoices, DV_WEAPON_CHOICES, nullptr},
    DV_ROW_ACTION(DV_KEY_HERO_APPLY, "Hero", "Apply",
                  "Use the hero and weapon above. In an action stage the stage restarts from its beginning: "
                  "HP, items and Val stay, its doors and keys reset."),

    DV_ROW_BOOL(DV_KEY_CHEAT_HP, "Cheats", "Infinite HP", "HP never drops."),
    DV_ROW_BOOL(DV_KEY_CHEAT_MP, "Cheats", "Infinite MP", "MP never drops."),
    DV_ROW_INT(DV_KEY_GEM_EVERY, "Cheats", "Gem every N sword kills",
               "0 keeps the game's rule; N drops a gem every N kills with the sword.", 0, 15, 1),
    DV_ROW_INT(DV_KEY_SCROLL_EVERY, "Cheats", "Scroll every N magic kills",
               "0 keeps the game's rule; N drops a scroll every N kills with magic.", 0, 5, 1),
    DV_ROW_INT(DV_RAM_VAL, "Cheats", "Val", "Your gold.", 0, 9999, 100),
    DV_ROW_INT("dv.ram.hpmax", "Cheats", "Max HP", "Maximum HP.", 1, 999, 10),
    DV_ROW_INT("dv.ram.mpmax", "Cheats", "Max MP", "Maximum MP.", 1, 999, 10),
    DV_ROW_INT("dv.ram.str", "Cheats", "STR", "Attack power.", 0, 999, 5),
    DV_ROW_INT("dv.ram.def", "Cheats", "DEF", "Defense.", 0, 999, 5),

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
    DV_ROW_ITEM("dv.ram.item14", "Gold Key", "Opens locked doors. Raising it also picks up this stage's gold keys, which is what the doors check."),
    DV_ROW_ITEM("dv.ram.item15", "Silver Key", "Opens locked doors. Raising it also picks up this stage's silver keys, which is what the doors check."),
    DV_ROW_ITEM("dv.ram.item16", "Copper Key", "Opens locked doors. Raising it also picks up this stage's copper keys, which is what the doors check."),

    /* The stage warp works, but it has known limits (garbage text in story
     * scenes, skipped stages' items and keys), so it sits apart from the
     * cheats (player's choice, 2026-10-16). */
    DV_ROW_INT("dv.ram.area", "Warp", "Area",
               "The area the next Continue loads, on this disc. The stage below moves to the nearest action "
               "stage there.", 0, 12, 1),
    DV_ROW_INT("dv.ram.stage", "Warp", "Stage",
               "The stage the next Continue loads; only action stages are offered. A story scene right after "
               "the jump can show garbled text, but it plays.", 0, 47, 1),
    DV_ROW_ACTION(DV_KEY_GO_NOW, "Warp", "Go now",
                  "End the stage with a Game Over and Continue into the area and stage above, in about 10 "
                  "seconds. Make a save state first: skipped stages can leave items and keys behind."),
    DV_ROW_ACTION(DV_KEY_SECRET_GO, "Warp", "Cut prologue scene",
                  "Watch a scene cut from the prologue, left on disc 1, its Japanese lines in English. Leaves "
                  "this stage as an exit would: no Game Over. Make a save state first."),

    DV_ROW_BOOL(RECOMP_RUNTIME_UI_KEY_FULLSCREEN, "Video", "Fullscreen", "Fill the screen, or play in a window."),
    DV_ROW_BOOL(RECOMP_RUNTIME_UI_KEY_LINEAR_FILTER, "Video", "Smooth picture",
                "Smooth the final picture when it is scaled up to the window."),
    {DV_KEY_TEXFILTER, "Video", "Texture filtering",
     "Nearest is the PS1 look; Bilinear and xBR smooth the textures (xBR keeps edges sharp).",
     RECOMP_RUNTIME_UI_CHOICE, 0, 2, 1, kTextureFilterChoices, 3, kTextureFilterValues},
    DV_ROW_BOOL(DV_KEY_SCANLINES, "Video", "Scanlines", "Draw CRT-style scanlines over the picture."),
    DV_ROW_BOOL(DV_KEY_DYNRES, "Video", "Dynamic resolution",
                "Lower the internal resolution when a frame runs late."),

    DV_ROW_INT(RECOMP_RUNTIME_UI_KEY_VOLUME, "Options", "Volume", "The game's volume.", 0, 100, 5),
    DV_ROW_BOOL(RECOMP_RUNTIME_UI_KEY_PAUSE_IN_MENU, "Options", "Pause in menu",
                "Hold the game while this menu is open."),
    DV_ROW_BOOL(DV_KEY_SKIP_INTRO, "Options", "Skip intro movie",
                "Press Start for you when the intro movie starts. From the next start of the game."),
    DV_ROW_INT(RECOMP_RUNTIME_UI_KEY_MENU_DIM, "Options", "Game dimming",
               "How dark the game gets behind this menu.", 0, 100, 10),
    DV_ROW_INT(RECOMP_RUNTIME_UI_KEY_MENU_OPACITY, "Options", "Menu opacity",
               "How solid this menu's background is.", 0, 100, 10),
};

#define DV_CHEATS_PKG   "dragonvalor.cheats"
#define DV_CHEATS_FEAT  "cheats"
#define DV_SKIP_PKG     "dragonvalor.skip-intro"
#define DV_SKIP_FEAT    "skip-intro"
#define DV_HS_PKG       "dragonvalor.heroswap"
#define DV_HS_FEAT      "play-as"

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
    int play_as = 0;                       /* index in kPlayAsValues: the applied choice */
    int weapon = 0;                        /* index in kWeaponValues: the applied choice */
    int play_as_pend = 0, weapon_pend = 0; /* what the Hero rows show; applied from the dialog */
    /* the dialog over the menu, and what its action button does */
    DvMenuDialog dialog;
    int dialog_kind = 0;                   /* DV_DIALOG_* */
    const RecompRuntimeUiItem* dialog_item = nullptr;
    bool confirmed = false;                /* cb_run_action called from the dialog */
    DvMenuToast toast;
    /* the save-state screen: its slots (dates, thumbnails) are read when it
     * opens and after a save lands */
    DvStatesView states;
    bool states_from_f7 = false;           /* Back closes the menu instead of returning to it */
    int states_pending = -1;               /* a slot being saved: re-read when the save is done */
    int states_cols = 6;                   /* the grid the view drew: 6 x 2 or 4 x 3 */
    int dialog_slot = -1;                  /* the slot a save-state dialog is about */
    int saved_count = -1;                  /* the Load state row's note */
    /* gamepad combo */
    bool pad_start = false, pad_back = false, combo_fired = false;
    /* the footer's hints follow the last device used (keys or a pad) */
    DvMenuHints hints;
    /* a held D-pad direction or stick repeats from the draw callback (SDL
     * repeats keys itself, not pad buttons): first after 380 ms, then
     * faster the longer it is held */
    int held = -1, held_count = 0;
    bool held_stick = false;
    uint64_t held_next = 0;
    int stick_x = 0, stick_y = 0;
    /* mouse: window coordinates are scaled to the framebuffer ImGui draws in */
    SDL_Window* window = nullptr;
    int fb_w = 0, fb_h = 0;
    /* fonts, rebuilt when the display height moves the scale */
    std::string assets_dir;
    DvMenuFonts fonts;
    bool gl_ready = false;                 /* the ImGui GL backend made its objects */
};
enum { DV_DIALOG_NONE, DV_DIALOG_ACTION, DV_DIALOG_HERO, DV_DIALOG_HERO_CLOSE, DV_DIALOG_STATE_SAVE,
       DV_DIALOG_STATE_LOAD };
/* Inputs of our own beside recomp-ui's RecompRuntimeUiInput: the shoulder
 * buttons (and Tab / Shift+Tab, Page Down / Up) step through the sections. */
enum { DV_INPUT_PREV_SECTION = 100, DV_INPUT_NEXT_SECTION = 101 };

/* "Enter", "Cross", "A"...: what the player presses to confirm, in texts. */
const char* accept_name(const Menu& m) {
    if (m.hints.style == 0) return "Enter";
    if (m.hints.style == 1) return "Cross";
    return m.hints.accept;
}

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
/* dragonvalor_heroswap.c: restart the running stage at the next plain-play frame. */
extern "C" volatile int dv_heroswap_restart_request;
/* dragonvalor_heroswap.c: a loaded state made with another hero / weapon */
extern "C" volatile int dv_heroswap_mismatch, dv_menu_present;
extern "C" char dv_heroswap_mismatch_saved[64], dv_heroswap_adopt_hero[16], dv_heroswap_adopt_weapon[16];
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

/* ---- Warp > Cut prologue scene: a trip (dragonvalor_cheats.c) -------------- */
extern "C" volatile int dv_trip_request, dv_trip_area, dv_trip_stage;
/* dragonvalor.cheats is in the running plan: its hook makes the trip. */
bool cheats_live() {
    char v[16];
    return psx_mod_option_value("dragonvalor.cheats", "cheats", "infinite_hp", v, sizeof v) != 0;
}
bool trip_possible() { return warp_possible() && !dv_trip_request && cheats_live(); }

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
    if (is(item, DV_KEY_PLAY_AS)) { *out = m.play_as_pend; return 1; }
    if (is(item, DV_KEY_WEAPON)) { *out = m.weapon_pend; return 1; }
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

void set_open_locked(Menu& m, bool open);

/* ---- notices and dialogs ---------------------------------------------------- */
uint64_t now_ms() { return (uint64_t)SDL_GetTicks(); }

/* A notice near the top of the screen; it outlives the menu. */
void toast(Menu& m, const char* title, const char* body, uint64_t ms = 3200) {
    std::snprintf(m.toast.title, sizeof m.toast.title, "%s", title ? title : "");
    std::snprintf(m.toast.body, sizeof m.toast.body, "%s", body ? body : "");
    m.toast.start_ms = now_ms();
    m.toast.end_ms = m.toast.start_ms + ms;
}

/* focus: 0 = the action button, 1 = Cancel (the default for leaving the game). */
void open_dialog(Menu& m, int kind, const RecompRuntimeUiItem* item, const char* title, const char* body,
                 const char* ok, const char* cancel, bool danger, int focus) {
    DvMenuDialog& d = m.dialog;
    std::snprintf(d.title, sizeof d.title, "%s", title);
    std::snprintf(d.body, sizeof d.body, "%s", body);
    std::snprintf(d.ok, sizeof d.ok, "%s", ok);
    std::snprintf(d.cancel, sizeof d.cancel, "%s", cancel ? cancel : "Cancel");
    d.danger = danger;
    d.focus = focus;
    d.alone = false;
    d.open = true;
    m.dialog_kind = kind;
    m.dialog_item = item;
    recomp_runtime_ui_set_status(m.ui, "");
}

bool in_action_stage() { return hero_loaded() && psx_mod_read_byte(DV_MODE) == 8; }

/* ---- Hero: Play as and Weapon, picked in the rows, applied together -------- */
bool hero_pending(const Menu& m) { return m.play_as_pend != m.play_as || m.weapon_pend != m.weapon; }

/* dragonvalor.heroswap is in the running plan (on by default since
 * 2026-11-18; a player can still switch it off in the launcher). */
bool heroswap_live() {
    char v[32];
    return psx_mod_option_value(DV_HS_PKG, DV_HS_FEAT, "hero", v, sizeof v) != 0;
}

/* "Arlen", "Arlen with Kadum", "each stage's own hero with Kadum"... for
 * the dialog's title and the notice. */
void hero_choice_text(int hero, int weapon, char* out, size_t n) {
    const char* who = hero ? kPlayAsChoices[hero] : "each stage's own hero";
    if (weapon) std::snprintf(out, n, "%s with %s", who, kWeaponChoices[weapon]);
    else std::snprintf(out, n, "%s", who);
}

/* closing: the player is leaving the menu with a choice not applied (Cancel
 * then reads Discard). */
void open_hero_dialog(Menu& m, bool closing) {
    char choice[96], title[128], body[400];
    hero_choice_text(m.play_as_pend, m.weapon_pend, choice, sizeof choice);
    if (m.play_as_pend != m.play_as)
        std::snprintf(title, sizeof title, m.play_as_pend ? "Play as %s?" : "Back to %s?", choice);
    else if (m.weapon_pend)
        std::snprintf(title, sizeof title, "Hold %s?", kWeaponChoices[m.weapon_pend]);
    else
        std::snprintf(title, sizeof title, "Back to the hero's own weapon?");
    const bool live = heroswap_live(), stage = live && in_action_stage();
    if (!live)
        std::snprintf(body, sizeof body,
                      "Play as is switched off in the launcher. This switches it on; the choice takes effect "
                      "the next time you start the game.");
    else if (stage)
        std::snprintf(body, sizeof body,
                      "This stage restarts from its beginning with the new choice. HP, items and Val stay; the "
                      "stage's doors and keys reset, as with a Continue.");
    else
        std::snprintf(body, sizeof body, "The choice takes effect when the next stage loads.");
    open_dialog(m, closing ? DV_DIALOG_HERO_CLOSE : DV_DIALOG_HERO, nullptr, title, body,
                stage ? "Restart the stage" : "Apply", closing ? "Discard" : "Not yet", false, 0);
}

/* Writes the picked hero and weapon to dragonvalor.heroswap. Live, the stage
 * being played restarts with them once the menu closes (dv_hs_vblank in
 * dragonvalor_heroswap.c waits for plain play); the menu then closes. */
void apply_hero(Menu& m) {
    const bool hero_changed = m.play_as_pend != m.play_as, weapon_changed = m.weapon_pend != m.weapon;
    bool live = true;
    if (hero_changed) live = m.host->mod_set(DV_HS_PKG, DV_HS_FEAT, "hero", kPlayAsValues[m.play_as_pend]) && live;
    if (weapon_changed) live = m.host->mod_set(DV_HS_PKG, DV_HS_FEAT, "weapon", kWeaponValues[m.weapon_pend]) && live;
    m.play_as = m.play_as_pend;
    m.weapon = m.weapon_pend;
    char title[128];
    if (m.play_as && m.weapon)
        std::snprintf(title, sizeof title, "%s, holding %s", kPlayAsChoices[m.play_as], kWeaponChoices[m.weapon]);
    else if (m.play_as)
        std::snprintf(title, sizeof title, "Playing as %s", kPlayAsChoices[m.play_as]);
    else if (m.weapon)
        std::snprintf(title, sizeof title, "Every hero holds %s", kWeaponChoices[m.weapon]);
    else
        std::snprintf(title, sizeof title, "Each stage's own hero and weapon");
    if (!live) {
        /* not in the running plan: switch the feature on for the next start */
        (void)m.host->mod_set(DV_HS_PKG, DV_HS_FEAT, nullptr, "true");
        toast(m, "Play as switched on", "Start the game again to apply it.");
        return;
    }
    if (in_action_stage()) {
        dv_heroswap_restart_request = 1;
        toast(m, title, "The stage restarts from its beginning.");
        set_open_locked(m, false);
    } else {
        toast(m, title, "From the next stage on.");
    }
}

const RecompRuntimeUiItem* selected_item(Menu& m) {
    if (!m.ui || !m.ui->in_section) return nullptr;
    return recomp_runtime_ui_page_entry(m.ui, m.ui->row_index, nullptr, 0);
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
    if (is(item, DV_KEY_PLAY_AS) || is(item, DV_KEY_WEAPON)) {
        /* only picked here: Enter (or Apply, or closing the menu) asks in a
         * dialog and apply_hero() does it */
        const bool w = is(item, DV_KEY_WEAPON);
        if (value < 0 || value > (w ? DV_WEAPON_CHOICES - 1 : 9)) return 0;
        (w ? m.weapon_pend : m.play_as_pend) = value;
        std::snprintf(text, sizeof text, "Press %s to apply", accept_name(m));
        recomp_runtime_ui_set_status(m.ui, hero_pending(m) ? text : "");
        return 0;   /* 1 would let recomp-ui show "Saved" */
    }
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

void request_close(Menu& m);

/* ---- the save-state screen ------------------------------------------------- */
/* Reads a slot's date, compatibility and thumbnail (all of them: -1). */
void states_refresh(Menu& m, int only = -1) {
    m.saved_count = 0;
    for (int i = 0; i < DV_STATE_SLOTS; ++i) {
        DvStatesView::Slot& sl = m.states.slots[i];
        if (only < 0 || i == only) {
            sl.used = savestate_slot_exists(i) != 0;
            sl.compatible = true;
            sl.date[0] = 0;
            if (sl.used) {
                char reason[128];
                int64_t t = 0;
                sl.compatible = savestate_slot_compatible(i, reason, sizeof reason) != 0;
                if (savestate_slot_mtime(i, &t)) {
                    const time_t tt = (time_t)t;
                    struct tm tmv;
#ifdef _WIN32
                    localtime_s(&tmv, &tt);
#else
                    localtime_r(&tt, &tmv);
#endif
                    std::strftime(sl.date, sizeof sl.date, "%b %d, %H:%M", &tmv);
                }
            }
            m.states.have_thumb[i] = sl.used && savestate_read_thumb(i, m.states.thumb[i], DV_THUMB_W, DV_THUMB_H);
            ++m.states.thumb_gen[i];
        }
        if (sl.used) ++m.saved_count;
    }
}

void states_open(Menu& m, int tab, bool from_f7) {
    if (!m.open) set_open_locked(m, true);
    if (!m.open) return;
    m.states.open = true;
    m.states.tab = tab;
    m.states.selected = std::clamp(m.slot - 1, 0, DV_STATE_SLOTS - 1);
    m.states_from_f7 = from_f7;
    states_refresh(m);
    recomp_runtime_ui_set_status(m.ui, "");
}

void states_close(Menu& m) {
    m.states.open = false;
    if (m.states_from_f7) request_close(m);
}

void states_save(Menu& m, int slot) {
    char text[64];
    std::snprintf(text, sizeof text, "Slot %d", slot + 1);
    if (savestate_request_save(slot)) {
        m.states_pending = slot;
        m.slot = slot + 1;
        toast(m, "State saved", text);
    } else {
        std::snprintf(text, sizeof text, "Save state refused (slot %d)", slot + 1);
        recomp_runtime_ui_set_status(m.ui, text);
    }
}

void states_load(Menu& m, int slot) {
    char text[64];
    std::snprintf(text, sizeof text, "Slot %d", slot + 1);
    if (savestate_request_load(slot)) {
        m.slot = slot + 1;
        toast(m, "State loaded", text);
        m.states.open = false;
        set_open_locked(m, false);
    } else {
        std::snprintf(text, sizeof text, "Load state refused (slot %d)", slot + 1);
        recomp_runtime_ui_set_status(m.ui, text);
    }
}

/* Enter on a slot: save or load it, asking first when that loses something. */
void states_accept(Menu& m) {
    const int i = m.states.selected;
    const DvStatesView::Slot& sl = m.states.slots[i];
    char title[96], body[256];
    if (m.states.tab == 0) {
        if (!sl.used) { states_save(m, i); return; }
        std::snprintf(title, sizeof title, "Overwrite slot %d?", i + 1);
        std::snprintf(body, sizeof body, "The moment saved there (%s) is replaced by this one.", sl.date);
        open_dialog(m, DV_DIALOG_STATE_SAVE, nullptr, title, body, "Overwrite", nullptr, false, 0);
    } else {
        if (!sl.used) {
            std::snprintf(body, sizeof body, "Slot %d is empty", i + 1);
            recomp_runtime_ui_set_status(m.ui, body);
            return;
        }
        if (!sl.compatible) {
            recomp_runtime_ui_set_status(m.ui, "Saved by another build: it cannot be loaded");
            return;
        }
        std::snprintf(title, sizeof title, "Load slot %d?", i + 1);
        std::snprintf(body, sizeof body,
                      "The game goes back to the moment saved %s. What happened since is lost unless you saved it.",
                      sl.date);
        open_dialog(m, DV_DIALOG_STATE_LOAD, nullptr, title, body, "Load", nullptr, false, 0);
    }
    m.dialog_slot = i;
}

/* Arrows move on the grid the view drew; a held direction stops at the edges. */
void states_move(Menu& m, int input) {
    int& sel = m.states.selected;
    const int cols = m.states_cols > 0 ? m.states_cols : 6;
    switch (input) {
        case RECOMP_RUNTIME_UI_INPUT_LEFT: if (sel % cols) --sel; break;
        case RECOMP_RUNTIME_UI_INPUT_RIGHT: if (sel % cols < cols - 1 && sel + 1 < DV_STATE_SLOTS) ++sel; break;
        case RECOMP_RUNTIME_UI_INPUT_UP: if (sel >= cols) sel -= cols; break;
        case RECOMP_RUNTIME_UI_INPUT_DOWN: if (sel + cols < DV_STATE_SLOTS) sel += cols; break;
        default: break;
    }
    m.slot = sel + 1;
}

/* The dialog an action asks first, or false when it needs none (or cannot
 * run: the action itself then says why). The dialog's action button calls
 * cb_run_action again with m.confirmed set. */
bool ask_first(Menu& m, const RecompRuntimeUiItem* item) {
    char title[96], body[400], ok[48];
    if (is(item, DV_KEY_DISC1) || is(item, DV_KEY_DISC2)) {
        const int n = is(item, DV_KEY_DISC1) ? 1 : 2;
        std::snprintf(title, sizeof title, "Insert disc %d?", n);
        std::snprintf(body, sizeof body, "The drive switches to disc %d now, as if you had swapped discs on the console.", n);
        std::snprintf(ok, sizeof ok, "Insert disc %d", n);
        open_dialog(m, DV_DIALOG_ACTION, item, title, body, ok, nullptr, false, 0);
        return true;
    }
    if (is(item, DV_KEY_GO_NOW)) {
        if (m.host->netplay_active() || !warp_possible()) return false;
        std::snprintf(title, sizeof title, "Warp to area %d, stage %d?", psx_mod_read_half(DV_AREA),
                      psx_mod_read_half(DV_STAGE_RECORD));
        open_dialog(m, DV_DIALOG_ACTION, item, title,
                    "The stage ends with a Game Over and the Continue loads it, in about 10 seconds. Make a save "
                    "state first: skipped stages can leave items and keys behind.",
                    "Warp", nullptr, false, 0);
        return true;
    }
    if (is(item, DV_KEY_SECRET_GO)) {
        if (m.host->netplay_active() || !trip_possible()) return false;
        std::snprintf(title, sizeof title, "Go to the cut prologue scene?");
        std::snprintf(body, sizeof body,
                      "A forest scene cut from the prologue: Raimun finds Elena, Clovis's sister, dead, and a dragon "
                      "offers him revenge. Its lines were left in Japanese; this recomp shows them in an English "
                      "translation. About a minute long; then chapter 1 starts.%s Make a save state first.",
                      psx_mod_read_half(DV_AREA) >= 7 ? " The game asks for disc 1 first." : "");
        open_dialog(m, DV_DIALOG_ACTION, item, title, body, "Go", nullptr, false, 0);
        return true;
    }
    if (is(item, RECOMP_RUNTIME_UI_KEY_RETURN_TO_LAUNCHER)) {
        open_dialog(m, DV_DIALOG_ACTION, item, "Return to the launcher?",
                    "The game closes. Progress since your last save is lost.", "Leave the game", nullptr, true, 1);
        return true;
    }
    if (is(item, RECOMP_RUNTIME_UI_KEY_QUIT)) {
        open_dialog(m, DV_DIALOG_ACTION, item, "Quit Dragon Valor?",
                    "The game closes. Progress since your last save is lost.", "Quit", nullptr, true, 1);
        return true;
    }
    return false;
}

int cb_run_action(void* ctx, const RecompRuntimeUiItem* item) {
    Menu& m = *static_cast<Menu*>(ctx);
    char text[96];
    if (!m.confirmed && ask_first(m, item)) return 0;
    if (is(item, RECOMP_RUNTIME_UI_KEY_RESUME)) { request_close(m); return 0; }
    if (is(item, DV_KEY_HERO_APPLY)) {
        if (hero_pending(m)) open_hero_dialog(m, false);
        return 0;
    }
    if (is(item, RECOMP_RUNTIME_UI_KEY_SAVE_STATE)) { states_open(m, 0, false); return 0; }
    if (is(item, RECOMP_RUNTIME_UI_KEY_LOAD_STATE)) { states_open(m, 1, false); return 0; }
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
    if (is(item, DV_KEY_SECRET_GO)) {
        if (m.host->netplay_active()) { recomp_runtime_ui_set_status(m.ui, "Not during netplay"); return 0; }
        if (!cheats_live()) { recomp_runtime_ui_set_status(m.ui, "Needs the Cheats mod (launcher)"); return 0; }
        if (!trip_possible()) { recomp_runtime_ui_set_status(m.ui, "Only in an action stage"); return 0; }
        dv_trip_area = DV_SECRET_AREA;
        dv_trip_stage = DV_SECRET_STAGE;
        dv_trip_request = 1;   /* last: the emulation thread reads the two above once it sees this */
        toast(m, "Cut prologue scene", "Stage 0/5");
        set_open_locked(m, false);
        return 0;
    }
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
        std::snprintf(text, sizeof text, "Area %d, stage %d in about 10 seconds", psx_mod_read_half(DV_AREA),
                      psx_mod_read_half(DV_STAGE_RECORD));
        dv_cheats_hold_hp = 1;
        g_warp.val = (uint16_t)psx_mod_read_half(0x800A2DD0u);
        g_warp.pressed_ms = g_warp.started_ms = g_warp.since_ms = SDL_GetTicks();
        g_warp.phase = 1;
        psx_mod_write_half(DV_HP, 0);
        toast(m, "Warping", text);
        set_open_locked(m, false);
        return 0;
    }
    if (is(item, DV_KEY_DISC1) || is(item, DV_KEY_DISC2)) {
        const int n = is(item, DV_KEY_DISC1) ? 1 : 2;
        const int rc = m.host->disc_insert(n);
        if (rc == 0) {
            std::snprintf(text, sizeof text, "Disc %d is in the drive", n);
            toast(m, "Changing discs", text);
            set_open_locked(m, false);
        } else {
            std::snprintf(text, sizeof text, rc == -1 ? "No disc %d in the roster" : "Refused during netplay", n);
            recomp_runtime_ui_set_status(m.ui, text);
        }
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
    if (is(item, DV_KEY_SECRET_GO)) return trip_possible() && !m.host->netplay_active() ? 1 : 0;
    if (is(item, DV_KEY_HERO_APPLY)) return hero_pending(m) ? 1 : 0;
    return 1;
}

/* The view's per-row extras. Draw thread, under g_menu.mu. */
void row_info(void* ctx, const RecompRuntimeUiItem* item, DvMenuRowInfo* out) {
    Menu& m = *static_cast<Menu*>(ctx);
    if (is(item, RECOMP_RUNTIME_UI_KEY_LOAD_STATE)) {
        if (m.saved_count < 0) states_refresh(m);
        std::snprintf(out->note, sizeof out->note, m.saved_count == 1 ? "1 saved" : "%d saved", m.saved_count);
    } else if (is(item, DV_KEY_DISC1) || is(item, DV_KEY_DISC2)) {
        const int n = is(item, DV_KEY_DISC1) ? 1 : 2;
        if (m.host->disc_mounted() == n) std::snprintf(out->note, sizeof out->note, "in the drive");
        else if (m.host->disc_count() < n) std::snprintf(out->note, sizeof out->note, "not set up");
    } else if (is(item, RECOMP_RUNTIME_UI_KEY_RETURN_TO_LAUNCHER) || is(item, RECOMP_RUNTIME_UI_KEY_QUIT)) {
        out->danger = true;
    } else if (is(item, DV_KEY_PLAY_AS) || is(item, DV_KEY_WEAPON)) {
        out->pending = is(item, DV_KEY_PLAY_AS) ? m.play_as_pend != m.play_as : m.weapon_pend != m.weapon;
        if (out->pending) std::snprintf(out->note, sizeof out->note, "not applied");
    } else if (is(item, DV_KEY_HERO_APPLY) && hero_pending(m)) {
        std::snprintf(out->note, sizeof out->note, in_action_stage() ? "restarts the stage" : "from the next stage");
    }
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
        /* the Hero rows start from what is applied; no dialog left over */
        m.play_as_pend = m.play_as;
        m.weapon_pend = m.weapon;
        m.dialog.open = false;
        m.states.open = false;
        m.saved_count = -1;
        if (m.pause_in_menu && !m.host->set_paused(1))
            recomp_runtime_ui_set_status(m.ui, "Running: pause refused during netplay");
    } else if (!open && m.open) {
        recomp_runtime_ui_close(m.ui);
        if (m.imgui) ImGui::GetIO().ClearInputMouse();   /* a click that closed the menu never gets its release */
        m.open = false;
        m.dialog.open = false;
        m.states.open = false;
        m.held = -1;                                     /* no repeat carried into the next open */
        (void)m.host->set_paused(0);
    }
}

/* The player leaves the menu (Esc at the top, Resume, Start, the combo): a
 * Hero choice not applied yet asks first. */
void request_close(Menu& m) {
    if (m.dialog.open) return;
    if (hero_pending(m)) { open_hero_dialog(m, true); return; }
    set_open_locked(m, false);
}

/* ---- the dialog ------------------------------------------------------------ */
void dialog_confirm(Menu& m) {
    const int kind = m.dialog_kind;
    const RecompRuntimeUiItem* item = m.dialog_item;
    m.dialog.open = false;
    m.dialog_kind = DV_DIALOG_NONE;
    m.dialog_item = nullptr;
    if (kind == DV_DIALOG_ACTION && item) {
        m.confirmed = true;
        (void)cb_run_action(&m, item);
        m.confirmed = false;
    } else if (kind == DV_DIALOG_HERO) {
        apply_hero(m);
    } else if (kind == DV_DIALOG_HERO_CLOSE) {
        apply_hero(m);
        set_open_locked(m, false);
    } else if (kind == DV_DIALOG_STATE_SAVE && m.dialog_slot >= 0) {
        states_save(m, m.dialog_slot);
    } else if (kind == DV_DIALOG_STATE_LOAD && m.dialog_slot >= 0) {
        states_load(m, m.dialog_slot);
    }
    m.dialog_slot = -1;
}

/* The hero and weapon of a save state just loaded become the Play as choice
 * (dragonvalor_heroswap.c left them in dv_heroswap_adopt_*: an empty hero
 * keeps the current one). Draw thread, like the menu's other mod_set calls. */
void adopt_save_choice(Menu& m) {
    if (dv_heroswap_adopt_hero[0])
        for (int i = 0; i < 10; ++i)
            if (std::strcmp(kPlayAsValues[i], dv_heroswap_adopt_hero) == 0 && i != m.play_as) {
                m.play_as = m.play_as_pend = i;
                (void)m.host->mod_set(DV_HS_PKG, DV_HS_FEAT, "hero", kPlayAsValues[i]);
            }
    for (int i = 0; i < DV_WEAPON_CHOICES; ++i)
        if (std::strcmp(kWeaponValues[i], dv_heroswap_adopt_weapon) == 0 && i != m.weapon) {
            m.weapon = m.weapon_pend = i;
            (void)m.host->mod_set(DV_HS_PKG, DV_HS_FEAT, "weapon", kWeaponValues[i]);
        }
    char title[96];
    std::snprintf(title, sizeof title, "Playing as %s", dv_heroswap_mismatch_saved);
    toast(m, title, "As in this save: Play as follows it.");
}

/* button: the Cancel button (Discard when closing) rather than Esc, which
 * only takes the dialog away. */
void dialog_cancel(Menu& m, bool button) {
    const int kind = m.dialog_kind;
    m.dialog.open = false;
    m.dialog_kind = DV_DIALOG_NONE;
    m.dialog_item = nullptr;
    if (kind == DV_DIALOG_HERO_CLOSE && button) {
        m.play_as_pend = m.play_as;
        m.weapon_pend = m.weapon;
        set_open_locked(m, false);
    }
    m.dialog_slot = -1;
}

/* Every key, pad button and mouse click goes through here. */
void dispatch(Menu& m, int input, bool repeat) {
    if (!m.ui || input < 0) return;
    if (m.dialog.open) {
        if (repeat) return;
        switch (input) {
            case RECOMP_RUNTIME_UI_INPUT_LEFT: m.dialog.focus = 1; break;      /* Cancel is on the left */
            case RECOMP_RUNTIME_UI_INPUT_RIGHT: m.dialog.focus = 0; break;
            case RECOMP_RUNTIME_UI_INPUT_UP:
            case RECOMP_RUNTIME_UI_INPUT_DOWN: m.dialog.focus ^= 1; break;
            case RECOMP_RUNTIME_UI_INPUT_ACCEPT:
                if (m.dialog.focus == 0) dialog_confirm(m);
                else dialog_cancel(m, true);
                break;
            case RECOMP_RUNTIME_UI_INPUT_BACK: dialog_cancel(m, false); break;
            default: break;
        }
        return;
    }
    if (m.states.open) {
        switch (input) {
            case RECOMP_RUNTIME_UI_INPUT_ACCEPT: if (!repeat) states_accept(m); break;
            case RECOMP_RUNTIME_UI_INPUT_BACK: if (!repeat) states_close(m); break;
            case DV_INPUT_PREV_SECTION:
            case DV_INPUT_NEXT_SECTION: if (!repeat) m.states.tab ^= 1; break;
            default: states_move(m, input); break;
        }
        return;
    }
    if (input == RECOMP_RUNTIME_UI_INPUT_ACCEPT && m.ui->in_section) {
        const RecompRuntimeUiItem* it = selected_item(m);
        if (it && (is(it, DV_KEY_PLAY_AS) || is(it, DV_KEY_WEAPON))) {
            if (repeat) return;
            if (hero_pending(m)) open_hero_dialog(m, false);
            else recomp_runtime_ui_set_status(m.ui, "Change it with Left / Right");
            return;
        }
    }
    if (input == RECOMP_RUNTIME_UI_INPUT_BACK && !m.ui->in_section) {
        if (!repeat) request_close(m);
        return;
    }
    if (input == DV_INPUT_PREV_SECTION || input == DV_INPUT_NEXT_SECTION) {
        /* the next / previous section, entered if the player is in one */
        const size_t n = m.ui->section_count;
        if (repeat || !n) return;
        const size_t to = input == DV_INPUT_NEXT_SECTION ? (m.ui->section_index + 1) % n
                                                         : (m.ui->section_index + n - 1) % n;
        if (m.ui->in_section) recomp_runtime_ui_enter_section(m.ui, to);
        else m.ui->section_index = to;
        return;
    }
    if (repeat && (input == RECOMP_RUNTIME_UI_INPUT_UP || input == RECOMP_RUNTIME_UI_INPUT_DOWN)) {
        /* a held direction stops at the ends instead of wrapping around */
        const size_t n = m.ui->in_section ? recomp_runtime_ui_page_entry_count(m.ui) : m.ui->section_count;
        const size_t at = m.ui->in_section ? m.ui->row_index : m.ui->section_index;
        if (input == RECOMP_RUNTIME_UI_INPUT_UP ? at == 0 : at + 1 >= n) return;
    }
    (void)recomp_runtime_ui_handle_input(m.ui, (RecompRuntimeUiInput)input, 1, repeat ? 1 : 0);
    sync_open_locked(m);
}

/* ---- the pad: which buttons it shows, held directions ---------------------- */
void note_pad(Menu& m, uint32_t which) {
    DvMenuHints h;
#if defined(PSX_SDL3)
    const SDL_GamepadType type = SDL_GetGamepadTypeForID((SDL_JoystickID)which);
    switch (SDL_GetGamepadButtonLabelForType(type, SDL_GAMEPAD_BUTTON_SOUTH)) {
        case SDL_GAMEPAD_BUTTON_LABEL_CROSS:
            h.style = 1;
            std::snprintf(h.prev, sizeof h.prev, "L1");
            std::snprintf(h.next, sizeof h.next, "R1");
            break;
        case SDL_GAMEPAD_BUTTON_LABEL_B:            /* Nintendo: B below, A on the right */
            h.style = 2;
            std::snprintf(h.accept, sizeof h.accept, "B");
            std::snprintf(h.back, sizeof h.back, "A");
            std::snprintf(h.prev, sizeof h.prev, "L");
            std::snprintf(h.next, sizeof h.next, "R");
            break;
        default:                                    /* Xbox and unknown pads */
            h.style = 2;
            break;
    }
#else
    (void)which;
    h.style = 1;
    std::snprintf(h.prev, sizeof h.prev, "L1");
    std::snprintf(h.next, sizeof h.next, "R1");
#endif
    m.hints = h;
}

void hold(Menu& m, int input, bool stick) {
    m.held = input;
    m.held_stick = stick;
    m.held_count = 0;
    m.held_next = now_ms() + 380;
}

void release(Menu& m, bool stick) {
    if (m.held >= 0 && m.held_stick == stick) m.held = -1;
}

/* The draw callback calls this every frame: a held direction repeats. */
void repeat_held(Menu& m) {
    if (!m.open || m.held < 0 || m.dialog.open) return;
    const uint64_t now = now_ms();
    if (now < m.held_next) return;
    dispatch(m, m.held, true);
    ++m.held_count;
    m.held_next = now + (uint64_t)std::max(35, 110 - 6 * m.held_count);
}

/* The left stick as a D-pad: past 50 % it presses, under 35 % it lets go. */
void stick_moved(Menu& m) {
    const int ax = std::abs(m.stick_x), ay = std::abs(m.stick_y);
    int dir = -1;
    if (ay >= 16384 && ay >= ax) dir = m.stick_y < 0 ? RECOMP_RUNTIME_UI_INPUT_UP : RECOMP_RUNTIME_UI_INPUT_DOWN;
    else if (ax >= 16384) dir = m.stick_x < 0 ? RECOMP_RUNTIME_UI_INPUT_LEFT : RECOMP_RUNTIME_UI_INPUT_RIGHT;
    if (dir >= 0) {
        if (!(m.held == dir && m.held_stick)) {
            dispatch(m, dir, false);
            hold(m, dir, true);
        }
    } else if (ax < 11500 && ay < 11500) {
        release(m, true);
    }
}

bool create_ui(Menu& m) {
    RecompRuntimeUiConfig cfg{};
    cfg.title = "Dragon Valor";
    cfg.subtitle = "PlayStation";
    cfg.theme = "psx";
    cfg.accept_label = "Enter / Cross";
    cfg.back_label = "Esc / Circle";
    cfg.items = kItems;
    cfg.item_count = sizeof kItems / sizeof kItems[0];
    cfg.callbacks.context = &m;
    cfg.callbacks.get_value = cb_get_value;
    cfg.callbacks.set_value = cb_set_value;
    cfg.callbacks.run_action = cb_run_action;
    cfg.callbacks.is_enabled = cb_is_enabled;
    cfg.callbacks.save = cb_save;
    m.ui = recomp_runtime_ui_create(&cfg);
    if (!m.ui) return false;
    recomp_runtime_ui_set_backdrop(m.ui, m.dim / 100.0f, m.opacity / 100.0f);
    return true;
}

/* ---- the runtime's pad shortcut to its save-state picker ----------------------
 * The runtime polls its [hotkeys] save_state_menu_pad (default Select + R1)
 * straight from the pad each frame, after the provider has seen the events,
 * and opens its own picker whatever the provider did (the player still got
 * the old panel from Select + R1, 2026-11-18). A title cannot claim that
 * shortcut, and the menu host's launcher-settings calls carry no shortcuts
 * (mh_launcher_settings_get leaves them out, the in-game apply only does the
 * video), so before main() reads settings.toml the static initializer below
 * turns the shortcut off there while it is the default Select + R1 (the
 * launcher then shows it unbound), and p_event opens the save-state screen
 * on Select + R1 itself. A binding the player changed is left alone; with no
 * settings.toml yet (the first start) nothing is written and the runtime's
 * default stands for that session. Encoding: psxrecomp main.cpp, a
 * combination = 1000 + a mask of SDL game-controller buttons (Back 4, right
 * shoulder 10). */
#define DV_PAD_SELECT_R1 (1000 + (1 << 4) + (1 << 10))
void dv_turn_off_runtime_picker_pad() {
    std::string dir;
    if (const char* base = SDL_GetBasePath()) {
        dir = base;
#if !defined(PSX_SDL3)
        SDL_free((void*)base);
#endif
    }
    const std::string path = dir + "settings.toml";
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return;
    std::string text;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
    std::fclose(f);
    const size_t sec = text.find("[hotkeys]");
    if (sec == std::string::npos) return;
    const size_t next = text.find("\n[", sec + 1);
    const size_t key = text.find("save_state_menu_pad", sec);
    if (key == std::string::npos || (next != std::string::npos && key > next)) return;
    const size_t eq = text.find('=', key), eol = text.find_first_of("\r\n", key);
    if (eq == std::string::npos || (eol != std::string::npos && eq > eol)) return;
    const std::string value = text.substr(eq + 1, (eol == std::string::npos ? text.size() : eol) - eq - 1);
    if (std::atoi(value.c_str()) != DV_PAD_SELECT_R1) return;
    text.replace(eq + 1, value.size(), " 0");
    if (FILE* w = std::fopen(path.c_str(), "wb")) {
        std::fwrite(text.data(), 1, text.size(), w);
        std::fclose(w);
    }
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
    {
        char hero[32];
        m.play_as = 0;
        if (psx_mod_option_value(DV_HS_PKG, DV_HS_FEAT, "hero", hero, sizeof hero))
            for (int i = 1; i < 10; i++)
                if (std::strcmp(hero, kPlayAsValues[i]) == 0) m.play_as = i;
        m.weapon = 0;
        if (psx_mod_option_value(DV_HS_PKG, DV_HS_FEAT, "weapon", hero, sizeof hero))
            for (int i = 1; i < DV_WEAPON_CHOICES; i++)
                if (std::strcmp(hero, kWeaponValues[i]) == 0) m.weapon = i;
        m.play_as_pend = m.play_as;
        m.weapon_pend = m.weapon;
    }
    if (!m.imgui) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        m.assets_dir = host->assets_dir ? host->assets_dir : "";
        m.fonts = DvMenuFonts{};       /* built at the first draw, at the display's scale */
        m.gl_ready = false;
        if (!ImGui_ImplOpenGL3_Init("#version 330")) {
            ImGui::DestroyContext();
            return 0;
        }
        m.imgui = true;
    }
    return 1;
}

/* The menu's four fonts at `scale` (display height / 1080): Lato from the
 * launcher's assets, ImGui's own font if they are missing. ImGui 1.91 bakes
 * glyphs at one size, so a window resized far enough rebuilds them. Draw
 * thread, outside a frame. */
void build_fonts(Menu& m, float scale) {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    auto add = [&](const char* file, float px) -> ImFont* {
        const std::string path = m.assets_dir + "/fonts/" + file;
        if (!m.assets_dir.empty())
            if (FILE* f = std::fopen(path.c_str(), "rb")) {
                std::fclose(f);
                if (ImFont* font = io.Fonts->AddFontFromFileTTF(path.c_str(), px)) return font;
            }
        ImFontConfig cfg;
        cfg.SizePixels = px;
        return io.Fonts->AddFontDefault(&cfg);
    };
    m.fonts.body = add("LatoLatin-Regular.ttf", std::round(21.0f * scale));
    m.fonts.bold = add("LatoLatin-Bold.ttf", std::round(21.0f * scale));
    m.fonts.title = add("LatoLatin-Bold.ttf", std::round(30.0f * scale));
    m.fonts.small = add("LatoLatin-Regular.ttf", std::round(17.0f * scale));
    m.fonts.scale = scale;
    if (m.gl_ready) {                   /* else the backend's first NewFrame makes the texture */
        ImGui_ImplOpenGL3_DestroyFontsTexture();
        ImGui_ImplOpenGL3_CreateFontsTexture();
    }
}

void p_detach(void* ctx) {
    Menu& m = *static_cast<Menu*>(ctx);
    std::lock_guard<std::mutex> lk(m.mu);
    dv_settings_sync(m);               /* numpad +/- volume changes since attach */
    if (m.open) { recomp_runtime_ui_close(m.ui); m.open = false; }
    m.dialog.open = false;
    if (m.imgui) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui::DestroyContext();
        m.imgui = false;
        m.fonts = DvMenuFonts{};
        m.gl_ready = false;
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
        case SDL_SCANCODE_TAB: case SDL_SCANCODE_PAGEDOWN: return DV_INPUT_NEXT_SECTION;   /* Shift+Tab: below */
        case SDL_SCANCODE_PAGEUP: return DV_INPUT_PREV_SECTION;
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
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return DV_INPUT_PREV_SECTION;
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return DV_INPUT_NEXT_SECTION;
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
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return DV_INPUT_PREV_SECTION;
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return DV_INPUT_NEXT_SECTION;
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
        const bool shift = (ev->key.mod & SDL_KMOD_SHIFT) != 0;
#else
    case SDL_KEYDOWN:
    case SDL_KEYUP: {
        const bool down = ev->type == SDL_KEYDOWN;
        const int sc = ev->key.keysym.scancode;
        const bool repeat = ev->key.repeat != 0;
        const bool shift = (ev->key.keysym.mod & KMOD_SHIFT) != 0;
#endif
        if (down) m.hints.style = 0;   /* the footer shows keys again */
#if defined(PSX_SDL3)
        const int keycode = (int)ev->key.key, keymod = (int)ev->key.mod;
#else
        const int keycode = (int)ev->key.keysym.sym, keymod = (int)ev->key.keysym.mod;
#endif
        if (host_keymap_match_event(HOST_KEYMAP_SAVE_STATE_MENU, keycode, sc, keymod)) {
            /* the save-state screen instead of the runtime's own picker */
            if (down && !repeat && !m.dialog.open) {
                if (!m.open) states_open(m, 1, true);
                else if (m.states.open) states_close(m);
                else states_open(m, 1, false);
            }
            return 1;
        }
        if (sc == SDL_SCANCODE_ESCAPE) {
            if (down && !repeat) {
                if (m.open) dispatch(m, RECOMP_RUNTIME_UI_INPUT_BACK, false);
                else set_open_locked(m, true);
            }
            return 1;
        }
        if (!m.open) return 0;
        int input = key_to_input(sc);
        if (input == DV_INPUT_NEXT_SECTION && sc == SDL_SCANCODE_TAB && shift) input = DV_INPUT_PREV_SECTION;
        if (input >= 0 && down) dispatch(m, input, repeat);
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
                dispatch(m, RECOMP_RUNTIME_UI_INPUT_BACK, false);
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
        const uint32_t which = (uint32_t)ev->gbutton.which;
#else
    case SDL_CONTROLLERBUTTONDOWN:
    case SDL_CONTROLLERBUTTONUP: {
        const bool down = ev->type == SDL_CONTROLLERBUTTONDOWN;
        const int button = ev->cbutton.button;
        const bool is_start = button == SDL_CONTROLLER_BUTTON_START;
        const bool is_back = button == SDL_CONTROLLER_BUTTON_BACK;
        const uint32_t which = (uint32_t)ev->cbutton.which;
#endif
        if (down) note_pad(m, which);   /* the footer shows this pad's buttons */
        if (is_start) m.pad_start = down;
        if (is_back) m.pad_back = down;
        if (m.pad_start && m.pad_back && !m.combo_fired) {
            m.combo_fired = true;
            if (m.open) request_close(m);
            else set_open_locked(m, true);
            return 1;
        }
        if (!m.pad_start && !m.pad_back) m.combo_fired = false;
#if defined(PSX_SDL3)
        const bool is_r1 = button == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER;
#else
        const bool is_r1 = button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
#endif
        if (is_r1 && m.pad_back) {
            /* Select + R1: the save-state screen (see dv_turn_off_runtime_picker_pad) */
            if (down && !m.dialog.open) {
                if (!m.open) states_open(m, 1, true);
                else if (m.states.open) states_close(m);
                else states_open(m, 1, false);
            }
            return 1;
        }
        if (!m.open) return 0;
        if (is_start && down) { request_close(m); return 1; }
        const int input = pad_to_input(button);
        const bool direction = input == RECOMP_RUNTIME_UI_INPUT_UP || input == RECOMP_RUNTIME_UI_INPUT_DOWN ||
                               input == RECOMP_RUNTIME_UI_INPUT_LEFT || input == RECOMP_RUNTIME_UI_INPUT_RIGHT;
        if (input >= 0 && down) {
            dispatch(m, input, false);
            if (direction && m.open) hold(m, input, false);   /* held: repeats from the draw callback */
        } else if (direction && !down && m.held == input) {
            release(m, false);
        }
        return 1;
    }
#if defined(PSX_SDL3)
    case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
        const int axis = ev->gaxis.axis, value = ev->gaxis.value;
        const bool lx = axis == SDL_GAMEPAD_AXIS_LEFTX, ly = axis == SDL_GAMEPAD_AXIS_LEFTY;
#else
    case SDL_CONTROLLERAXISMOTION: {
        const int axis = ev->caxis.axis, value = ev->caxis.value;
        const bool lx = axis == SDL_CONTROLLER_AXIS_LEFTX, ly = axis == SDL_CONTROLLER_AXIS_LEFTY;
#endif
        if (!m.open) return 0;
        if (lx) m.stick_x = value;
        if (ly) m.stick_y = value;
        if (lx || ly) stick_moved(m);
        return 1;
    }
    default:
        return 0;
    }
}

void p_draw(void* ctx, int width, int height) {
    Menu& m = *static_cast<Menu*>(ctx);
    std::lock_guard<std::mutex> lk(m.mu);
    if (!m.ui || !m.imgui) return;
    repeat_held(m);                     /* a held D-pad direction or stick */
    if (dv_heroswap_mismatch) {
        /* a state made with another hero / weapon was loaded: its choice
         * becomes the current one (the player, 2026-11-18: rather than a
         * dialog each time), written like a Hero choice and said in a notice */
        dv_heroswap_mismatch = 0;
        adopt_save_choice(m);
    }
    if (m.states_pending >= 0 && !savestate_pending()) {   /* a save landed: its date and picture */
        states_refresh(m, m.states_pending);
        m.states_pending = -1;
    }
    const uint64_t now = now_ms();
    const bool toast_up = m.toast.end_ms && now < m.toast.end_ms;
    if (!m.open && !toast_up) return;
    if (width <= 0 || height <= 0) return;
    /* fonts follow the display height, in 5 % steps */
    const float scale = std::round(std::clamp((float)height / 1080.0f, 0.6f, 2.4f) * 20.0f) / 20.0f;
    if (!m.fonts.body || std::fabs(scale - m.fonts.scale) > 0.001f) build_fonts(m, scale);
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)width, (float)height);
    io.DeltaTime = 1.0f / 60.0f;
    m.fb_w = width; m.fb_h = height;
    ImGui_ImplOpenGL3_NewFrame();
    m.gl_ready = true;
    ImGui::NewFrame();
    DvMenuHeader hd;
    if (m.open && hero_loaded()) {
        const int idx = psx_mod_read_byte(0x80086318u);        /* g_Hero.index */
        hd.hero = idx >= 0 && idx < 9;
        if (hd.hero) {
            std::snprintf(hd.name, sizeof hd.name, "%s", kPlayAsChoices[idx + 1]);
            hd.area = (int16_t)psx_mod_read_half(0x800A2F18u);
            hd.stage = (int16_t)psx_mod_read_half(0x800A2F1Au);
            hd.hp = psx_mod_read_half(0x8008A5AEu);
            hd.hpmax = psx_mod_read_half(0x8008A5B0u);
            hd.mp = psx_mod_read_half(0x8008A5B2u);
            hd.mpmax = psx_mod_read_half(0x8008A5B4u);
        }
    }
    const DvMenuClick click =
        dv_menu_view_draw(m.ui, m.open, m.fonts, hd, m.dialog, m.toast, m.hints, m.states, now, row_info, &m);
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    if (!toast_up) m.toast.end_ms = 0;
    /* the mouse, after the frame: the same paths as the keys */
    if (click.slot >= 0 && m.states.open && !m.dialog.open) {
        m.states.selected = click.slot;
        m.slot = click.slot + 1;
    }
    if (click.tab >= 0 && m.states.open && !m.dialog.open) m.states.tab = click.tab;
    if (click.cols > 0) m.states_cols = click.cols;
    if (click.dialog == 1) dialog_confirm(m);
    else if (click.dialog == 2) dialog_cancel(m, true);
    else if (click.input >= 0) dispatch(m, click.input, false);
}

const PsxMenuProvider g_provider = {
    PSX_MENU_ABI_VERSION, "Dragon Valor menu", &g_menu,
    p_attach, p_event, p_is_open, p_set_open, p_draw, p_detach,
};

struct Registrar {
    Registrar() {
        (void)psx_menu_set_provider(&g_provider);
        dv_turn_off_runtime_picker_pad();   /* Select + R1 is ours (p_event) */
        dv_menu_present = 1;   /* the heroswap plugin leaves its notices to us */
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
