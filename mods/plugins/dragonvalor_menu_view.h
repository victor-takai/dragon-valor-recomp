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

/* What the mouse did this frame, for the owner to route like a key. */
struct DvMenuClick {
    int input = -1;   /* a RecompRuntimeUiInput, or -1 */
    int dialog = 0;   /* 1 = the action button, 2 = Cancel */
};

/* Draws the open menu and its dialog, then the toast (if one is up). */
DvMenuClick dv_menu_view_draw(RecompRuntimeUi* ui, bool open, const DvMenuFonts& fonts,
                              const DvMenuHeader& header, const DvMenuDialog& dialog,
                              const DvMenuToast& toast, uint64_t now_ms,
                              DvMenuRowInfoFn row_info, void* row_ctx);

#endif
