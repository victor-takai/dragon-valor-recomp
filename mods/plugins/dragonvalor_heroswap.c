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
 * Kits. At activation the plugin reads DATA.CDR from the mounted disc
 * (psx_mod_read_disc_file, the whole file, about 440 / 500 MB, freed again)
 * and keeps, for each hero on that disc, a kit taken from the first stage of
 * the hero's first area (Clovis 1/1, Arlen 2/1, ..., Anita 9/1): the hero's
 * ops, the entries they name (animations no slot uses left out), the VAB,
 * the hero-page TIMs and the portrait. With them it keeps a table of every
 * action stage of the disc: the sizes the rebuild needs to lay the stage
 * out. Kits and tables are cached in mods/cache/dragonvalor.heroswap/ beside
 * the executable, so the next start reads nothing, and a hero of the other
 * disc (Anna, Mihael, Anita on disc 2; Clovis to Phillip on disc 1) is
 * available once that disc has been started with this feature on.
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
 *     hero's texture becomes the kit's (swap_page_reloads), and a story
 *     scene's own copy of the stage's hero takes the kit's parts
 *     (swap_actors), when the rebuilt pack fits before the next stage buffer.
 *
 * Inert unless the feature is on and a hero chosen. Not handled: the scene
 * motions (op 23 into the hero's slots, a copy's own slots) stay the stage
 * hero's; the stage text bank (op 20) may name the hero; enemy groups of
 * 5/19, 5/20, 5/24, 12/29 put other images into the hero's page.
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

/* A story scene's copy of the stage's hero in the stage's room files (65..):
 * a 10+ part op 3 whose vertex entries are the hero's (see swap_actors).
 * Notes the bytes of its parts and of its weld lists, which the swap
 * replaces: the warp row grows by the difference. */
static void scan_scene_copy(const Cdr* d, uint32_t idx, const uint32_t* starts, const uint32_t* counts, int nf,
                            const Split* s, StageRec* r)
{
    static Ct ce;
    static Prog pp;
    const uint8_t* hv[14];
    uint32_t hl[14], i, len;
    int n, m, j, nh = 0;
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
            uint32_t nodes, hits = 0, parts = 0, weld = 0, k2, rr;
            if (o->op != 3) continue;
            nodes = le32(e0 + o->pos + 12);
            if (nodes < 10 || nodes > 32) continue;
            for (i = 0; i < nodes && i < 14; i++) {
                uint32_t v = le32(e0 + o->pos + 16 + i * DV_NODE + 0x6C);
                if (v < ce.n && hv[i] && CT_L(&ce, v) == hl[i] && !memcmp(CT_E(&ce, v), hv[i], hl[i])) hits++;
            }
            if (hits < 10) continue;
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

/* Table and kits of the mounted disc (disc 0 = areas 0..6, 1 = 7..12). */
static int disc_scan(int disc)
{
    static Split s;
    static Ct c0, cp;
    static Tim tims[64];
    uint32_t size = 0, got = 0, idx, starts[256], counts[256];
    uint8_t* data;
    Cdr d;
    int area, stage, nf, h;

    if (!psx_mod_read_disc_file("DATA.CDR", NULL, 0, &size) || !size) return 0;
    if (!(data = (uint8_t*)malloc(size))) return 0;
    if (!psx_mod_read_disc_file("DATA.CDR", data, size, &got) || got != size) { free(data); return 0; }
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
            scan_scene_copy(&d, idx, starts, counts, nf, &s, r);
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
#define DV_TABLE_VER   2u   /* 2: scene copies */

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

/* The mounted disc: 0 (SLUS_010.92) or 1, and a key for its DATA.CDR. */
static int mounted_disc(uint32_t* key)
{
    uint32_t lba = 0, size = 0;
    if (!psx_mod_disc_file_extent("DATA.CDR", &lba, &size)) return -1;
    *key = lba ^ size;
    return psx_mod_disc_file_extent("SLUS_010.92", &lba, &size) ? 0 : 1;
}

/* Table and kits for the mounted disc: from the cache, else from DATA.CDR. */
static void ensure_disc(void)
{
    uint32_t key = 0;
    int disc = mounted_disc(&key), h, need = 0;
    if (disc < 0 || s_disc_ready[disc]) return;
    cache_dir();
    for (h = 0; h < HERO_COUNT; h++) if (!s_kits[h].ready) kit_load(h);
    for (h = 0; h < HERO_COUNT; h++) need |= !s_kits[h].ready && (kKitSource[h][0] >= 7) == disc;
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
    fprintf(stderr, "%s: disc %d read: %d action stages, kits:", DV_HS_PLUGIN, disc + 1, s_nstages[disc]);
    for (h = 0; h < HERO_COUNT; h++) if (s_kits[h].ready) fprintf(stderr, " %s", kHeroNames[h]);
    fprintf(stderr, "\n");
}

/* --- the swap -------------------------------------------------------------- */

static int s_enabled;
static int s_hero = -1;                    /* chosen hero, -1 = each stage's own */
static int s_swap, s_swap_area, s_swap_stage, s_swap_hero;
static int s_saved_hero = -1;              /* g_Hero.index the swap replaced */
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
    }
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
    if (!s_enabled) return;
    ensure_disc();
    if (s_hero < 0 || s_hero == dv_area_hero(area) || !(r = stage_find(area, stage))) {
        restore_hero_index();
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
    uint32_t i, plen = s_orig_portrait.n;
    int done = 0, j, nt;
    if (!plen || plen != k->portrait.n || !ct_parse(&c, b, len)) return 0;
    for (i = 0; i < c.n; i++) {
        uint8_t* e = b + c.offs[i];
        nt = tim_chain(e, CT_L(&c, i), t, 64, NULL);
        for (j = 0; j < nt; j++)
            if (t[j].end - t[j].start == plen && tim_is_portrait(&t[j]) && !memcmp(e + t[j].start, s_orig_portrait.p, plen)) {
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
 * over in the last (tools/heroswap.py replace_page_tims). Other images in
 * the page (enemy groups of 5/19, 5/20, 5/24, 12/29) are left. */
static int tim_is_orig_page(const uint8_t* t, uint32_t n)
{
    static Tim ot[64];
    int no = tim_chain(s_orig_page.p, s_orig_page.n, ot, 64, NULL), j;
    for (j = 0; j < no; j++)
        if (ot[j].end - ot[j].start == n && !memcmp(s_orig_page.p + ot[j].start, t, n)) return 1;
    return 0;
}

static int swap_page_reloads(Buf* out, const uint8_t* b, uint32_t len, const Kit* k, int* swapped)
{
    static Ct c;
    static Tim t[64], kt[64];
    static const uint8_t* ents[4097];
    static uint32_t lens[4097];
    Buf pool = {0};
    uint32_t offs[64], idx[64], i;
    int nchanged = 0, nk, ok;
    *swapped = 0;
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
        if (!slots || !all) continue;
        for (j = 0; j < nt; j++) {
            if (!tim_in_page(&t[j])) continue;
            buf_put(&pool, e + pos, t[j].start - pos);
            for (m = slot; m < nk && (m == slot || slot == slots - 1); m++)
                buf_put(&pool, k->page.p + kt[m].start, kt[m].end - kt[m].start);
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
    const uint8_t* src;
    uint32_t i, kn = le32(k->ops.p + 12), part_entry[4097], first_drop = 0, insert_at = 0, n;
    int j, nact = 0, ok = 1;

    *swapped = 0;
    if (!ct_parse(&c, b, len) || c.n + 16 > 4200 || !prog_walk(&p, CT_E(&c, 0), CT_L(&c, 0))) return 0;
    src = CT_E(&c, 0);
    for (i = 0; i < c.n; i++) { ents[i] = CT_E(&c, i); lens[i] = CT_L(&c, i); }
    memset(part_entry, 0, sizeof part_entry);        /* prims entry -> node index + 1 */
    memset(drop, 0, sizeof drop);
    for (j = 0; j < p.nops; j++) {
        const Op* o = &p.ops[j];
        uint32_t nodes, m = 0;
        if (o->op != 3) continue;
        nodes = le32(src + o->pos + 12);
        if (nodes < 10 || nodes > 32) continue;
        for (i = 0; i < nodes && i < 14; i++) {
            uint32_t v = le32(src + o->pos + 16 + i * DV_NODE + 0x6C);
            if (v < c.n && s_hero_verts_len[i] == CT_L(&c, v) && fnv64(CT_E(&c, v), CT_L(&c, v)) == s_hero_verts[i]) m++;
        }
        if (m < 10) continue;
        /* a copy of the stage's hero: its parts become the kit's */
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

static void dv_hs_pack_install_entry(struct CPUState* cpu, uint32_t address)
{
    uint32_t pack = cpu->gpr[4], n, len;
    const Kit* k;
    static Buf in, out;
    (void)address;
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
        int portraits = swap_portraits(in.p, in.n, k), names = 0, actors = 0, pages = 0;
        if (swap_names(&mid, cur, curn, &names) && names) { cur = mid.p; curn = mid.n; }
        if (swap_page_reloads(&mid2, cur, curn, k, &pages) && pages) { cur = mid2.p; curn = mid2.n; }
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
        fprintf(stderr, "%s: pack at 0x%08X: %d portrait(s), %d name(s), %d hero texture reload(s), "
                "%d scene copy(ies) of the hero (%u -> %u bytes of %u)\n",
                DV_HS_PLUGIN, pack, portraits, names, pages, actors, len, curn, room);
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
        cur = psx_mod_read_byte(DV_HERO_INDEX);
        if (cur != (uint8_t)s_swap_hero) {
            s_saved_hero = cur;
            psx_mod_write_byte(DV_HERO_INDEX, (uint8_t)s_swap_hero);
            fprintf(stderr, "%s: g_Hero.index %u -> %d\n", DV_HS_PLUGIN, cur, s_swap_hero);
        }
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

PSX_MOD_CONSTRUCTOR(dv_register_heroswap)
{
    int ok = psx_mod_register_activation_plugin(DV_HS_PLUGIN, dv_hs_activate)
          && psx_mod_register_option_changed_plugin(DV_HS_PLUGIN, dv_hs_option_changed)
          && psx_mod_register_function_entry_plugin(DV_HS_PLUGIN, DV_STAGE_BUFFERS_ALLOC,
                                                    dv_hs_buffers_entry)
          && psx_mod_register_function_entry_plugin(DV_HS_PLUGIN, DV_PACK_INSTALL,
                                                    dv_hs_pack_install_entry);
    if (!ok) fprintf(stderr, "%s: plugin failed to register\n", DV_HS_PLUGIN);
}
