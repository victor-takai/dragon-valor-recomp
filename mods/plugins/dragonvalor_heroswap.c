/*
 * Dragon Valor (USA) "Play as" (experimental): trusted static plugin behind
 * mods/preloaded/packages/dragonvalor.heroswap.
 *
 * Every action stage loads with the hero the player picks (option `hero`,
 * also a row of the in-game menu's Experimental section) instead of its own.
 * The hero is data, not code (github victor-takai/dragon-valor-decomp,
 * docs/targets/hero-swap.md, tools/heroswap.py, whose rebuild this file
 * ports):
 *
 *   action pack (stage file 1)  the head of entry 0's install program: op 3
 *       (the hero object: behaviour script, 14 parts, 200 animation slots),
 *       its op 31 weld lists, op 52 kind 0 (its sound bank) and op 7 (its
 *       shadow); the entries those name. The stage's ops and entries follow.
 *   stage file 0  the VAB that op 51 kind 0 sends to sound slot 1 (the pack's
 *       entry 1 is its head), the TIMs that land in VRAM (384..447, 0..255),
 *       and the portrait, the TIM at (960, 432) 40 x 64 (CLUT row (0, 500)).
 *   event packs   frame 0 of every event image set (op 66) is the stage
 *       hero's portrait, uploaded back at each dialogue's end.
 *   name          g_Hero.index (0x80086318), written at a chapter start.
 *
 * Kits. At activation the plugin reads DATA.CDR from the disc in the drive
 * (psx_mod_read_disc_file, the whole file, about 440 / 500 MB, freed again;
 * a disc other than the session's straight from its image, see *the discs*)
 * and keeps, for each hero on that disc, a kit taken from the first stage of
 * the hero's first area (Clovis 1/1, Arlen 2/1, ..., Anita 9/1): the hero's
 * ops, the entries they name (animations no slot uses left out), the VAB,
 * the hero-page TIMs and the portrait. With them it keeps a table of every
 * action stage of the disc: the sizes the rebuild needs to lay the stage
 * out. Kits and tables are cached in mods/cache/dragonvalor.heroswap/ beside
 * the executable, so the next start reads nothing. The other disc's heroes
 * (Anna, Mihael, Anita on disc 2; Clovis to Phillip on disc 1) come from the
 * other disc's image when the runtime's disc roster knows where it is, and a
 * disc changed while the game runs (the game asks for disc 2) is read at the
 * next stage load.
 *
 * Two function-entry hooks (game.toml [recompiler] mod_function_entry_funcs):
 *
 *   StageBuffersAlloc (0x80035AB8), every stage load: when the stage is an
 *     action stage of the table and the chosen hero is not its own, the
 *     rebuilt sizes decide row 0 (file 0, then the pack, in whole sectors;
 *     g_StageBufferSizes[0], 0x80078B20) and sound slot 1 (the VAB body
 *     rounded up to 64 bytes; g_SoundBodySize[1], 0x80077DBC); they are
 *     raised when needed, unless the stage buffers or the sound slots would
 *     then go past the largest layout the game itself uses (that stage keeps
 *     its own hero). Otherwise the hero index the swap replaced comes back.
 *
 *   PackInstall (0x80035E48, a0 = the file just read): in a swapped stage,
 *     file 0 and the action pack (both read to ACTION's heap, 0x800C5E98)
 *     are rebuilt with the kit in place before the install; in any other
 *     pack, copies of the stage hero's portrait become the kit's (same size),
 *     the dialogues name the chosen hero (swap_names), a reload of the stage
 *     hero's texture becomes the kit's and a twin's variant of it is
 *     neutralised (swap_page_reloads), and a story scene's own copy of the
 *     stage's hero, or of the area hero's usual look, takes the kit's parts
 *     (swap_actors), when the rebuilt pack fits before the next stage buffer.
 *
 * Inert unless the feature is on and a hero chosen. Not handled: the scene
 * motions (op 23 into the hero's slots, a copy's own slots) stay the stage
 * hero's; the stage text bank (op 20) may name the hero; a scene copy's own
 * weapon (5/19's Kadum, 12/29's great sword) becomes the chosen hero's.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#define dv_mkdir(p) _mkdir(p)
#else
#include <sys/stat.h>
#define dv_mkdir(p) mkdir((p), 0755)
#endif

#include "cpu_state.h"
#include "mod_plugins.h"

#define DV_HS_PLUGIN          "dragonvalor.heroswap"
#define DV_HS_PKG             "dragonvalor.heroswap"
#define DV_HS_FEATURE         "play-as"
#define DV_HS_CACHE_DIR       "mods/cache/dragonvalor.heroswap"

#define DV_STAGE_AREA         0x800A2F18u   /* g_StageSelect.area (s16) */
#define DV_STAGE_STAGE        0x800A2F1Au   /* g_StageSelect.stage (s16) */
#define DV_STAGE_BUFFER_SIZES 0x80078B20u   /* g_StageBufferSizes[10], words */
#define DV_SOUND_BODY_SIZE    0x80077DB8u   /* g_SoundBodySize[12], bytes */
#define DV_HERO_INDEX         0x80086318u   /* g_Hero.index (u8) */
#define DV_STAGE_BUFFERS_ALLOC 0x80035AB8u
#define DV_PACK_INSTALL       0x80035E48u
#define DV_ACTION_HEAP        0x800C5E98u   /* g_ModuleHeapStart[3]: row 0 in ACTION */
#define DV_ARENA_END_MAX      0x801EE69Cu   /* largest stage layout of the discs (12/29) */
#define DV_SPU_END_MAX        476096u       /* largest sound slots 1..9 of the discs (0/9) */
#define DV_RAM_END            0x80200000u
#define DV_SECTOR             2048u
#define DV_NODE               0x70u

#define HERO_COUNT 9
static const char* const kHeroValues[HERO_COUNT] = {
    "clovis", "arlen", "kodel", "felippe", "phillip", "gerome", "anna", "mihael", "anita"};
static const char* const kHeroNames[HERO_COUNT] = {
    "Clovis", "Arlen", "Kodel", "Felippe", "Phillip", "Gerome", "Anna", "Mihael", "Anita"};
/* Where each kit comes from: the first stage of the hero's first area. */
static const int kKitSource[HERO_COUNT][2] = {
    {1, 1}, {2, 1}, {3, 1}, {4, 1}, {5, 1}, {6, 1}, {7, 1}, {8, 1}, {9, 1}};
static const uint8_t kHeroByArea[13] = {0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 7, 6, 5};   /* 0x8006CC70 */

/* g_Hero.index during an area's chapter: ChapterInit writes g_HeroByArea,
 * ChapterStashMerge 8 in area 12 (decomp src/main/exeB_stats.c). */
static int dv_area_hero(int area) { return area == 12 ? 8 : (area >= 0 && area < 13 ? kHeroByArea[area] : -1); }

static const uint16_t kHeroPage[4] = {384, 0, 64, 256};
static const uint16_t kPortrait[4] = {960, 432, 40, 64};

/* --- bytes -------------------------------------------------------------- */

static uint32_t le32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static void put32(uint8_t* p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static int16_t le16s(const uint8_t* p) { return (int16_t)(p[0] | p[1] << 8); }

typedef struct { uint8_t* p; uint32_t n, cap; } Buf;
static int buf_put(Buf* b, const void* d, uint32_t n)
{
    if (b->n + n > b->cap) {
        uint32_t cap = b->cap ? b->cap : 4096;
        uint8_t* q;
        while (cap < b->n + n) cap *= 2;
        if (!(q = (uint8_t*)realloc(b->p, cap))) return 0;
        b->p = q;
        b->cap = cap;
    }
    if (n) memcpy(b->p + b->n, d, n);
    b->n += n;
    return 1;
}
static int buf_u32(Buf* b, uint32_t v) { uint8_t t[4]; put32(t, v); return buf_put(b, t, 4); }
static void buf_free(Buf* b) { free(b->p); b->p = NULL; b->n = b->cap = 0; }

/* A container: { u32 count; u32 offset[count + 1]; entries }. */
typedef struct { const uint8_t* b; uint32_t len, n; uint32_t offs[4097]; } Ct;
static int ct_parse(Ct* c, const uint8_t* b, uint32_t len)
{
    uint32_t i;
    if (len < 8) return 0;
    c->b = b;
    c->len = len;
    c->n = le32(b);
    if (c->n < 1 || c->n > 4096 || 4u * (c->n + 2) > len) return 0;
    for (i = 0; i <= c->n; i++) c->offs[i] = le32(b + 4 + 4 * i);
    if (c->offs[0] != 4u * (c->n + 2) || c->offs[c->n] > len) return 0;
    for (i = 0; i < c->n; i++) if (c->offs[i] > c->offs[i + 1]) return 0;
    return 1;
}
#define CT_E(c, i) ((c)->b + (c)->offs[i])
#define CT_L(c, i) ((c)->offs[(i) + 1] - (c)->offs[i])

static int ct_build(Buf* out, const uint8_t* const* ents, const uint32_t* lens, uint32_t n)
{
    uint32_t i, off = 4 * (n + 2);
    out->n = 0;
    if (!buf_u32(out, n)) return 0;
    for (i = 0; i < n; i++) { if (!buf_u32(out, off)) return 0; off += lens[i]; }
    if (!buf_u32(out, off)) return 0;
    for (i = 0; i < n; i++) if (!buf_put(out, ents[i], lens[i])) return 0;
    return 1;
}

/* --- entry 0's program (decomp include/dv/pack.h, src/main/pack_install.c) -- */

typedef struct { uint32_t pos, op, size, ref0, nref; } Op;
typedef struct { Op ops[128]; int nops; uint32_t refs[2048]; int nrefs; } Prog;

/* Fixed size in words (opcode included) and the argument indices that hold
 * entry numbers; 0 = no such opcode (PackInstall would not advance). */
static const struct { uint8_t words; uint8_t refs[4]; uint8_t nrefs; } kOps[101] = {
    [1] = {15, {1, 2, 3, 4}, 4}, [2] = {1, {0}, 0}, [5] = {1, {0}, 0}, [6] = {2, {0}, 0},
    [7] = {3, {1, 2}, 2}, [8] = {2, {0}, 0}, [9] = {4, {0}, 0}, [10] = {2, {0}, 0},
    [11] = {2, {0}, 0}, [12] = {4, {0}, 0}, [13] = {3, {0}, 0}, [15] = {7, {1, 2, 3}, 3},
    [16] = {7, {0}, 0}, [17] = {7, {0}, 0}, [18] = {3, {0}, 0}, [19] = {3, {0}, 0},
    [20] = {2, {1}, 1}, [21] = {2, {1}, 1}, [23] = {4, {3}, 1}, [24] = {1, {0}, 0},
    [25] = {1, {0}, 0}, [28] = {2, {0}, 0}, [29] = {2, {0}, 0}, [30] = {2, {1}, 1},
    [54] = {2, {0}, 0}, [55] = {2, {0}, 0}, [56] = {1, {0}, 0}, [57] = {2, {0}, 0},
    [63] = {1, {0}, 0}, [64] = {1, {0}, 0}, [65] = {2, {1}, 1}, [66] = {3, {1, 2}, 2},
    [68] = {2, {0}, 0}, [70] = {3, {0}, 0}, [71] = {3, {0}, 0}, [72] = {2, {0}, 0},
    [73] = {2, {0}, 0}, [74] = {2, {0}, 0}, [75] = {2, {0}, 0}, [76] = {2, {0}, 0},
};

static int prog_ref(Prog* p, uint32_t pos)
{
    if (p->nrefs >= (int)(sizeof p->refs / sizeof p->refs[0])) return 0;
    p->refs[p->nrefs++] = pos;
    return 1;
}

static int prog_walk(Prog* p, const uint8_t* e0, uint32_t len)
{
    uint32_t pos = 0, i;
    p->nops = p->nrefs = 0;
    for (;;) {
        Op* o;
        uint32_t op;
        if (pos + 4 > len || p->nops >= (int)(sizeof p->ops / sizeof p->ops[0])) return 0;
        op = le32(e0 + pos);
        o = &p->ops[p->nops++];
        o->pos = pos;
        o->op = op;
        o->ref0 = (uint32_t)p->nrefs;
        if (op == 0) { o->size = 4; o->nref = 0; return 1; }
        if (op == 3) {
            uint32_t n, after, anims;
            if (pos + 16 > len) return 0;
            n = le32(e0 + pos + 12);
            after = pos + 16 + n * DV_NODE;
            if (n > 64 || after + 4 > len) return 0;
            anims = le32(e0 + after);
            if (anims > 1024 || after + 4 + 4 * anims > len) return 0;
            if (!prog_ref(p, pos + 8)) return 0;
            for (i = 0; i < n; i++)
                if (!prog_ref(p, pos + 16 + i * DV_NODE + 0x68) || !prog_ref(p, pos + 16 + i * DV_NODE + 0x6C)) return 0;
            for (i = 0; i < anims; i++) if (!prog_ref(p, after + 4 + 4 * i)) return 0;
            o->size = after + 4 + 4 * anims - pos;
        } else if (op == 31) {
            uint32_t n;
            if (pos + 12 > len) return 0;
            n = le32(e0 + pos + 8);
            o->size = 12 + 4 * n;
            if (!prog_ref(p, pos + 4)) return 0;
            for (i = 0; i < n && pos + 12 + 4 * i < len; i++) if (!prog_ref(p, pos + 12 + 4 * i)) return 0;
        } else if (op == 50) {
            if (pos + 8 > len) return 0;
            o->size = 8 + 0x48 * le32(e0 + pos + 4);
        } else if (op == 62) {
            uint32_t n = pos + 8 <= len ? le32(e0 + pos + 4) : 0;
            o->size = 8 + 4 * n;
            for (i = 0; i < n && pos + 8 + 4 * i < len; i++) if (!prog_ref(p, pos + 8 + 4 * i)) return 0;
        } else if (op == 67 || op == 100) {
            uint32_t n = pos + 8 <= len ? le32(e0 + pos + 4) : 0;
            o->size = 8 + 8 * n;
            for (i = 0; i < 2 * n && pos + 8 + 4 * i < len; i++) if (!prog_ref(p, pos + 8 + 4 * i)) return 0;
        } else if (op == 51 || op == 52 || op == 53) {
            uint32_t kind = pos + 8 <= len ? le32(e0 + pos + 4) : 99;
            o->size = 12;
            if (op == 53 ? (kind == 3 || kind == 4 || kind == 6) : (kind == 0 || (kind >= 3 && kind <= 6)))
                if (!prog_ref(p, pos + 8)) return 0;
        } else if (op <= 100 && kOps[op].words) {
            o->size = 4u * kOps[op].words;
            for (i = 0; i < kOps[op].nrefs; i++) if (!prog_ref(p, pos + 4u * kOps[op].refs[i])) return 0;
        } else {
            return 0;
        }
        o->nref = (uint32_t)p->nrefs - o->ref0;
        pos += o->size;
        if (pos > len) return 0;
    }
}

static int is_hero_op(const uint8_t* e0, const Op* o)
{
    return o->op == 31 || o->op == 7 || (o->op == 52 && le32(e0 + o->pos + 4) == 0);
}

/* The hero / stage split of an action pack (tools/heroswap.py split_pack). */
typedef struct {
    Ct ct;
    Prog prog;
    int k;                 /* ops[0..k) are the hero's */
    uint32_t prefix_end;   /* their bytes in entry 0 */
    uint32_t core;         /* entries 1..core */
    uint32_t tail[16];     /* hero entries after core, sorted */
    int ntail;
} Split;

static int split_pack(Split* s, const uint8_t* b, uint32_t len)
{
    const uint8_t* e0;
    int i, n7 = 0, n52 = 0;
    uint32_t r;
    if (!ct_parse(&s->ct, b, len)) return 0;
    e0 = CT_E(&s->ct, 0);
    if (!prog_walk(&s->prog, e0, CT_L(&s->ct, 0)) || s->prog.ops[0].op != 3) return 0;
    for (s->k = 1; s->prog.ops[s->k].op != 0 && is_hero_op(e0, &s->prog.ops[s->k]); s->k++) {}
    s->prefix_end = s->prog.ops[s->k].pos;
    s->core = 0;
    for (i = 0; i < s->k; i++) {
        const Op* o = &s->prog.ops[i];
        n7 += o->op == 7;
        n52 += o->op == 52;
        if (o->op == 3 || o->op == 52)
            for (r = 0; r < o->nref; r++) {
                uint32_t v = le32(e0 + s->prog.refs[o->ref0 + r]);
                if (v < 1) return 0;
                if (v > s->core) s->core = v;
            }
    }
    if (n7 != 1 || n52 != 1) return 0;
    s->ntail = 0;
    for (i = 0; i < s->k; i++) {
        const Op* o = &s->prog.ops[i];
        for (r = 0; r < o->nref; r++) {
            uint32_t v = le32(e0 + s->prog.refs[o->ref0 + r]);
            int j, dup = 0;
            if (o->op == 31 && r > 0 && v > s->core) return 0;   /* weld parts must be core */
            if (v >= s->ct.n) return 0;
            if (v <= s->core) continue;
            for (j = 0; j < s->ntail; j++) dup |= s->tail[j] == v;
            if (!dup) {
                if (s->ntail == 16) return 0;
                s->tail[s->ntail++] = v;
            }
        }
    }
    for (i = 1; i < s->ntail; i++) {           /* sort */
        uint32_t v = s->tail[i];
        int j = i - 1;
        while (j >= 0 && s->tail[j] > v) { s->tail[j + 1] = s->tail[j]; j--; }
        s->tail[j + 1] = v;
    }
    for (i = s->k; i < s->prog.nops; i++) {    /* the stage's ops name no hero entry */
        const Op* o = &s->prog.ops[i];
        for (r = 0; r < o->nref; r++) {
            uint32_t v = le32(e0 + s->prog.refs[o->ref0 + r]);
            int j;
            if (v <= s->core || v >= s->ct.n) return 0;
            for (j = 0; j < s->ntail; j++) if (s->tail[j] == v) return 0;
        }
    }
    return 1;
}

static int split_is_tail(const Split* s, uint32_t v)
{
    int j;
    for (j = 0; j < s->ntail; j++) if (s->tail[j] == v) return 1;
    return 0;
}

/* --- TIM chains (decomp src/main/exeD_texture.c, TimQueueAdd) ----------- */

typedef struct { uint32_t start, end; uint16_t clut[4], image[4]; int has_clut; } Tim;

static int tim_chain(const uint8_t* b, uint32_t len, Tim* out, int max, uint32_t* end)
{
    uint32_t pos = 0;
    int n = 0, k;
    while (pos + 0x1C <= len && !memcmp(b + pos, "TIM_CODE", 8) && le32(b + pos + 8) == 0x10 && n < max) {
        uint32_t flags = le32(b + pos + 12), bn = le32(b + pos + 16);
        Tim* t = &out[n];
        t->start = pos;
        if (flags == 2) {
            t->has_clut = 0;
            for (k = 0; k < 4; k++) t->image[k] = (uint16_t)le16s(b + pos + 20 + 2 * k);
            t->end = pos + 16 + bn;
        } else if (flags == 8 || flags == 9) {
            uint32_t ib = pos + 16 + bn;
            if (ib + 12 > len) break;
            t->has_clut = 1;
            for (k = 0; k < 4; k++) t->clut[k] = (uint16_t)le16s(b + pos + 20 + 2 * k);
            for (k = 0; k < 4; k++) t->image[k] = (uint16_t)le16s(b + ib + 4 + 2 * k);
            t->end = ib + le32(b + ib);
        } else {
            break;
        }
        if (t->end > len || t->end <= pos) break;
        pos = t->end;
        n++;
    }
    if (end) *end = pos;
    return n;
}

static int rect_overlaps(const uint16_t* a, const uint16_t* b)
{
    return a[0] < b[0] + b[2] && b[0] < a[0] + a[2] && a[1] < b[1] + b[3] && b[1] < a[1] + a[3];
}
static int tim_in_page(const Tim* t)
{
    return rect_overlaps(t->image, kHeroPage) || (t->has_clut && rect_overlaps(t->clut, kHeroPage));
}
static int tim_is_portrait(const Tim* t) { return !memcmp(t->image, kPortrait, sizeof kPortrait); }

/* --- kits ------------------------------------------------------------------ */

typedef struct {
    int ready;
    Buf ops;                  /* the hero's ops, entry refs renumbered to kit entries 1..n */
    uint32_t refs[512];       /* byte offsets of those refs in ops */
    uint32_t nrefs;
    uint32_t nent, ntail;     /* entries; the last ntail are the hero-tail ones */
    uint8_t* ent[400];
    uint32_t entlen[400];
    Buf vab;
    Buf page;                 /* the hero-page TIMs laid end to end */
    uint32_t npage;
    Buf portrait;
} Kit;

static Kit s_kits[HERO_COUNT];

/* The world map's walker (decomp src/map/mod2_worldmap.c, MapWalkerDraw):
 * an 8-bit sprite on page (832, 0), 8 frames of 32 px per direction, CLUT
 * (384, 240). Map stages upload it in a 192 x 176 sheet at VRAM (768, 0)
 * (CLUT block (384, 240, 272, 2)), whose left half holds the map's own
 * labels and buttons: the walker is the right half (units 64..191) of rows
 * 0..143, its palette the first 256 entries of the CLUT's row 0. */
#define DV_SHEET_W      192u                  /* 16-bit units */
#define DV_SHEET_H      176u
#define DV_WALK_X0      64u
#define DV_WALK_W       128u
#define DV_WALK_H       144u
typedef struct {
    int ready;
    uint8_t walker[DV_WALK_H * DV_WALK_W * 2];
    uint8_t pal[512];
} MapKit;
static MapKit s_mapkits[HERO_COUNT];

/* The sheet TIM in a chain (img (768, 0, 192, 176), CLUT (384, 240, 272, 2)):
 * offsets of its CLUT data and image data in b, or 0. */
static int sheet_find(const uint8_t* b, uint32_t len, uint32_t* clut_at, uint32_t* img_at)
{
    static Tim t[64];
    int n = tim_chain(b, len, t, 64, NULL), j;
    for (j = 0; j < n; j++) {
        const uint16_t want_img[4] = {768, 0, DV_SHEET_W, DV_SHEET_H}, want_clut[4] = {384, 240, 272, 2};
        uint32_t cbn;
        if (!t[j].has_clut || memcmp(t[j].image, want_img, 8) || memcmp(t[j].clut, want_clut, 8)) continue;
        cbn = le32(b + t[j].start + 16);
        *clut_at = t[j].start + 16 + 12;
        *img_at = t[j].start + 16 + cbn + 12;
        if (*img_at + DV_SHEET_W * DV_SHEET_H * 2 > len || *clut_at + 512 > len) return 0;
        return 1;
    }
    return 0;
}

static void mapkit_take(MapKit* m, const uint8_t* b, uint32_t clut_at, uint32_t img_at)
{
    uint32_t y;
    for (y = 0; y < DV_WALK_H; y++)
        memcpy(m->walker + y * DV_WALK_W * 2, b + img_at + (y * DV_SHEET_W + DV_WALK_X0) * 2, DV_WALK_W * 2);
    memcpy(m->pal, b + clut_at, 512);
    m->ready = 1;
}

static void mapkit_put(const MapKit* m, uint8_t* b, uint32_t clut_at, uint32_t img_at)
{
    uint32_t y;
    for (y = 0; y < DV_WALK_H; y++)
        memcpy(b + img_at + (y * DV_SHEET_W + DV_WALK_X0) * 2, m->walker + y * DV_WALK_W * 2, DV_WALK_W * 2);
    memcpy(b + clut_at, m->pal, 512);
}

static void kit_free(Kit* k)
{
    uint32_t i;
    for (i = 0; i < k->nent; i++) free(k->ent[i]);
    buf_free(&k->ops);
    buf_free(&k->vab);
    buf_free(&k->page);
    buf_free(&k->portrait);
    memset(k, 0, sizeof *k);
}

/* File 0: its VAB entry (op 51 kind 0) and TIM chain entry (op 30). */
static int file0_parts(const Ct* c, uint32_t* vab, uint32_t* tims)
{
    static Prog p;
    int i, nv = 0, nt = 0;
    if (!prog_walk(&p, CT_E(c, 0), CT_L(c, 0))) return 0;
    for (i = 0; i < p.nops; i++) {
        const Op* o = &p.ops[i];
        const uint8_t* e0 = CT_E(c, 0);
        if (o->op == 51 && le32(e0 + o->pos + 4) == 0 && o->nref) { *vab = le32(e0 + p.refs[o->ref0]); nv++; }
        if (o->op == 30 && o->nref) { *tims = le32(e0 + p.refs[o->ref0]); nt++; }
    }
    return nv == 1 && nt == 1 && *vab < c->n && *tims < c->n;
}

static int kit_build(Kit* k, const uint8_t* f0, uint32_t f0len, const uint8_t* pk, uint32_t pklen)
{
    static Split s;
    static Ct c0;
    static Tim tims[64];
    static uint32_t map[4097];
    const uint8_t* e0;
    uint32_t i, vab, te, end;
    int nt, j;

    memset(k, 0, sizeof *k);
    if (!split_pack(&s, pk, pklen)) return 0;
    e0 = CT_E(&s.ct, 0);
    memset(map, 0, sizeof map);
    /* entries: the named core ones in order, then the tail ones */
    for (i = 1; i <= s.core; i++) {
        int named = 0;
        uint32_t r;
        for (j = 0; j < s.k && !named; j++)
            for (r = 0; r < s.prog.ops[j].nref; r++)
                if (le32(e0 + s.prog.refs[s.prog.ops[j].ref0 + r]) == i) { named = 1; break; }
        if (!named) continue;
        if (k->nent == 400) return 0;
        map[i] = ++k->nent;
    }
    for (j = 0; j < s.ntail; j++) {
        if (k->nent == 400) return 0;
        map[s.tail[j]] = ++k->nent;
    }
    k->ntail = (uint32_t)s.ntail;
    for (i = 1; i <= s.core; i++)
        if (map[i]) {
            k->entlen[map[i] - 1] = CT_L(&s.ct, i);
            k->ent[map[i] - 1] = (uint8_t*)malloc(CT_L(&s.ct, i) ? CT_L(&s.ct, i) : 1);
            if (!k->ent[map[i] - 1]) return 0;
            memcpy(k->ent[map[i] - 1], CT_E(&s.ct, i), CT_L(&s.ct, i));
        }
    for (j = 0; j < s.ntail; j++) {
        uint32_t t = s.tail[j], m = map[t] - 1;
        k->entlen[m] = CT_L(&s.ct, t);
        k->ent[m] = (uint8_t*)malloc(CT_L(&s.ct, t) ? CT_L(&s.ct, t) : 1);
        if (!k->ent[m]) return 0;
        memcpy(k->ent[m], CT_E(&s.ct, t), CT_L(&s.ct, t));
    }
    /* the hero's ops, renumbered */
    if (!buf_put(&k->ops, e0, s.prefix_end)) return 0;
    for (j = 0; j < s.k; j++) {
        uint32_t r;
        for (r = 0; r < s.prog.ops[j].nref; r++) {
            uint32_t pos = s.prog.refs[s.prog.ops[j].ref0 + r];
            if (k->nrefs == 512) return 0;
            put32(k->ops.p + pos, map[le32(e0 + pos)]);
            k->refs[k->nrefs++] = pos;
        }
    }
    /* file 0: VAB, hero-page TIMs, portrait */
    if (!ct_parse(&c0, f0, f0len) || !file0_parts(&c0, &vab, &te)) return 0;
    if (!buf_put(&k->vab, CT_E(&c0, vab), CT_L(&c0, vab))) return 0;
    nt = tim_chain(CT_E(&c0, te), CT_L(&c0, te), tims, 64, &end);
    for (j = 0; j < nt; j++) {
        const uint8_t* t = CT_E(&c0, te) + tims[j].start;
        uint32_t n = tims[j].end - tims[j].start;
        if (tim_in_page(&tims[j])) { if (!buf_put(&k->page, t, n)) return 0; k->npage++; }
        if (tim_is_portrait(&tims[j])) { k->portrait.n = 0; if (!buf_put(&k->portrait, t, n)) return 0; }
    }
    if (!k->npage || !k->portrait.n) return 0;
    k->ready = 1;
    return 1;
}

/* VAB body bytes of a VAB_CODE section (decomp src/main/exeD_sound.c). */
static uint32_t vab_body(const uint8_t* v, uint32_t len)
{
    uint32_t hdr;
    int16_t ps;
    if (len < 12 || memcmp(v, "VAB_CODE", 8)) return 0;
    hdr = le32(v + 8) * 16 + 12;
    if (hdr + 0x20 > len || memcmp(v + hdr, "pBAV", 4)) return 0;
    ps = le16s(v + hdr + 0x12);
    return le32(v + hdr + 12) - (0xA20u + ((uint32_t)ps << 9));
}

/* --- per-stage table ------------------------------------------------------ */

typedef struct {
    uint8_t area, stage, ok, pad;
    uint32_t f0_len, f0_vab, f0_page;   /* file 0's length, its VAB, its hero-page TIMs */
    uint32_t pk_stage_n, pk_stage_bytes, pk_stage_ops;  /* the pack's stage entries, ops bytes */
    uint32_t copy_parts, copy_weld;     /* a story scene's copy of the hero: its parts, its weld lists (bytes) */
} StageRec;

static StageRec s_stages[2][64 * 13];
static int s_nstages[2];
static int s_disc_ready[2];

static const StageRec* stage_find(int area, int stage)
{
    int d = area >= 7, i;
    for (i = 0; i < s_nstages[d]; i++)
        if (s_stages[d][i].area == area && s_stages[d][i].stage == stage) return &s_stages[d][i];
    return NULL;
}

static uint32_t kit_entries_bytes(const Kit* k)
{
    uint32_t i, t = 0;
    for (i = 0; i < k->nent; i++) t += k->entlen[i];
    return t;
}

/* Bytes of the kit's 14 parts (prims + verts) and of its weld lists. */
static void kit_part_bytes(const Kit* k, uint32_t* parts, uint32_t* weld)
{
    uint32_t i, kn = le32(k->ops.p + 12), pos = 0;
    *parts = *weld = 0;
    for (i = 0; i < kn && i < 32; i++) {
        uint32_t pe = le32(k->ops.p + 16 + i * DV_NODE + 0x68), ve = le32(k->ops.p + 16 + i * DV_NODE + 0x6C);
        if (pe && pe <= k->nent) *parts += k->entlen[pe - 1];
        if (ve && ve <= k->nent) *parts += k->entlen[ve - 1];
    }
    /* the ops after op 3: op 31 = { 31, list, n, parts[n] } */
    pos = 16 + kn * DV_NODE;
    pos += 4 + 4 * le32(k->ops.p + pos);
    while (pos + 12 <= k->ops.n) {
        uint32_t op = le32(k->ops.p + pos);
        if (op == 31) {
            uint32_t list = le32(k->ops.p + pos + 4);
            if (list && list <= k->nent) *weld += k->entlen[list - 1] + 12 + 4 * le32(k->ops.p + pos + 8);
            pos += 12 + 4 * le32(k->ops.p + pos + 8);
        } else if (op == 52 || op == 7) {
            pos += 12;
        } else {
            break;
        }
    }
}

/* Sizes of the rebuilt files (the merges below build exactly these). */
static void rebuilt_sizes(const StageRec* r, const Kit* k, uint32_t* f0, uint32_t* pk)
{
    uint32_t n = 1 + k->nent + r->pk_stage_n;
    *pk = 4 * (n + 2) + (k->ops.n + r->pk_stage_ops) + kit_entries_bytes(k) + r->pk_stage_bytes;
    *f0 = r->f0_len - r->f0_vab + k->vab.n - r->f0_page + k->page.n;
}

/* --- DATA.CDR -------------------------------------------------------------- */

typedef struct { const uint8_t* b; uint32_t len; } Cdr;

static uint32_t msf(const uint8_t* p) { return ((uint32_t)p[0] * 60 + p[1]) * 75 + p[2]; }

static const uint8_t* cdr_at(const Cdr* d, uint32_t sector, uint32_t* avail)
{
    if ((uint64_t)sector * DV_SECTOR >= d->len) return NULL;
    *avail = d->len - sector * DV_SECTOR;
    return d->b + (size_t)sector * DV_SECTOR;
}

/* The stage's files: start sector (relative to the index) and sector count. */
static int cdr_stage(const Cdr* d, int area, int stage, uint32_t* idx, uint32_t* starts, uint32_t* counts, int* nfiles)
{
    static Ct h, sec;
    uint32_t avail, ah, ns, i, s[256];
    const uint8_t* p = cdr_at(d, 0, &avail);
    int n = 0, j;
    if (!p) return 0;
    ah = 1 + msf(p + 4 * area);
    if (!(p = cdr_at(d, ah, &avail)) || !ct_parse(&h, p, avail) || h.n != 3) return 0;
    if ((uint32_t)stage * 4 + 4 > CT_L(&h, 1) || !msf(CT_E(&h, 1) + 4 * stage)) return 0;
    *idx = ah + msf(CT_E(&h, 1) + 4 * stage);
    if (!(p = cdr_at(d, *idx, &avail)) || !ct_parse(&sec, p, avail) || sec.n != 4) return 0;
    ns = CT_L(&sec, 3) / 4;
    for (i = 0; i < ns && i < 256; i++) {
        uint32_t v = msf(CT_E(&sec, 3) + 4 * i);
        if (v) s[n++] = v;
    }
    *nfiles = 0;
    for (j = 0; j < n; j++) {
        uint32_t next = 0;
        int m;
        for (m = j + 1; m < n; m++) if (s[m] > s[j]) { next = s[m]; break; }
        if (!next) break;
        starts[*nfiles] = s[j];
        counts[(*nfiles)++] = next - s[j];
    }
    return *nfiles >= 2;
}

static const uint8_t* cdr_file(const Cdr* d, uint32_t idx, uint32_t start, uint32_t count, uint32_t* len)
{
    uint32_t avail;
    const uint8_t* p = cdr_at(d, idx + start, &avail);
    if (!p) return NULL;
    *len = count * DV_SECTOR < avail ? count * DV_SECTOR : avail;
    return p;
}

/* The area hero's look: its kit's vertex entries (node i -> bytes), so a
 * scene copy is found when the stage's own hero looks different. 5/20 and
 * 5/24 play Phillip with Kadum (13 of 14 vertex entries differ from 5/1's)
 * while their scene copies (file 80, object 0) are the plain Phillip of
 * 5/1 (12 of 14 equal). */
static uint32_t kit_node_ref(const Kit* k, uint32_t i, uint32_t field);
static int kit_verts(const Kit* k, const uint8_t** v, uint32_t* l)
{
    uint32_t i, n = 0;
    for (i = 0; i < 14; i++) { v[i] = NULL; l[i] = 0; }
    if (!k || !k->ready) return 0;
    for (i = 0; i < 14 && i < le32(k->ops.p + 12); i++) {
        uint32_t e = kit_node_ref(k, i, 0x6C);
        if (e && e <= k->nent) { v[i] = k->ent[e - 1]; l[i] = k->entlen[e - 1]; n++; }
    }
    return (int)n;
}

/* A story scene's copy of the stage's hero in the stage's room files (65..):
 * a 10+ part op 3 whose vertex entries are the hero's, or the area hero's
 * kit's (see swap_actors). Notes the bytes of its parts and of its weld
 * lists, which the swap replaces: the warp row grows by the difference. */
static void scan_scene_copy(const Cdr* d, uint32_t idx, const uint32_t* starts, const uint32_t* counts, int nf,
                            const Split* s, const Kit* ak, StageRec* r)
{
    static Ct ce;
    static Prog pp;
    const uint8_t *hv[14], *kv[14];
    uint32_t hl[14], kl[14], i, len;
    int n, m, j, nh = 0;
    kit_verts(ak, kv, kl);
    for (i = 0; i < 14; i++) {
        uint32_t v = le32(CT_E(&s->ct, 0) + 16 + i * DV_NODE + 0x6C);
        hv[i] = v < s->ct.n ? CT_E(&s->ct, v) : NULL;
        hl[i] = v < s->ct.n ? CT_L(&s->ct, v) : 0;
        nh += hv[i] != NULL;
    }
    if (nh < 10) return;
    for (n = 65; n < nf; n++) {
        const uint8_t* f;
        const uint8_t* e0;
        for (m = 65; m < n && starts[m] != starts[n]; m++) {}
        if (m < n) continue;                         /* same bytes as an earlier number */
        if (!(f = cdr_file(d, idx, starts[n], counts[n], &len)) || !ct_parse(&ce, f, len) ||
            !prog_walk(&pp, CT_E(&ce, 0), CT_L(&ce, 0)))
            continue;
        e0 = CT_E(&ce, 0);
        for (j = 0; j < pp.nops; j++) {
            const Op* o = &pp.ops[j];
            uint32_t nodes, hits = 0, khits = 0, parts = 0, weld = 0, k2, rr;
            if (o->op != 3) continue;
            nodes = le32(e0 + o->pos + 12);
            if (nodes < 10 || nodes > 32) continue;
            for (i = 0; i < nodes && i < 14; i++) {
                uint32_t v = le32(e0 + o->pos + 16 + i * DV_NODE + 0x6C);
                if (v < ce.n && hv[i] && CT_L(&ce, v) == hl[i] && !memcmp(CT_E(&ce, v), hv[i], hl[i])) hits++;
                if (v < ce.n && kv[i] && CT_L(&ce, v) == kl[i] && !memcmp(CT_E(&ce, v), kv[i], kl[i])) khits++;
            }
            if (hits < 10 && khits < 10) continue;
            for (i = 0; i < nodes && i < 14; i++) {
                uint32_t pe = le32(e0 + o->pos + 16 + i * DV_NODE + 0x68), ve = le32(e0 + o->pos + 16 + i * DV_NODE + 0x6C);
                if (pe < ce.n) parts += CT_L(&ce, pe);
                if (ve < ce.n) parts += CT_L(&ce, ve);
            }
            for (k2 = 0; k2 < (uint32_t)pp.nops; k2++) {      /* its weld lists */
                const Op* w = &pp.ops[k2];
                int mine = 0;
                if (w->op != 31 || !w->nref) continue;
                for (rr = 1; rr < w->nref && !mine; rr++) {
                    uint32_t pe = le32(e0 + pp.refs[w->ref0 + rr]);
                    for (i = 0; i < nodes && i < 14; i++)
                        if (pe == le32(e0 + o->pos + 16 + i * DV_NODE + 0x68)) mine = 1;
                }
                if (mine && le32(e0 + pp.refs[w->ref0]) < ce.n) weld += CT_L(&ce, le32(e0 + pp.refs[w->ref0]));
            }
            r->copy_parts = parts;
            r->copy_weld = weld;
            return;
        }
    }
}

/* --- the discs --------------------------------------------------------------
 * psxrecomp's plugin disc reads (psx_mod_read_disc_file, _disc_file_extent)
 * read the disc the session started with. The game asks for disc 2 at the end
 * of disc 1 and the runtime answers by mounting the roster's other image
 * (cdrom_disc_select, runtime include/cdrom.h); those reads never see it, so
 * disc 2's stages found no table and kept their own hero (seen 2026-11-17,
 * 9/10 reached from a disc 1 session). The roster says which image is in the
 * drive and where each image is, so a disc other than the session's is read
 * here from its image: the .cue's first FILE, MODE2/2352 sectors (2048 data
 * bytes at +24; a bare image of 2048-byte sectors also works), DATA.CDR in
 * the ISO 9660 root. The same reader fetches the other disc's kits, so a
 * disc 2 hero no longer needs disc 2 started once. */
int cdrom_disc_selected(void);
const char* cdrom_disc_roster_path(int index_1based);
int cdrom_disc_roster_count(void);

typedef struct { FILE* f; uint32_t ss, off; } DiscImg;

static int img_open(DiscImg* im, const char* path)
{
    char bin[1024];
    size_t n = path ? strlen(path) : 0;
    im->f = NULL;
    im->ss = 2352;
    im->off = 24;
    if (!n || n >= sizeof bin) return 0;
    if (n > 4 && (!strcmp(path + n - 4, ".cue") || !strcmp(path + n - 4, ".CUE"))) {
        FILE* c = fopen(path, "rb");
        char text[4096], *q, *e;
        size_t got;
        if (!c) return 0;
        got = fread(text, 1, sizeof text - 1, c);
        fclose(c);
        text[got] = 0;
        if (!(q = strstr(text, "FILE \"")) || !(e = strchr(q + 6, '"'))) return 0;
        *e = 0;
        q += 6;
        if (q[0] == '/' || q[0] == '\\' || (q[0] && q[1] == ':')) {
            if (strlen(q) >= sizeof bin) return 0;
            strcpy(bin, q);
        } else {
            const char* s1 = strrchr(path, '/');
            const char* s2 = strrchr(path, '\\');
            size_t dir = (size_t)((s1 > s2 ? s1 : s2) ? (s1 > s2 ? s1 : s2) - path + 1 : 0);
            if (dir + strlen(q) >= sizeof bin) return 0;
            memcpy(bin, path, dir);
            strcpy(bin + dir, q);
        }
        if (strstr(e + 1, "MODE1/2352") && !strstr(e + 1, "MODE2/2352")) im->off = 16;
    } else {
        strcpy(bin, path);
    }
    if (!(im->f = fopen(bin, "rb"))) return 0;
    if (fseek(im->f, 0, SEEK_END) == 0) {
        long len = ftell(im->f);
        if (len > 0 && len % 2352 != 0 && len % 2048 == 0) { im->ss = 2048; im->off = 0; }
    }
    return 1;
}

static int img_sector(DiscImg* im, uint32_t lba, uint8_t* out)
{
    return fseek(im->f, (long)(lba * im->ss + im->off), SEEK_SET) == 0 && fread(out, 1, 2048, im->f) == 2048;
}

/* LBA and size of a file in the image's ISO 9660 root ("DATA.CDR"). */
static int img_find(DiscImg* im, const char* name, uint32_t* lba, uint32_t* size)
{
    uint8_t sec[2048];
    uint32_t root, rsize, i, nl = (uint32_t)strlen(name);
    if (!img_sector(im, 16, sec) || memcmp(sec + 1, "CD001", 5)) return 0;
    root = le32(sec + 156 + 2);
    rsize = le32(sec + 156 + 10);
    for (i = 0; i < (rsize + 2047) / 2048 && i < 64; i++) {
        uint32_t p = 0;
        if (!img_sector(im, root + i, sec)) return 0;
        while (p + 33 < 2048 && sec[p]) {
            uint32_t len = sec[p], fl = sec[p + 32];
            if (fl >= nl && !memcmp(sec + p + 33, name, nl) && (fl == nl || sec[p + 33 + nl] == ';')) {
                *lba = le32(sec + p + 2);
                *size = le32(sec + p + 10);
                return 1;
            }
            p += len;
        }
    }
    return 0;
}

/* The disc psx_mod_read_disc_file reads: 0, 1, or -1 if unknown. */
static int session_disc(void)
{
    static int disc = -2;
    uint32_t lba, size;
    if (disc == -2) {
        if (!psx_mod_disc_file_extent("DATA.CDR", &lba, &size)) return -1;
        disc = psx_mod_disc_file_extent("SLUS_010.92", &lba, &size) ? 0 : 1;
    }
    return disc;
}

/* DATA.CDR of disc 0 / 1 (malloc'd), from the session's reads when it is the
 * session's disc, else from the roster's image; its LBA ^ size as the key. */
static uint8_t* disc_data_cdr(int disc, uint32_t* size, uint32_t* key)
{
    uint8_t* data;
    uint32_t got = 0, lba = 0, i;
    DiscImg im;
    *size = 0;
    if (disc == session_disc()) {
        if (!psx_mod_disc_file_extent("DATA.CDR", &lba, size) ||
            !psx_mod_read_disc_file("DATA.CDR", NULL, 0, size) || !*size) return NULL;
        *key = lba ^ *size;
        if (!(data = (uint8_t*)malloc(*size))) return NULL;
        if (!psx_mod_read_disc_file("DATA.CDR", data, *size, &got) || got != *size) { free(data); return NULL; }
        return data;
    }
    if (cdrom_disc_roster_count() < disc + 1 || !img_open(&im, cdrom_disc_roster_path(disc + 1))) return NULL;
    data = NULL;
    if (img_find(&im, "DATA.CDR", &lba, size) && *size && (data = (uint8_t*)malloc(*size + 2048))) {
        for (i = 0; i * 2048 < *size; i++)
            if (!img_sector(&im, lba + i, data + i * 2048)) { free(data); data = NULL; break; }
    }
    fclose(im.f);
    *key = lba ^ *size;
    return data;
}

/* Table and kits of disc 0 (areas 0..6) or 1 (7..12). */
static int disc_scan(int disc)
{
    static Split s;
    static Ct c0, cp;
    static Tim tims[64];
    uint32_t size = 0, key = 0, idx, starts[256], counts[256];
    uint8_t* data;
    Cdr d;
    int area, stage, nf, h;

    if (!(data = disc_data_cdr(disc, &size, &key))) return 0;
    (void)key;
    d.b = data;
    d.len = size;
    s_nstages[disc] = 0;
    for (area = disc ? 7 : 0; area <= (disc ? 12 : 6); area++) {
        for (stage = 1; stage < 64; stage++) {
            uint32_t l0, l1, vab, te, end, page = 0, i;
            const uint8_t *f0, *f1;
            StageRec* r;
            int j, nt;
            if (!cdr_stage(&d, area, stage, &idx, starts, counts, &nf)) continue;
            f0 = cdr_file(&d, idx, starts[0], counts[0], &l0);
            f1 = cdr_file(&d, idx, starts[1], counts[1], &l1);
            /* a map stage of a kit's area: the walker for that hero */
            if (f0 && ct_parse(&c0, f0, l0) && c0.n >= 2) {
                uint32_t ca, ia;
                for (h = 0; h < HERO_COUNT; h++)
                    if (kKitSource[h][0] == area && !s_mapkits[h].ready && sheet_find(CT_E(&c0, 1), CT_L(&c0, 1), &ca, &ia))
                        mapkit_take(&s_mapkits[h], CT_E(&c0, 1), ca, ia);
            }
            if (!f0 || !f1 || !ct_parse(&cp, f1, l1) || cp.n <= 100 || memcmp(CT_E(&cp, 1), "VAB_CODE", 8)) continue;
            if (!split_pack(&s, f1, l1) || !ct_parse(&c0, f0, l0) || !file0_parts(&c0, &vab, &te)) continue;
            nt = tim_chain(CT_E(&c0, te), CT_L(&c0, te), tims, 64, &end);
            for (j = 0; j < nt; j++) if (tim_in_page(&tims[j])) page += tims[j].end - tims[j].start;
            if (s_nstages[disc] == (int)(sizeof s_stages[0] / sizeof s_stages[0][0])) break;
            r = &s_stages[disc][s_nstages[disc]++];
            memset(r, 0, sizeof *r);
            r->area = (uint8_t)area;
            r->stage = (uint8_t)stage;
            r->ok = 1;
            r->f0_len = c0.offs[c0.n];
            r->f0_vab = CT_L(&c0, vab);
            r->f0_page = page;
            for (i = s.core + 1; i < s.ct.n; i++)
                if (!split_is_tail(&s, i)) { r->pk_stage_n++; r->pk_stage_bytes += CT_L(&s.ct, i); }
            r->pk_stage_ops = CT_L(&s.ct, 0) - s.prefix_end;
            scan_scene_copy(&d, idx, starts, counts, nf, &s,
                            dv_area_hero(area) >= 0 ? &s_kits[dv_area_hero(area)] : NULL, r);
            for (h = 0; h < HERO_COUNT; h++)
                if (kKitSource[h][0] == area && kKitSource[h][1] == stage && !s_kits[h].ready) {
                    if (!kit_build(&s_kits[h], f0, l0, f1, l1)) {
                        kit_free(&s_kits[h]);
                        fprintf(stderr, "%s: no kit for %s from %d/%d\n", DV_HS_PLUGIN, kHeroNames[h], area, stage);
                    }
                }
        }
    }
    free(data);
    s_disc_ready[disc] = s_nstages[disc] > 0;
    return s_disc_ready[disc];
}

/* --- cache ----------------------------------------------------------------- */

static void cache_dir(void)
{
    dv_mkdir("mods");
    dv_mkdir("mods/cache");
    dv_mkdir(DV_HS_CACHE_DIR);
}

static int w32(FILE* f, uint32_t v) { uint8_t t[4]; put32(t, v); return fwrite(t, 1, 4, f) == 4; }
static int r32(FILE* f, uint32_t* v) { uint8_t t[4]; if (fread(t, 1, 4, f) != 4) return 0; *v = le32(t); return 1; }
static int wbuf(FILE* f, const uint8_t* p, uint32_t n) { return w32(f, n) && (!n || fwrite(p, 1, n, f) == n); }
static int rbuf(FILE* f, Buf* b, uint32_t max)
{
    uint32_t n;
    b->n = 0;
    if (!r32(f, &n) || n > max) return 0;
    if (!buf_put(b, NULL, 0)) return 0;
    if (n > b->cap) {
        uint8_t* q = (uint8_t*)realloc(b->p, n);
        if (!q) return 0;
        b->p = q;
        b->cap = n;
    }
    if (n && fread(b->p, 1, n, f) != n) return 0;
    b->n = n;
    return 1;
}

#define DV_KIT_MAGIC   0x4B485644u   /* "DVHK" */
#define DV_TABLE_MAGIC 0x53485644u   /* "DVHS" */
#define DV_KIT_VER     1u
#define DV_TABLE_VER   3u   /* 2: scene copies; 3: copies of the area hero's kit look too */

static void kit_save(int h)
{
    char path[256];
    const Kit* k = &s_kits[h];
    FILE* f;
    uint32_t i, ok;
    snprintf(path, sizeof path, DV_HS_CACHE_DIR "/hero%d.kit", h);
    if (!(f = fopen(path, "wb"))) return;
    ok = w32(f, DV_KIT_MAGIC) && w32(f, DV_KIT_VER) && w32(f, (uint32_t)h) && wbuf(f, k->ops.p, k->ops.n) &&
         w32(f, k->nrefs);
    for (i = 0; ok && i < k->nrefs; i++) ok = w32(f, k->refs[i]);
    ok = ok && w32(f, k->nent) && w32(f, k->ntail);
    for (i = 0; ok && i < k->nent; i++) ok = wbuf(f, k->ent[i], k->entlen[i]);
    ok = ok && wbuf(f, k->vab.p, k->vab.n) && w32(f, k->npage) && wbuf(f, k->page.p, k->page.n) &&
         wbuf(f, k->portrait.p, k->portrait.n);
    fclose(f);
    if (!ok) remove(path);
}

static int kit_load(int h)
{
    char path[256];
    Kit* k = &s_kits[h];
    FILE* f;
    uint32_t v, i;
    int ok;
    snprintf(path, sizeof path, DV_HS_CACHE_DIR "/hero%d.kit", h);
    if (!(f = fopen(path, "rb"))) return 0;
    memset(k, 0, sizeof *k);
    ok = r32(f, &v) && v == DV_KIT_MAGIC && r32(f, &v) && v == DV_KIT_VER && r32(f, &v) && v == (uint32_t)h &&
         rbuf(f, &k->ops, 0x10000) && r32(f, &k->nrefs) && k->nrefs <= 512;
    for (i = 0; ok && i < k->nrefs; i++) ok = r32(f, &k->refs[i]) && k->refs[i] + 4 <= k->ops.n;
    ok = ok && r32(f, &k->nent) && k->nent <= 400 && r32(f, &k->ntail) && k->ntail <= k->nent;
    for (i = 0; ok && i < k->nent; i++) {
        Buf b = {0};
        ok = rbuf(f, &b, 0x200000);
        k->ent[i] = b.p;
        k->entlen[i] = b.n;
        if (ok && !b.p) k->ent[i] = (uint8_t*)malloc(1);
    }
    ok = ok && rbuf(f, &k->vab, 0x200000) && r32(f, &k->npage) && rbuf(f, &k->page, 0x100000) &&
         rbuf(f, &k->portrait, 0x10000) && k->npage && k->portrait.n;
    fclose(f);
    if (!ok) { kit_free(k); return 0; }
    k->ready = 1;
    return 1;
}

#define DV_MAP_MAGIC 0x4D485644u   /* "DVHM" */
#define DV_MAP_VER   1u

static void mapkit_save(int h)
{
    char path[256];
    FILE* f;
    int ok;
    snprintf(path, sizeof path, DV_HS_CACHE_DIR "/hero%d.map", h);
    if (!(f = fopen(path, "wb"))) return;
    ok = w32(f, DV_MAP_MAGIC) && w32(f, DV_MAP_VER) && w32(f, (uint32_t)h) &&
         fwrite(s_mapkits[h].walker, 1, sizeof s_mapkits[h].walker, f) == sizeof s_mapkits[h].walker &&
         fwrite(s_mapkits[h].pal, 1, sizeof s_mapkits[h].pal, f) == sizeof s_mapkits[h].pal;
    fclose(f);
    if (!ok) remove(path);
}

static int mapkit_load(int h)
{
    char path[256];
    FILE* f;
    uint32_t v;
    int ok;
    snprintf(path, sizeof path, DV_HS_CACHE_DIR "/hero%d.map", h);
    if (!(f = fopen(path, "rb"))) return 0;
    ok = r32(f, &v) && v == DV_MAP_MAGIC && r32(f, &v) && v == DV_MAP_VER && r32(f, &v) && v == (uint32_t)h &&
         fread(s_mapkits[h].walker, 1, sizeof s_mapkits[h].walker, f) == sizeof s_mapkits[h].walker &&
         fread(s_mapkits[h].pal, 1, sizeof s_mapkits[h].pal, f) == sizeof s_mapkits[h].pal;
    fclose(f);
    s_mapkits[h].ready = ok;
    return ok;
}

static void table_save(int disc, uint32_t key)
{
    char path[256];
    FILE* f;
    snprintf(path, sizeof path, DV_HS_CACHE_DIR "/disc%d.stages", disc + 1);
    if (!(f = fopen(path, "wb"))) return;
    if (!(w32(f, DV_TABLE_MAGIC) && w32(f, DV_TABLE_VER) && w32(f, key) && w32(f, (uint32_t)s_nstages[disc]) &&
          fwrite(s_stages[disc], sizeof(StageRec), (size_t)s_nstages[disc], f) == (size_t)s_nstages[disc])) {
        fclose(f);
        remove(path);
        return;
    }
    fclose(f);
}

static int table_load(int disc, uint32_t key)
{
    char path[256];
    FILE* f;
    uint32_t v, n;
    int ok;
    snprintf(path, sizeof path, DV_HS_CACHE_DIR "/disc%d.stages", disc + 1);
    if (!(f = fopen(path, "rb"))) return 0;
    ok = r32(f, &v) && v == DV_TABLE_MAGIC && r32(f, &v) && v == DV_TABLE_VER && r32(f, &v) && v == key &&
         r32(f, &n) && n <= sizeof s_stages[0] / sizeof s_stages[0][0] &&
         fread(s_stages[disc], sizeof(StageRec), n, f) == n;
    fclose(f);
    if (!ok) return 0;
    s_nstages[disc] = (int)n;
    s_disc_ready[disc] = n > 0;
    return s_disc_ready[disc];
}

/* The key of disc 0 / 1's DATA.CDR (LBA ^ size), from wherever it is read. */
static int disc_key(int disc, uint32_t* key)
{
    uint32_t lba = 0, size = 0;
    if (disc == session_disc()) {
        if (!psx_mod_disc_file_extent("DATA.CDR", &lba, &size)) return 0;
    } else {
        DiscImg im;
        int ok;
        if (cdrom_disc_roster_count() < disc + 1 || !img_open(&im, cdrom_disc_roster_path(disc + 1))) return 0;
        ok = img_find(&im, "DATA.CDR", &lba, &size);
        fclose(im.f);
        if (!ok) return 0;
    }
    *key = lba ^ size;
    return 1;
}

/* The disc in the drive: the roster's selection (game.toml discs: disc 1,
 * disc 2) when there is one, else the session's. */
static int mounted_disc(void)
{
    int sel = cdrom_disc_roster_count() >= 2 ? cdrom_disc_selected() : 0;
    return (sel == 1 || sel == 2) ? sel - 1 : session_disc();
}

/* Table and kits of one disc: from the cache, else from its DATA.CDR. */
static void disc_prepare(int disc)
{
    uint32_t key = 0;
    int h, need = 0;
    if (disc < 0 || s_disc_ready[disc] || !disc_key(disc, &key)) return;
    cache_dir();
    for (h = 0; h < HERO_COUNT; h++) if (!s_kits[h].ready) kit_load(h);
    for (h = 0; h < HERO_COUNT; h++) if (!s_mapkits[h].ready) mapkit_load(h);
    for (h = 0; h < HERO_COUNT; h++)
        need |= (!s_kits[h].ready || !s_mapkits[h].ready) && (kKitSource[h][0] >= 7) == disc;
    if (!need && table_load(disc, key)) {
        fprintf(stderr, "%s: disc %d from the cache (%d action stages)\n", DV_HS_PLUGIN, disc + 1, s_nstages[disc]);
        return;
    }
    if (!disc_scan(disc)) {
        fprintf(stderr, "%s: DATA.CDR of disc %d not readable\n", DV_HS_PLUGIN, disc + 1);
        return;
    }
    table_save(disc, key);
    for (h = 0; h < HERO_COUNT; h++)
        if (s_kits[h].ready && (kKitSource[h][0] >= 7) == disc) kit_save(h);
    for (h = 0; h < HERO_COUNT; h++)
        if (s_mapkits[h].ready && (kKitSource[h][0] >= 7) == disc) mapkit_save(h);
    fprintf(stderr, "%s: disc %d read: %d action stages, kits:", DV_HS_PLUGIN, disc + 1, s_nstages[disc]);
    for (h = 0; h < HERO_COUNT; h++) if (s_kits[h].ready) fprintf(stderr, " %s", kHeroNames[h]);
    fprintf(stderr, "\n");
}

/* The disc in the drive (again at every stage load: the game may have asked
 * for the other one), and once the other disc too when some of its heroes
 * have no kit yet and its image can be read. */
static void ensure_disc(void)
{
    static int tried_other[2];
    int disc = mounted_disc(), h, missing = 0;
    if (disc < 0) return;
    disc_prepare(disc);
    for (h = 0; h < HERO_COUNT; h++) missing |= !s_kits[h].ready && (kKitSource[h][0] >= 7) != disc;
    if (missing && !tried_other[1 - disc]) {
        tried_other[1 - disc] = 1;
        disc_prepare(1 - disc);
    }
}

/* --- the swap -------------------------------------------------------------- */

static int s_enabled;
static int s_hero = -1;                    /* chosen hero, -1 = each stage's own */
static int s_swap, s_swap_area, s_swap_stage, s_swap_hero;
static int s_saved_hero = -1;              /* g_Hero.index the swap replaced (the game's) */
static int s_written_hero = -1;            /* the one this plugin wrote, -1 = none */
extern volatile int dv_heroswap_restart_request;   /* set by the in-game menu, see dv_hs_vblank */
static Buf s_orig_portrait;                /* the stage hero's portrait (from its file 0) */
static Buf s_orig_page;                    /* the stage hero's hero-page TIMs (from its file 0), end to end */
static uint64_t s_hero_verts[14];          /* the stage hero's vertex entries (hash), for scene copies */
static uint32_t s_hero_verts_len[14];
static uint16_t s_name_old[16], s_name_new[16];   /* the stage hero's and the chosen hero's names (glyphs) */
static int s_name_old_n, s_name_new_n;

static void read_options(void)
{
    char v[32];
    int h;
    s_hero = -1;
    if (!psx_mod_current_option_value("hero", v, sizeof v) &&
        !psx_mod_option_value(DV_HS_PKG, DV_HS_FEATURE, "hero", v, sizeof v))
        return;
    for (h = 0; h < HERO_COUNT; h++) if (!strcmp(v, kHeroValues[h])) s_hero = h;
}

static void ram_read(uint32_t addr, uint8_t* out, uint32_t n)
{
    uint32_t i;
    for (i = 0; i + 4 <= n; i += 4) put32(out + i, psx_mod_read_word(addr + i));
    for (; i < n; i++) out[i] = psx_mod_read_byte(addr + i);
}

static void restore_hero_index(void)
{
    if (s_saved_hero >= 0) {
        fprintf(stderr, "%s: g_Hero.index back to %d\n", DV_HS_PLUGIN, s_saved_hero);
        psx_mod_write_byte(DV_HERO_INDEX, (uint8_t)s_saved_hero);
        s_saved_hero = -1;
        s_written_hero = -1;
    }
}

/* g_Hero.index = hero (the name in the menus and the save). The value to
 * give back later is the game's own: one this plugin wrote (the hero of a
 * previous swap) is never taken for it. */
static void hold_hero_index(int hero)
{
    uint8_t cur = psx_mod_read_byte(DV_HERO_INDEX);
    if (s_saved_hero < 0 || (int)cur != s_written_hero) s_saved_hero = cur;
    if (cur != (uint8_t)hero) {
        psx_mod_write_byte(DV_HERO_INDEX, (uint8_t)hero);
        fprintf(stderr, "%s: g_Hero.index %u -> %d (the game's %d)\n", DV_HS_PLUGIN, cur, hero, s_saved_hero);
    }
    s_written_hero = hero;
}

static uint32_t sectors_bytes(uint32_t n) { return (n + DV_SECTOR - 1) / DV_SECTOR * DV_SECTOR; }

/* Layout of the stage buffers (StageBuffersAlloc) and the SPU slots
 * (SoundSpuLayout), from the sizes in RAM. */
static uint32_t arena_end(const uint32_t* b, int area, int stage)
{
    uint32_t w[10], warp;
    int i;
    for (i = 0; i < 10; i++) w[i] = b[i];
    if ((area == 6 && stage == 22) || (area == 7 && stage == 29) || (area == 8 && stage == 29) ||
        (area == 9 && stage == 29) || (area == 4 && stage == 26) || (area == 3 && stage == 26))
        w[5] = 0;
    warp = w[8] > w[6] + w[7] ? w[8] - (w[6] + w[7]) : 0;
    return DV_ACTION_HEAP + 4 * (w[0] + w[1] + w[3] + w[4] + w[5] + w[2] + w[6] + w[7] + warp) + 4;
}
static uint32_t spu_end(const uint32_t* s0)
{
    uint32_t s[12], a[10], end = 0;
    int i;
    for (i = 0; i < 12; i++) s[i] = s0[i];
    s[3] = s[11] = s[3] > s[11] ? s[3] : s[11];
    a[1] = 0;
    a[2] = a[1] + s[1];
    a[4] = a[2] + s[2];
    a[5] = a[4] + s[4];
    a[6] = a[5] + s[5];
    a[3] = a[6] + s[6];
    a[7] = a[9] = a[3] + s[3];
    a[8] = a[7] + s[7];
    for (i = 1; i <= 9; i++) if (a[i] + s[i] > end) end = a[i] + s[i];
    return end;
}

static void dv_hs_buffers_entry(struct CPUState* cpu, uint32_t address)
{
    int area = (int16_t)psx_mod_read_half(DV_STAGE_AREA), stage = (int16_t)psx_mod_read_half(DV_STAGE_STAGE);
    const StageRec* r;
    const Kit* k;
    uint32_t f0, pk, need, body, area64, sizes[10], snd[12], i;
    (void)cpu;
    (void)address;
    s_swap = 0;
    dv_heroswap_restart_request = 0;   /* a stage loads anyway */
    if (!s_enabled) return;
    ensure_disc();
    if (s_hero < 0) {
        restore_hero_index();
        return;
    }
    if (s_hero == dv_area_hero(area) || !(r = stage_find(area, stage))) {
        /* nothing to rebuild (the stage's own hero is the chosen one, or not
         * an action stage: world map, scenes, the save screen), but the name
         * stays the chosen hero's, so the menus and the save say who is played */
        hold_hero_index(s_hero);
        return;
    }
    k = &s_kits[s_hero];
    if (!k->ready) {
        fprintf(stderr, "%s: stage %d/%d: no %s kit yet (start the disc that has %d/%d once)\n", DV_HS_PLUGIN,
                area, stage, kHeroNames[s_hero], kKitSource[s_hero][0], kKitSource[s_hero][1]);
        restore_hero_index();
        return;
    }
    for (i = 0; i < 10; i++) sizes[i] = psx_mod_read_word(DV_STAGE_BUFFER_SIZES + 4 * i);
    for (i = 0; i < 12; i++) snd[i] = psx_mod_read_word(DV_SOUND_BODY_SIZE + 4 * i);
    rebuilt_sizes(r, k, &f0, &pk);
    need = (sectors_bytes(f0) > sectors_bytes(pk) ? sectors_bytes(f0) : sectors_bytes(pk)) / 4;
    if (need > sizes[0]) sizes[0] = need;
    body = vab_body(k->vab.p, k->vab.n);
    area64 = (body + 63) / 64 * 64;
    if (area64 > snd[1]) snd[1] = area64;
    if (r->copy_parts) {
        /* a story scene's copy of the hero is swapped too: its event pack
         * (warp row, g_StageBufferSizes[8]) grows by the parts' difference */
        uint32_t kparts, kweld;
        int32_t grow;
        kit_part_bytes(k, &kparts, &kweld);
        grow = (int32_t)(kparts + kweld) - (int32_t)(r->copy_parts + r->copy_weld) + 256;
        if (grow > 0) sizes[8] += sectors_bytes((uint32_t)grow) / 4;
    }
    if (arena_end(sizes, area, stage) > DV_ARENA_END_MAX || spu_end(snd) > DV_SPU_END_MAX) {
        fprintf(stderr, "%s: stage %d/%d: %s does not fit (buffers end 0x%08X, sound %u B): own hero\n",
                DV_HS_PLUGIN, area, stage, kHeroNames[s_hero], arena_end(sizes, area, stage), spu_end(snd));
        restore_hero_index();
        return;
    }
    psx_mod_write_word(DV_STAGE_BUFFER_SIZES, sizes[0]);
    psx_mod_write_word(DV_STAGE_BUFFER_SIZES + 8 * 4, sizes[8]);
    psx_mod_write_word(DV_SOUND_BODY_SIZE + 4, snd[1]);
    s_swap = 1;
    s_swap_area = area;
    s_swap_stage = stage;
    s_swap_hero = s_hero;
    s_orig_portrait.n = 0;
    memset(s_hero_verts_len, 0, sizeof s_hero_verts_len);   /* noted again from this stage's pack */
    s_name_old_n = s_name_new_n = 0;
    fprintf(stderr, "%s: stage %d/%d as %s: file 0 %u B, pack %u B, row 0 %u words, slot 1 %u B\n",
            DV_HS_PLUGIN, area, stage, kHeroNames[s_hero], f0, pk, sizes[0], snd[1]);
}

/* File 0 with the kit's VAB, hero-page TIMs and portrait. */
static int merge_file0(Buf* out, const uint8_t* b, uint32_t len, const Kit* k)
{
    static Ct c;
    static Tim tims[64], kt[64];
    static const uint8_t* ents[16];
    static uint32_t lens[16];
    Buf chain = {0};
    uint32_t vab, te, end, i, pos = 0;
    int nt, nk, j, m, slots = 0, slot = 0;
    if (!ct_parse(&c, b, len) || c.n > 16 || !file0_parts(&c, &vab, &te)) return 0;
    nt = tim_chain(CT_E(&c, te), CT_L(&c, te), tims, 64, &end);
    nk = tim_chain(k->page.p, k->page.n, kt, 64, NULL);
    s_orig_page.n = 0;
    for (j = 0; j < nt; j++) slots += tim_in_page(&tims[j]);
    /* the hero-page TIMs: the kit's take their places in turn, any left over
     * go in the last (tools/heroswap.py replace_page_tims) */
    for (j = 0; j < nt; j++) {
        const uint8_t* e = CT_E(&c, te);
        if (tim_is_portrait(&tims[j])) {
            s_orig_portrait.n = 0;
            buf_put(&s_orig_portrait, e + tims[j].start, tims[j].end - tims[j].start);
        }
        if (!tim_in_page(&tims[j])) continue;
        buf_put(&s_orig_page, e + tims[j].start, tims[j].end - tims[j].start);   /* for reloads elsewhere */
        buf_put(&chain, e + pos, tims[j].start - pos);
        for (m = slot; m < nk && (m == slot || slot == slots - 1); m++)
            buf_put(&chain, k->page.p + kt[m].start, kt[m].end - kt[m].start);
        slot++;
        pos = tims[j].end;
    }
    if (!slots) {
        buf_put(&chain, CT_E(&c, te), end);
        buf_put(&chain, k->page.p, k->page.n);
        pos = end;
    }
    buf_put(&chain, CT_E(&c, te) + pos, CT_L(&c, te) - pos);
    /* the portrait, in place of the stage's (same size) */
    if (s_orig_portrait.n && s_orig_portrait.n == k->portrait.n) {
        Tim ct[64];
        int n2 = tim_chain(chain.p, chain.n, ct, 64, NULL);
        for (j = 0; j < n2; j++)
            if (tim_is_portrait(&ct[j]) && ct[j].end - ct[j].start == k->portrait.n)
                memcpy(chain.p + ct[j].start, k->portrait.p, k->portrait.n);
    }
    for (i = 0; i < c.n; i++) {
        ents[i] = CT_E(&c, i);
        lens[i] = CT_L(&c, i);
    }
    ents[vab] = k->vab.p;
    lens[vab] = k->vab.n;
    ents[te] = chain.p;
    lens[te] = chain.n;
    i = ct_build(out, ents, lens, c.n);
    buf_free(&chain);
    return (int)i;
}

/* The action pack with the kit's hero (tools/heroswap.py merge_packs, --prune). */
static int merge_pack(Buf* out, const uint8_t* b, uint32_t len, const Kit* k)
{
    static Split s;
    static const uint8_t* ents[4097];
    static uint32_t lens[4097], tmap[4097], kmap[401];
    Buf e0 = {0};
    uint32_t n = 1, i, slots = 0, r;
    int j, ok;
    if (!split_pack(&s, b, len)) return 0;
    memset(tmap, 0, sizeof tmap);
    for (i = 0; i < k->nent - k->ntail; i++) { kmap[i + 1] = n; ents[n] = k->ent[i]; lens[n++] = k->entlen[i]; }
#define PUT_KIT_TAIL(t) do { uint32_t q = k->nent - k->ntail + (t); kmap[q + 1] = n; ents[n] = k->ent[q]; lens[n++] = k->entlen[q]; } while (0)
    for (i = s.core + 1; i < s.ct.n; i++) {
        if (split_is_tail(&s, i)) {
            slots++;
            if (slots == (uint32_t)s.ntail) { uint32_t t; for (t = slots - 1; t < k->ntail; t++) PUT_KIT_TAIL(t); }
            else if (slots - 1 < k->ntail) PUT_KIT_TAIL(slots - 1);
            continue;
        }
        tmap[i] = n;
        ents[n] = CT_E(&s.ct, i);
        lens[n++] = CT_L(&s.ct, i);
    }
    if (!s.ntail) { uint32_t t; for (t = 0; t < k->ntail; t++) PUT_KIT_TAIL(t); }
#undef PUT_KIT_TAIL
    /* entry 0: the kit's ops, then the stage's, renumbered */
    ok = buf_put(&e0, k->ops.p, k->ops.n) && buf_put(&e0, CT_E(&s.ct, 0) + s.prefix_end, CT_L(&s.ct, 0) - s.prefix_end);
    for (i = 0; ok && i < k->nrefs; i++) put32(e0.p + k->refs[i], kmap[le32(k->ops.p + k->refs[i])]);
    for (j = s.k; ok && j < s.prog.nops; j++)
        for (r = 0; r < s.prog.ops[j].nref; r++) {
            uint32_t pos = s.prog.refs[s.prog.ops[j].ref0 + r];
            put32(e0.p + k->ops.n + (pos - s.prefix_end), tmap[le32(CT_E(&s.ct, 0) + pos)]);
        }
    ents[0] = e0.p;
    lens[0] = e0.n;
    ok = ok && ct_build(out, ents, lens, n);
    buf_free(&e0);
    return ok;
}

/* In another pack: copies of the stage hero's portrait become the kit's. */
/* In another pack (host copy): copies of the stage hero's portrait become
 * the kit's, in place (same size). */
static int swap_portraits(uint8_t* b, uint32_t len, const Kit* k)
{
    static Ct c;
    static Tim t[64];
    /* the stage's own portrait (file 0), or the area hero's usual one (its
     * kit's): 12/29's ending scene shows 9/1's Anita, not 12/29's file 0 */
    int ah = dv_area_hero(s_swap_area);
    const Buf* alt = (ah >= 0 && s_kits[ah].ready && ah != s_swap_hero) ? &s_kits[ah].portrait : NULL;
    uint32_t i, plen = s_orig_portrait.n;
    int done = 0, j, nt;
    if (!plen || plen != k->portrait.n || !ct_parse(&c, b, len)) return 0;
    if (alt && alt->n != plen) alt = NULL;
    for (i = 0; i < c.n; i++) {
        uint8_t* e = b + c.offs[i];
        nt = tim_chain(e, CT_L(&c, i), t, 64, NULL);
        for (j = 0; j < nt; j++)
            if (t[j].end - t[j].start == plen && tim_is_portrait(&t[j]) &&
                (!memcmp(e + t[j].start, s_orig_portrait.p, plen) || (alt && !memcmp(e + t[j].start, alt->p, plen)))) {
                memcpy(e + t[j].start, k->portrait.p, plen);
                done++;
            }
    }
    return done;
}

/* --- story scene actors ------------------------------------------------------
 * A few event packs install their own copy of the stage's hero for a scene
 * (14-part op 3 objects with their own script, motions and texture page;
 * 0/1, 3/1, 4/13, 5/19, 9/10, 12/29 on the discs). Recognised by their
 * vertex entries, which are the stage hero's parts byte for byte (10 of 14
 * at least), their parts become the kit's (whose packets use the hero page,
 * where the kit's texture is) and their weld lists (op 31, packet offsets
 * into those parts) the kit's. Their motions stay the scene's. */

static uint64_t fnv64(const uint8_t* p, uint32_t n)
{
    uint64_t h = 1469598103934665603ull;
    while (n--) { h ^= *p++; h *= 1099511628211ull; }
    return h;
}

static void note_hero_verts(const uint8_t* b, uint32_t len)
{
    static Split s;
    uint32_t i, n;
    memset(s_hero_verts_len, 0, sizeof s_hero_verts_len);
    if (!split_pack(&s, b, len)) return;
    n = le32(CT_E(&s.ct, 0) + 12);
    for (i = 0; i < n && i < 14; i++) {
        uint32_t v = le32(CT_E(&s.ct, 0) + 16 + i * DV_NODE + 0x6C);
        if (v < s.ct.n) {
            s_hero_verts[i] = fnv64(CT_E(&s.ct, v), CT_L(&s.ct, v));
            s_hero_verts_len[i] = CT_L(&s.ct, v);
        }
    }
}

/* Kit node i's prims / verts: kit entry numbers (1-based) from its op 3. */
static uint32_t kit_node_ref(const Kit* k, uint32_t i, uint32_t field)
{
    return le32(k->ops.p + 16 + i * DV_NODE + field);
}

/* --- reloads of the hero's texture ----------------------------------------
 * A pack can upload the stage hero's texture into the hero page again (0/1's
 * event file 81, after a scene that used the page): a chain whose hero-page
 * TIMs are all copies of file 0's gets the kit's, place by place, any left
 * over in the last (tools/heroswap.py replace_page_tims).
 * The other images in the page (2026-11-17) are variants of the stage
 * hero's texture for a story scene's twin of the hero: the enemy-group
 * files 77 / 75 / 77 of 5/19, 5/20, 5/24 upload Phillip's page with a
 * scythe and a dark palette (CLUT row 243) for file 80's copy (object 0),
 * file 77 of 12/29 Anita's with another face and a great sword (rows up to
 * 239, CLUT rows 241..255) for file 80's object 8. Those copies are the
 * scene copies swap_actors gives the chosen hero's parts, so they need the
 * kit's texture too, and the variant would paint the chosen hero (and the
 * twin) with the stage hero's texture: a variant becomes a two-unit TIM
 * that leaves the kit's texture (there since file 0) as it is. */
static int tim_is_orig_page(const uint8_t* t, uint32_t n)
{
    static Tim ot[64];
    int no = tim_chain(s_orig_page.p, s_orig_page.n, ot, 64, NULL), j;
    for (j = 0; j < no; j++)
        if (ot[j].end - ot[j].start == n && !memcmp(s_orig_page.p + ot[j].start, t, n)) return 1;
    return 0;
}

/* The first two image units at VRAM (384, 0) of the kit's page, from the
 * last of its TIMs that starts there (the last upload wins); 0 if none. */
static uint32_t kit_page_first_units(const Kit* k)
{
    static Tim kt[64];
    int nk = tim_chain(k->page.p, k->page.n, kt, 64, NULL), j;
    for (j = nk - 1; j >= 0; j--) {
        uint32_t img = kt[j].start + 0x10;
        if (kt[j].image[0] != 384 || kt[j].image[1] != 0 || kt[j].image[2] < 2) continue;
        if (kt[j].has_clut) img += le32(k->page.p + img);
        if (img + 16 > kt[j].end) continue;
        return le32(k->page.p + img + 12);
    }
    return 0;
}

static int swap_page_reloads(Buf* out, const uint8_t* b, uint32_t len, const Kit* k, int* swapped,
                             int* variants)
{
    static Ct c;
    static Tim t[64], kt[64];
    static const uint8_t* ents[4097];
    static uint32_t lens[4097];
    Buf pool = {0};
    uint32_t offs[64], idx[64], i;
    int nchanged = 0, nk, ok;
    *swapped = 0;
    *variants = 0;
    if (!s_orig_page.n || !ct_parse(&c, b, len)) return 0;
    nk = tim_chain(k->page.p, k->page.n, kt, 64, NULL);
    for (i = 0; i < c.n && nchanged < 64; i++) {
        const uint8_t* e = CT_E(&c, i);
        int nt = tim_chain(e, CT_L(&c, i), t, 64, NULL), j, slots = 0, slot = 0, all = 1, m;
        uint32_t pos = 0, start = pool.n;
        for (j = 0; j < nt; j++)
            if (tim_in_page(&t[j])) {
                slots++;
                all &= tim_is_orig_page(e + t[j].start, t[j].end - t[j].start);
            }
        if (!slots) continue;
        if (!all) *variants += 1;
        for (j = 0; j < nt; j++) {
            if (!tim_in_page(&t[j])) continue;
            buf_put(&pool, e + pos, t[j].start - pos);
            if (!all) {
                /* A variant: the kit's texture is in the page since file 0
                 * and nothing else in these stages uploads there, and the
                 * kit's TIM can be bigger than the variant (Felippe, Anna),
                 * more than the enemy group's buffer holds. A 16-bit TIM of
                 * two units at (384, 0) with the kit's own values keeps the
                 * chain valid and changes nothing. */
                uint8_t tiny[32];
                memcpy(tiny, "TIM_CODE", 8);
                put32(tiny + 8, 0x10u);
                put32(tiny + 12, 2u);
                put32(tiny + 16, 16u);
                put32(tiny + 20, 384u);          /* x 384, y 0 */
                put32(tiny + 24, 2u | (1u << 16)); /* w 2, h 1 */
                put32(tiny + 28, kit_page_first_units(k));
                buf_put(&pool, tiny, sizeof tiny);
            } else {
                for (m = slot; m < nk && (m == slot || slot == slots - 1); m++)
                    buf_put(&pool, k->page.p + kt[m].start, kt[m].end - kt[m].start);
            }
            slot++;
            pos = t[j].end;
        }
        buf_put(&pool, e + pos, CT_L(&c, i) - pos);
        offs[nchanged] = start;
        idx[nchanged++] = i;
        lens[i] = pool.n - start;
        *swapped += slots;
    }
    if (!nchanged) { buf_free(&pool); return 0; }
    for (i = 0; i < c.n; i++) {
        int j, hit = 0;
        for (j = 0; j < nchanged; j++) if (idx[j] == i) { ents[i] = pool.p + offs[j]; hit = 1; }
        if (!hit) { ents[i] = CT_E(&c, i); lens[i] = CT_L(&c, i); }
    }
    ok = ct_build(out, ents, lens, c.n);
    buf_free(&pool);
    return ok;
}

/* --- names in the dialogues -------------------------------------------------
 * Text is u16 glyph codes (decomp docs/FORMATS.md, Text). An event pack's
 * messages (op 62 entries) name the stage's hero in its speaker label
 * ("Kodel:", a message of its own) and in the lines: every whole-word copy
 * of the stage hero's name becomes the chosen hero's. The names come from
 * the game itself, ACTION.BIN's g_HeroNames (0x800C5E14, the Magic menu's,
 * the same glyph codes), read when the stage's pack is installed. */
#define DV_HERO_NAMES 0x800C5E14u

static int read_hero_name(int hero, uint16_t* out)
{
    uint32_t p, i;
    if (hero < 0 || hero >= HERO_COUNT) return 0;
    p = psx_mod_read_word(DV_HERO_NAMES + 4 * (uint32_t)hero);
    if (p < 0x80000000u || p >= DV_RAM_END - 32 || (p & 1u)) return 0;
    for (i = 0; i < 16; i++) {
        uint16_t w = psx_mod_read_half(p + 2 * i);
        if (w == 0x8000) return (int)i;
        out[i] = w;
    }
    return 0;
}

static int glyph_letter(uint16_t w)
{
    int idx = (w & 31) * 21 + ((w >> 5) & 31);
    return w < 0x8000 && idx >= 43 && idx <= 94;
}

static int swap_names(Buf* out, const uint8_t* b, uint32_t len, int* swapped)
{
    static Ct c;
    static Prog p;
    static const uint8_t* ents[4097];
    static uint32_t lens[4097];
    static uint8_t is_msg[4097];
    Buf pool = {0};
    uint32_t offs[4097], i;
    int j, ok;
    *swapped = 0;
    if (!s_name_old_n || !s_name_new_n || !ct_parse(&c, b, len) || !prog_walk(&p, CT_E(&c, 0), CT_L(&c, 0))) return 0;
    memset(is_msg, 0, sizeof is_msg);
    for (j = 0; j < p.nops; j++)
        if (p.ops[j].op == 62) {
            uint32_t r;
            for (r = 0; r < p.ops[j].nref; r++) {
                uint32_t e = le32(CT_E(&c, 0) + p.refs[p.ops[j].ref0 + r]);
                if (e < c.n) is_msg[e] = 1;
            }
        }
    for (i = 0; i < c.n; i++) {
        const uint8_t* e = CT_E(&c, i);
        uint32_t nw = CT_L(&c, i) / 2, w, start = pool.n;
        int hit = 0;
        offs[i] = UINT32_MAX;
        if (!is_msg[i]) continue;
        for (w = 0; w < nw;) {
            uint16_t cur = (uint16_t)(e[2 * w] | e[2 * w + 1] << 8);
            int m = 0, q;
            if (w + (uint32_t)s_name_old_n <= nw && (w == 0 || !glyph_letter((uint16_t)(e[2 * w - 2] | e[2 * w - 1] << 8)))) {
                for (q = 0; q < s_name_old_n; q++)
                    if ((uint16_t)(e[2 * (w + q)] | e[2 * (w + q) + 1] << 8) != s_name_old[q]) break;
                m = q == s_name_old_n && (w + q == nw || !glyph_letter((uint16_t)(e[2 * (w + q)] | e[2 * (w + q) + 1] << 8)));
            }
            if (m) {
                for (q = 0; q < s_name_new_n; q++) { uint8_t t[2] = {(uint8_t)s_name_new[q], (uint8_t)(s_name_new[q] >> 8)}; buf_put(&pool, t, 2); }
                w += (uint32_t)s_name_old_n;
                hit++;
            } else {
                uint8_t t[2] = {(uint8_t)cur, (uint8_t)(cur >> 8)};
                buf_put(&pool, t, 2);
                w++;
            }
        }
        if (!hit) { pool.n = start; continue; }
        if ((pool.n - start) & 3) { uint8_t z[2] = {0, 0}; buf_put(&pool, z, 2); }   /* entries stay 4-aligned */
        offs[i] = start;
        lens[i] = pool.n - start;
        *swapped += hit;
    }
    if (!*swapped) { buf_free(&pool); return 0; }
    for (i = 0; i < c.n; i++) {
        if (offs[i] != UINT32_MAX) ents[i] = pool.p + offs[i];
        else { ents[i] = CT_E(&c, i); lens[i] = CT_L(&c, i); }
    }
    ok = ct_build(out, ents, lens, c.n);
    buf_free(&pool);
    return ok;
}

static int swap_actors(Buf* out, const uint8_t* b, uint32_t len, const Kit* k, int* swapped)
{
    static Ct c;
    static Prog p, kp;
    static const uint8_t* ents[4200];
    static uint32_t lens[4200];
    static uint8_t drop[128];
    Buf e0 = {0}, kops = {0}, ins = {0};
    const uint8_t *src, *kv[14];
    uint32_t i, kn = le32(k->ops.p + 12), part_entry[4097], first_drop = 0, insert_at = 0, n, kl[14];
    int j, nact = 0, ok = 1;

    *swapped = 0;
    if (!ct_parse(&c, b, len) || c.n + 16 > 4200 || !prog_walk(&p, CT_E(&c, 0), CT_L(&c, 0))) return 0;
    src = CT_E(&c, 0);
    for (i = 0; i < c.n; i++) { ents[i] = CT_E(&c, i); lens[i] = CT_L(&c, i); }
    memset(part_entry, 0, sizeof part_entry);        /* prims entry -> node index + 1 */
    memset(drop, 0, sizeof drop);
    kit_verts(dv_area_hero(s_swap_area) >= 0 ? &s_kits[dv_area_hero(s_swap_area)] : NULL, kv, kl);
    for (j = 0; j < p.nops; j++) {
        const Op* o = &p.ops[j];
        uint32_t nodes, m = 0, mk = 0;
        if (o->op != 3) continue;
        nodes = le32(src + o->pos + 12);
        if (nodes < 10 || nodes > 32) continue;
        for (i = 0; i < nodes && i < 14; i++) {
            uint32_t v = le32(src + o->pos + 16 + i * DV_NODE + 0x6C);
            if (v < c.n && s_hero_verts_len[i] == CT_L(&c, v) && fnv64(CT_E(&c, v), CT_L(&c, v)) == s_hero_verts[i]) m++;
            if (v < c.n && kv[i] && kl[i] == CT_L(&c, v) && !memcmp(CT_E(&c, v), kv[i], kl[i])) mk++;
        }
        if (m < 10 && mk < 10) continue;
        /* a copy of the stage's hero (or of the area hero's usual look):
         * its parts become the kit's */
        for (i = 0; i < nodes && i < kn; i++) {
            uint32_t pe = le32(src + o->pos + 16 + i * DV_NODE + 0x68), ve = le32(src + o->pos + 16 + i * DV_NODE + 0x6C);
            uint32_t kpe = kit_node_ref(k, i, 0x68), kve = kit_node_ref(k, i, 0x6C);
            if (pe >= c.n || ve >= c.n || !kpe || !kve || kpe > k->nent || kve > k->nent) { ok = 0; break; }
            ents[pe] = k->ent[kpe - 1]; lens[pe] = k->entlen[kpe - 1];
            ents[ve] = k->ent[kve - 1]; lens[ve] = k->entlen[kve - 1];
            part_entry[pe] = i + 1;
        }
        if (!insert_at) insert_at = o->pos + o->size;
        nact++;
    }
    if (!ok || !nact) return 0;
    /* its weld lists name its old parts: drop them */
    for (j = 0; j < p.nops && j < 128; j++) {
        const Op* o = &p.ops[j];
        uint32_t r;
        if (o->op != 31 || !o->nref) continue;
        for (r = 1; r < o->nref; r++) {
            uint32_t v = le32(src + p.refs[o->ref0 + r]);
            if (v < c.n && part_entry[v]) { drop[j] = 1; break; }
        }
        if (!drop[j]) continue;
        if (!first_drop) first_drop = o->pos;
        r = le32(src + p.refs[o->ref0]);              /* its list, used by nothing else now */
        if (r && r < c.n) lens[r] = 0;
    }
    if (first_drop) insert_at = first_drop;
    /* the kit's weld lists, on the copy's (new) parts: kit part entry -> node -> copy prims entry */
    n = c.n;
    buf_put(&kops, k->ops.p, k->ops.n);
    buf_u32(&kops, 0);
    if (prog_walk(&kp, kops.p, kops.n))
        for (j = 0; j < kp.nops; j++) {
            const Op* o = &kp.ops[j];
            uint32_t r, list, mark = ins.n;
            int all = 1;
            if (o->op != 31 || !o->nref) continue;
            list = le32(kops.p + kp.refs[o->ref0]);
            if (!list || list > k->nent) continue;
            buf_u32(&ins, 31);
            buf_u32(&ins, n);
            buf_u32(&ins, o->nref - 1);
            for (r = 1; r < o->nref; r++) {
                uint32_t ke = le32(kops.p + kp.refs[o->ref0 + r]), node = 0, q, target = 0;
                for (q = 0; q < kn; q++) if (kit_node_ref(k, q, 0x68) == ke) node = q + 1;
                for (q = 1; q < c.n && node; q++) if (part_entry[q] == node) target = q;
                buf_u32(&ins, target);
                all &= target != 0;
            }
            if (!all) { ins.n = mark; continue; }    /* a part the copy lacks: no such list */
            ents[n] = k->ent[list - 1];
            lens[n++] = k->entlen[list - 1];
        }
    buf_free(&kops);
    /* entry 0: the old ops less the dropped lists, the kit's lists at insert_at */
    for (j = 0; j < p.nops; j++) {
        const Op* o = &p.ops[j];
        if (o->pos == insert_at) buf_put(&e0, ins.p, ins.n);
        if (j < 128 && drop[j]) continue;
        buf_put(&e0, src + o->pos, o->size);
    }
    if (insert_at >= CT_L(&c, 0)) buf_put(&e0, ins.p, ins.n);
    buf_put(&e0, src + p.ops[p.nops - 1].pos + 4, CT_L(&c, 0) - (p.ops[p.nops - 1].pos + 4));   /* any tail */
    ents[0] = e0.p;
    lens[0] = e0.n;
    ok = ct_build(out, ents, lens, n);
    buf_free(&e0);
    buf_free(&ins);
    *swapped = nact;
    return ok;
}

/* Bytes free at a pack: up to the next stage buffer (or the heap's end),
 * at least the sectors its own read filled. */
#define DV_LOAD_ROWS   0x800A2B84u   /* g_GameState.row[10], 0x28 bytes each */
#define DV_HEAP_CURSOR 0x800A2B80u
static uint32_t pack_room(uint32_t pack, uint32_t orig_len)
{
    uint32_t end = psx_mod_read_word(DV_HEAP_CURSOR), r, c;
    /* the warp row (6) starts on the enemy groups' memory (row 5) and runs
     * over it, so a pack there ends at the next buffer after rows 5 / 6 */
    int warp = pack == psx_mod_read_word(DV_LOAD_ROWS + 6 * 0x28);
    for (r = 0; r < 10; r++)
        for (c = 0; c < 2; c++) {
            uint32_t b = psx_mod_read_word(DV_LOAD_ROWS + r * 0x28 + c * 4);
            if (warp && (r == 5 || (r == 6 && c == 0))) continue;
            if (b > pack && b < end) end = b;
        }
    if (end < pack + sectors_bytes(orig_len)) end = pack + sectors_bytes(orig_len);
    return end - pack;
}

/* A world map stage's file 0 (program [30 chain, 21 data, 20 text, 71 10 0],
 * decomp src/map/mod2_worldmap.c): its walker and its portrait (the map's
 * Status screen) become the chosen hero's, in place (same sizes). */
static void swap_map_stage(uint32_t pack)
{
    static Prog p;
    static Buf e0, chain;
    static Tim t[64];
    uint32_t n = psx_mod_read_word(pack), a0, a1, ta = 0, tz = 0, ca, ia;
    int j, ok30 = 0, ok21 = 0, ok71 = 0, nt, por = 0, area = (int16_t)psx_mod_read_half(DV_STAGE_AREA);
    const Kit* k;
    if (n < 2 || n > 16 || s_hero == dv_area_hero(area)) return;
    a0 = psx_mod_read_word(pack + 4);
    a1 = psx_mod_read_word(pack + 8);
    if (a1 <= a0 || a1 - a0 > 0x1000) return;
    if (e0.cap < a1 - a0 && !(e0.p = (uint8_t*)realloc(e0.p, e0.cap = a1 - a0))) { e0.cap = 0; return; }
    ram_read(pack + a0, e0.p, a1 - a0);
    if (!prog_walk(&p, e0.p, a1 - a0)) return;
    for (j = 0; j < p.nops; j++) {
        const Op* o = &p.ops[j];
        if (o->op == 30 && o->nref) {
            uint32_t e = le32(e0.p + p.refs[o->ref0]);
            if (e >= n) return;
            ta = psx_mod_read_word(pack + 4 + 4 * e);
            tz = psx_mod_read_word(pack + 8 + 4 * e);
            ok30 = 1;
        }
        ok21 |= o->op == 21;
        ok71 |= o->op == 71 && le32(e0.p + o->pos + 4) == 10;
    }
    if (!ok30 || !ok21 || !ok71 || tz <= ta || tz - ta > 0x100000) return;
    if (chain.cap < tz - ta && !(chain.p = (uint8_t*)realloc(chain.p, chain.cap = tz - ta))) { chain.cap = 0; return; }
    ram_read(pack + ta, chain.p, tz - ta);
    k = &s_kits[s_hero];
    if (s_mapkits[s_hero].ready && sheet_find(chain.p, tz - ta, &ca, &ia))
        mapkit_put(&s_mapkits[s_hero], chain.p, ca, ia);
    else
        ca = 0;
    nt = tim_chain(chain.p, tz - ta, t, 64, NULL);
    for (j = 0; j < nt; j++)
        if (k->ready && tim_is_portrait(&t[j]) && t[j].end - t[j].start == k->portrait.n) {
            memcpy(chain.p + t[j].start, k->portrait.p, k->portrait.n);
            por++;
        }
    if (!ca && !por) return;
    psx_mod_host_write_ram(pack + ta, chain.p, tz - ta);
    fprintf(stderr, "%s: map %d/%d: %s%s for %s\n", DV_HS_PLUGIN, area, (int16_t)psx_mod_read_half(DV_STAGE_STAGE),
            ca ? "walker" : "", por ? (ca ? " and portrait" : "portrait") : "", kHeroNames[s_hero]);
}

/* --- the chapter epilogue ----------------------------------------------------
 * Besides the dialogues (op 62, swap_names), the only texts of either disc
 * that name a hero are the op-20 banks of the epilogue stages (MAP.BIN mode
 * 11, program [30 1, 20 2, 71 11 0]: 1/35, 1/37, 2/29, 3/29, 3/31, 4/27,
 * 5/26, 6/23, 7/30, 8/30, 9/30; decomp src/map/mod2_epilogue.c) and of the
 * character profiles after them (mode 12). The epilogue tells what the
 * chapter's hero did ("... once Clovis rescued Princess Celia ..."), so the
 * area hero's name becomes the chosen hero's, whole words; the lines are
 * centred when drawn. The profiles stay: each is a character's page with its
 * own picture. The text: glyph codes, 0x3000 / 0x1800 spaces, 0x8888 a new
 * line, 0xFCCC + a colour, 0xFFFF the end. ACTION.BIN, where g_HeroNames
 * lives, is not in RAM during MAP.BIN's modes, so the names are kept here
 * (the glyph codes carry each glyph's advance in their top bits). */
static const uint16_t kNameGlyphs[HERO_COUNT][8] = {
    {0x2862, 0x1623, 0x1e83, 0x20c4, 0x15c3, 0x1864},                  /* Clovis */
    {0x2822, 0x1c44, 0x1623, 0x1d43, 0x2263},                          /* Arlen */
    {0x2962, 0x1e83, 0x2123, 0x1d43, 0x1623},                          /* Kodel */
    {0x24c2, 0x1d43, 0x1623, 0x15c3, 0x2004, 0x2004, 0x1d43},          /* Felippe */
    {0x2602, 0x25a3, 0x15c3, 0x1623, 0x1623, 0x15c3, 0x2004},          /* Phillip */
    {0x28e2, 0x1d43, 0x1c44, 0x1e83, 0x2e43, 0x1d43},                  /* Gerome */
    {0x2822, 0x2263, 0x2263, 0x20c3},                                  /* Anna */
    {0x31a2, 0x15c3, 0x25a3, 0x20c3, 0x1d43, 0x1623},                  /* Mihael */
    {0x2822, 0x2263, 0x15c3, 0x1884, 0x20c3},                          /* Anita */
};
static const int kNameLen[HERO_COUNT] = {6, 5, 5, 7, 7, 6, 4, 6, 5};

static void swap_epilogue(uint32_t pack)
{
    static Ct c;
    static Prog p;
    static Buf in, text, out;
    static const uint8_t* ents[64];
    static uint32_t lens[64];
    uint32_t n = psx_mod_read_word(pack), len, e20 = UINT32_MAX, nw, w, i, room;
    int area = (int16_t)psx_mod_read_half(DV_STAGE_AREA), ah = dv_area_hero(area), j, mode11 = 0, hits = 0;
    const uint16_t *on, *nn;
    if (ah < 0 || ah == s_hero || n < 2 || n > 63) return;
    len = psx_mod_read_word(pack + 4 + 4 * n);
    if (len < 8 || len > 0x100000u || pack + len > DV_RAM_END) return;
    if (in.cap < len && !(in.p = (uint8_t*)realloc(in.p, in.cap = len))) { in.cap = 0; return; }
    ram_read(pack, in.p, len);
    if (!ct_parse(&c, in.p, len) || !prog_walk(&p, CT_E(&c, 0), CT_L(&c, 0))) return;
    for (j = 0; j < p.nops; j++) {
        const Op* o = &p.ops[j];
        mode11 |= o->op == 71 && le32(CT_E(&c, 0) + o->pos + 4) == 11;
        if (o->op == 20 && o->nref) e20 = le32(CT_E(&c, 0) + p.refs[o->ref0]);
    }
    if (!mode11 || e20 >= c.n) return;
    on = kNameGlyphs[ah];
    nn = kNameGlyphs[s_hero];
    text.n = 0;
    nw = CT_L(&c, e20) / 2;
    for (w = 0; w < nw;) {
        const uint8_t* e = CT_E(&c, e20);
        uint16_t cur = (uint16_t)(e[2 * w] | e[2 * w + 1] << 8);
        int m = 0, q;
        if (cur == 0xFCCC && w + 1 < nw) {                  /* a colour and its argument */
            buf_put(&text, e + 2 * w, 4);
            w += 2;
            continue;
        }
        if (cur != 0xFFFF && w + (uint32_t)kNameLen[ah] <= nw &&
            (w == 0 || !glyph_letter((uint16_t)(e[2 * w - 2] | e[2 * w - 1] << 8)))) {
            for (q = 0; q < kNameLen[ah]; q++)
                if ((uint16_t)(e[2 * (w + q)] | e[2 * (w + q) + 1] << 8) != on[q]) break;
            m = q == kNameLen[ah] &&
                (w + q == nw || !glyph_letter((uint16_t)(e[2 * (w + q)] | e[2 * (w + q) + 1] << 8)));
        }
        if (m) {
            for (q = 0; q < kNameLen[s_hero]; q++) {
                uint8_t t2[2] = {(uint8_t)nn[q], (uint8_t)(nn[q] >> 8)};
                buf_put(&text, t2, 2);
            }
            w += (uint32_t)kNameLen[ah];
            hits++;
        } else {
            buf_put(&text, e + 2 * w, 2);
            w++;
        }
    }
    if (!hits) return;
    if (text.n & 3) { uint8_t z[2] = {0xFF, 0xFF}; buf_put(&text, z, 2); }   /* entries stay 4-aligned */
    for (i = 0; i < c.n; i++) { ents[i] = CT_E(&c, i); lens[i] = CT_L(&c, i); }
    ents[e20] = text.p;
    lens[e20] = text.n;
    if (!ct_build(&out, ents, lens, c.n)) return;
    room = pack_room(pack, len);
    if (out.n > room) {
        fprintf(stderr, "%s: epilogue %d/%d left as it is: %u bytes, room %u\n", DV_HS_PLUGIN, area,
                (int16_t)psx_mod_read_half(DV_STAGE_STAGE), out.n, room);
        return;
    }
    psx_mod_host_write_ram(pack, out.p, out.n);
    fprintf(stderr, "%s: epilogue %d/%d: %d name(s) for %s (%u -> %u bytes)\n", DV_HS_PLUGIN, area,
            (int16_t)psx_mod_read_half(DV_STAGE_STAGE), hits, kHeroNames[s_hero], len, out.n);
}

static void dv_hs_pack_install_entry(struct CPUState* cpu, uint32_t address)
{
    uint32_t pack = cpu->gpr[4], n, len;
    const Kit* k;
    static Buf in, out;
    (void)address;
    if (s_enabled && !s_swap && s_hero >= 0 && pack >= 0x80000000u && !(pack & 3u) && pack < DV_RAM_END) {
        swap_map_stage(pack);   /* a world map stage, or nothing */
        swap_epilogue(pack);    /* a chapter epilogue, or nothing */
        return;
    }
    if (!s_enabled || !s_swap || (int16_t)psx_mod_read_half(DV_STAGE_AREA) != s_swap_area ||
        (int16_t)psx_mod_read_half(DV_STAGE_STAGE) != s_swap_stage || pack < 0x80000000u || (pack & 3u))
        return;
    k = &s_kits[s_swap_hero];
    n = psx_mod_read_word(pack);
    if (n < 1 || n > 4096) return;
    len = psx_mod_read_word(pack + 4 + 4 * n);
    if (len < 8 || len > 0x200000u || pack + len > DV_RAM_END) return;
    if (len > in.cap) {
        uint8_t* q = (uint8_t*)realloc(in.p, len);
        if (!q) return;
        in.p = q;
        in.cap = len;
    }
    ram_read(pack, in.p, len);
    in.n = len;
    if (pack != DV_ACTION_HEAP) {
        /* any other pack: the portrait, and a scene's copy of the stage's hero */
        static Buf mid;
        const uint8_t* cur = in.p;
        uint32_t curn = in.n, room;
        static Buf mid2;
        int portraits = swap_portraits(in.p, in.n, k), names = 0, actors = 0, pages = 0, variants = 0;
        if (swap_names(&mid, cur, curn, &names) && names) { cur = mid.p; curn = mid.n; }
        if (swap_page_reloads(&mid2, cur, curn, k, &pages, &variants) && pages) { cur = mid2.p; curn = mid2.n; }
        if (swap_actors(&out, cur, curn, k, &actors) && actors) { cur = out.p; curn = out.n; }
        if (!portraits && !names && !actors && !pages) return;
        room = pack_room(pack, len);
        if (curn > room) {
            fprintf(stderr, "%s: pack at 0x%08X left as it is but its portraits: %u bytes rebuilt, room %u\n",
                    DV_HS_PLUGIN, pack, curn, room);
            if (portraits) psx_mod_host_write_ram(pack, in.p, in.n);
            return;
        }
        psx_mod_host_write_ram(pack, cur, curn);
        fprintf(stderr, "%s: pack at 0x%08X: %d portrait(s), %d name(s), %d hero texture reload(s) "
                "(%d twin variant(s) neutralised), %d scene copy(ies) of the hero (%u -> %u bytes of %u)\n",
                DV_HS_PLUGIN, pack, portraits, names, pages, variants, actors, len, curn, room);
        return;
    }
    if (n == 3) {                                    /* file 0 */
        if (!merge_file0(&out, in.p, in.n, k)) { fprintf(stderr, "%s: file 0 not rebuilt\n", DV_HS_PLUGIN); return; }
        if (!psx_mod_host_write_ram(pack, out.p, out.n)) return;
        fprintf(stderr, "%s: stage %d/%d file 0 rebuilt (%u bytes)\n", DV_HS_PLUGIN, s_swap_area, s_swap_stage, out.n);
    } else if (n > 100) {                            /* the action pack */
        uint8_t cur;
        note_hero_verts(in.p, in.n);
        s_name_old_n = read_hero_name(dv_area_hero(s_swap_area), s_name_old);
        s_name_new_n = read_hero_name(s_swap_hero, s_name_new);
        if (!s_name_old_n || !s_name_new_n) s_name_old_n = s_name_new_n = 0;
        if (!merge_pack(&out, in.p, in.n, k)) { fprintf(stderr, "%s: pack not rebuilt\n", DV_HS_PLUGIN); return; }
        if (!psx_mod_host_write_ram(pack, out.p, out.n)) return;
        fprintf(stderr, "%s: stage %d/%d pack rebuilt (%u bytes)\n", DV_HS_PLUGIN, s_swap_area, s_swap_stage, out.n);
        (void)cur;
        hold_hero_index(s_swap_hero);
    }
}

static void dv_hs_activate(void)
{
    s_enabled = 1;
    s_swap = 0;
    read_options();
    ensure_disc();
    fprintf(stderr, "%s: play as %s\n", DV_HS_PLUGIN, s_hero >= 0 ? kHeroNames[s_hero] : "each stage's hero");
}

static int dv_hs_option_changed(const char* option_id, const char* value)
{
    (void)option_id;
    (void)value;
    read_options();
    fprintf(stderr, "%s: play as %s (from the next stage load)\n", DV_HS_PLUGIN,
            s_hero >= 0 ? kHeroNames[s_hero] : "each stage's hero");
    return 1;
}

/* --- restart the stage from the menu ----------------------------------------
 * The in-game menu's Play as row sets this after a live change of hero: the
 * running stage is reloaded from its start, the way a stage exit or the
 * CONTINUE prompt does it (g_GameFlow mode 17 / sub-mode 0 and
 * g_GameState.stagePhase = 1; the loader's phase 1 tears the stage down),
 * with no GAME OVER, no Val and no HP change. Done by the VBlank callback
 * only in plain play: mode 8 / sub-mode 7 (decomp docs/SUBSYSTEMS.md), the
 * game's pause off, no event script running, no load phase or load slot
 * busy; a request waits for that and is dropped when a stage loads anyway. */
#define DV_MODE          0x8008634Cu   /* g_GameFlow.mode, +1 subMode */
#define DV_PAUSE_STATE   0x8007D826u   /* g_PauseState */
#define DV_EVENT_RUNNING 0x8009F9F8u   /* g_EventVm.running */
#define DV_AREA_PHASE    0x800A2D14u   /* g_GameState.areaPhase */
#define DV_STAGE_PHASE   0x800A2D18u   /* g_GameState.stagePhase */
volatile int dv_heroswap_restart_request = 0;

static void dv_hs_vblank(void)
{
    uint32_t r, c;
    if (!dv_heroswap_restart_request) return;
    if (!s_enabled) { dv_heroswap_restart_request = 0; return; }
    if (psx_mod_read_byte(DV_MODE) != 8 || psx_mod_read_byte(DV_MODE + 1) != 7 || psx_mod_read_byte(DV_PAUSE_STATE) ||
        psx_mod_read_word(DV_EVENT_RUNNING) || psx_mod_read_word(DV_AREA_PHASE) || psx_mod_read_word(DV_STAGE_PHASE))
        return;
    for (r = 0; r < 10; r++)
        for (c = 0; c < 2; c++)
            if (psx_mod_read_byte(DV_LOAD_ROWS + r * 0x28 + 0x10 + c)) return;
    psx_mod_write_byte(DV_MODE, 17);
    psx_mod_write_byte(DV_MODE + 1, 0);
    psx_mod_write_word(DV_STAGE_PHASE, 1);
    dv_heroswap_restart_request = 0;
    fprintf(stderr, "%s: stage %d/%d restarts as %s\n", DV_HS_PLUGIN, (int16_t)psx_mod_read_half(DV_STAGE_AREA),
            (int16_t)psx_mod_read_half(DV_STAGE_STAGE), s_hero >= 0 ? kHeroNames[s_hero] : "its own hero");
}

PSX_MOD_CONSTRUCTOR(dv_register_heroswap)
{
    int ok = psx_mod_register_activation_plugin(DV_HS_PLUGIN, dv_hs_activate)
          && psx_mod_register_option_changed_plugin(DV_HS_PLUGIN, dv_hs_option_changed)
          && psx_mod_register_vblank_plugin(DV_HS_PLUGIN, dv_hs_vblank)
          && psx_mod_register_function_entry_plugin(DV_HS_PLUGIN, DV_STAGE_BUFFERS_ALLOC,
                                                    dv_hs_buffers_entry)
          && psx_mod_register_function_entry_plugin(DV_HS_PLUGIN, DV_PACK_INSTALL,
                                                    dv_hs_pack_install_entry);
    if (!ok) fprintf(stderr, "%s: plugin failed to register\n", DV_HS_PLUGIN);
}
