/*
 * wmogen - deterministic WMO binary fixture generator
 *
 * Generates a minimal WoW classic (1.12) WMO root file and one group file
 * byte-compatible with Wow_LoadWmoModel / Wow_LoadWmoGroup parsers.
 * Output root:  <out.wmo>
 * Output group: <out_000.wmo>  (auto-derived from root path)
 *
 * Usage:
 *   wmogen <out.wmo>
 *       [--amb <R> <G> <B>]          MOHD ambient color 0-255, default 64 64 64
 *       [--flags <HEX>]              MOHD flags, default 0x00
 *       [--indoor]                   mark group MOGP flags as interior (0x2000)
 *       [--mocv <R> <G> <B> <A>]     MOCV vertex color in BGRA file order, default 128 128 128 255
 *       [--trans-batches <N>]        number of transparent (batch-A) batches, default 0
 *       [--doodad <path> <x> <y> <z> <qx> <qy> <qz> <qw> <scale>]  add doodad (repeatable)
 *
 * Example:
 *   wmogen test.wmo --amb 80 40 20 --indoor
 *       --doodad World/Generic/Human/Passive Doodads/Barrel/Barrel.mdx 1.0 2.0 0.5  0 0 0 1  1.5
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "games/world-of-warcraft/common/wow_chunks.h"

/* -------------------------------------------------------------------------
   Write-buffer helpers (same pattern as m2gen)
   ---------------------------------------------------------------------- */
typedef struct { uint8_t *data; size_t size, cap; } wbuf_t;

static void wb_grow(wbuf_t *b, size_t need) {
    if (b->size + need <= b->cap) return;
    size_t nc = b->cap ? b->cap * 2 : 4096;
    while (nc < b->size + need) nc *= 2;
    b->data = realloc(b->data, nc);
    if (!b->data) { fprintf(stderr, "wmogen: out of memory\n"); exit(1); }
    b->cap = nc;
}
static void wb_write(wbuf_t *b, const void *src, size_t n) {
    wb_grow(b, n); memcpy(b->data + b->size, src, n); b->size += n;
}
static void wb_u8(wbuf_t *b, uint8_t  v) { wb_write(b, &v, 1); }
static void wb_u16(wbuf_t *b, uint16_t v) { wb_write(b, &v, 2); }
static void wb_u32(wbuf_t *b, uint32_t v) { wb_write(b, &v, 4); }
static void wb_f32(wbuf_t *b, float    v) { wb_write(b, &v, 4); }
static void wb_zero(wbuf_t *b, size_t n)  { wb_grow(b, n); memset(b->data + b->size, 0, n); b->size += n; }
static void wb_free(wbuf_t *b) { free(b->data); b->data = NULL; b->size = b->cap = 0; }

/* Write a chunk: reversed-tag FOURCC, 4-byte size, then payload. */
static void wb_chunk(wbuf_t *out, uint32_t fourcc, wbuf_t *payload) {
    wb_write(out, &fourcc, 4);
    wb_u32(out, (uint32_t)payload->size);
    wb_write(out, payload->data, payload->size);
}

/* -------------------------------------------------------------------------
   Doodad list
   ---------------------------------------------------------------------- */
#define MAX_DOODADS 64
typedef struct {
    char path[256];
    float x, y, z;
    float qx, qy, qz, qw;
    float scale;
} Doodad;

static Doodad doodads[MAX_DOODADS];
static int    num_doodads;

#define MAX_LIGHTS 16
typedef struct {
    uint8_t type;       /* 0=OMNI 1=SPOT 2=DIRECT 3=AMBIENT */
    uint8_t use_atten;
    uint8_t color_b, color_g, color_r, color_a;
    float x, y, z;     /* WMO local position */
    float intensity;
    float atten_start;
    float atten_end;
} Light;

static Light lights[MAX_LIGHTS];
static int   num_lights;

/* -------------------------------------------------------------------------
   Helpers: write fixed-size WMO structs into a wbuf_t
   ---------------------------------------------------------------------- */

/* MVER chunk payload */
static void write_mver(wbuf_t *p) { wb_u32(p, 17); }

/* MOHD chunk payload (52 bytes minimum for our parser).
   Layout from PLAN §1.1:
     +0x00 DWORD nTextures
     +0x04 DWORD nGroups
     +0x08 DWORD nPortals
     +0x0C DWORD nLights
     +0x10 DWORD nDoodadNames
     +0x14 DWORD nDoodadDefs
     +0x18 DWORD nDoodadSets
     +0x1C COLOR32 ambColor  (BGRA in file: byte[0x1C]=B, [0x1D]=G, [0x1E]=R, [0x1F]=A)
     +0x20 DWORD wmoID
     ...
     +0x30 WORD flags
     total we need at least 0x32 = 50 bytes
*/
static void write_mohd(wbuf_t *p,
                        uint32_t n_groups, uint32_t n_lights,
                        uint32_t n_doodad_sets, uint32_t n_doodad_defs,
                        uint8_t amb_r, uint8_t amb_g, uint8_t amb_b,
                        uint16_t flags) {
    wb_u32(p, 0);           /* nTextures */
    wb_u32(p, n_groups);    /* nGroups */
    wb_u32(p, 0);           /* nPortals */
    wb_u32(p, n_lights);    /* nLights */
    wb_u32(p, 0);           /* nDoodadNames */
    wb_u32(p, n_doodad_defs); /* nDoodadDefs */
    wb_u32(p, n_doodad_sets); /* nDoodadSets */
    /* ambColor at +0x1C: BGRA in file = bytes {B, G, R, A} */
    wb_u8(p, amb_b);        /* +0x1C = B */
    wb_u8(p, amb_g);        /* +0x1D = G */
    wb_u8(p, amb_r);        /* +0x1E = R */
    wb_u8(p, 0xFF);         /* +0x1F = A */
    wb_u32(p, 0);           /* wmoID +0x20 */
    /* +0x24 to +0x2F: bounding box (3+3 floats) */
    wb_f32(p, -10.0f); wb_f32(p, -10.0f); wb_f32(p, -10.0f);
    wb_f32(p,  10.0f); wb_f32(p,  10.0f); wb_f32(p,  10.0f);
    /* +0x30: flags (WORD) */
    wb_u16(p, flags);
    wb_zero(p, 2);          /* pad to DWORD boundary */
}

/* MOTX chunk payload: single null byte (empty texture block) */
static void write_motx_empty(wbuf_t *p) { wb_u8(p, 0); }

/* MOMT chunk payload: one 64-byte material record (all zeros = opaque white) */
static void write_momt_one(wbuf_t *p) { wb_zero(p, 64); }

/* VPOM (MOPV) chunk payload: 4 portal vertices forming a quad entrance */
static void write_mopv(wbuf_t *p) {
    /* Quad portal at x=2: 4 vertices in YZ plane */
    wb_f32(p,  2.0f); wb_f32(p, -1.5f); wb_f32(p, -1.5f);
    wb_f32(p,  2.0f); wb_f32(p,  1.5f); wb_f32(p, -1.5f);
    wb_f32(p,  2.0f); wb_f32(p,  1.5f); wb_f32(p,  1.5f);
    wb_f32(p,  2.0f); wb_f32(p, -1.5f); wb_f32(p,  1.5f);
}

/* TPOM (MOPT) chunk payload: one 20-byte portal record
   Layout: uint16 startVertex, uint16 count, float[4] plane = 2+2+16 = 20 bytes */
static void write_mopt(wbuf_t *p) {
    wb_u16(p, 0);    /* startVertex = 0 */
    wb_u16(p, 4);    /* count = 4 vertices */
    wb_f32(p, 1.0f); wb_f32(p, 0.0f); wb_f32(p, 0.0f); wb_f32(p, -2.0f); /* plane (1,0,0,-2) */
}

/* RPOM (MOPR) chunk payload: one 8-byte portal ref connecting group 0 to outside */
static void write_mopr(wbuf_t *p) {
    wb_u16(p, 0);   /* portal_index = 0 */
    wb_u16(p, 0);   /* group_index = 0 (this group) */
    /* side = 1 (group is on the positive side of the portal plane) */
    int16_t side = 1;
    wb_write(p, &side, 2);
    wb_u16(p, 0);   /* pad */
}

/* MOGN chunk payload: null-terminated group name */
static void write_mogn(wbuf_t *p, const char *name) {
    wb_write(p, name, strlen(name) + 1);
}

/* TLOM chunk payload: array of 48-byte wowWmoLight_t records */
static void write_molt(wbuf_t *p) {
    for (int i = 0; i < num_lights; i++) {
        Light *l = &lights[i];
        wb_u8(p, l->type);
        wb_u8(p, l->use_atten);
        wb_u8(p, 0); wb_u8(p, 0); /* pad[2] */
        /* color BGRA */
        wb_u8(p, l->color_b); wb_u8(p, l->color_g);
        wb_u8(p, l->color_r); wb_u8(p, l->color_a);
        /* position */
        wb_f32(p, l->x); wb_f32(p, l->y); wb_f32(p, l->z);
        wb_f32(p, l->intensity);
        wb_f32(p, l->atten_start);
        wb_f32(p, l->atten_end);
        /* unk[4] */
        wb_zero(p, 16);
    }
}

/* MODS chunk payload: one doodad set record (32 bytes) */
static void write_mods_one(wbuf_t *p, const char *set_name,
                            uint32_t start, uint32_t count) {
    char buf[20] = {0};
    size_t len = strlen(set_name);
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    memcpy(buf, set_name, len);
    wb_write(p, buf, 20);   /* name[20] */
    wb_u32(p, start);
    wb_u32(p, count);
    wb_u32(p, 0);           /* pad */
}

/* MODN chunk payload: null-terminated filename blob.
   Returns the byte offset of each doodad path in the blob. */
static void write_modn(wbuf_t *p, uint32_t offsets[MAX_DOODADS]) {
    for (int i = 0; i < num_doodads; i++) {
        offsets[i] = (uint32_t)p->size;
        wb_write(p, doodads[i].path, strlen(doodads[i].path) + 1);
    }
    if (p->size == 0) wb_u8(p, 0); /* at least one null byte */
}

/* MODD chunk payload: one 40-byte SMODoodadDef per doodad.
   Layout: 4 bytes name_flags (bits 0-23 = offset, 24-31 = flags)
           + wowVec3_t position (12)
           + float quat[4] (16)
           + float scale (4)
           + COLOR32 color (4)
   Total = 40 bytes */
static void write_modd(wbuf_t *p, uint32_t const offsets[MAX_DOODADS]) {
    for (int i = 0; i < num_doodads; i++) {
        uint32_t name_flags = offsets[i] & 0x00FFFFFF; /* flags = 0 */
        wb_u32(p, name_flags);
        wb_f32(p, doodads[i].x);
        wb_f32(p, doodads[i].y);
        wb_f32(p, doodads[i].z);
        wb_f32(p, doodads[i].qx);
        wb_f32(p, doodads[i].qy);
        wb_f32(p, doodads[i].qz);
        wb_f32(p, doodads[i].qw);
        wb_f32(p, doodads[i].scale);
        wb_u32(p, 0x00000000); /* color = BGRA black, alpha=0 */
    }
}

/* MOGP fixed header (0x44 = 68 bytes) for the group file.
   Our parser reads:
     +0x08 DWORD mogpFlags  (bit 0x2000 = indoor)
     +0x30 WORD  transBatchCount
*/
static void write_mogp_fixed_header(wbuf_t *p, uint32_t mogp_flags,
                                     uint16_t portal_start, uint16_t portal_count,
                                     uint16_t trans_batch_count,
                                     uint16_t int_batch_count,
                                     uint16_t ext_batch_count) {
    wb_u32(p, 0);           /* +0x00 nameOffset */
    wb_u32(p, 0);           /* +0x04 descOffset */
    wb_u32(p, mogp_flags);  /* +0x08 mogpFlags */
    /* +0x0C bounding box (6 floats) */
    wb_f32(p, -1.0f); wb_f32(p, -1.0f); wb_f32(p, 0.0f);
    wb_f32(p,  1.0f); wb_f32(p,  1.0f); wb_f32(p, 0.0f);
    /* +0x24 portal start/count */
    wb_u16(p, portal_start); wb_u16(p, portal_count);
    /* +0x28 to +0x2F: zeros */
    wb_zero(p, 8);
    /* +0x30 batch counts as our parser expects them */
    wb_u16(p, trans_batch_count);  /* +0x30 transBatchCount */
    wb_u16(p, int_batch_count);    /* +0x32 intBatchCount */
    wb_u16(p, ext_batch_count);    /* +0x34 extBatchCount */
    wb_u16(p, 0);                  /* +0x36 pad */
    /* +0x38 replacement_for_header_color */
    wb_u32(p, 0);
    /* +0x3C to +0x43: fog indices, liquid type, group id */
    wb_zero(p, 8);
    /* Total: 0x44 = 68 bytes */
}

/* Simple quad geometry: 4 vertices (unit square in XY), 2 triangles */
static void write_mopy(wbuf_t *p, uint16_t material_id) {
    /* 2 polygons (triangles): each = 1 byte flags + 1 byte material_id */
    wb_u8(p, 0x00); wb_u8(p, (uint8_t)material_id); /* poly 0 */
    wb_u8(p, 0x00); wb_u8(p, (uint8_t)material_id); /* poly 1 */
}

static void write_movi(wbuf_t *p) {
    /* 2 triangles from 4 verts: CCW winding */
    wb_u16(p, 0); wb_u16(p, 1); wb_u16(p, 2);
    wb_u16(p, 0); wb_u16(p, 2); wb_u16(p, 3);
}

static void write_movt(wbuf_t *p) {
    wb_f32(p, -1.0f); wb_f32(p, -1.0f); wb_f32(p, 0.0f);
    wb_f32(p,  1.0f); wb_f32(p, -1.0f); wb_f32(p, 0.0f);
    wb_f32(p,  1.0f); wb_f32(p,  1.0f); wb_f32(p, 0.0f);
    wb_f32(p, -1.0f); wb_f32(p,  1.0f); wb_f32(p, 0.0f);
}

static void write_monr(wbuf_t *p) {
    for (int i = 0; i < 4; i++) {
        wb_f32(p, 0.0f); wb_f32(p, 0.0f); wb_f32(p, 1.0f);
    }
}

static void write_motv(wbuf_t *p) {
    wb_f32(p, 0.0f); wb_f32(p, 0.0f);
    wb_f32(p, 1.0f); wb_f32(p, 0.0f);
    wb_f32(p, 1.0f); wb_f32(p, 1.0f);
    wb_f32(p, 0.0f); wb_f32(p, 1.0f);
}

/* MOBA: one batch covering all 6 indices from vertices 0-3.
   SMOBatch layout (16 bytes in classic):
     SHORT box_min[3], box_max[3], first_index, num_indices, first_vertex,
     last_vertex, flags, material_id */
static void write_moba(wbuf_t *p, uint16_t trans_batch_count, uint16_t material_id) {
    int n_batches = trans_batch_count + 1; /* trans_batch_count batch-A + 1 batch-B/C */
    for (int i = 0; i < n_batches; i++) {
        /* box_min[3] */
        int16_t neg1 = -1;
        wb_write(p, &neg1, 2); wb_write(p, &neg1, 2); wb_write(p, &neg1, 2);
        /* box_max[3] */
        int16_t pos1 = 1;
        wb_write(p, &pos1, 2); wb_write(p, &pos1, 2); wb_write(p, &pos1, 2);
        wb_u32(p, 0);               /* first_index (DWORD in classic) */
        wb_u16(p, 6);               /* num_indices */
        wb_u16(p, 0);               /* first_vertex */
        wb_u16(p, 3);               /* last_vertex */
        wb_u8(p, 0);                /* flags */
        wb_u8(p, (uint8_t)material_id); /* material_id */
    }
}

/* MOCV: 4 BGRA vertex colors */
static void write_mocv(wbuf_t *p,
                        uint8_t mocv_b, uint8_t mocv_g,
                        uint8_t mocv_r, uint8_t mocv_a) {
    for (int i = 0; i < 4; i++) {
        wb_u8(p, mocv_b);
        wb_u8(p, mocv_g);
        wb_u8(p, mocv_r);
        wb_u8(p, mocv_a);
    }
}

/* -------------------------------------------------------------------------
   write_file: open path and write buf contents
   ---------------------------------------------------------------------- */
static void write_file(const char *path, wbuf_t *b) {
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "wmogen: cannot open '%s'\n", path); exit(1); }
    fwrite(b->data, 1, b->size, f);
    fclose(f);
    fprintf(stderr, "wmogen: wrote %s (%zu bytes)\n", path, b->size);
}

/* Derive group path: strip .wmo, append _000.wmo */
static void group_path(const char *root, char *out, size_t outsz) {
    size_t len = strlen(root);
    if (len > 4 && !strcasecmp(root + len - 4, ".wmo")) len -= 4;
    snprintf(out, outsz, "%.*s_000.wmo", (int)len, root);
}

/* -------------------------------------------------------------------------
   main
   ---------------------------------------------------------------------- */
int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr,
            "usage: wmogen <out.wmo>\n"
            "   [--amb <R> <G> <B>]                  ambient color (default 64 64 64)\n"
            "   [--flags <HEX>]                       MOHD flags (default 0)\n"
            "   [--indoor]                            group is interior (mogpFlags |= 0x2000)\n"
            "   [--mocv <B> <G> <R> <A>]              MOCV color BGRA file order (default 128 128 128 255)\n"
            "   [--trans-batches <N>]                 transparent (batch-A) batches (default 0)\n"
            "   [--doodad <path> <x> <y> <z> <qx> <qy> <qz> <qw> <scale>]\n"
            "   [--light <type> <x> <y> <z> <B> <G> <R> <intensity>]\n"
            "       type: 0=OMNI 1=SPOT 2=DIRECT 3=AMBIENT\n");
        return 1;
    }

    const char *out_root = argv[1];
    uint8_t  amb_r = 64, amb_g = 64, amb_b = 64;
    uint16_t mohd_flags = 0;
    uint32_t mogp_flags = 0;
    uint8_t  mocv_b = 128, mocv_g = 128, mocv_r = 128, mocv_a = 255;
    uint16_t trans_batches = 0;

    for (int i = 2; i < argc; ) {
        if (!strcmp(argv[i], "--amb") && i + 3 < argc) {
            amb_r = (uint8_t)atoi(argv[i+1]);
            amb_g = (uint8_t)atoi(argv[i+2]);
            amb_b = (uint8_t)atoi(argv[i+3]);
            i += 4;
        } else if (!strcmp(argv[i], "--flags") && i + 1 < argc) {
            mohd_flags = (uint16_t)strtoul(argv[i+1], NULL, 16);
            i += 2;
        } else if (!strcmp(argv[i], "--indoor")) {
            mogp_flags |= 0x2000;
            i++;
        } else if (!strcmp(argv[i], "--mocv") && i + 4 < argc) {
            mocv_b = (uint8_t)atoi(argv[i+1]);
            mocv_g = (uint8_t)atoi(argv[i+2]);
            mocv_r = (uint8_t)atoi(argv[i+3]);
            mocv_a = (uint8_t)atoi(argv[i+4]);
            i += 5;
        } else if (!strcmp(argv[i], "--trans-batches") && i + 1 < argc) {
            trans_batches = (uint16_t)atoi(argv[i+1]);
            i += 2;
        } else if (!strcmp(argv[i], "--light") && i + 8 < argc) {
            /* --light <type> <use_atten> <B> <G> <R> <A> <x> <y> <z> <intensity> ... */
            /* Simplified: --light <type> <x> <y> <z> <B> <G> <R> <intensity> */
            if (num_lights >= MAX_LIGHTS) { fprintf(stderr, "wmogen: too many lights\n"); return 1; }
            Light *lt = &lights[num_lights++];
            lt->type       = (uint8_t)atoi(argv[i+1]);
            lt->use_atten  = 1;
            lt->x = (float)atof(argv[i+2]);
            lt->y = (float)atof(argv[i+3]);
            lt->z = (float)atof(argv[i+4]);
            lt->color_b = (uint8_t)atoi(argv[i+5]);
            lt->color_g = (uint8_t)atoi(argv[i+6]);
            lt->color_r = (uint8_t)atoi(argv[i+7]);
            lt->color_a = 0xFF;
            lt->intensity   = (float)atof(argv[i+8]);
            lt->atten_start = 5.0f;
            lt->atten_end   = 20.0f;
            i += 9;
        } else if (!strcmp(argv[i], "--doodad") && i + 9 < argc) {
            if (num_doodads >= MAX_DOODADS) {
                fprintf(stderr, "wmogen: too many doodads (max %d)\n", MAX_DOODADS);
                return 1;
            }
            Doodad *d = &doodads[num_doodads++];
            snprintf(d->path, sizeof(d->path), "%s", argv[i+1]);
            d->x  = (float)atof(argv[i+2]);
            d->y  = (float)atof(argv[i+3]);
            d->z  = (float)atof(argv[i+4]);
            d->qx = (float)atof(argv[i+5]);
            d->qy = (float)atof(argv[i+6]);
            d->qz = (float)atof(argv[i+7]);
            d->qw = (float)atof(argv[i+8]);
            d->scale = (float)atof(argv[i+9]);
            i += 10;
        } else {
            fprintf(stderr, "wmogen: unknown option '%s'\n", argv[i]);
            return 1;
        }
    }

    /* ---- Build root WMO ---- */
    uint32_t doodad_offsets[MAX_DOODADS] = {0};
    wbuf_t root = {0};
    {
        wbuf_t p = {0};
        write_mver(&p);
        wb_chunk(&root, ID_REVM, &p); p.size = 0;

        write_mohd(&p, 1, (uint32_t)num_lights, num_doodads ? 1 : 0, (uint32_t)num_doodads,
                   amb_r, amb_g, amb_b, mohd_flags);
        wb_chunk(&root, ID_DHOM, &p); p.size = 0;

        write_motx_empty(&p);
        wb_chunk(&root, ID_XTOM, &p); p.size = 0;

        write_momt_one(&p);
        wb_chunk(&root, ID_TMOM, &p); p.size = 0;

        write_mogn(&p, "wmogen_group_000");
        wb_chunk(&root, ID_NGOM, &p); p.size = 0;

        /* Emit minimal portal geometry for Phase 6 testing */
        write_mopv(&p); wb_chunk(&root, ID_VPOM, &p); p.size = 0;
        write_mopt(&p); wb_chunk(&root, ID_TPOM, &p); p.size = 0;
        write_mopr(&p); wb_chunk(&root, ID_RPOM, &p); p.size = 0;

        if (num_doodads > 0) {
            write_mods_one(&p, "Set_$DefaultGlobal", 0, (uint32_t)num_doodads);
            wb_chunk(&root, ID_SDOM, &p); p.size = 0;

            write_modn(&p, doodad_offsets);
            wb_chunk(&root, ID_NDOM, &p); p.size = 0;

            write_modd(&p, doodad_offsets);
            wb_chunk(&root, ID_DDOM, &p); p.size = 0;
        }
        if (num_lights > 0) {
            write_molt(&p);
            wb_chunk(&root, ID_TLOM, &p); p.size = 0;
        }
        wb_free(&p);
    }
    write_file(out_root, &root);
    wb_free(&root);

    /* ---- Build group WMO ---- */
    wbuf_t grp = {0};
    {
        wbuf_t p = {0};
        write_mver(&p);
        wb_chunk(&grp, ID_REVM, &p); p.size = 0;

        /* MOGP outer chunk: fixed header + subchunks */
        {
            wbuf_t mogp_payload = {0};
            write_mogp_fixed_header(&mogp_payload, mogp_flags,
                                    0 /*portal_start*/, 1 /*portal_count*/,
                                    trans_batches, 0, 0);

            wbuf_t sub = {0};
            write_mopy(&sub, 0); wb_chunk(&mogp_payload, ID_YPOM, &sub); sub.size = 0;
            write_movi(&sub);    wb_chunk(&mogp_payload, ID_IVOM, &sub); sub.size = 0;
            write_movt(&sub);    wb_chunk(&mogp_payload, ID_TVOM, &sub); sub.size = 0;
            write_monr(&sub);    wb_chunk(&mogp_payload, ID_RNOM, &sub); sub.size = 0;
            write_motv(&sub);    wb_chunk(&mogp_payload, ID_VTOM, &sub); sub.size = 0;
            write_moba(&sub, trans_batches, 0);
            wb_chunk(&mogp_payload, ID_ABOM, &sub); sub.size = 0;
            write_mocv(&sub, mocv_b, mocv_g, mocv_r, mocv_a);
            wb_chunk(&mogp_payload, ID_VCOM, &sub); sub.size = 0;
            wb_free(&sub);

            wb_chunk(&grp, ID_PGOM, &mogp_payload);
            wb_free(&mogp_payload);
        }
        wb_free(&p);
    }
    char grp_path[1024];
    group_path(out_root, grp_path, sizeof(grp_path));
    write_file(grp_path, &grp);
    wb_free(&grp);

    return 0;
}
