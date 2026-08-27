/*
 * sc2fixturegen - deterministic StarCraft II map binary fixture generator.
 *
 * Generates the small terrain/pathing files that SC2_MapLoad already parses:
 * MapInfo, t3HeightMap, t3SyncHeightMap, t3CellFlags, t3SyncCliffLevel,
 * and t3TextureMasks.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void wr_u32le(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)(v & 0xff);
    p[1] = (unsigned char)((v >> 8) & 0xff);
    p[2] = (unsigned char)((v >> 16) & 0xff);
    p[3] = (unsigned char)((v >> 24) & 0xff);
}

static void wr_u16le(unsigned char *p, uint16_t v) {
    p[0] = (unsigned char)(v & 0xff);
    p[1] = (unsigned char)((v >> 8) & 0xff);
}

static int write_file(const char *path, const unsigned char *data, size_t size) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "sc2fixturegen: cannot open %s\n", path);
        return 0;
    }
    if (fwrite(data, 1, size, f) != size) {
        fprintf(stderr, "sc2fixturegen: short write %s\n", path);
        fclose(f);
        return 0;
    }
    fclose(f);
    return 1;
}

static int write_cell_flags(const char *path) {
    unsigned char data[32 + 8 * 6];
    memset(data, 0, sizeof(data));
    memcpy(data, "LFCT", 4);
    wr_u32le(data + 24, 8);
    wr_u32le(data + 28, 6);
    for (unsigned int i = 0; i < 8 * 6; i++) {
        data[32 + i] = (unsigned char)(0x10 + i);
    }
    return write_file(path, data, sizeof(data));
}

static int write_map_info(const char *path) {
    static const char name[] = "SC2 Tiny Fixture";
    unsigned char data[24 + sizeof(name) + 1 + 16];
    unsigned int offset = 24;

    memset(data, 0, sizeof(data));
    memcpy(data, "IpaM", 4);
    wr_u32le(data + 4, 0xffffffffu);
    wr_u32le(data + 8, 33);
    wr_u32le(data + 16, 8);
    wr_u32le(data + 20, 6);
    memcpy(data + offset, name, sizeof(name));
    offset += (unsigned int)sizeof(name);
    offset++;
    wr_u32le(data + offset, 2);
    wr_u32le(data + offset + 4, 1);
    wr_u32le(data + offset + 8, 6);
    wr_u32le(data + offset + 12, 4);
    return write_file(path, data, sizeof(data));
}

static unsigned int playable_height(unsigned int x, unsigned int y) {
    static unsigned int const heights[4][5] = {
        {  0,  8,  8,  8,  8 },
        {  8,  8,  8,  8,  8 },
        {  8, 10, 10, 10, 10 },
        { 10, 10, 10, 10, 12 },
    };
    return heights[y][x];
}

static int write_height_map(const char *path) {
    unsigned char data[32 + 9 * 7 * 6];
    memset(data, 0, sizeof(data));
    memcpy(data, "HMAP", 4);
    wr_u32le(data + 4, 101);
    wr_u32le(data + 8, 9);
    wr_u32le(data + 12, 7);
    for (unsigned int y = 0; y < 7; y++) {
        for (unsigned int x = 0; x < 9; x++) {
            unsigned int i = x + y * 9;
            unsigned int h = 0;
            if (x >= 2 && x <= 6 && y >= 1 && y <= 4) {
                h = playable_height(x - 2, y - 1);
            }
            wr_u16le(data + 32 + i * 6, 0);
            wr_u16le(data + 32 + i * 6 + 2, (uint16_t)(h + 1));
            wr_u16le(data + 32 + i * 6 + 4, 0);
        }
    }
    return write_file(path, data, sizeof(data));
}

static int write_sync_height_map(const char *path) {
    unsigned char data[64 + 9 * 7 * 4];
    memset(data, 0, sizeof(data));
    memcpy(data, "SMAP", 4);
    wr_u32le(data + 4, 102);
    wr_u32le(data + 8, 9);
    wr_u32le(data + 12, 7);
    for (unsigned int i = 0; i < 9 * 7; i++) {
        wr_u16le(data + 64 + i * 4, 0);
        wr_u16le(data + 64 + i * 4 + 2, 0);
    }
    wr_u16le(data + 64 + (6 + 4 * 9) * 4, 128);
    return write_file(path, data, sizeof(data));
}

static int write_cliff_levels(const char *path) {
    unsigned char data[32 + 8 * 6 * 2];
    memset(data, 0, sizeof(data));
    memcpy(data, "CLIF", 4);
    wr_u32le(data + 8, 8);
    wr_u32le(data + 12, 6);
    for (unsigned int i = 0; i < 8 * 6; i++) {
        wr_u16le(data + 32 + i * 2, (uint16_t)(i + 1));
    }
    return write_file(path, data, sizeof(data));
}

static int write_texture_masks(const char *path) {
    unsigned char data[64 + 8 * 2];
    memset(data, 0, sizeof(data));
    memcpy(data, "MASK", 4);
    wr_u32le(data + 12, 4);
    wr_u32le(data + 16, 4);
    memset(data + 64, 0x12, 8);
    memset(data + 64 + 8, 0xab, 8);
    return write_file(path, data, sizeof(data));
}

static void wr_f32le(unsigned char *p, float value) { uint32_t bits; memcpy(&bits, &value, sizeof(bits)); wr_u32le(p, bits); }

static int write_hard_tiles(const char *path) {
    unsigned char data[28 + 4 + 2 * 58 + 19];
    unsigned int offset = 28;
    float shape[9] = { 0,0,1, -1,0,0, 1,0,0 };

    memset(data, 0, sizeof(data)); memcpy(data, "HRDT", 4); wr_u32le(data + 4, 102); wr_u32le(data + 24, 1);
    wr_u32le(data + offset, 2); offset += 4;
    for (unsigned int i = 0; i < 2; i++) {
        wr_f32le(data + offset, 2.0f + i * 3.0f); wr_f32le(data + offset + 4, 4.0f + i); wr_f32le(data + offset + 8, 0.25f * i);
        for (unsigned int j = 0; j < 9; j++) wr_f32le(data + offset + 12 + j * 4, shape[j]);
        wr_f32le(data + offset + 48, 1.0f + i); wr_f32le(data + offset + 52, 0.5f + i); wr_u16le(data + offset + 56, (uint16_t)(7 + i));
        offset += 58;
    }
    wr_u16le(data + offset, 0); offset += 2; data[offset++] = 0; memcpy(data + offset, "FixtureTile", 11); offset += 11;
    data[offset++] = 0; wr_u32le(data + offset, 0);
    return write_file(path, data, sizeof(data));
}

/* Write a flat NxM height-map binary (HMAP).  w and h are cell counts;
 * the height-map has (w+1)*(h+1) vertices. height_val is the raw sample
 * height written to every vertex (world units depend on t3Terrain.xml
 * quantise params; 8 gives height ~0 with the default MarSara bias). */
static int write_flat_height_map(const char *path, unsigned int w, unsigned int h, unsigned int height_val) {
    unsigned int vw = w + 1, vh = h + 1;
    size_t data_size = 32 + vw * vh * 6;
    unsigned char *data = calloc(1, data_size);
    if (!data) { fprintf(stderr, "sc2fixturegen: OOM\n"); return 0; }
    memcpy(data, "HMAP", 4);
    wr_u32le(data + 4,  101);
    wr_u32le(data + 8,  vw);
    wr_u32le(data + 12, vh);
    for (unsigned int i = 0; i < vw * vh; i++)
        wr_u16le(data + 32 + i * 6 + 2, (uint16_t)height_val);
    int ok = write_file(path, data, data_size);
    free(data);
    return ok;
}

static int write_flat_sync_height_map(const char *path, unsigned int w, unsigned int h) {
    unsigned int vw = w + 1, vh = h + 1;
    size_t data_size = 64 + vw * vh * 4;
    unsigned char *data = calloc(1, data_size);
    if (!data) { fprintf(stderr, "sc2fixturegen: OOM\n"); return 0; }
    memcpy(data, "SMAP", 4);
    wr_u32le(data + 4,  102);
    wr_u32le(data + 8,  vw);
    wr_u32le(data + 12, vh);
    int ok = write_file(path, data, data_size);
    free(data);
    return ok;
}

static int write_flat_cell_flags(const char *path, unsigned int w, unsigned int h) {
    size_t data_size = 32 + w * h;
    unsigned char *data = calloc(1, data_size);
    if (!data) { fprintf(stderr, "sc2fixturegen: OOM\n"); return 0; }
    memcpy(data, "LFCT", 4);
    wr_u32le(data + 24, w);
    wr_u32le(data + 28, h);
    int ok = write_file(path, data, data_size);
    free(data);
    return ok;
}

static int write_flat_cliff_levels(const char *path, unsigned int w, unsigned int h) {
    size_t data_size = 32 + w * h * 2;
    unsigned char *data = calloc(1, data_size);
    if (!data) { fprintf(stderr, "sc2fixturegen: OOM\n"); return 0; }
    memcpy(data, "CLIF", 4);
    wr_u32le(data + 8,  w);
    wr_u32le(data + 12, h);
    int ok = write_file(path, data, data_size);
    free(data);
    return ok;
}

/* Flat texture-mask: 1x1 pixels, 1 layer.  All weight on layer 0. */
static int write_flat_texture_masks(const char *path, unsigned int w, unsigned int h) {
    size_t data_size = 64 + w * h;
    unsigned char *data = calloc(1, data_size);
    if (!data) { fprintf(stderr, "sc2fixturegen: OOM\n"); return 0; }
    memcpy(data, "MASK", 4);
    wr_u32le(data + 12, w);
    wr_u32le(data + 16, h);
    memset(data + 64, 0x0f, w * h); /* max weight for layer 0 (nibble scale: 0x0f = 15/15) */
    int ok = write_file(path, data, data_size);
    free(data);
    return ok;
}

/* flat-terrain <dir> <width> <height>: generate all binary terrain files for
 * a flat WxH-cell map.  Files are written as <dir>/t3HeightMap, etc. */
static int write_flat_terrain(const char *dir, unsigned int w, unsigned int h) {
    char path[1024];
    int ok = 1;
#define FLAT_FILE(name, fn, ...) \
    snprintf(path, sizeof(path), "%s/" name, dir); \
    if (!fn(path, ##__VA_ARGS__)) { fprintf(stderr, "failed: %s\n", path); ok = 0; }
    FLAT_FILE("t3HeightMap",      write_flat_height_map,      w, h, 8)
    FLAT_FILE("t3SyncHeightMap",  write_flat_sync_height_map, w, h)
    FLAT_FILE("t3CellFlags",      write_flat_cell_flags,      w, h)
    FLAT_FILE("t3SyncCliffLevel", write_flat_cliff_levels,    w, h)
    FLAT_FILE("t3TextureMasks",   write_flat_texture_masks,   w, h)
#undef FLAT_FILE
    return ok;
}

int main(int argc, char **argv) {
    if (argc == 5 && !strcmp(argv[1], "flat-terrain")) {
        unsigned int w = (unsigned int)atoi(argv[3]);
        unsigned int h = (unsigned int)atoi(argv[4]);
        if (!w || !h) { fprintf(stderr, "sc2fixturegen: bad dimensions\n"); return 1; }
        return write_flat_terrain(argv[2], w, h) ? 0 : 1;
    }
    if (argc != 3) {
        fprintf(stderr, "Usage: sc2fixturegen <map-info|height-map|sync-height-map|cell-flags|cliff-levels|texture-masks|hard-tiles> <out>\n"
                        "       sc2fixturegen flat-terrain <dir> <width> <height>\n");
        return 1;
    }
    if (!strcmp(argv[1], "map-info")) {
        return write_map_info(argv[2]) ? 0 : 1;
    }
    if (!strcmp(argv[1], "cell-flags")) {
        return write_cell_flags(argv[2]) ? 0 : 1;
    }
    if (!strcmp(argv[1], "height-map")) {
        return write_height_map(argv[2]) ? 0 : 1;
    }
    if (!strcmp(argv[1], "sync-height-map")) {
        return write_sync_height_map(argv[2]) ? 0 : 1;
    }
    if (!strcmp(argv[1], "cliff-levels")) {
        return write_cliff_levels(argv[2]) ? 0 : 1;
    }
    if (!strcmp(argv[1], "texture-masks")) {
        return write_texture_masks(argv[2]) ? 0 : 1;
    }
    if (!strcmp(argv[1], "hard-tiles")) {
        return write_hard_tiles(argv[2]) ? 0 : 1;
    }
    fprintf(stderr, "sc2fixturegen: unknown fixture kind %s\n", argv[1]);
    return 1;
}
