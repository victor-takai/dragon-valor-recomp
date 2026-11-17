/*
 * Dragon Valor (USA) "Fixes": trusted static plugin behind
 * mods/preloaded/packages/dragonvalor.fixes (on by default). It works around
 * places where the recomp draws something differently from a console.
 *
 * Screen-copy seam (seen 2026-11-17, OpenGL renderer, 4:3 and wide alike;
 * the software renderer does not show it). The screen transitions
 * (g_Transition 0x800A2EF8; decomp src/main/r2B_transitions.c and
 * exeA_transition.c) and the motion blur (g_Render.blur, FrameDraw) redraw
 * the frame buffer with itself as a 15-bit texture, in two sprites: the left
 * 256 columns from the texture page at x 0, the right 129 (blur: 128) from
 * the page at x 256, both from u 0. The GL renderer's texel fetch at the
 * first column of a page lands just before u 0 and wraps to u 255 of the
 * same page: VRAM x 511 for the page at 256, texture memory, not the
 * picture. The transitions feed every frame back into the next, so that one
 * column grows into a coloured vertical line at x 256 (two thirds across the
 * screen) for the whole effect; the user saw it as "a stripe in the middle
 * of the screen at some moments". Checked from VRAM: the line is in the
 * frame buffer itself (CPU copy and FBO agree), bright where its left
 * neighbour has already faded to black.
 *
 * The fix moves those fetches one page column left with u + 64: the same
 * VRAM texels (page x 192, u 64..192 = VRAM 256..384), but the texel left of
 * the first one is now the real picture column 255. Done just before the
 * buffer is drawn, at the entry of ModelWeldReset (0x80021CE4; FrameDraw
 * calls it after building the frame and before DrawOTag of the frame, the
 * transition chain and the window list), for
 *   - every 15-bit textured SPRT (with its E1 page word) and POLY_FT4 of the
 *     transition chain of that buffer whose u starts at 0 on a page column
 *     >= 1 (effects 0..9: the right halves, scatter tiles at x = 64 n);
 *   - the right blur sprite of that buffer while the blur is on.
 * Each word is put back at the next frame's entry if it still holds the
 * patched value, so the game reads and rewrites its own packets as written
 * (effect 7 rewrites u every frame); transition words are put back only
 * while the same chain is still installed, so memory reused after an effect
 * is never touched. A save-state load forgets the list.
 */
#include <stdio.h>
#include <stdint.h>

#include "cpu_state.h"
#include "mod_plugins.h"

#define DV_FX_PLUGIN          "dragonvalor.fixes"

#define DV_RENDER             0x8009E840u
#define DV_RENDER_FRAME       (DV_RENDER + 0x100Cu)   /* buffer being built: 0 / 1 */
#define DV_RENDER_BLUR_ON     (DV_RENDER + 0x1068u)   /* u8 */
#define DV_RENDER_BLUR_TPAGE  (DV_RENDER + 0x109Cu)   /* DR_TPAGE [2][2], 8 bytes */
#define DV_RENDER_BLUR_SPRT   (DV_RENDER + 0x10BCu)   /* SPRT [2][2], 20 bytes */
#define DV_TRANSITION         0x800A2EF8u             /* s32 state, s32 effect */
#define DV_TRANSITION_OT      (DV_TRANSITION + 0x10u) /* u32 *ot[2] */
#define DV_MODEL_WELD_RESET   0x80021CE4u

#define DV_FX_MAX_WORDS       768
#define DV_FX_MAX_PACKETS     4096
#define DV_FX_MAX_PACKET_LEN  32u
#define DV_FX_PAGE_SHIFT      64u   /* texels of one page column */

static int s_enabled;

static struct {
    uint32_t addr, orig, patched;
    int chain;                      /* 1 = transition chain, 0 = g_Render */
} s_words[DV_FX_MAX_WORDS];
static int s_nwords;
static uint32_t s_chain_head, s_chain_effect;

static int dv_fx_ram(uint32_t p, uint32_t bytes)
{
    return p >= 0x80000000u && !(p & 3u) && p + bytes <= 0x80200000u;
}

static void dv_fx_put(uint32_t addr, uint32_t value, int chain)
{
    s_words[s_nwords].addr = addr;
    s_words[s_nwords].orig = psx_mod_read_word(addr);
    s_words[s_nwords].patched = value;
    s_words[s_nwords].chain = chain;
    s_nwords++;
    psx_mod_write_word(addr, value);
}

static void dv_fx_restore(void)
{
    int i, chain_ok;
    chain_ok = s_chain_head != 0u &&
               psx_mod_read_word(DV_TRANSITION) != 0u &&
               psx_mod_read_word(DV_TRANSITION + 4u) == s_chain_effect &&
               (psx_mod_read_word(DV_TRANSITION_OT) == s_chain_head ||
                psx_mod_read_word(DV_TRANSITION_OT + 4u) == s_chain_head);
    for (i = 0; i < s_nwords; i++) {
        if (s_words[i].chain && !chain_ok) continue;
        if (psx_mod_read_word(s_words[i].addr) == s_words[i].patched)
            psx_mod_write_word(s_words[i].addr, s_words[i].orig);
    }
    s_nwords = 0;
    s_chain_head = 0u;
}

/* A 15-bit page (E1 / FT4 tpage bits 7..8 = 2) on column >= 1, sampled from
 * u 0 with at most 192 texels: the same texels one column to the left. */
static int dv_fx_wraps(uint32_t tpage, uint32_t umin, uint32_t umax)
{
    return ((tpage >> 7) & 3u) == 2u && (tpage & 0xFu) != 0u &&
           umin == 0u && umax + DV_FX_PAGE_SHIFT <= 255u;
}

static uint32_t dv_fx_left_page(uint32_t tpage)
{
    return (tpage & ~0xFu) | ((tpage & 0xFu) - 1u);
}

/* GP0 command lengths in words; 0 = stop reading this packet (lines,
 * CPU-to-VRAM data, unknown). */
static uint32_t dv_fx_cmd_words(uint32_t op)
{
    if (op == 0x00u || op == 0x01u || (op >= 0xE1u && op <= 0xE6u)) return 1u;
    if (op == 0x02u) return 3u;
    if (op >= 0x20u && op < 0x40u) {
        static const uint8_t poly[8] = { 4, 7, 5, 9, 6, 9, 8, 12 };
        return poly[(op - 0x20u) >> 2];
    }
    if (op >= 0x60u && op < 0x80u) {
        static const uint8_t rect[8] = { 3, 4, 2, 3, 2, 3, 2, 3 };
        return rect[(op - 0x60u) >> 2];
    }
    if (op >= 0x80u && op < 0xA0u) return 4u;
    return 0u;
}

static void dv_fx_packet(uint32_t p, uint32_t len)
{
    uint32_t w[DV_FX_MAX_PACKET_LEN], i, n, tp_at = 0xFFu;
    for (i = 0; i < len; i++) w[i] = psx_mod_read_word(p + 4u + 4u * i);
    for (i = 0; i < len; i += n) {
        uint32_t op = w[i] >> 24;
        n = dv_fx_cmd_words(op);
        if (n == 0u || i + n > len) return;
        if (op == 0xE1u) {
            tp_at = i;
        } else if (op >= 0x64u && op <= 0x67u && tp_at != 0xFFu) {
            uint32_t u = w[i + 2] & 0xFFu, width = w[i + 3] & 0x3FFu;
            if (width && dv_fx_wraps(w[tp_at], u, u + width - 1u) &&
                s_nwords + 2 <= DV_FX_MAX_WORDS) {
                dv_fx_put(p + 4u + 4u * tp_at, dv_fx_left_page(w[tp_at]), 1);
                dv_fx_put(p + 4u + 4u * (i + 2u), w[i + 2] + DV_FX_PAGE_SHIFT, 1);
            }
        } else if (op >= 0x2Cu && op <= 0x2Fu) {
            static const uint8_t uv[4] = { 2, 4, 6, 8 };
            uint32_t k, umin = 255u, umax = 0u, tpage = w[i + 4] >> 16;
            for (k = 0; k < 4u; k++) {
                uint32_t u = w[i + uv[k]] & 0xFFu;
                if (u < umin) umin = u;
                if (u > umax) umax = u;
            }
            if (dv_fx_wraps(tpage, umin, umax) && s_nwords + 4 <= DV_FX_MAX_WORDS) {
                for (k = 0; k < 4u; k++) {
                    uint32_t word = w[i + uv[k]] + DV_FX_PAGE_SHIFT;
                    if (k == 1u) word = (dv_fx_left_page(tpage) << 16) | (word & 0xFFFFu);
                    dv_fx_put(p + 4u + 4u * (i + uv[k]), word, 1);
                }
            }
        }
    }
}

static void dv_fx_transition(uint32_t frame)
{
    uint32_t p, tag, n = 0;
    if (psx_mod_read_word(DV_TRANSITION) == 0u) return;
    p = psx_mod_read_word(DV_TRANSITION_OT + frame * 4u);
    if (!dv_fx_ram(p, 4u)) return;
    s_chain_head = p;
    s_chain_effect = psx_mod_read_word(DV_TRANSITION + 4u);
    while (n++ < DV_FX_MAX_PACKETS) {
        uint32_t len;
        tag = psx_mod_read_word(p);
        len = tag >> 24;
        if (len && len <= DV_FX_MAX_PACKET_LEN && dv_fx_ram(p, 4u + 4u * len))
            dv_fx_packet(p, len);
        if ((tag & 0xFFFFFFu) == 0xFFFFFFu) break;
        p = 0x80000000u | (tag & 0xFFFFFFu);
        if (!dv_fx_ram(p, 4u)) break;
    }
}

/* Right blur sprite of this buffer: DR_TPAGE [frame][1] (E1 word at +4),
 * SPRT [frame][1] (u / v / CLUT at +12, w / h at +16). */
static void dv_fx_blur(uint32_t frame)
{
    uint32_t tp_addr = DV_RENDER_BLUR_TPAGE + (frame * 2u + 1u) * 8u + 4u;
    uint32_t sp_addr = DV_RENDER_BLUR_SPRT + (frame * 2u + 1u) * 20u;
    uint32_t tpage, uv, width;
    if (psx_mod_read_byte(DV_RENDER_BLUR_ON) == 0u) return;
    tpage = psx_mod_read_word(tp_addr);
    uv = psx_mod_read_word(sp_addr + 12u);
    width = psx_mod_read_word(sp_addr + 16u) & 0x3FFu;
    if ((tpage >> 24) != 0xE1u || !width || (psx_mod_read_word(sp_addr + 4u) >> 26) != (0x64u >> 2))
        return;
    if (!dv_fx_wraps(tpage, uv & 0xFFu, (uv & 0xFFu) + width - 1u) ||
        s_nwords + 2 > DV_FX_MAX_WORDS)
        return;
    dv_fx_put(tp_addr, dv_fx_left_page(tpage), 0);
    dv_fx_put(sp_addr + 12u, uv + DV_FX_PAGE_SHIFT, 0);
}

static void dv_fx_weld_reset_entry(struct CPUState* cpu, uint32_t address)
{
    uint32_t frame;
    (void)cpu;
    (void)address;
    if (!s_enabled) return;
    dv_fx_restore();
    frame = psx_mod_read_byte(DV_RENDER_FRAME) & 1u;
    dv_fx_transition(frame);
    dv_fx_blur(frame);
    if (s_nwords) psx_mod_counter_add("dv.fix.screen_copy_words", (uint32_t)s_nwords);
}

static void dv_fx_savestate_loaded(void)
{
    s_nwords = 0;
    s_chain_head = 0u;
}

static void dv_fx_activate(void)
{
    s_enabled = 1;
    s_nwords = 0;
    s_chain_head = 0u;
}

PSX_MOD_CONSTRUCTOR(dv_register_fixes)
{
    int ok = psx_mod_register_activation_plugin(DV_FX_PLUGIN, dv_fx_activate)
          && psx_mod_register_savestate_plugin(DV_FX_PLUGIN, dv_fx_savestate_loaded)
          && psx_mod_register_function_entry_plugin(DV_FX_PLUGIN, DV_MODEL_WELD_RESET,
                                                    dv_fx_weld_reset_entry);
    if (!ok) fprintf(stderr, "dragonvalor.fixes: plugin failed to register\n");
}
