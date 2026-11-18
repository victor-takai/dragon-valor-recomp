/*
 * Dragon Valor in-game menu: its look. dragonvalor_menu.cpp owns the menu
 * (recomp-ui's runtime model, the rows, input, dialogs and notices); this
 * draws it with Dear ImGui inside the caller's frame, in the title's own
 * style instead of recomp-ui's generic presentation (the player found that
 * one plain: 2026-11-18). Mouse input comes back as the same semantic inputs
 * the keys produce, so one path handles both.
 */
#ifndef DRAGONVALOR_MENU_VIEW_H
#define DRAGONVALOR_MENU_VIEW_H

#include <cstddef>
#include <cstdint>

#include "recomp_runtime_ui.h"

struct ImFont;

struct DvMenuFonts {
    ImFont* body = nullptr;    /* rows, sections */
    ImFont* bold = nullptr;    /* page titles, hero name, buttons */
    ImFont* title = nullptr;   /* the game's name, dialog titles */
    ImFont* small = nullptr;   /* help, footer, header details */
    float scale = 1.0f;        /* layout scale: display height / 1080 */
};

/* A question over the menu: Cancel on the left, the action on the right. */
struct DvMenuDialog {
    bool open = false;
    char title[96] = "";
    char body[400] = "";
    char ok[48] = "OK";
    char cancel[32] = "Cancel";
    bool danger = false;   /* a crimson action button (leaves the game) */
    int focus = 0;         /* 0 = the action, 1 = Cancel */
    bool alone = false;    /* over the game, without the menu behind it */
};

/* A notice near the top of the screen, shown with the menu open or closed. */
struct DvMenuToast {
    char title[96] = "";
    char body[200] = "";
    uint64_t start_ms = 0, end_ms = 0;   /* end_ms 0: none */
};

/* The header's right side: who is playing, where. */
struct DvMenuHeader {
    bool hero = false;
    char name[48] = "";
    int area = 0, stage = 0;
    int hp = 0, hpmax = 0, mp = 0, mpmax = 0;
};

/* What the owner knows about a row and the model does not. */
struct DvMenuRowInfo {
    bool danger = false;    /* leaves the game: crimson label */
    bool pending = false;   /* a choice made but not applied yet */
    char note[48] = "";     /* muted text before the value: "empty", "in the drive" */
};
typedef void (*DvMenuRowInfoFn)(void* ctx, const RecompRuntimeUiItem* item, DvMenuRowInfo* out);

/* The footer's button hints: the keyboard's keys, or the pad's buttons once
 * the player uses a pad (shapes on a PlayStation pad, letters on the others,
 * as SDL labels the south / east buttons). */
struct DvMenuHints {
    int style = 0;           /* 0 keyboard, 1 PlayStation shapes, 2 letters */
    char accept[4] = "A";    /* style 2: the south button's letter */
    char back[4] = "B";      /* style 2: the east button's letter */
    char prev[4] = "LB";     /* the shoulder buttons */
    char next[4] = "RB";
};

/* The save-state screen (F7, or Game > Save state / Load state), drawn in the
 * menu's frame instead of its sections: a Save and a Load tab over the
 * runtime's 12 slots, each with its thumbnail and date. The owner fills it
 * from savestate.h; the view uploads the thumbnails as textures. */
#define DV_STATE_SLOTS 12
#define DV_THUMB_W 128
#define DV_THUMB_H 96
struct DvStatesView {
    bool open = false;
    int tab = 1;            /* 0 Save, 1 Load */
    int selected = 0;       /* 0-based slot */
    struct Slot {
        bool used = false;
        bool compatible = true;
        char date[40] = "";
    } slots[DV_STATE_SLOTS];
    uint32_t thumb[DV_STATE_SLOTS][DV_THUMB_W * DV_THUMB_H];   /* 0xAARRGGBB */
    bool have_thumb[DV_STATE_SLOTS] = {};
    unsigned thumb_gen[DV_STATE_SLOTS] = {};   /* bumped when a thumbnail changes */
};

/* What the mouse did this frame, for the owner to route like a key. */
struct DvMenuClick {
    int input = -1;   /* a RecompRuntimeUiInput, or -1 */
    int dialog = 0;   /* 1 = the action button, 2 = Cancel */
    int slot = -1;    /* the save-state screen: a slot pointed at */
    int tab = -1;     /* the save-state screen: a tab clicked */
    int cols = 0;     /* the save-state screen: the grid's columns this frame */
};

/* Draws the open menu and its dialog, then the toast (if one is up). */
DvMenuClick dv_menu_view_draw(RecompRuntimeUi* ui, bool open, const DvMenuFonts& fonts,
                              const DvMenuHeader& header, const DvMenuDialog& dialog,
                              const DvMenuToast& toast, const DvMenuHints& hints, const DvStatesView& states,
                              uint64_t now_ms, DvMenuRowInfoFn row_info, void* row_ctx);

#endif
