/*
 * Dragon Valor (USA) "Skip intro": trusted static plugin behind
 * mods/preloaded/packages/dragonvalor.skip-intro.
 *
 * How the boot runs (OPENNING.BIN; decomp src/openning/logo.c, intro.c and
 * titlemenu.c; mode byte 0x8008634C = g_GameFlow.mode, sub-state 0x8008634D;
 * docs/DEVELOPMENT.md "Skip intro"). The game logic runs at 30 Hz (two
 * VBlanks a frame):
 *   mode 0  boot (GameInit, OPENNING.BIN read).
 *   mode 1  Namco logo, g_LogoState (pointer at 0x800D8DD0: +4 timer,
 *           +6 timerMax): sub 0 LogoLoad (the boot TIMs), 1 LogoSetupPrims
 *           (the logo's two POLY_GT4, black), 2 LogoFadeIn (32 frames, then
 *           timer = 64), 3 LogoLoadTitlePack (\TITLE\DB_TITLE.TIP read into
 *           g_FileBuffer: CdLoadDiscFile blocks until the read is done),
 *           4 LogoHold (64 frames), 5 LogoFadeOut (32 frames) -> mode 2.
 *           Only sub 3 matters to the title: the rest is ~4.5 s of show.
 *   mode 2  intro movie (MOVIE/MOVIE1.STR, ~31 s): MoviePlayMode leaves on
 *           Start (a press edge) and does mode++.
 *   mode 3  the title (sub 0 TitleEnter: the title pack's TIMs and sounds;
 *           sub 1 TitleLoop keeps control): g_TitleWork (0x8014F228) screen
 *           (+0x7D0A) 0 = main, state (+0x7D0B) 0 Init, 1 LogoFlyIn,
 *           2 RingBurst, 3 MenuFadeIn, 4 MenuOpen, 5 MenuUpdate (the menu:
 *           New Game / Load Game / Configuration). Start in states 1..3
 *           jumps to state 4 (the game's own skip). Start or Cross on the
 *           menu picks New Game, so no Start may reach it. 1500 idle frames
 *           there start the attract demo (mode 5, DEMO.BIN).
 *
 * With the feature on, power-on reaches the menu in well under half the
 * time: the logo keeps its title-pack read and nothing else (it is never
 * lit), the movie gets Start at once, the title's logo flight gets Start,
 * and the whole black part runs faster than real time (fast boot, below).
 *
 * Everything is done from one place: the entry of the pad-response copy
 * (func_8006C250, listed in game.toml [recompiler] mod_function_entry_funcs),
 * which the game's per-frame pad routine calls after its own SIO exchange
 * (pad struct +0x3C = the response buffer: id, 0x5A, buttons active-low;
 * 0x800BBC78 for pad 1) and before the frame's mode handler runs: Start is
 * pressed or released in the response there, and the logo's sub-state and
 * timer are set there, so a handler never sees them change in the middle of
 * its frame. Start cannot be injected from a VBlank callback (the game
 * refreshes the response itself), and a sub-state written at a VBlank can
 * land between a handler's read and its own `subMode++`.
 */
#include <stdio.h>
#include <stdint.h>

#include "cpu_state.h"
#include "mod_plugins.h"

#define DV_MODE          0x8008634Cu
#define DV_SUB           0x8008634Du
#define DV_LOGO_STATE    0x800D8DD0u /* OpnLogoState* (8 bytes of the work-area heap) */
#define DV_LOGO_TIMER    4u          /* s16 */
#define DV_TITLE_SCREEN  0x80156F32u /* g_TitleWork.screen (0 = title / main menu) */
#define DV_TITLE_STATE   0x80156F33u /* g_TitleWork.state */
#define DV_PAD_COPY_FUNC 0x8006C250u /* copy(pad_struct): response (+0x3C) -> struct buffer (+0x30) */
#define DV_PAD_RESPONSE  0x3Cu       /* pad struct: pointer to the SIO response buffer */
#define PSX_PAD_START    0x0008u

static int g_skip_logo = 1, g_skip_movie = 1, g_skip_title = 1, g_fast_boot = 1;
static int g_boot_held;              /* psx_mod_hold_acceleration is on (the black boot) */
static uint32_t g_vblanks;           /* VBlank clock */
static uint8_t g_prev_mode = 0xFF;
static uint32_t g_mode_since;        /* VBlank of the current mode's start */
static uint32_t g_menu_since;        /* VBlank the title menu opened (state 4), 0 = not yet */

/* The movie player reacts to a press edge and polls at the movie's frame
 * rate (it misses presses shorter than ~4 VBlanks): Start is released for
 * 4 VBlanks from the mode's start, then pressed for 4, and so on, so the
 * first edge comes 4 VBlanks in. */
#define DV_PRESS_VBLANKS 4u
static int dv_movie_start_down(void) { return ((g_vblanks - g_mode_since) / DV_PRESS_VBLANKS) & 1u; }

/* After the title menu opens, Start stays released this long: a Start that
 * reached the menu would pick New Game. */
#define DV_MENU_GUARD_VBLANKS 60u

static int dv_opt_bool(const char* id, int dflt)
{
    char text[8];
    if (!psx_mod_current_option_value(id, text, sizeof text)) return dflt;
    return text[0] == 't' || text[0] == '1';
}

/* Fast boot (option `boot`): from activation until the title's loop starts
 * (mode 3 sub 1), the runtime paces the boot like an accelerated load
 * (psx_mod_hold_acceleration, patches/psxrecomp/0005): every guest VBlank,
 * interrupt and CD deadline happens as ever, only faster than real time,
 * while the screen is black: GameInit's memory-card probing (~2.5 s of
 * authentic SIO reads), OPENNING.BIN's read, the logo's title-pack read, the
 * movie's skip and TitleEnter's VSync-paced uploads. Released for good at the
 * first VBlank of anything else (a save state loaded at once, a game mode) or
 * after 60 s of guest time. */
#define DV_BOOT_HOLD_MAX_VBLANKS 3600u

static void dv_boot_hold(int on)
{
    if (on == g_boot_held) return;
    if (psx_mod_hold_acceleration(on, 0)) {
        g_boot_held = on;
        fprintf(stderr, "dragonvalor.skip-intro: fast boot %s at VBlank %u\n", on ? "on" : "off", g_vblanks);
    } else if (!on) {
        g_boot_held = 0;
    }
}

static void dv_skip_activate(void)
{
    g_skip_logo = dv_opt_bool("logo", 1);
    g_skip_movie = dv_opt_bool("movie", 1);
    g_skip_title = dv_opt_bool("title", 1);
    g_fast_boot = dv_opt_bool("boot", 1);
    fprintf(stderr, "dragonvalor.skip-intro: logo %s, movie %s, title animation %s, fast boot %s\n",
            g_skip_logo ? "skipped" : "kept", g_skip_movie ? "skipped" : "kept", g_skip_title ? "skipped" : "kept",
            g_fast_boot ? "on" : "off");
    g_boot_held = 0;
    if (g_fast_boot && psx_mod_read_byte(DV_MODE) == 0) dv_boot_hold(1);
}

static void dv_skip_vblank(void)
{
    g_vblanks++;
    if (g_boot_held) {
        uint8_t mode = psx_mod_read_byte(DV_MODE), sub = psx_mod_read_byte(DV_SUB);
        int black = mode == 0 || mode == 1 || mode == 2 || (mode == 3 && sub == 0);
        if (!black || g_vblanks > DV_BOOT_HOLD_MAX_VBLANKS) {
            dv_boot_hold(0);
            g_fast_boot = 0;                          /* once per start */
        }
    }
}

/* The Namco logo, before this frame's ModeLogo: the fade-in is left out
 * (the logo stays black, as LogoSetupPrims made it) and the hold comes
 * with its timer run out; the title-pack read (sub 3) runs as ever; the
 * fade-out starts from black and ends at once. */
static void dv_skip_logo_step(uint8_t sub)
{
    uint32_t st = psx_mod_read_word(DV_LOGO_STATE);
    if (st < 0x80000000u || st >= 0x80200000u) return;
    if (sub == 2) {                                   /* LogoFadeIn: not run */
        psx_mod_write_half(st + DV_LOGO_TIMER, 0);    /* the hold's timer, run out */
        psx_mod_write_byte(DV_SUB, 3);                /* straight to LogoLoadTitlePack */
        psx_mod_counter_add("dv.skip.logo_fade_in", 1);
    } else if (sub == 5 && (int16_t)psx_mod_read_half(st + DV_LOGO_TIMER) >= 0) {
        psx_mod_write_half(st + DV_LOGO_TIMER, (uint16_t)-1);   /* level 0, then mode 2 */
        psx_mod_counter_add("dv.skip.logo_fade_out", 1);
    }
}

static void dv_skip_pad_copy_entry(struct CPUState* cpu, uint32_t address)
{
    uint8_t mode, sub;
    uint32_t pad_struct, response;
    uint16_t buttons;
    int start = -1;                                   /* -1 leave, 0 release, 1 press */
    (void)address;
    psx_mod_counter_add("dv.skip.pad_copy", 1);
    mode = psx_mod_read_byte(DV_MODE);
    sub = psx_mod_read_byte(DV_SUB);
    if (mode != g_prev_mode) {
        g_prev_mode = mode;
        g_mode_since = g_vblanks;
        g_menu_since = 0;
    }
    if (mode == 1 && g_skip_logo) dv_skip_logo_step(sub);
    if (mode == 2 && g_skip_movie) {
        start = dv_movie_start_down();
    } else if (mode == 3 && (g_skip_movie || g_skip_title)) {
        uint8_t screen = psx_mod_read_byte(DV_TITLE_SCREEN), state = psx_mod_read_byte(DV_TITLE_STATE);
        if (sub == 1 && screen == 0 && state >= 1 && state <= 3 && g_skip_title) {
            start = 1;                                /* the logo flight: the game's skip to the menu */
        } else if (sub == 0 || (screen == 0 && state == 0)) {
            start = 0;                                /* no Start left over from the movie */
        } else if (screen == 0 && state >= 4) {
            if (!g_menu_since) g_menu_since = g_vblanks ? g_vblanks : 1;
            if (g_vblanks - g_menu_since < DV_MENU_GUARD_VBLANKS) start = 0;
        }
    }
    if (start < 0) return;
    pad_struct = cpu->gpr[4];
    if (pad_struct < 0x80000000u || pad_struct >= 0x80200000u) return;
    response = psx_mod_read_word(pad_struct + DV_PAD_RESPONSE);
    if (response < 0x80000000u || response >= 0x80200000u) return;
    buttons = psx_mod_read_half(response + 2);
    if (start) {
        psx_mod_counter_add("dv.skip.start_down", 1);
        buttons &= (uint16_t)~PSX_PAD_START;          /* pressed (active-low) */
    } else {
        buttons |= PSX_PAD_START;                     /* released */
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
