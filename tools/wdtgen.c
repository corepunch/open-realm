/*
 * wdtgen - deterministic WDT binary fixture generator
 *
 * Generates a minimal WoW 1.12 WDT file for use as test fixtures.
 * Output is byte-compatible with Wow_LoadWdtTiles / Wow_LoadWdtFlags parsers.
 *
 * Usage:
 *   wdtgen <out.wdt>
 *       [--global-wmo <path.wmo>]   generate global-WMO WDT (dungeons/instances)
 *       [--flags <HEX>]             MPHD flags (merged with 0x01 when --global-wmo used)
 *
 * Without --global-wmo, generates a standard outdoor WDT with all 64×64 tile
 * presence bits cleared (no ADTs present). The MAIN chunk is still required by
 * the parser.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint8_t *data; size_t size, cap; } wbuf_t;

static void wb_grow(wbuf_t *b, size_t need) {
    if (b->size + need <= b->cap) return;
    size_t nc = b->cap ? b->cap * 2 : 4096;
    while (nc < b->size + need) nc *= 2;
    b->data = realloc(b->data, nc);
    if (!b->data) { fprintf(stderr, "wdtgen: out of memory\n"); exit(1); }
    b->cap = nc;
}
static void wb_write(wbuf_t *b, const void *src, size_t n) {
    wb_grow(b, n); memcpy(b->data + b->size, src, n); b->size += n;
}
static void wb_u8(wbuf_t *b, uint8_t v)  { wb_write(b, &v, 1); }
static void wb_u16(wbuf_t *b, uint16_t v) { wb_write(b, &v, 2); }
static void wb_u32(wbuf_t *b, uint32_t v) { wb_write(b, &v, 4); }
static void wb_f32(wbuf_t *b, float    v) { wb_write(b, &v, 4); }
static void wb_zero(wbuf_t *b, size_t n)  { wb_grow(b, n); memset(b->data + b->size, 0, n); b->size += n; }
static void wb_free(wbuf_t *b) { free(b->data); b->data = NULL; b->size = b->cap = 0; }

static void wb_chunk(wbuf_t *out, const char *tag, wbuf_t *payload) {
    wb_write(out, tag, 4);
    wb_u32(out, (uint32_t)payload->size);
    wb_write(out, payload->data, payload->size);
}

/* MVER payload: version 18 for WDT */
static void write_mver(wbuf_t *p) { wb_u32(p, 18); }

/* MPHD payload: one DWORD of flags */
static void write_mphd(wbuf_t *p, uint32_t flags) { wb_u32(p, flags); wb_zero(p, 28); /* 8 DWORDs total */ }

/* MAIN payload: 64×64 × 8-byte entries, all zero (no tiles present) */
static void write_main_empty(wbuf_t *p) { wb_zero(p, 64 * 64 * 8); }

/* OMWM payload: one null-terminated WMO path */
static void write_mwmo(wbuf_t *p, const char *wmo_path) {
    wb_write(p, wmo_path, strlen(wmo_path) + 1);
}

/* DIWM payload: one DWORD offset → 0 (first entry in MWMO blob) */
static void write_mwid(wbuf_t *p) { wb_u32(p, 0); }

/* FDOM payload: one 64-byte wowMapObjDef_t record at world origin.
   Layout:
     DWORD name_id = 0
     DWORD unique_id = 0
     float position[3] = {0, 0, 0}
     float rotation[3] = {0, 0, 0}
     float extents_min[3] = {-64,-64,-64}
     float extents_max[3] = { 64, 64, 64}
     WORD flags = 0
     WORD doodad_set = 0
     WORD name_set = 0
     WORD unk = 0
   Total = 4+4+12+12+24+2+2+2+2 = 64 bytes */
static void write_modf_origin(wbuf_t *p) {
    wb_u32(p, 0);  /* name_id */
    wb_u32(p, 1);  /* unique_id = 1 */
    wb_f32(p, 0.0f); wb_f32(p, 0.0f); wb_f32(p, 0.0f); /* position */
    wb_f32(p, 0.0f); wb_f32(p, 0.0f); wb_f32(p, 0.0f); /* rotation */
    /* extents: min+max = 6 floats = 24 bytes */
    wb_f32(p, -64.0f); wb_f32(p, -64.0f); wb_f32(p, -64.0f);
    wb_f32(p,  64.0f); wb_f32(p,  64.0f); wb_f32(p,  64.0f);
    wb_u16(p, 0);  /* flags */
    wb_u16(p, 0);  /* doodad_set */
    wb_u16(p, 0);  /* name_set */
    wb_u16(p, 0);  /* unk */
}

static void write_file(const char *path, wbuf_t *b) {
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "wdtgen: cannot open '%s'\n", path); exit(1); }
    fwrite(b->data, 1, b->size, f);
    fclose(f);
    fprintf(stderr, "wdtgen: wrote %s (%zu bytes)\n", path, b->size);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr,
            "usage: wdtgen <out.wdt>\n"
            "   [--global-wmo <path.wmo>]  global-WMO map (dungeon/instance)\n"
            "   [--flags <HEX>]            MPHD flags (default 0)\n");
        return 1;
    }

    const char *out_path = argv[1];
    const char *global_wmo = NULL;
    uint32_t mphd_flags = 0;

    for (int i = 2; i < argc; ) {
        if (!strcmp(argv[i], "--global-wmo") && i + 1 < argc) {
            global_wmo = argv[i+1];
            i += 2;
        } else if (!strcmp(argv[i], "--flags") && i + 1 < argc) {
            mphd_flags = (uint32_t)strtoul(argv[i+1], NULL, 16);
            i += 2;
        } else {
            fprintf(stderr, "wdtgen: unknown option '%s'\n", argv[i]);
            return 1;
        }
    }

    if (global_wmo) mphd_flags |= 0x01;

    wbuf_t out = {0};
    wbuf_t p = {0};

    write_mver(&p); wb_chunk(&out, "REVM", &p); p.size = 0;
    write_mphd(&p, mphd_flags); wb_chunk(&out, "DHPM", &p); p.size = 0;
    write_main_empty(&p); wb_chunk(&out, "NIAM", &p); p.size = 0;

    if (global_wmo) {
        write_mwmo(&p, global_wmo); wb_chunk(&out, "OMWM", &p); p.size = 0;
        write_mwid(&p);             wb_chunk(&out, "DIWM", &p); p.size = 0;
        write_modf_origin(&p);      wb_chunk(&out, "FDOM", &p); p.size = 0;
    }

    wb_free(&p);
    write_file(out_path, &out);
    wb_free(&out);
    return 0;
}
