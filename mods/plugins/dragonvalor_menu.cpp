/*
 * Dragon Valor (USA) in-game menu: the title's PsxMenuProvider.
 *
 * psxrecomp owns the contract (psx_menu_host.h): the runtime pauses the guest,
 * applies video settings live, records mod changes and runs disc changes; the
 * provider only draws a menu and translates input. recomp-ui owns the menu
 * model and its Dear ImGui presentation (recomp_runtime_ui.h): sections, rows,
 * navigation, status line. This file is the glue: it registers the provider
 * (static initializer, before the game window opens), opens on Escape or
 * Select+Start, routes keyboard/gamepad input into the model, and maps the
 * rows to the host.
 *
 * Threads: attach/event/is_open/set_open run on the main thread, draw on the
 * thread that owns the GL context (the main thread unless the render thread
 * is on). The model is used from both, under one mutex; the ImGui context is
 * touched by draw only (no platform backend: the model navigates itself).
 */
#if defined(RECOMP_LAUNCHER)

#include <cstdio>
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

/* ---- live RAM editors: a row shows the value in guest RAM and writes it ----
 * Addresses: docs/DEVELOPMENT.md, "Known RAM addresses (USA EXE)". The rows
 * are enabled only while a hero is loaded (HPmax != 0); the guest is paused
 * while the menu is up and the event path runs on the emulation thread, so a
 * plain write is a one-shot cheat, not a per-frame lock. */
struct RamRow { const char* key; uint32_t addr; int width; };
#define DV_RAM_VAL "dv.ram.val"
const RamRow kRamRows[] = {
    {DV_RAM_VAL, 0x800A2DD0u, 2},        {"dv.ram.hpmax", 0x8008A5B0u, 2},
    {"dv.ram.mpmax", 0x8008A5B4u, 2},    {"dv.ram.str", 0x8008A5B6u, 2},
    {"dv.ram.def", 0x8008A5B8u, 2},
    {"dv.ram.magic0", 0x800A2E06u, 1}, {"dv.ram.magic1", 0x800A2E07u, 1},
    {"dv.ram.magic2", 0x800A2E08u, 1}, {"dv.ram.magic3", 0x800A2E09u, 1},
    {"dv.ram.magic4", 0x800A2E0Au, 1}, {"dv.ram.magic5", 0x800A2E0Bu, 1},
    {"dv.ram.magic6", 0x800A2E0Cu, 1}, {"dv.ram.magic7", 0x800A2E0Du, 1},
    {"dv.ram.item0", 0x800A2DD2u, 2},  {"dv.ram.item1", 0x800A2DD4u, 2},
    {"dv.ram.item2", 0x800A2DD6u, 2},  {"dv.ram.item3", 0x800A2DD8u, 2},
    {"dv.ram.item4", 0x800A2DDAu, 2},  {"dv.ram.item5", 0x800A2DDCu, 2},
    {"dv.ram.item6", 0x800A2DDEu, 2},  {"dv.ram.item7", 0x800A2DE0u, 2},
    {"dv.ram.item8", 0x800A2DE2u, 2},  {"dv.ram.item9", 0x800A2DE4u, 2},
    {"dv.ram.item10", 0x800A2DE6u, 2}, {"dv.ram.item11", 0x800A2DE8u, 2},
    {"dv.ram.item12", 0x800A2DEAu, 2}, {"dv.ram.item13", 0x800A2DECu, 2},
    {"dv.ram.item14", 0x800A2DEEu, 2}, {"dv.ram.item15", 0x800A2DF0u, 2},
    {"dv.ram.item16", 0x800A2DF2u, 2},
};
const char* const kMagicChoices[] = {"Not learned", "Level 1", "Level 2", "Level 3"};

#define DV_ROW_INT(key, section, label, desc, lo, hi, step) \
    {key, section, label, desc, RECOMP_RUNTIME_UI_INT, lo, hi, step, nullptr, 0, nullptr}
#define DV_ROW_MAGIC(key, label) \
    {key, "Magic", label, "Spell level, written to the game at once.", RECOMP_RUNTIME_UI_CHOICE, 0, 3, 1, kMagicChoices, 4, nullptr}
#define DV_ROW_ITEM(key, label) \
    DV_ROW_INT(key, "Items", label, "How many you carry, written at once.", 0, 99, 1)

const RecompRuntimeUiItem kExtraItems[] = {
    {DV_KEY_TEXFILTER, "Graphics", "Texture filtering",
     "Nearest is the PS1 look; xBR smooths diagonals at high resolution.",
     RECOMP_RUNTIME_UI_CHOICE, 0, 2, 1, kTextureFilterChoices, 3, kTextureFilterValues},
    {DV_KEY_SCANLINES, "Graphics", "Scanlines", "Draw CRT-style scanlines over the picture.",
     RECOMP_RUNTIME_UI_BOOL, 0, 1, 1, nullptr, 0, nullptr},
    {DV_KEY_DYNRES, "Graphics", "Dynamic resolution",
     "Lower the internal resolution when a frame runs late.",
     RECOMP_RUNTIME_UI_BOOL, 0, 1, 1, nullptr, 0, nullptr},
    {DV_KEY_CHEAT_HP, "Cheats", "Infinite HP", "HP refills every frame. Applies at once.",
     RECOMP_RUNTIME_UI_BOOL, 0, 1, 1, nullptr, 0, nullptr},
    {DV_KEY_CHEAT_MP, "Cheats", "Infinite MP", "MP refills every frame. Applies at once.",
     RECOMP_RUNTIME_UI_BOOL, 0, 1, 1, nullptr, 0, nullptr},
    {DV_KEY_GEM_EVERY, "Cheats", "Gem every N sword kills",
     "0 = game rule (15, 22, 28, ... sword kills). N = next gem after N sword kills; 1 = every kill.",
     RECOMP_RUNTIME_UI_INT, 0, 15, 1, nullptr, 0, nullptr},
    {DV_KEY_SCROLL_EVERY, "Cheats", "Scroll every N magic kills",
     "0 = game rule (2, 4, 7, 12, 20 magic kills). N = next scroll after N magic kills; 1 = every kill.",
     RECOMP_RUNTIME_UI_INT, 0, 5, 1, nullptr, 0, nullptr},
    DV_ROW_INT(DV_RAM_VAL, "Cheats", "Val", "Your gold, written at once (the HUD shows four digits).", 0, 9999, 100),
    DV_ROW_INT("dv.ram.hpmax", "Cheats", "Max HP", "Maximum HP, written at once.", 1, 999, 10),
    DV_ROW_INT("dv.ram.mpmax", "Cheats", "Max MP", "Maximum MP, written at once.", 1, 999, 10),
    DV_ROW_INT("dv.ram.str", "Cheats", "STR", "Attack power, spread over the game's five hidden hit values so damage really changes.", 0, 999, 5),
    DV_ROW_INT("dv.ram.def", "Cheats", "DEF", "Defense shown on the Status screen, written at once.", 0, 999, 5),
    DV_ROW_MAGIC("dv.ram.magic0", "Heal"),    DV_ROW_MAGIC("dv.ram.magic1", "Defense"),
    DV_ROW_MAGIC("dv.ram.magic2", "Fire"),    DV_ROW_MAGIC("dv.ram.magic3", "Ice"),
    DV_ROW_MAGIC("dv.ram.magic4", "Thunder"), DV_ROW_MAGIC("dv.ram.magic5", "Mine"),
    DV_ROW_MAGIC("dv.ram.magic6", "Stealth"), DV_ROW_MAGIC("dv.ram.magic7", "Vortex"),
    DV_ROW_ITEM("dv.ram.item0", "Pendant"),     DV_ROW_ITEM("dv.ram.item1", "Lithograph"),
    DV_ROW_ITEM("dv.ram.item2", "Ring"),        DV_ROW_ITEM("dv.ram.item3", "Talisman"),
    DV_ROW_ITEM("dv.ram.item4", "Wine"),        DV_ROW_ITEM("dv.ram.item5", "Pepper"),
    DV_ROW_ITEM("dv.ram.item6", "Earrings"),    DV_ROW_ITEM("dv.ram.item7", "Bracelet"),
    DV_ROW_ITEM("dv.ram.item8", "Horn"),        DV_ROW_ITEM("dv.ram.item9", "Perfume"),
    DV_ROW_ITEM("dv.ram.item10", "Tea"),        DV_ROW_ITEM("dv.ram.item11", "Mask"),
    DV_ROW_ITEM("dv.ram.item12", "Silver Key"), DV_ROW_ITEM("dv.ram.item13", "Gold Key"),
    DV_ROW_ITEM("dv.ram.item14", "Harp"),       DV_ROW_ITEM("dv.ram.item15", "Tiara"),
    DV_ROW_ITEM("dv.ram.item16", "Copper Key"),
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

void apply_video(Menu& m, const PsxHostVideoSettings& v) {
    const unsigned bits = m.host->video_apply(&v);
    if (bits & PSX_MENU_RESTART_AFTER_MATCH) recomp_runtime_ui_set_status(m.ui, "Applies after the match");
    else if (bits) recomp_runtime_ui_set_status(m.ui, "Restart the game to apply");
}

void set_mod(Menu& m, const char* pkg, const char* feat, const char* opt, const char* value) {
    const int live = m.host->mod_set(pkg, feat, opt, value);
    recomp_runtime_ui_set_status(m.ui, live ? "Applied" : "Saved; takes effect at the next start");
}

int cb_set_value(void* ctx, const RecompRuntimeUiItem* item, int value) {
    Menu& m = *static_cast<Menu*>(ctx);
    PsxHostVideoSettings v{};
    char text[16];
    if (is(item, RECOMP_RUNTIME_UI_KEY_FULLSCREEN)) {
        m.host->video_get(&v); v.fullscreen = value ? (v.fullscreen ? v.fullscreen : 1) : 0; apply_video(m, v); return 1;
    }
    if (is(item, RECOMP_RUNTIME_UI_KEY_LINEAR_FILTER)) { m.host->video_get(&v); v.present_linear = value ? 1 : 0; apply_video(m, v); return 1; }
    if (is(item, DV_KEY_TEXFILTER)) { m.host->video_get(&v); v.texture_filter = value; apply_video(m, v); return 1; }
    if (is(item, DV_KEY_SCANLINES)) { m.host->video_get(&v); v.scanlines = value ? 1 : 0; apply_video(m, v); return 1; }
    if (is(item, DV_KEY_DYNRES)) { m.host->video_get(&v); v.dynamic_resolution = value ? 1 : 0; apply_video(m, v); return 1; }
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
        ram_write(*r, value);
        std::snprintf(text, sizeof text, "Written: %d", value);
        recomp_runtime_ui_set_status(m.ui, text);
        return 1;
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
    if (is(item, RECOMP_RUNTIME_UI_KEY_RETURN_TO_LAUNCHER)) { m.host->request_exit(PSX_MENU_EXIT_TO_FRONTEND); return 1; }
    if (is(item, RECOMP_RUNTIME_UI_KEY_QUIT)) { m.host->request_exit(PSX_MENU_EXIT_QUIT); return 1; }
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
        m.open = true;
        if (m.pause_in_menu && !m.host->set_paused(1))
            recomp_runtime_ui_set_status(m.ui, "Running: pause refused during netplay");
    } else if (!open && m.open) {
        recomp_runtime_ui_close(m.ui);
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
    (void)sdl_window; (void)gl_context;
    std::lock_guard<std::mutex> lk(m.mu);
    if (!host || host->abi_version != PSX_MENU_ABI_VERSION) return 0;
    m.host = host;
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
