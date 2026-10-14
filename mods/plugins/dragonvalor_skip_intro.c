/*
 * Dragon Valor (USA) "Skip intro": trusted static plugin behind
 * mods/preloaded/packages/dragonvalor.skip-intro.
 *
 * How the boot runs (intro module at 0x800BBEF0, mode byte 0x8008634C,
 * sub-state 0x8008634D; docs/DEVELOPMENT.md "Skip intro"):
 *   mode 1  Namco logo, sub 0..5 (loads, fade in, hold, hold, fade out),
 *           ~6 s; left alone (see dv_skip_vblank).
 *   mode 2  intro movie (MOVIE/MOVIE1.STR); the player polls the game's raw
 *           pad word and leaves on Start (func_800BD038 tears the movie
 *           down and does mode++).
 *   mode 3  title logo animation (~28 s). Not skipped: Start there starts
 *           the story demo (17 -> 8), and forcing its countdown
 *           (0x80156F40) takes the game through an unusual mode 4 path.
 *   mode 5  title still (sub 0, ~5 s) -> 17 (menu load) -> 5 sub 2 = menu.
 * Start cannot be injected from a VBlank callback: the game runs its own SIO
 * pad exchange inside its per-frame pad routine and only then copies the
 * response buffer (pad struct +0x3C; 0x800BBC78 for pad 1: id, 0x5A, buttons
 * active-low, sticks) into its pad struct (+0x30, 0x800A2F20) through
 * func_8006C250. That copy is listed in game.toml [recompiler]
 * mod_function_entry_funcs, so a function-entry hook on it runs after the
 * exchange and before the copy: it clears the Start bit in the response, and
 * the game's own Start handling does the skip. Nothing is written outside
 * modes 1..2.
 */
#include <stdio.h>
#include <stdint.h>

#include "cpu_state.h"
#include "mod_plugins.h"

#define DV_MODE          0x8008634Cu
#define DV_SUB           0x8008634Du
#define DV_PAD_COPY_FUNC 0x8006C250u /* copy(pad_struct): response (+0x3C) -> struct buffer (+0x30) */
#define DV_PAD_RESPONSE  0x3Cu       /* pad struct: pointer to the SIO response buffer */
#define PSX_PAD_START    0x0008u

static int g_skip_movie = 1;
static uint32_t g_vblanks;   /* clock for the Start pulses */

/* The movie player reacts to a press edge, not to a held button: a response
 * that always carries Start never triggers it. And every extra press after
 * the one that skips lands on the title screen, where Start starts the story
 * demo. So: a 4-frame press every 24 frames (the player polls at the movie's
 * frame rate and misses shorter ones), stopped the moment the mode changes. */
#define DV_PRESS_FRAMES  4u
#define DV_PRESS_PERIOD  24u
static int dv_start_down(void) { return (g_vblanks % DV_PRESS_PERIOD) < DV_PRESS_FRAMES; }

static int dv_opt_bool(const char* id, int dflt)
{
    char text[8];
    if (!psx_mod_current_option_value(id, text, sizeof text)) return dflt;
    return text[0] == 't' || text[0] == '1';
}

static void dv_skip_activate(void)
{
    g_skip_movie = dv_opt_bool("movie", 1);
}

/* VBlank: only the clock for the Start pulses. The Namco logo is left alone:
 * ending it early (mode poke, or expiring its stage timers from sub 2 or 3)
 * makes the title screen fall into the story demo (3/1 -> 17/1 -> 8/1), and
 * cutting only sub 4 stalls the title animation, so its stages evidently
 * cover loads the title depends on. */
static void dv_skip_vblank(void)
{
    g_vblanks++;
}

/* Movie: at the entry of the pad-response copy (a0 = pad struct), put the
 * pulsed Start into the response the copy is about to publish. Both pulse
 * phases are written: without a controller the SIO response is not refreshed
 * every frame, and a Start left in it would read as held into the title,
 * whose own Start handling starts the story demo. For the same reason the
 * first frames of mode 3 get Start written as released. */
#define DV_TITLE_GUARD_FRAMES 60u
static uint8_t g_prev_mode;
static uint32_t g_mode3_since;

static void dv_skip_pad_copy_entry(struct CPUState* cpu, uint32_t address)
{
    uint8_t mode;
    uint32_t pad_struct, response;
    uint16_t buttons;
    (void)address;
    psx_mod_counter_add("dv.skip.pad_copy", 1);
    mode = psx_mod_read_byte(DV_MODE);
    if (mode != g_prev_mode) {
        g_prev_mode = mode;
        if (mode == 3) g_mode3_since = g_vblanks;
    }
    if (!g_skip_movie) return;
    if (!(mode == 2 || (mode == 3 && g_vblanks - g_mode3_since < DV_TITLE_GUARD_FRAMES))) return;
    pad_struct = cpu->gpr[4];
    if (pad_struct < 0x80000000u || pad_struct >= 0x80200000u) return;
    response = psx_mod_read_word(pad_struct + DV_PAD_RESPONSE);
    if (response < 0x80000000u || response >= 0x80200000u) return;
    buttons = psx_mod_read_half(response + 2);
    if (mode == 2 && dv_start_down()) {
        psx_mod_counter_add("dv.skip.start_down", 1);
        buttons &= (uint16_t)~PSX_PAD_START;      /* pressed (active-low) */
    } else {
        buttons |= PSX_PAD_START;                 /* released */
    }
    psx_mod_write_half(response + 2, buttons);
}

PSX_MOD_CONSTRUCTOR(dv_register_skip_intro)
{
    int ok = psx_mod_register_activation_plugin("dragonvalor.skip-intro", dv_skip_activate)
          && psx_mod_register_vblank_plugin("dragonvalor.skip-intro", dv_skip_vblank)
          && psx_mod_register_function_entry_plugin("dragonvalor.skip-intro", DV_PAD_COPY_FUNC,
                                                    dv_skip_pad_copy_entry);
    if (!ok) fprintf(stderr, "dragonvalor.skip-intro: plugin failed to register\n");
}
