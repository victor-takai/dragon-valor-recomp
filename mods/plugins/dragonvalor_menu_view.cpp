/*
 * Dragon Valor in-game menu: its look (see dragonvalor_menu_view.h).
 *
 * Reads recomp-ui's runtime model directly, as recomp-ui's own ImGui
 * presentation does (recomp_runtime_ui_internal.h: sections, the open page's
 * rows, selection, status), and draws the title's menu: a dark panel with a
 * gold accent, the game's name and who is playing on top, sections on the
 * left, one line per row on the right (switches, arrows around a choice, a
 * chevron on an action), the selected row's help under the rows, key caps in
 * the footer. A dialog and the toast go on the foreground draw list, over
 * everything. Sizes scale with the display height (fonts.scale).
 *
 * The mouse never changes a value here: hovering selects (only when the
 * pointer moves, so a pointer resting on the menu does not fight the keys),
 * a click comes back as DvMenuClick for the owner to route like a key.
 */
#if defined(RECOMP_LAUNCHER)

#define IMGUI_DEFINE_MATH_OPERATORS
#include "dragonvalor_menu_view.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "imgui.h"
#include "../../recomp-ui/src/common/recomp_runtime_ui_internal.h"
/* The OpenGL entry points ImGui's OpenGL3 backend loaded (imgl3w, its
 * global proc table): the save-state thumbnails become textures with them. */
#include "imgui_impl_opengl3_loader.h"
#ifndef GL_UNPACK_ALIGNMENT
#define GL_UNPACK_ALIGNMENT 0x0CF5
#endif
#ifndef GL_TEXTURE_WRAP_T
#define GL_TEXTURE_WRAP_T 0x2803
#endif

namespace {

ImU32 rgba(int r, int g, int b, float a = 1.0f) {
    return IM_COL32(r, g, b, (int)(std::clamp(a, 0.0f, 1.0f) * 255.0f + 0.5f));
}

struct Pal {
    ImU32 panel, page, edge, row_hover, row_sel, rule, ink;
    ImU32 gold, gold_soft, gold_faint, gold_clear;
    ImU32 text, muted, faint, crimson, crimson_text, hp, mp, dark_text;
};

/* `bg` scales the backgrounds (the Menu opacity row), `fade` everything (a
 * toast fading in and out). */
Pal palette(float bg, float fade = 1.0f) {
    Pal p;
    p.panel = rgba(15, 13, 19, 0.97f * bg * fade);
    p.page = rgba(22, 19, 27, 0.96f * bg * fade);
    p.edge = rgba(64, 54, 70, fade);
    p.row_hover = rgba(34, 30, 41, 0.95f * fade);
    p.row_sel = rgba(48, 40, 33, 0.96f * fade);
    p.rule = rgba(58, 50, 64, fade);
    p.ink = rgba(10, 9, 13, 0.9f * fade);
    p.gold = rgba(222, 183, 102, fade);
    p.gold_soft = rgba(222, 183, 102, 0.62f * fade);
    p.gold_faint = rgba(222, 183, 102, 0.28f * fade);
    p.gold_clear = rgba(222, 183, 102, 0.0f);
    p.text = rgba(240, 233, 218, fade);
    p.muted = rgba(163, 154, 139, fade);
    p.faint = rgba(104, 97, 90, fade);
    p.crimson = rgba(178, 52, 62, fade);
    p.crimson_text = rgba(236, 132, 132, fade);
    p.hp = rgba(226, 112, 98, fade);
    p.mp = rgba(116, 156, 224, fade);
    p.dark_text = rgba(24, 18, 10, fade);
    return p;
}

ImU32 with_alpha(ImU32 c, float a) {
    const int al = (int)((float)((c >> IM_COL32_A_SHIFT) & 0xFF) * std::clamp(a, 0.0f, 1.0f) + 0.5f);
    return (c & ~IM_COL32_A_MASK) | ((ImU32)al << IM_COL32_A_SHIFT);
}

ImVec2 measure(ImFont* f, const char* s, float wrap = 0.0f) {
    return f->CalcTextSizeA(f->FontSize, FLT_MAX, wrap, s);
}

void put(ImDrawList* dl, ImFont* f, ImVec2 p, ImU32 c, const char* s, float wrap = 0.0f) {
    dl->AddText(f, f->FontSize, ImVec2(std::floor(p.x), std::floor(p.y)), c, s, nullptr, wrap);
}

/* Capitals with extra tracking, for the game's name and small tags. */
float spaced_width(ImFont* f, const char* s, float sp) {
    float w = 0.0f;
    for (const char* p = s; *p; ++p) {
        const char c[2] = {*p, 0};
        w += measure(f, c).x + (p[1] ? sp : 0.0f);
    }
    return w;
}

void spaced(ImDrawList* dl, ImFont* f, ImVec2 p, ImU32 col, const char* s, float sp) {
    for (const char* q = s; *q; ++q) {
        const char c[2] = {*q, 0};
        put(dl, f, p, col, c);
        p.x += measure(f, c).x + sp;
    }
}

/* Filled shapes go through ImGui's anti-aliased convex fill, which feathers
 * the edge outward only for one winding; the other feathers it inward and
 * the shape comes out smaller and softer (the player saw the left arrow
 * smaller than the right one). Every filled shape here lists its points in
 * that one order: top, left, bottom for a triangle, and so on. */
void diamond(ImDrawList* dl, ImVec2 c, float r, ImU32 col) {
    dl->AddQuadFilled(ImVec2(c.x, c.y - r), ImVec2(c.x - r, c.y), ImVec2(c.x, c.y + r), ImVec2(c.x + r, c.y), col);
}

void diamond_outline(ImDrawList* dl, ImVec2 c, float r, ImU32 col, float th) {
    dl->AddQuad(ImVec2(c.x, c.y - r), ImVec2(c.x + r, c.y), ImVec2(c.x, c.y + r), ImVec2(c.x - r, c.y), col, th);
}

/* A filled triangle pointing left (dir -1) or right (+1), centred on c. */
void arrow(ImDrawList* dl, ImVec2 c, float r, int dir, ImU32 col) {
    const float d = (float)dir;
    const ImVec2 tip(c.x + d * r * 0.7f, c.y), top(c.x - d * r * 0.55f, c.y - r), bottom(c.x - d * r * 0.55f, c.y + r);
    if (dir > 0) dl->AddTriangleFilled(tip, top, bottom, col);
    else dl->AddTriangleFilled(tip, bottom, top, col);   /* mirrored: same winding */
}

void chevron(ImDrawList* dl, ImVec2 c, float r, ImU32 col, float th) {
    const ImVec2 pts[3] = {ImVec2(c.x - r * 0.45f, c.y - r), ImVec2(c.x + r * 0.45f, c.y),
                           ImVec2(c.x - r * 0.45f, c.y + r)};
    dl->AddPolyline(pts, 3, col, ImDrawFlags_None, th);
}

/* A gold rule that fades out to both ends, a diamond in the middle. */
void ornament(ImDrawList* dl, float x0, float x1, float y, const Pal& P, float s) {
    const float mid = std::floor((x0 + x1) * 0.5f), gap = 16.0f * s, h = std::max(1.0f, 1.2f * s);
    dl->AddRectFilledMultiColor(ImVec2(x0, y - h * 0.5f), ImVec2(mid - gap, y + h * 0.5f), P.gold_clear, P.gold_soft,
                                P.gold_soft, P.gold_clear);
    dl->AddRectFilledMultiColor(ImVec2(mid + gap, y - h * 0.5f), ImVec2(x1, y + h * 0.5f), P.gold_soft, P.gold_clear,
                                P.gold_clear, P.gold_soft);
    diamond(dl, ImVec2(mid, y), 4.5f * s, P.gold);
    diamond_outline(dl, ImVec2(mid, y), 9.0f * s, P.gold_faint, std::max(1.0f, 1.0f * s));
}

void toggle(ImDrawList* dl, ImVec2 right_mid, bool on, bool selected, const Pal& P, float s, float alpha) {
    const float w = 46.0f * s, h = 24.0f * s;
    const ImVec2 a(right_mid.x - w, right_mid.y - h * 0.5f), b(right_mid.x, right_mid.y + h * 0.5f);
    if (on) {
        dl->AddRectFilled(a, b, with_alpha(P.gold, alpha), h * 0.5f);
    } else {
        dl->AddRectFilled(a, b, with_alpha(P.ink, alpha), h * 0.5f);
        dl->AddRect(a, b, with_alpha(selected ? P.gold_soft : P.faint, alpha), h * 0.5f, 0, std::max(1.0f, 1.5f * s));
    }
    const float r = h * 0.5f - 4.0f * s;
    const ImVec2 k(on ? b.x - h * 0.5f : a.x + h * 0.5f, right_mid.y);
    dl->AddCircleFilled(k, r, with_alpha(on ? P.panel : (selected ? P.muted : P.faint), on ? 1.0f : alpha), 20);
}

/* Text placed by the shape of its letters, not by the font's line box (which
 * also holds room for accents and descenders, so a label sat low or high in
 * a button: the player saw A / B off centre in their circles). Horizontally
 * the ink of the glyphs, vertically the capital letters' height (glyph 'H'),
 * so a word with a descender still lines up with the others. ASCII labels. */
void ink_box(ImFont* f, float size, const char* s, float* x0, float* x1, float* top, float* base) {
    const float k = size / f->FontSize;
    float x = 0.0f, lo = FLT_MAX, hi = -FLT_MAX;
    for (const char* p = s; *p; ++p) {
        const ImFontGlyph* g = f->FindGlyph((ImWchar)(unsigned char)*p);
        if (!g) continue;
        if (g->Visible) {
            lo = std::min(lo, x + g->X0 * k);
            hi = std::max(hi, x + g->X1 * k);
        }
        x += g->AdvanceX * k;
    }
    if (lo > hi) { lo = 0.0f; hi = x; }
    const ImFontGlyph* H = f->FindGlyph((ImWchar)'H');
    *x0 = lo;
    *x1 = hi;
    *top = H ? H->Y0 * k : 0.0f;
    *base = H ? H->Y1 * k : size;
}

/* Centred on c (both ways). */
void put_centered(ImDrawList* dl, ImFont* f, float size, ImVec2 c, ImU32 col, const char* s) {
    float x0, x1, top, base;
    ink_box(f, size, s, &x0, &x1, &top, &base);
    dl->AddText(f, size, ImVec2(std::round(c.x - (x0 + x1) * 0.5f), std::round(c.y - (top + base) * 0.5f)), col, s);
}

/* Starting at x, its capitals centred on cy. */
void put_mid(ImDrawList* dl, ImFont* f, float x, float cy, ImU32 col, const char* s) {
    float x0, x1, top, base;
    ink_box(f, f->FontSize, s, &x0, &x1, &top, &base);
    dl->AddText(f, f->FontSize, ImVec2(std::round(x), std::round(cy - (top + base) * 0.5f)), col, s);
}

/* A filled triangle pointing up (dir -1) or down (+1), centred on c; the
 * same winding as arrow(). */
void arrow_v(ImDrawList* dl, ImVec2 c, float r, int dir, ImU32 col) {
    const float d = (float)dir;
    const ImVec2 tip(c.x, c.y + d * r * 0.7f), a(c.x - r, c.y - d * r * 0.55f), b(c.x + r, c.y - d * r * 0.55f);
    if (dir < 0) dl->AddTriangleFilled(tip, a, b, col);
    else dl->AddTriangleFilled(tip, b, a, col);
}

/* A key cap and what it does; returns the width used. */
float key_cap(ImDrawList* dl, const DvMenuFonts& F, ImVec2 p, const char* key, const char* label, const Pal& P,
              float s) {
    ImFont* f = F.small;
    const bool arrows = key == nullptr;
    const float h = f->FontSize + 10.0f * s;
    const float kw = arrows ? 40.0f * s : std::max(h, measure(f, key).x + 18.0f * s);
    const ImVec2 a(p.x, p.y), b(p.x + kw, p.y + h);
    dl->AddRectFilled(a, b, P.ink, 6.0f * s);
    dl->AddRect(a, b, P.faint, 6.0f * s, 0, std::max(1.0f, 1.2f * s));
    if (arrows) {
        arrow(dl, ImVec2(a.x + kw * 0.32f, a.y + h * 0.5f), 5.0f * s, -1, P.text);
        arrow(dl, ImVec2(a.x + kw * 0.68f, a.y + h * 0.5f), 5.0f * s, 1, P.text);
    } else {
        put_centered(dl, f, f->FontSize, ImVec2(a.x + kw * 0.5f, a.y + h * 0.5f), P.text, key);
    }
    if (!label[0]) return kw + 6.0f * s;   /* a cap followed by another (L1 R1) */
    put_mid(dl, f, b.x + 9.0f * s, a.y + h * 0.5f, P.muted, label);
    return kw + 9.0f * s + measure(f, label).x + 26.0f * s;
}

/* A round face button: 0 PlayStation cross, 1 PlayStation circle, 2 a letter. */
float pad_button(ImDrawList* dl, const DvMenuFonts& F, ImVec2 p, int kind, const char* letter, const char* label,
                 const Pal& P, float s) {
    ImFont* f = F.small;
    const float h = f->FontSize + 10.0f * s, r = h * 0.5f, th = std::max(1.0f, 1.8f * s);
    const ImVec2 c(p.x + r, p.y + r);
    dl->AddCircleFilled(c, r, P.ink, 28);
    dl->AddCircle(c, r, P.faint, 28, std::max(1.0f, 1.2f * s));
    if (kind == 0) {
        const float k = r * 0.38f;
        dl->AddLine(ImVec2(c.x - k, c.y - k), ImVec2(c.x + k, c.y + k), P.text, th);
        dl->AddLine(ImVec2(c.x - k, c.y + k), ImVec2(c.x + k, c.y - k), P.text, th);
    } else if (kind == 1) {
        dl->AddCircle(c, r * 0.42f, P.text, 24, th);
    } else {
        put_centered(dl, F.bold, F.small->FontSize, c, P.text, letter);
    }
    put_mid(dl, f, p.x + h + 9.0f * s, c.y, P.muted, label);
    return h + 9.0f * s + measure(f, label).x + 26.0f * s;
}

/* A D-pad (2026-11-18, the player asked for a better icon than the plus):
 * a cross with rounded arm ends and a raised centre, an arrow embossed in
 * each arm; the left and right arms are lit, as they change a value. */
float dpad(ImDrawList* dl, const DvMenuFonts& F, ImVec2 p, const char* label, const Pal& P, float s) {
    ImFont* f = F.small;
    const float h = f->FontSize + 12.0f * s, r = h * 0.5f, w = h * 0.36f, rr = 2.5f * s, t = w * 0.30f;
    const ImVec2 c(p.x + r, p.y + (f->FontSize + 10.0f * s) * 0.5f);
    const ImU32 dim = P.muted, lit = P.text, emboss = P.ink;
    dl->AddRectFilled(ImVec2(c.x - w * 0.5f, c.y - r), ImVec2(c.x + w * 0.5f, c.y + r), dim, rr);
    dl->AddRectFilled(ImVec2(c.x - r, c.y - w * 0.5f), ImVec2(c.x - w * 0.5f, c.y + w * 0.5f), lit, rr,
                      ImDrawFlags_RoundCornersLeft);
    dl->AddRectFilled(ImVec2(c.x + w * 0.5f, c.y - w * 0.5f), ImVec2(c.x + r, c.y + w * 0.5f), lit, rr,
                      ImDrawFlags_RoundCornersRight);
    dl->AddRectFilled(ImVec2(c.x - w * 0.5f, c.y - w * 0.5f), ImVec2(c.x + w * 0.5f, c.y + w * 0.5f), P.muted);
    dl->AddCircleFilled(c, w * 0.22f, P.faint, 12);   /* the dimple in the middle */
    arrow(dl, ImVec2(c.x - r + w * 0.62f, c.y), t, -1, emboss);
    arrow(dl, ImVec2(c.x + r - w * 0.62f, c.y), t, 1, emboss);
    arrow_v(dl, ImVec2(c.x, c.y - r + w * 0.62f), t, -1, emboss);
    arrow_v(dl, ImVec2(c.x, c.y + r - w * 0.62f), t, 1, emboss);
    put_mid(dl, f, p.x + h + 9.0f * s, c.y, P.muted, label);
    return h + 9.0f * s + measure(f, label).x + 26.0f * s;
}

struct SectionNote { const char* name; const char* blurb; bool experimental; };
const SectionNote kSectionNotes[] = {
    {"Game", "Save states, discs, and leaving the game.", false},
    {"Hero", "Play the action stages as any of the nine heroes, holding any of the game's swords.", true},
    {"Cheats", "Infinite HP and MP, more drops, Val and the hero's stats.", false},
    {"Magic", "Learn the spells and set their levels.", false},
    {"Items", "How many of each item the hero carries.", false},
    {"Warp", "Jump to another stage through a Game Over and Continue.", true},
    {"Video", "The window and the picture.", false},
    {"Options", "Sound, this menu, and the intro movie.", false},
};
const SectionNote* section_note(const char* name) {
    for (const SectionNote& n : kSectionNotes)
        if (std::strcmp(n.name, name) == 0) return &n;
    return nullptr;
}

void value_text(RecompRuntimeUi* ui, const RecompRuntimeUiItem* item, char* out, size_t n) {
    int value = 0;
    out[0] = 0;
    if (!recomp_runtime_ui_current_value(ui, item, &value)) { std::snprintf(out, n, "-"); return; }
    if (item->type == RECOMP_RUNTIME_UI_CHOICE && item->choices) {
        size_t index = item->choice_count;
        if (item->choice_values) {
            for (size_t i = 0; i < item->choice_count; ++i)
                if (item->choice_values[i] == value) { index = i; break; }
        } else if (value >= item->minimum && (size_t)(value - item->minimum) < item->choice_count) {
            index = (size_t)(value - item->minimum);
        }
        std::snprintf(out, n, "%s", index < item->choice_count ? item->choices[index] : "-");
    } else {
        std::snprintf(out, n, "%d", value);
    }
}

/* ---- the save-state screen ---------------------------------------------------- */

/* One texture per slot, (re)made on the draw thread when the owner bumps the
 * slot's generation. GL state touched here (the bound texture, the unpack
 * row length and alignment) is put back. */
GLuint g_thumb_tex[DV_STATE_SLOTS];
unsigned g_thumb_gen[DV_STATE_SLOTS];

ImTextureID thumb_texture(const DvStatesView& st, int slot) {
    if (!st.have_thumb[slot]) return (ImTextureID)0;
    if (!g_thumb_tex[slot] || g_thumb_gen[slot] != st.thumb_gen[slot]) {
        static uint8_t rgba[DV_THUMB_W * DV_THUMB_H * 4];
        for (int i = 0; i < DV_THUMB_W * DV_THUMB_H; ++i) {
            const uint32_t p = st.thumb[slot][i];
            rgba[4 * i + 0] = (uint8_t)(p >> 16);
            rgba[4 * i + 1] = (uint8_t)(p >> 8);
            rgba[4 * i + 2] = (uint8_t)p;
            rgba[4 * i + 3] = 0xFF;
        }
        GLint last_tex = 0, last_row = 0, last_align = 4;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_tex);
        glGetIntegerv(GL_UNPACK_ROW_LENGTH, &last_row);
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &last_align);
        if (!g_thumb_tex[slot]) glGenTextures(1, &g_thumb_tex[slot]);
        glBindTexture(GL_TEXTURE_2D, g_thumb_tex[slot]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, DV_THUMB_W, DV_THUMB_H, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, last_row);
        glPixelStorei(GL_UNPACK_ALIGNMENT, last_align);
        glBindTexture(GL_TEXTURE_2D, (GLuint)last_tex);
        g_thumb_gen[slot] = st.thumb_gen[slot];
    }
    return (ImTextureID)(intptr_t)g_thumb_tex[slot];
}

/* Tabs, a 6 x 2 grid of slot cards, the selected slot's line. */
void draw_states(ImDrawList* dl, const DvMenuFonts& F, const DvStatesView& st, float x0, float x1, float y0,
                 float y1, bool modal, DvMenuClick& click) {
    ImGuiIO& io = ImGui::GetIO();
    const float s = F.scale;
    const Pal P = palette(1.0f);
    const bool moved = io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f;

    /* tabs */
    const char* tabs[2] = {"Save", "Load"};
    float tx = x0;
    const float tab_h = F.bold->FontSize + 18.0f * s;
    for (int t = 0; t < 2; ++t) {
        const float tw = measure(F.bold, tabs[t]).x + 44.0f * s;
        const ImVec2 a(tx, y0), b(tx + tw, y0 + tab_h);
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(1000 + t);
        ImGui::InvisibleButton("##tab", b - a);
        if (!modal && ImGui::IsItemClicked(ImGuiMouseButton_Left)) click.tab = t;
        const bool hov = !modal && ImGui::IsItemHovered();
        ImGui::PopID();
        const bool on = st.tab == t;
        dl->AddRectFilled(a, b, on ? P.row_sel : (hov ? P.row_hover : P.page), tab_h * 0.5f);
        dl->AddRect(a, b, on ? P.gold_soft : P.edge, tab_h * 0.5f, 0, std::max(1.0f, 1.2f * s));
        put_centered(dl, F.bold, F.bold->FontSize, ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f),
                     on ? P.gold : P.muted, tabs[t]);
        tx = b.x + 12.0f * s;
    }
    {
        const char* note = st.tab == 0 ? "Pick a slot to save this moment in." : "Pick a saved moment to go back to.";
        const ImVec2 ns = measure(F.small, note);
        put(dl, F.small, ImVec2(x1 - ns.x, y0 + (tab_h - ns.y) * 0.5f), P.faint, note);
    }

    /* the grid: 6 x 2 or 4 x 3, whichever gives the larger cards in the room
     * there is (the owner moves the selection on the same grid: click.cols) */
    const float gap = 16.0f * s, gy0 = y0 + tab_h + 22.0f * s, pad = 8.0f * s;
    const float labels = F.bold->FontSize + F.small->FontSize + 22.0f * s;
    const float room_h = y1 - gy0 - (F.small->FontSize + 30.0f * s);   /* the line under the grid */
    int cols = 6;
    float cw = 0.0f;
    for (int c : {6, 4}) {
        const int r = DV_STATE_SLOTS / c;
        const float by_w = (x1 - x0 - gap * (c - 1)) / c;
        const float by_h = ((room_h - gap * (r - 1)) / r - pad - labels) / 0.75f + 2.0f * pad;
        const float w = std::floor(std::min(by_w, by_h));
        if (w > cw) { cw = w; cols = c; }
    }
    click.cols = cols;
    const int rows = DV_STATE_SLOTS / cols;
    const float grid_w = cols * cw + gap * (cols - 1);
    x0 = x0 + std::floor((x1 - x0 - grid_w) * 0.5f);              /* centred */
    x1 = x0 + grid_w;
    const float th = std::floor((cw - 2.0f * pad) * 0.75f);   /* the thumbnail, 4:3 */
    const float ch = pad + th + labels;
    for (int i = 0; i < DV_STATE_SLOTS; ++i) {
        const int c = i % cols, r = i / cols;
        const ImVec2 a(x0 + c * (cw + gap), gy0 + r * (ch + gap)), b(a.x + cw, a.y + ch);
        if (b.y > y1) break;
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(2000 + i);
        ImGui::InvisibleButton("##slot", b - a);
        const bool hov = !modal && ImGui::IsItemHovered();
        if (hov && moved) click.slot = i;
        if (!modal && ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            if (st.selected == i) click.input = RECOMP_RUNTIME_UI_INPUT_ACCEPT;
            else click.slot = i;
        }
        ImGui::PopID();
        const DvStatesView::Slot& sl = st.slots[i];
        const bool sel = st.selected == i;
        dl->AddRectFilled(a, b, sel ? P.row_sel : (hov ? P.row_hover : P.page), 10.0f * s);
        dl->AddRect(a, b, sel ? P.gold : P.edge, 10.0f * s, 0, std::max(1.0f, (sel ? 2.0f : 1.0f) * s));
        const ImVec2 ta(a.x + pad, a.y + pad), tb(b.x - pad, a.y + pad + th);
        const ImTextureID tex = sl.used ? thumb_texture(st, i) : (ImTextureID)0;
        if (tex) {
            dl->AddImageRounded(tex, ta, tb, ImVec2(0, 0), ImVec2(1, 1),
                                sl.compatible ? IM_COL32_WHITE : IM_COL32(255, 255, 255, 110), 6.0f * s);
        } else {
            dl->AddRectFilled(ta, tb, P.ink, 6.0f * s);
            const char* e = sl.used ? "No picture" : "Empty";
            const ImVec2 es = measure(F.small, e);
            put(dl, F.small, ImVec2((ta.x + tb.x - es.x) * 0.5f, (ta.y + tb.y - es.y) * 0.5f), P.faint, e);
        }
        if (sel) dl->AddRect(ta, tb, P.gold_soft, 6.0f * s, 0, std::max(1.0f, 1.0f * s));
        char label[24];
        std::snprintf(label, sizeof label, "Slot %d", i + 1);
        float ly = tb.y + 8.0f * s;
        put(dl, F.bold, ImVec2(a.x + pad + 2.0f * s, ly), sel ? P.gold : P.text, label);
        ly += F.bold->FontSize + 2.0f * s;
        const char* sub = !sl.used ? (st.tab == 0 && sel ? "New save" : "Empty")
                        : !sl.compatible ? "Other build" : sl.date;
        put(dl, F.small, ImVec2(a.x + pad + 2.0f * s, ly),
            !sl.used ? (st.tab == 0 && sel ? P.gold_soft : P.faint) : !sl.compatible ? P.crimson_text : P.muted, sub);
    }

    /* the selected slot, in words */
    const DvStatesView::Slot& sl = st.slots[st.selected];
    char line[160];
    if (!sl.used)
        std::snprintf(line, sizeof line, st.tab == 0 ? "Slot %d is empty: saving here keeps this moment."
                                                     : "Slot %d is empty.", st.selected + 1);
    else if (!sl.compatible)
        std::snprintf(line, sizeof line, "Slot %d was saved by another build of the game and cannot be loaded.",
                      st.selected + 1);
    else
        std::snprintf(line, sizeof line, st.tab == 0 ? "Slot %d, saved %s: saving here replaces it."
                                                     : "Slot %d, saved %s.", st.selected + 1, sl.date);
    const float ly = gy0 + rows * (ch + gap) + 4.0f * s;
    if (ly + F.small->FontSize < y1) {
        dl->AddLine(ImVec2(x0, ly), ImVec2(x1, ly), P.rule, std::max(1.0f, 1.0f * s));
        put(dl, F.small, ImVec2(x0, ly + 12.0f * s), P.muted, line);
    }
}

void draw_menu(RecompRuntimeUi* ui, const DvMenuFonts& F, const DvMenuHeader& hd, const DvMenuHints& hints,
               const DvStatesView& states, bool modal, DvMenuRowInfoFn row_info, void* row_ctx,
               DvMenuClick& click) {
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 D = io.DisplaySize;
    const float s = F.scale;
    const Pal P = palette(ui->opacity);
    const bool moved = io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f;

    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), D,
                                                  IM_COL32(0, 0, 0, (int)(255.0f * ui->dim + 0.5f)));

    const float W = std::floor(std::min(1200.0f * s, D.x - 40.0f * s));
    const float H = std::floor(std::min(800.0f * s, D.y - 40.0f * s));
    const ImVec2 o(std::floor((D.x - W) * 0.5f), std::floor((D.y - H) * 0.5f));
    ImGui::SetNextWindowPos(o);
    ImGui::SetNextWindowSize(ImVec2(W, H));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, std::max(4.0f, 6.0f * s));
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, 3.0f * s);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, P.faint);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, P.gold_soft);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, P.gold);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNavInputs |
                                   ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse;
    if (ImGui::Begin("##dv-menu", nullptr, flags) && ui->section_count) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float R = 18.0f * s, pad = 32.0f * s;
        dl->AddRectFilled(o, o + ImVec2(W, H), P.panel, R);
        dl->AddRect(o, o + ImVec2(W, H), P.edge, R, 0, std::max(1.0f, 1.0f * s));

        /* ---- header: the game's name; who is playing, where ---- */
        const float x0 = o.x + pad, x1 = o.x + W - pad;
        const float head_h = std::max(F.title->FontSize, F.bold->FontSize + F.small->FontSize + 4.0f * s);
        float y = o.y + pad - 6.0f * s;
        spaced(dl, F.title, ImVec2(x0, y + (head_h - F.title->FontSize) * 0.5f), P.gold, "DRAGON VALOR", 4.0f * s);
        if (hd.hero) {
            const float ny = y + (head_h - F.bold->FontSize - F.small->FontSize - 4.0f * s) * 0.5f;
            put(dl, F.bold, ImVec2(x1 - measure(F.bold, hd.name).x, ny), P.text, hd.name);
            char hp[24], mp[24], where[48];
            std::snprintf(hp, sizeof hp, "%d/%d", hd.hp, hd.hpmax);
            std::snprintf(mp, sizeof mp, "%d/%d", hd.mp, hd.mpmax);
            std::snprintf(where, sizeof where, "Area %d \xC2\xB7 Stage %d", hd.area, hd.stage);
            struct Piece { const char* t; ImU32 c; } pieces[] = {
                {"HP ", P.hp}, {hp, P.muted}, {"     MP ", P.mp}, {mp, P.muted}, {"     ", P.muted}, {where, P.muted},
            };
            float w = 0.0f;
            for (const Piece& pc : pieces) w += measure(F.small, pc.t).x;
            float px = x1 - w;
            const float sy = ny + F.bold->FontSize + 4.0f * s;
            for (const Piece& pc : pieces) {
                put(dl, F.small, ImVec2(px, sy), pc.c, pc.t);
                px += measure(F.small, pc.t).x;
            }
        }
        y += head_h + 16.0f * s;
        ornament(dl, x0, x1, y, P, s);
        y += 24.0f * s;

        /* ---- footer: key caps; the status on the right ---- */
        const float foot_h = F.small->FontSize + 10.0f * s;
        const float foot_y = o.y + H - pad + 8.0f * s - foot_h;
        const float body_y1 = foot_y - 18.0f * s;
        {
            float fx = x0;
            /* on the save-state screen: what Enter does on this tab, and the tabs */
            const char* act = states.open ? (states.tab == 0 ? "Save" : "Load") : "Select";
            const char* move = states.open ? "Slot" : "Change";
            const char* swap = states.open ? "Save / Load" : "Section";
            if (hints.style == 0) {
                fx += key_cap(dl, F, ImVec2(fx, foot_y), "Enter", act, P, s);
                fx += key_cap(dl, F, ImVec2(fx, foot_y), "Esc", "Back", P, s);
                fx += key_cap(dl, F, ImVec2(fx, foot_y), nullptr, move, P, s);
                fx += key_cap(dl, F, ImVec2(fx, foot_y), "Tab", swap, P, s);
            } else {
                const bool ps = hints.style == 1;
                fx += pad_button(dl, F, ImVec2(fx, foot_y), ps ? 0 : 2, hints.accept, act, P, s);
                fx += pad_button(dl, F, ImVec2(fx, foot_y), ps ? 1 : 2, hints.back, "Back", P, s);
                fx += dpad(dl, F, ImVec2(fx, foot_y), move, P, s);
                fx += key_cap(dl, F, ImVec2(fx, foot_y), hints.prev, "", P, s);
                fx += key_cap(dl, F, ImVec2(fx, foot_y), hints.next, swap, P, s);
                fx += key_cap(dl, F, ImVec2(fx, foot_y), "Start", "Close", P, s);
            }
            if (ui->status_frames && ui->status[0]) {
                const ImVec2 rs = measure(F.small, ui->status);
                put(dl, F.small, ImVec2(std::max(fx, x1 - rs.x), foot_y + (foot_h - rs.y) * 0.5f), P.gold,
                    ui->status);
            }
            if (ui->status_frames) --ui->status_frames;
        }

        if (states.open) {
            draw_states(dl, F, states, x0, x1, y, body_y1, modal, click);
        } else {
        /* ---- sections ---- */
        const float nav_w = 232.0f * s, gap = 20.0f * s, item_h = 46.0f * s, item_gap = 4.0f * s;
        for (size_t i = 0; i < ui->section_count; ++i) {
            const ImVec2 a(x0, y + (float)i * (item_h + item_gap)), b(x0 + nav_w, a.y + item_h);
            ImGui::SetCursorScreenPos(a);
            ImGui::PushID((int)i);
            ImGui::InvisibleButton("##section", b - a);
            const bool hov = !modal && ImGui::IsItemHovered();
            if (hov && moved && !ui->in_section) ui->section_index = i;
            if (!modal && ImGui::IsItemClicked(ImGuiMouseButton_Left)) recomp_runtime_ui_enter_section(ui, i);
            ImGui::PopID();
            const bool sel = i == ui->section_index;
            if (sel) {
                dl->AddRectFilled(a, b, ui->in_section ? P.row_hover : P.row_sel, 8.0f * s);
                dl->AddRectFilled(a, ImVec2(a.x + 3.0f * s, b.y), ui->in_section ? P.gold_soft : P.gold, 2.0f * s);
            } else if (hov) {
                dl->AddRectFilled(a, b, P.row_hover, 8.0f * s);
            }
            ImFont* f = sel ? F.bold : F.body;
            const ImVec2 ts = measure(f, ui->sections[i]);
            put(dl, f, ImVec2(a.x + 20.0f * s, a.y + (item_h - ts.y) * 0.5f),
                sel ? (ui->in_section ? P.text : P.gold) : P.muted, ui->sections[i]);
            if (sel && !ui->in_section)
                chevron(dl, ImVec2(b.x - 18.0f * s, a.y + item_h * 0.5f), 6.0f * s, P.gold, std::max(1.0f, 2.0f * s));
        }

        /* ---- the open section's page ---- */
        const float px0 = x0 + nav_w + gap, px1 = x1, ip = 26.0f * s;
        const ImVec2 pa(px0, y), pb(px1, body_y1);
        dl->AddRectFilled(pa, pb, P.page, 12.0f * s);
        dl->AddRect(pa, pb, P.edge, 12.0f * s, 0, std::max(1.0f, 1.0f * s));
        const char* sec = ui->sections[ui->section_index];
        const SectionNote* note = section_note(sec);
        float py = pa.y + ip - 4.0f * s;
        diamond(dl, ImVec2(px0 + ip + 5.0f * s, py + F.bold->FontSize * 0.5f), 5.0f * s, P.gold);
        put(dl, F.bold, ImVec2(px0 + ip + 20.0f * s, py), P.text, sec);
        if (note && note->experimental) {
            const char* tag = "EXPERIMENTAL";
            const float tw = spaced_width(F.small, tag, 1.5f * s), th = F.small->FontSize;
            const ImVec2 ta(px1 - ip - tw - 20.0f * s, py + (F.bold->FontSize - th) * 0.5f - 4.0f * s);
            const ImVec2 tb(px1 - ip, ta.y + th + 8.0f * s);
            dl->AddRect(ta, tb, P.gold_faint, (tb.y - ta.y) * 0.5f, 0, std::max(1.0f, 1.0f * s));
            spaced(dl, F.small, ImVec2(ta.x + 10.0f * s, ta.y + 4.0f * s), P.gold_soft, tag, 1.5f * s);
        }
        py += F.bold->FontSize + 14.0f * s;
        dl->AddLine(ImVec2(px0 + ip, py), ImVec2(px1 - ip, py), P.rule, std::max(1.0f, 1.0f * s));
        py += 10.0f * s;

        const float help_h = F.small->FontSize * 3.0f + 30.0f * s;
        const float rows_y1 = pb.y - help_h;
        ImGui::SetCursorScreenPos(ImVec2(px0 + ip * 0.5f, py));
        ImGui::BeginChild("##rows", ImVec2(px1 - px0 - ip, std::max(10.0f, rows_y1 - py)), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoNavInputs | ImGuiWindowFlags_NoBackground |
                              (modal ? ImGuiWindowFlags_NoScrollWithMouse : 0));
        {
            static size_t last_row = (size_t)-1, last_section = (size_t)-1;
            static int last_in = -1;
            const bool sel_changed =
                ui->row_index != last_row || ui->section_index != last_section || ui->in_section != last_in;
            last_row = ui->row_index;
            last_section = ui->section_index;
            last_in = ui->in_section;
            ImDrawList* cl = ImGui::GetWindowDrawList();
            const size_t count = recomp_runtime_ui_page_entry_count(ui);
            const float rw = ImGui::GetContentRegionAvail().x, row_h = 50.0f * s, row_gap = 4.0f * s;
            for (size_t i = 0; i < count; ++i) {
                char folder[96] = "";
                const RecompRuntimeUiItem* it = recomp_runtime_ui_page_entry(ui, i, folder, sizeof folder);
                ImGui::PushID((int)i);
                const ImVec2 a = ImGui::GetCursorScreenPos();
                ImGui::InvisibleButton("##row", ImVec2(rw, row_h));
                const ImVec2 b(a.x + rw, a.y + row_h);
                const bool hov = !modal && ImGui::IsItemHovered();
                const bool clicked = !modal && ImGui::IsItemClicked(ImGuiMouseButton_Left);
                const bool sel = ui->in_section && i == ui->row_index;
                if (sel && sel_changed) {
                    const float wy = ImGui::GetWindowPos().y, wh = ImGui::GetWindowSize().y;
                    if (a.y < wy) ImGui::SetScrollHereY(0.0f);
                    else if (b.y > wy + wh) ImGui::SetScrollHereY(1.0f);
                }
                const bool enabled = it ? recomp_runtime_ui_item_enabled(ui, it) != 0 : true;
                const float alpha = enabled ? 1.0f : 0.4f;
                DvMenuRowInfo info;
                if (it && row_info) row_info(row_ctx, it, &info);
                if (sel) {
                    cl->AddRectFilled(a, b, P.row_sel, 8.0f * s);
                    cl->AddRectFilled(a, ImVec2(a.x + 3.0f * s, b.y), P.gold, 2.0f * s);
                } else if (hov) {
                    cl->AddRectFilled(a, b, P.row_hover, 8.0f * s);
                }
                const float cy = a.y + row_h * 0.5f;
                const char* label = it ? it->label : folder;
                const ImVec2 ls = measure(F.body, label);
                put(cl, F.body, ImVec2(a.x + 20.0f * s, cy - ls.y * 0.5f),
                    with_alpha(info.danger ? P.crimson_text : P.text, alpha), label);

                /* the value side, right to left */
                const float xr = b.x - 18.0f * s;
                float vleft = xr;   /* left end of what is drawn on the right */
                float zone_l0 = 0, zone_l1 = 0, zone_r0 = 0, zone_r1 = 0;
                if (!it || it->type == RECOMP_RUNTIME_UI_ACTION) {
                    chevron(cl, ImVec2(xr - 4.0f * s, cy), 7.0f * s, with_alpha(sel ? P.gold : P.faint, alpha),
                            std::max(1.0f, 2.0f * s));
                    vleft = xr - 12.0f * s;
                } else if (it->type == RECOMP_RUNTIME_UI_BOOL) {
                    int v = 0;
                    recomp_runtime_ui_current_value(ui, it, &v);
                    toggle(cl, ImVec2(xr, cy), v != 0, sel, P, s, alpha);
                    vleft = xr - 46.0f * s;
                } else {
                    char vt[96];
                    value_text(ui, it, vt, sizeof vt);
                    const ImVec2 vs = measure(F.body, vt);
                    const float ar = 6.0f * s, aw = 26.0f * s;
                    const float vx = xr - aw - vs.x;
                    put(cl, F.body, ImVec2(vx, cy - vs.y * 0.5f),
                        with_alpha(sel ? P.gold : (info.pending ? P.text : P.muted), alpha), vt);
                    if (sel && enabled) {
                        arrow(cl, ImVec2(xr - ar, cy), ar, 1, P.gold);
                        arrow(cl, ImVec2(vx - aw + ar, cy), ar, -1, P.gold);
                    } else {
                        arrow(cl, ImVec2(xr - ar, cy), ar * 0.8f, 1, with_alpha(P.faint, alpha * 0.8f));
                        arrow(cl, ImVec2(vx - aw + ar, cy), ar * 0.8f, -1, with_alpha(P.faint, alpha * 0.8f));
                    }
                    zone_l0 = vx - aw - 8.0f * s; zone_l1 = vx - 2.0f * s;
                    zone_r0 = xr - aw + 2.0f * s; zone_r1 = b.x;
                    vleft = vx - aw;
                }
                if (info.note[0]) {
                    const ImVec2 ns = measure(F.small, info.note);
                    put(cl, F.small, ImVec2(vleft - 14.0f * s - ns.x, cy - ns.y * 0.5f),
                        with_alpha(info.pending ? P.gold_soft : P.faint, alpha), info.note);
                }

                if (hov && moved) { ui->in_section = 1; ui->row_index = i; }
                if (clicked) {
                    ui->in_section = 1;
                    ui->row_index = i;
                    const float mx = io.MousePos.x;
                    if (zone_l1 > zone_l0 && mx >= zone_l0 && mx <= zone_l1) click.input = RECOMP_RUNTIME_UI_INPUT_LEFT;
                    else if (zone_r1 > zone_r0 && mx >= zone_r0 && mx <= zone_r1)
                        click.input = RECOMP_RUNTIME_UI_INPUT_RIGHT;
                    else click.input = RECOMP_RUNTIME_UI_INPUT_ACCEPT;
                }
                ImGui::PopID();
                ImGui::Dummy(ImVec2(rw, row_gap));
            }
        }
        ImGui::EndChild();

        /* ---- help: the selected row's description, or the section's ---- */
        const float hy = rows_y1 + 6.0f * s;
        dl->AddLine(ImVec2(px0 + ip, hy), ImVec2(px1 - ip, hy), P.rule, std::max(1.0f, 1.0f * s));
        const char* help = nullptr;
        if (ui->in_section) {
            const RecompRuntimeUiItem* it = recomp_runtime_ui_page_entry(ui, ui->row_index, nullptr, 0);
            help = it ? it->description : nullptr;
        } else if (note) {
            help = note->blurb;
        }
        if (help && help[0]) {
            dl->PushClipRect(ImVec2(px0, hy), ImVec2(px1, pb.y - 6.0f * s), true);
            put(dl, F.small, ImVec2(px0 + ip, hy + 12.0f * s), P.muted, help, px1 - px0 - ip * 2.0f);
            dl->PopClipRect();
        }
        }   /* the sections and their page */
    }
    ImGui::End();
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(5);
}

bool hit(ImVec2 p, ImVec2 a, ImVec2 b) { return p.x >= a.x && p.x < b.x && p.y >= a.y && p.y < b.y; }

void draw_dialog(const DvMenuDialog& dg, const DvMenuFonts& F, DvMenuClick& click) {
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 D = io.DisplaySize;
    const float s = F.scale;
    const Pal P = palette(1.0f);
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    fg->AddRectFilled(ImVec2(0, 0), D, IM_COL32(0, 0, 0, 150));

    const float w = std::floor(std::min(640.0f * s, D.x - 40.0f * s)), pad = 32.0f * s, inner = w - pad * 2.0f;
    const ImVec2 ts = measure(F.title, dg.title, inner), bs = measure(F.body, dg.body, inner);
    const float btn_h = 48.0f * s;
    const float h = pad + ts.y + 16.0f * s + bs.y + 30.0f * s + btn_h + pad;
    const ImVec2 a(std::floor((D.x - w) * 0.5f), std::floor((D.y - h) * 0.5f)), b(a.x + w, a.y + h);
    const ImU32 accent = dg.danger ? P.crimson : P.gold;
    fg->AddRectFilled(a + ImVec2(0, 10.0f * s), b + ImVec2(0, 14.0f * s), IM_COL32(0, 0, 0, 90), 18.0f * s);
    fg->AddRectFilled(a, b, rgba(19, 16, 23), 16.0f * s);
    fg->AddRect(a, b, dg.danger ? rgba(178, 52, 62, 0.6f) : P.gold_faint, 16.0f * s, 0, std::max(1.0f, 1.2f * s));
    fg->AddRectFilled(a, ImVec2(b.x, a.y + 4.0f * s), accent, 16.0f * s, ImDrawFlags_RoundCornersTop);
    float y = a.y + pad;
    put(fg, F.title, ImVec2(a.x + pad, y), dg.danger ? P.crimson_text : P.gold, dg.title, inner);
    y += ts.y + 16.0f * s;
    put(fg, F.body, ImVec2(a.x + pad, y), P.text, dg.body, inner);

    /* buttons: Cancel, then the action on the right */
    const float ow = std::max(160.0f * s, measure(F.bold, dg.ok).x + 52.0f * s);
    const float cw = std::max(140.0f * s, measure(F.bold, dg.cancel).x + 52.0f * s);
    const float by = b.y - pad - btn_h;
    const ImVec2 oa(b.x - pad - ow, by), ob(b.x - pad, by + btn_h);
    const ImVec2 ca(oa.x - 14.0f * s - cw, by), cb(oa.x - 14.0f * s, by + btn_h);
    const bool ohov = hit(io.MousePos, oa, ob), chov = hit(io.MousePos, ca, cb);
    const float r = 10.0f * s;
    fg->AddRectFilled(oa, ob, ohov ? with_alpha(accent, 0.86f) : accent, r);
    put_centered(fg, F.bold, F.bold->FontSize, ImVec2((oa.x + ob.x) * 0.5f, (oa.y + ob.y) * 0.5f),
                 dg.danger ? rgba(255, 244, 240) : P.dark_text, dg.ok);
    fg->AddRectFilled(ca, cb, chov ? P.row_hover : rgba(26, 23, 31), r);
    fg->AddRect(ca, cb, P.faint, r, 0, std::max(1.0f, 1.2f * s));
    put_centered(fg, F.bold, F.bold->FontSize, ImVec2((ca.x + cb.x) * 0.5f, (ca.y + cb.y) * 0.5f), P.text,
                 dg.cancel);
    const float ring = 4.0f * s;
    if (dg.focus == 0)
        fg->AddRect(oa - ImVec2(ring, ring), ob + ImVec2(ring, ring), P.text, r + ring, 0, std::max(1.0f, 2.0f * s));
    else
        fg->AddRect(ca - ImVec2(ring, ring), cb + ImVec2(ring, ring), P.text, r + ring, 0, std::max(1.0f, 2.0f * s));
    if (io.MouseClicked[0]) {
        if (ohov) click.dialog = 1;
        else if (chov) click.dialog = 2;
    }
}

void draw_toast(const DvMenuToast& t, const DvMenuFonts& F, uint64_t now) {
    if (!t.end_ms || now >= t.end_ms || now < t.start_ms) return;
    const float fade = std::min({1.0f, (float)(now - t.start_ms) / 180.0f, (float)(t.end_ms - now) / 450.0f});
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 D = io.DisplaySize;
    const float s = F.scale;
    const Pal P = palette(1.0f, fade);
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    const float pad = 20.0f * s, bar = 4.0f * s, mark = 18.0f * s;
    const ImVec2 ts = measure(F.bold, t.title), bs = t.body[0] ? measure(F.small, t.body) : ImVec2(0, 0);
    const float w = std::clamp(std::max(ts.x + mark, bs.x) + pad * 2.0f + bar, 300.0f * s, std::min(760.0f * s, D.x - 40.0f * s));
    const float h = pad * 2.0f + ts.y + (t.body[0] ? bs.y + 6.0f * s : 0.0f);
    /* top right: the game's HUD (HP, MP, the spell) is on the left */
    const float y0 = 28.0f * s - (1.0f - fade) * 12.0f * s;
    const ImVec2 a(std::floor(D.x - 28.0f * s - w), std::floor(y0)), b(a.x + w, a.y + h);
    fg->AddRectFilled(a + ImVec2(0, 6.0f * s), b + ImVec2(0, 10.0f * s), IM_COL32(0, 0, 0, (int)(80 * fade)), 14.0f * s);
    fg->AddRectFilled(a, b, rgba(19, 16, 23, 0.97f * fade), 12.0f * s);
    fg->AddRect(a, b, P.gold_faint, 12.0f * s, 0, std::max(1.0f, 1.0f * s));
    fg->AddRectFilled(a, ImVec2(a.x + bar, b.y), P.gold, 12.0f * s, ImDrawFlags_RoundCornersLeft);
    diamond(fg, ImVec2(a.x + bar + pad + 5.0f * s, a.y + pad + ts.y * 0.5f), 5.0f * s, P.gold);
    put(fg, F.bold, ImVec2(a.x + bar + pad + mark, a.y + pad), P.gold, t.title);
    if (t.body[0]) put(fg, F.small, ImVec2(a.x + bar + pad, a.y + pad + ts.y + 6.0f * s), P.text, t.body);
}

}  // namespace

DvMenuClick dv_menu_view_draw(RecompRuntimeUi* ui, bool open, const DvMenuFonts& fonts,
                              const DvMenuHeader& header, const DvMenuDialog& dialog,
                              const DvMenuToast& toast, const DvMenuHints& hints, const DvStatesView& states,
                              uint64_t now_ms, DvMenuRowInfoFn row_info, void* row_ctx) {
    DvMenuClick click;
    if (!ui || !fonts.body || !fonts.bold || !fonts.title || !fonts.small) return click;
    if (open) {
        if (!(dialog.open && dialog.alone))
            draw_menu(ui, fonts, header, hints, states, dialog.open, row_info, row_ctx, click);
        if (dialog.open) draw_dialog(dialog, fonts, click);
    }
    draw_toast(toast, fonts, now_ms);
    return click;
}

#endif  /* RECOMP_LAUNCHER */
