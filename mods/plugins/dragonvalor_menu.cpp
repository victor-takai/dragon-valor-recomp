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
#define DV_KEY_CHEAT_VAL   "dv.cheats.val_floor"
#define DV_KEY_SKIP_INTRO  "dv.game.skip_intro"
#define DV_KEY_DISC1       "dv.disc.1"
#define DV_KEY_DISC2       "dv.disc.2"

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
    {DV_KEY_CHEAT_VAL, "Cheats", "Val floor",
     "Val never drops below this amount (0 = off). Applies at once.",
     RECOMP_RUNTIME_UI_INT, 0, 9999, 100, nullptr, 0, nullptr},
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
    int cheat_hp = 0, cheat_mp = 0, cheat_val = 0, skip_intro = 0;
    /* gamepad combo */
    bool pad_start = false, pad_back = false, combo_fired = false;
};

Menu g_menu;

int opt_int(const char* pkg, const char* feat, const char* opt, int dflt) {
    char text[32];
    if (!psx_mod_option_value(pkg, feat, opt, text, sizeof text)) return dflt;
    if (text[0] == 't') return 1;
    if (text[0] == 'f') return 0;
    return std::atoi(text);
}

/* ---- model callbacks (main thread, under g_menu.mu) ----------------------- */
bool is(const RecompRuntimeUiItem* item, const char* key) { return std::strcmp(item->key, key) == 0; }

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
    if (is(item, DV_KEY_CHEAT_VAL)) { *out = m.cheat_val; return 1; }
    if (is(item, DV_KEY_SKIP_INTRO)) { *out = m.skip_intro; return 1; }
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
    if (is(item, RECOMP_RUNTIME_UI_KEY_VOLUME)) { host_volume_set(value); return 1; }
    if (is(item, RECOMP_RUNTIME_UI_KEY_MENU_DIM)) { m.dim = value; recomp_runtime_ui_set_backdrop(m.ui, m.dim / 100.0f, m.opacity / 100.0f); return 1; }
    if (is(item, RECOMP_RUNTIME_UI_KEY_MENU_OPACITY)) { m.opacity = value; recomp_runtime_ui_set_backdrop(m.ui, m.dim / 100.0f, m.opacity / 100.0f); return 1; }
    if (is(item, RECOMP_RUNTIME_UI_KEY_PAUSE_IN_MENU)) {
        m.pause_in_menu = value != 0;
        if (m.open) (void)m.host->set_paused(m.pause_in_menu ? 1 : 0);
        return 1;
    }
    if (is(item, RECOMP_RUNTIME_UI_KEY_STATE_SLOT)) { m.slot = value; return 1; }
    if (is(item, DV_KEY_CHEAT_HP)) { m.cheat_hp = value ? 1 : 0; set_mod(m, DV_CHEATS_PKG, DV_CHEATS_FEAT, "infinite_hp", value ? "true" : "false"); return 1; }
    if (is(item, DV_KEY_CHEAT_MP)) { m.cheat_mp = value ? 1 : 0; set_mod(m, DV_CHEATS_PKG, DV_CHEATS_FEAT, "infinite_mp", value ? "true" : "false"); return 1; }
    if (is(item, DV_KEY_CHEAT_VAL)) {
        m.cheat_val = value; std::snprintf(text, sizeof text, "%d", value);
        set_mod(m, DV_CHEATS_PKG, DV_CHEATS_FEAT, "val_floor", text); return 1;
    }
    if (is(item, DV_KEY_SKIP_INTRO)) { m.skip_intro = value ? 1 : 0; set_mod(m, DV_SKIP_PKG, DV_SKIP_FEAT, nullptr, value ? "true" : "false"); return 1; }
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
    if (!m.ui && !create_ui(m)) return 0;
    /* the committed selection is what the launcher left; mirror it */
    m.cheat_hp = opt_int(DV_CHEATS_PKG, DV_CHEATS_FEAT, "infinite_hp", 0);
    m.cheat_mp = opt_int(DV_CHEATS_PKG, DV_CHEATS_FEAT, "infinite_mp", 0);
    m.cheat_val = opt_int(DV_CHEATS_PKG, DV_CHEATS_FEAT, "val_floor", 0);
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
    Registrar() { (void)psx_menu_set_provider(&g_provider); }
} g_registrar;

}  // namespace

#endif  /* RECOMP_LAUNCHER */
