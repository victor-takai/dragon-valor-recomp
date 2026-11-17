/*
 * Dragon Valor (USA) "Hero swap" (experimental): trusted static plugin
 * behind mods/preloaded/packages/dragonvalor.heroswap.
 *
 * Plays a stage with the hero of another stage. The hero is data, not code:
 * the head of the action pack's install program (the hero object with its
 * behaviour script, 14 parts and 200 animation slots, its weld lists, sound
 * bank and shadow), the entries those name, the hero's VAB and the TIMs that
 * land in VRAM (384..447, 0..255) in the stage's first file. The decomp's
 * tools/heroswap.py (github victor-takai/dragon-valor-decomp,
 * docs/targets/hero-swap.md) rebuilds a target stage's files with a source
 * stage's hero from the player's own disc and writes them, with
 * heroswap.txt, into a folder; the player selects that folder as this
 * feature's resource. Nothing from the disc ships with the package.
 *
 * heroswap.txt:
 *   target AREA STAGE
 *   patch ADDRESS VALUE          u32 written when the target's buffers are laid out
 *   hero INDEX                   g_Hero.index while the target stage runs
 *   file N NAME SIZE HEADHEX     rebuilt stage file N; HEADHEX = the original
 *                                file's container head (count + offsets)
 *
 * The files are file 0 (the hero's VAB, texture and portrait), file 1 (the
 * action pack) and any room / event file that holds the hero's texture or
 * portrait: dialogues upload their speakers' portraits where the Status
 * screen's is, and frame 0 of each event image set, which the event's END
 * puts back, is the stage hero's portrait.
 *
 * g_Hero.index (0x80086318) names the hero in the menus and the save; the
 * game writes it at a chapter start only. The hook writes INDEX when it
 * replaces the target's action pack and gives the old value back when
 * another stage's buffers are laid out.
 *
 * Two function-entry hooks (game.toml [recompiler] mod_function_entry_funcs):
 *
 *   - StageBuffersAlloc (0x80035AB8) lays the stage buffers out from
 *     g_StageBufferSizes (0x80078B20, words) and ends with SoundSpuLayout
 *     (g_SoundBodySize, 0x80077DB8). A bigger hero needs row 0 (file 0, then
 *     the action pack, in whole sectors) and sound slot 1 (its VAB body)
 *     larger: the tool computes the values and checks them against the
 *     largest layouts the game itself uses; the hook writes them at entry,
 *     only while the target stage (g_StageSelect 0x800A2F18 area, +2 stage)
 *     is being loaded.
 *
 *   - PackInstall (0x80035E48, a0 = the pack) installs every stage file the
 *     game has just read. When the target stage is loaded and the bytes at
 *     a0 start with an original file's head, the hook writes the rebuilt
 *     file over it before the install runs. The CD read itself is left alone,
 *     so a rebuilt file longer than its place on the disc (the game reads up
 *     to the next file's start) still arrives whole.
 *
 * Inert unless the feature is enabled and its folder parsed: activation sets
 * everything, the hooks check s_ready first, and they touch nothing outside
 * the target stage's own loads.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "cpu_state.h"
#include "mod_plugins.h"

#define DV_HS_PLUGIN          "dragonvalor.heroswap"
#define DV_HS_RESOURCE        "build"

#define DV_STAGE_AREA         0x800A2F18u   /* g_StageSelect.area (s16) */
#define DV_STAGE_STAGE        0x800A2F1Au   /* g_StageSelect.stage (s16) */
#define DV_STAGE_BUFFERS_ALLOC 0x80035AB8u
#define DV_PACK_INSTALL       0x80035E48u
#define DV_HERO_INDEX         0x80086318u   /* g_Hero.index (u8) */
#define DV_RAM_END            0x80200000u

#define DV_HS_MAX_FILES       32
#define DV_HS_MAX_PATCHES     4

typedef struct {
    int number;
    uint8_t* data;
    uint32_t size;
    uint8_t* head;
    uint32_t head_size;
} DvHsFile;

static int s_ready;
static int s_area = -1, s_stage = -1;
static DvHsFile s_files[DV_HS_MAX_FILES];
static int s_file_count;
static uint32_t s_patch_addr[DV_HS_MAX_PATCHES], s_patch_value[DV_HS_MAX_PATCHES];
static int s_patch_count;
static int s_hero = -1;          /* g_Hero.index to write, -1 = none */
static int s_saved_hero = -1;    /* the value it replaced, to give back */

static void dv_hs_clear(void)
{
    int i;
    for (i = 0; i < s_file_count; i++) {
        free(s_files[i].data);
        free(s_files[i].head);
    }
    memset(s_files, 0, sizeof s_files);
    s_file_count = s_patch_count = 0;
    s_area = s_stage = -1;
    s_hero = s_saved_hero = -1;
    s_ready = 0;
}

static int dv_hs_target_loading(void)
{
    return (int16_t)psx_mod_read_half(DV_STAGE_AREA) == s_area &&
           (int16_t)psx_mod_read_half(DV_STAGE_STAGE) == s_stage;
}

static uint8_t* dv_hs_read_file(const char* dir, const char* name, uint32_t expect)
{
    char path[1024];
    FILE* f;
    uint8_t* data;
    long size;

    snprintf(path, sizeof path, "%s/%s", dir, name);
    f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "%s: cannot open %s\n", DV_HS_PLUGIN, path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size != (long)expect || (data = (uint8_t*)malloc(expect)) == NULL) {
        fprintf(stderr, "%s: %s is %ld bytes, heroswap.txt says %u\n", DV_HS_PLUGIN, path, size, expect);
        fclose(f);
        return NULL;
    }
    if (fread(data, 1, expect, f) != expect) {
        free(data);
        data = NULL;
    }
    fclose(f);
    return data;
}

static uint8_t* dv_hs_hex(const char* hex, uint32_t* out_size)
{
    size_t n = strlen(hex), i;
    uint8_t* b;
    if (n == 0 || n % 2 || (b = (uint8_t*)malloc(n / 2)) == NULL) return NULL;
    for (i = 0; i < n / 2; i++) {
        unsigned v;
        if (sscanf(hex + 2 * i, "%2x", &v) != 1) {
            free(b);
            return NULL;
        }
        b[i] = (uint8_t)v;
    }
    *out_size = (uint32_t)(n / 2);
    return b;
}

static int dv_hs_load(const char* dir)
{
    char path[1024];
    static char line[16384];
    FILE* f;
    int ok = 1;

    snprintf(path, sizeof path, "%s/heroswap.txt", dir);
    f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "%s: no heroswap.txt in %s\n", DV_HS_PLUGIN, dir);
        return 0;
    }
    while (ok && fgets(line, sizeof line, f)) {
        char name[256], hex[sizeof line];
        int a, s, n;
        unsigned addr, size;
        long long value;
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        if (sscanf(line, "target %d %d", &a, &s) == 2) {
            s_area = a;
            s_stage = s;
        } else if (sscanf(line, "hero %d", &n) == 1) {
            if (n < 0 || n > 8) ok = 0;
            else s_hero = n;
        } else if (sscanf(line, "patch %x %lld", &addr, &value) == 2) {
            if (s_patch_count == DV_HS_MAX_PATCHES || addr < 0x80000000u || addr >= DV_RAM_END) ok = 0;
            else {
                s_patch_addr[s_patch_count] = addr;
                s_patch_value[s_patch_count++] = (uint32_t)value;
            }
        } else if (sscanf(line, "file %d %255s %u %16383s", &n, name, &size, hex) == 4) {
            DvHsFile* fl = &s_files[s_file_count];
            if (s_file_count == DV_HS_MAX_FILES || size == 0 || size > 0x200000u) {
                ok = 0;
                break;
            }
            fl->number = n;
            fl->size = size;
            fl->head = dv_hs_hex(hex, &fl->head_size);
            fl->data = fl->head ? dv_hs_read_file(dir, name, size) : NULL;
            if (!fl->data) {
                free(fl->head);
                fl->head = NULL;
                ok = 0;
                break;
            }
            s_file_count++;
        } else {
            fprintf(stderr, "%s: bad line in heroswap.txt: %s", DV_HS_PLUGIN, line);
            ok = 0;
        }
    }
    fclose(f);
    return ok && s_area >= 0 && s_file_count > 0;
}

static void dv_hs_buffers_entry(struct CPUState* cpu, uint32_t address)
{
    int i;
    (void)cpu;
    (void)address;
    if (!s_ready) return;
    if (!dv_hs_target_loading()) {
        /* another stage: the hero index the swap replaced comes back */
        if (s_saved_hero >= 0) {
            fprintf(stderr, "%s: g_Hero.index back to %d\n", DV_HS_PLUGIN, s_saved_hero);
            psx_mod_write_byte(DV_HERO_INDEX, (uint8_t)s_saved_hero);
            s_saved_hero = -1;
        }
        return;
    }
    for (i = 0; i < s_patch_count; i++) {
        fprintf(stderr, "%s: stage %d/%d layout 0x%08X = %u (was %u)\n", DV_HS_PLUGIN, s_area, s_stage,
                s_patch_addr[i], s_patch_value[i], psx_mod_read_word(s_patch_addr[i]));
        psx_mod_write_word(s_patch_addr[i], s_patch_value[i]);
    }
    psx_mod_counter_add("dv.heroswap.layout_patched", 1);
}

static void dv_hs_pack_install_entry(struct CPUState* cpu, uint32_t address)
{
    uint32_t pack = cpu->gpr[4], k;
    int i;
    (void)address;
    if (!s_ready || !dv_hs_target_loading() || pack < 0x80000000u || (pack & 3u)) return;
    for (i = 0; i < s_file_count; i++) {
        const DvHsFile* fl = &s_files[i];
        if (pack + fl->size > DV_RAM_END || pack + fl->head_size > DV_RAM_END) continue;
        for (k = 0; k < fl->head_size; k++)
            if (psx_mod_read_byte(pack + k) != fl->head[k]) break;
        if (k != fl->head_size) continue;
        if (!psx_mod_host_write_ram(pack, fl->data, fl->size)) {
            fprintf(stderr, "%s: file %d does not fit at 0x%08X\n", DV_HS_PLUGIN, fl->number, pack);
            return;
        }
        psx_mod_counter_add("dv.heroswap.files_replaced", 1);
        fprintf(stderr, "%s: stage %d/%d file %d replaced at 0x%08X (%u bytes)\n",
                DV_HS_PLUGIN, s_area, s_stage, fl->number, pack, fl->size);
        if (fl->number == 1 && s_hero >= 0) {
            uint8_t cur = psx_mod_read_byte(DV_HERO_INDEX);
            if (s_saved_hero < 0 && cur != (uint8_t)s_hero) s_saved_hero = cur;
            psx_mod_write_byte(DV_HERO_INDEX, (uint8_t)s_hero);
            fprintf(stderr, "%s: g_Hero.index %u -> %d\n", DV_HS_PLUGIN, cur, s_hero);
        }
        return;
    }
}

static void dv_hs_activate(void)
{
    char dir[1024];
    dv_hs_clear();
    if (!psx_mod_current_resource_path(DV_HS_RESOURCE, dir, sizeof dir)) {
        fprintf(stderr, "%s: no folder selected\n", DV_HS_PLUGIN);
        return;
    }
    if (!dv_hs_load(dir)) {
        fprintf(stderr, "%s: %s not usable; hero swap off\n", DV_HS_PLUGIN, dir);
        dv_hs_clear();
        return;
    }
    s_ready = 1;
    fprintf(stderr, "%s: stage %d/%d, %d files, %d layout writes, hero index %d from %s\n",
            DV_HS_PLUGIN, s_area, s_stage, s_file_count, s_patch_count, s_hero, dir);
}

PSX_MOD_CONSTRUCTOR(dv_register_heroswap)
{
    int ok = psx_mod_register_activation_plugin(DV_HS_PLUGIN, dv_hs_activate)
          && psx_mod_register_function_entry_plugin(DV_HS_PLUGIN, DV_STAGE_BUFFERS_ALLOC,
                                                    dv_hs_buffers_entry)
          && psx_mod_register_function_entry_plugin(DV_HS_PLUGIN, DV_PACK_INSTALL,
                                                    dv_hs_pack_install_entry);
    if (!ok) fprintf(stderr, "%s: plugin failed to register\n", DV_HS_PLUGIN);
}
