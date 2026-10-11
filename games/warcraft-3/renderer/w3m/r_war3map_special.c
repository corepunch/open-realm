#include "r_war3map.h"
#include "common/stb_slk.h"

/* Visual-only generated-cliff suppression for special terrain doodads.
 * Keep enough of the surrounding cliff at footprint edges to meet the model;
 * ground geometry and gameplay pathing are separate from this mask. */
static uint8_t *w3_special_cutouts;
static uint32_t w3_cutout_width, w3_cutout_height;

bool R_W3SpecialCellCovered(war3map_t const *map, uint32_t x, uint32_t y) {
    return map && map == tr.world && w3_special_cutouts &&
           x < w3_cutout_width && y < w3_cutout_height &&
           w3_special_cutouts[x + (size_t)y * w3_cutout_width] != 0;
}

void R_W3ClearSpecialCutouts(void) {
    SAFE_DELETE(w3_special_cutouts, ri.MemFree);
    w3_cutout_width = w3_cutout_height = 0;
}

static bool R_W3BeginSpecialCutouts(war3map_t const *map) {
    R_W3ClearSpecialCutouts();
    if (!map || map->width < 2 || map->height < 2) return false;
    size_t width = (size_t)map->width - 1, height = (size_t)map->height - 1;
    if (height > SIZE_MAX / width) return false;
    w3_special_cutouts = ri.MemAlloc(width * height);
    if (!w3_special_cutouts) return false;
    memset(w3_special_cutouts, 0, width * height);
    w3_cutout_width = (uint32_t)width;
    w3_cutout_height = (uint32_t)height;
    return true;
}

static void R_W3MarkSpecialCutout(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    if (!w3_special_cutouts || !width || !height ||
        x >= w3_cutout_width || y >= w3_cutout_height) return;
    /* Clip even malformed or edge-overlapping footprints; never wrap indexes.
     * The authored pathing rectangle is broader than the mouth which replaces
     * generated cliff faces. Cutting its full rectangle left unnaturally straight
     * holes above and beside the special cliff model (Rexxar Goblin Cave).
     *
     * Retain the north/rear row and the two side columns behind the entrance,
     * but keep the south/front row clear for the mouth and its barrier. This
     * preserves the surrounding cliff where it can hide the cave model's flat
     * upper/side boundaries without bringing back the complete cliff wall.
     * Small footprints cannot be inset safely: preserve the original mask. */
    uint32_t right = x + MIN(width, w3_cutout_width - x);
    uint32_t bottom = y + MIN(height, w3_cutout_height - y);
    bool const refine = right - x >= 3 && bottom - y >= 2;
    for (uint32_t cy = y; cy < bottom; cy++) {
        if (refine && cy == bottom - 1) continue; /* keep rear rim */
        uint32_t from = x, to = right;
        if (refine && cy != y) { /* keep side joins above the mouth */
            from++;
            to--;
        }
        if (to > from)
            memset(w3_special_cutouts + (size_t)cy * w3_cutout_width + from, 1, to - from);
    }
}

/* A renderer-local view of only the doodad property needed for cutouts. */
typedef struct { uint32_t id; cstring_t pathTex; } w3CutoutDoodadRow_t;
static slkField_t const w3_cutout_doodad_schema[] = {
    { "", offsetof(w3CutoutDoodadRow_t, id), STB_SLK_FOURCC },
    { "pathTex", offsetof(w3CutoutDoodadRow_t, pathTex), STB_SLK_STR },
    { NULL, 0, 0 }
};

typedef struct {
    uint32_t target, base;
    char pathTex[MAX_PATHLEN];
    bool overrides_path;
} w3CutoutDoodadOverride_t;

/* Read only map-local W3D inheritance and the pathTex override; no dependency
 * on the game's mutable object tables or specific Rexxar object IDs. */
typedef struct { uint8_t const *p, *end; } w3CutoutReader_t;
static bool R_W3CutoutTake(w3CutoutReader_t *r, size_t length, uint8_t const **out) {
    if (length > (size_t)(r->end - r->p)) return false;
    if (out) *out = r->p;
    r->p += length;
    return true;
}
static bool R_W3CutoutU32(w3CutoutReader_t *r, uint32_t *out) {
    uint8_t const *p;
    if (!R_W3CutoutTake(r, 4, &p)) return false;
    *out = (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
    return true;
}
static bool R_W3CutoutString(w3CutoutReader_t *r, cstring_t *out) {
    uint8_t const *terminator = memchr(r->p, '\0', (size_t)(r->end - r->p));
    if (!terminator) return false;
    *out = (cstring_t)r->p;
    r->p = terminator + 1;
    return true;
}

static bool R_W3ReadOverrideTable(w3CutoutReader_t *r, w3CutoutDoodadOverride_t *rows,
                                  uint32_t *used, uint32_t capacity) {
    uint32_t count;
    if (!R_W3CutoutU32(r, &count) || count > (uint32_t)((r->end - r->p) / 12)) return false;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t base, target, mods;
        if (!R_W3CutoutU32(r, &base) || !R_W3CutoutU32(r, &target) ||
            !R_W3CutoutU32(r, &mods) || mods > (uint32_t)((r->end - r->p) / 20)) return false;
        w3CutoutDoodadOverride_t item = { .target = target ? target : base, .base = base };
        for (uint32_t m = 0; m < mods; m++) {
            uint32_t field, type, discard;
            cstring_t value = NULL;
            if (!R_W3CutoutU32(r, &field) || !R_W3CutoutU32(r, &type) ||
                !R_W3CutoutTake(r, 8, NULL)) return false; /* variation + data pointer */
            if (type == mod_int || type == mod_real || type == mod_unreal) {
                if (!R_W3CutoutTake(r, 4, NULL)) return false;
            } else if (type == mod_bool || type == mod_char) {
                if (!R_W3CutoutTake(r, 1, NULL)) return false;
            } else if (type <= mod_attackBits) {
                if (!R_W3CutoutString(r, &value)) return false;
            } else return false;
            if (!R_W3CutoutU32(r, &discard)) return false; /* original object ID */
            if (field == MAKEFOURCC('d','p','t','x') && value && strlen(value) < sizeof(item.pathTex)) {
                strlcpy(item.pathTex, value, sizeof(item.pathTex));
                item.overrides_path = true;
            }
        }
        if (*used < capacity) rows[(*used)++] = item;
    }
    return true;
}

static uint32_t R_W3ReadCutoutOverrides(handle_t archive, w3CutoutDoodadOverride_t **out) {
    handle_t file;
    uint32_t size, version;
    uint8_t *bytes;
    w3CutoutReader_t r;
    uint32_t used = 0;
    *out = NULL;
    if (!SFileOpenFileEx(archive, "war3map.w3d", SFILE_OPEN_FROM_MPQ, &file)) return 0;
    size = SFileGetFileSize(file, NULL);
    if (size < 12 || size > 8u * 1024u * 1024u) { SFileCloseFile(file); return 0; }
    bytes = ri.MemAlloc(size);
    if (!bytes || !SFileReadFile(file, bytes, size, NULL, NULL)) {
        if (bytes) ri.MemFree(bytes);
        SFileCloseFile(file); return 0;
    }
    SFileCloseFile(file);
    r = (w3CutoutReader_t){bytes, bytes + size};
    /* Each record needs 12 bytes; cap allocations independently of map input. */
    uint32_t capacity = MIN(size / 12, 4096u);
    w3CutoutDoodadOverride_t *rows = ri.MemAlloc((size_t)capacity * sizeof(*rows));
    if (!rows) { ri.MemFree(bytes); return 0; }
    if (!R_W3CutoutU32(&r, &version) || version > 3 ||
        !R_W3ReadOverrideTable(&r, rows, &used, capacity) ||
        !R_W3ReadOverrideTable(&r, rows, &used, capacity)) used = 0;
    ri.MemFree(bytes);
    if (!used) { ri.MemFree(rows); return 0; }
    *out = rows;
    return used;
}

static cstring_t R_W3CutoutPath(uint32_t id, slkIndex_t const *slk,
                                w3CutoutDoodadOverride_t const *overrides, uint32_t count) {
    uint32_t parent = id;
    w3CutoutDoodadRow_t const *row;

    /* Map-local doodad rows can inherit from other map-local rows. Bound the
     * walk by the table size so malformed cyclic input cannot loop forever. */
    for (uint32_t depth = 0; depth <= count; depth++) {
        w3CutoutDoodadOverride_t const *override = NULL;
        for (uint32_t i = 0; i < count; i++) {
            if (overrides[i].target == parent) {
                override = overrides + i;
                break;
            }
        }
        if (!override) break;
        if (override->overrides_path) return override->pathTex;
        if (override->base == parent) break;
        parent = override->base;
    }
    row = FS_SLKLookup(slk, parent);
    return row ? row->pathTex : NULL;
}

/* war3map.doo has variable-length ordinary placements before the fixed 16-byte
 * terrain doodad tail. Advance with strict bounds so a malformed map cannot
 * accidentally erase terrain or make us read outside its archive bytes. */
static void R_W3ReadSpecialPlacements(handle_t archive, war3map_t const *map,
                                     slkIndex_t const *slk,
                                     w3CutoutDoodadOverride_t const *overrides, uint32_t override_count) {
    handle_t file;
    uint32_t size;
    uint8_t *bytes;
    w3CutoutReader_t r;
    uint32_t magic, version, subversion, count;
    (void)map; /* footprint tiles are validated against the allocated map mask */
    if (!SFileOpenFileEx(archive, "war3map.doo", SFILE_OPEN_FROM_MPQ, &file)) return;
    size = SFileGetFileSize(file, NULL);
    if (size < 24 || size > 32u * 1024u * 1024u) { SFileCloseFile(file); return; }
    bytes = ri.MemAlloc(size);
    if (!bytes || !SFileReadFile(file, bytes, size, NULL, NULL)) {
        if (bytes) ri.MemFree(bytes);
        SFileCloseFile(file); return;
    }
    SFileCloseFile(file);
    r = (w3CutoutReader_t){bytes, bytes + size};
    if (!R_W3CutoutU32(&r, &magic) || magic != MAKEFOURCC('W','3','d','o') ||
        !R_W3CutoutU32(&r, &version) || (version != 7 && version != 8) ||
        !R_W3CutoutU32(&r, &subversion) || !R_W3CutoutU32(&r, &count) ||
        count > (uint32_t)((r.end - r.p) / (38 + 4))) goto done;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t set_count, item_count, ignored;
        if (!R_W3CutoutTake(&r, 38, NULL)) goto done;
        if (version == 8) {
            if (!R_W3CutoutTake(&r, 4, NULL) ||
                !R_W3CutoutU32(&r, &set_count) ||
                set_count > (uint32_t)((r.end - r.p) / 4)) goto done;
            for (uint32_t set = 0; set < set_count; set++) {
                if (!R_W3CutoutU32(&r, &item_count) ||
                    item_count > (uint32_t)((r.end - r.p) / 8) ||
                    !R_W3CutoutTake(&r, (size_t)item_count * 8, NULL)) goto done;
            }
        }
        if (!R_W3CutoutU32(&r, &ignored)) goto done; /* editor ID */
    }
    uint32_t special_version, special_count;
    if (!R_W3CutoutU32(&r, &special_version) || special_version != 0 ||
        !R_W3CutoutU32(&r, &special_count) ||
        special_count > (uint32_t)((r.end - r.p) / 16)) goto done;
    for (uint32_t i = 0; i < special_count; i++) {
        uint32_t id, unused, x, y;
        if (!R_W3CutoutU32(&r, &id) || !R_W3CutoutU32(&r, &unused) ||
            !R_W3CutoutU32(&r, &x) || !R_W3CutoutU32(&r, &y)) break;
        cstring_t path = R_W3CutoutPath(id, slk, overrides, override_count);
        if (!path || !*path || !strcmp(path, "_")) continue;
        texture_t const *texture = R_LoadTexture(path);
        if (!texture || texture == tr.texture[TEX_PLACEHOLDER] ||
            !texture->width || !texture->height ||
            texture->width % 4 || texture->height % 4) continue;
        R_W3MarkSpecialCutout(x, y, texture->width / 4, texture->height / 4);
    }
done:
    ri.MemFree(bytes);
}

void R_W3LoadSpecialCutouts(handle_t archive, war3map_t const *map) {
    w3CutoutDoodadRow_t *rows = NULL;
    slkIndex_t slk = {0};
    w3CutoutDoodadOverride_t *overrides = NULL;
    uint32_t count, override_count;
    if (!R_W3BeginSpecialCutouts(map)) return;
    count = ri.LoadSlk("Doodads\\Doodads.slk", w3_cutout_doodad_schema,
                       (void **)&rows, sizeof(*rows));
    if (!count) return;
    FS_SLKBuildIndex(&slk, rows, count, sizeof(*rows));
    override_count = R_W3ReadCutoutOverrides(archive, &overrides);
    R_W3ReadSpecialPlacements(archive, map, &slk, overrides, override_count);
    if (overrides) ri.MemFree(overrides);
    FS_SLKFreeIndex(&slk);
    FS_SLKFreeRows(w3_cutout_doodad_schema, rows, count, sizeof(*rows));
}

#ifdef BZ_TESTS
#include "shared/test.h"
TEST(wc3_terrain, special_doodad_w3d_path_override_decodes) {
    uint8_t bytes[128] = {0};
    size_t n = 0;
    uint32_t words[] = { 1, MAKEFOURCC('L','O','f','l'), MAKEFOURCC('D','0','0','E'), 1,
                         MAKEFOURCC('d','p','t','x'), mod_pathingTexture, 0, 0 };
    for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++) {
        memcpy(bytes + n, &words[i], sizeof(words[i]));
        n += sizeof(words[i]);
    }
    cstring_t path = "PathTextures\\CustomCave.tga";
    memcpy(bytes + n, path, strlen(path) + 1);
    n += strlen(path) + 1;
    uint32_t trailing_id = MAKEFOURCC('L','O','f','l');
    memcpy(bytes + n, &trailing_id, 4);
    n += 4;
    w3CutoutReader_t reader = { bytes, bytes + n };
    w3CutoutDoodadOverride_t row = {0};
    uint32_t used = 0;
    T_ASSERT(R_W3ReadOverrideTable(&reader, &row, &used, 1));
    T_EQ(used, 1);
    T_EQ(row.target, MAKEFOURCC('D','0','0','E'));
    T_EQ(row.base, MAKEFOURCC('L','O','f','l'));
    T_ASSERT(row.overrides_path);
    T_STREQ(row.pathTex, path);
    reader = (w3CutoutReader_t){ bytes, bytes + n - 2 };
    used = 0;
    T_ASSERT(!R_W3ReadOverrideTable(&reader, &row, &used, 1));
}

TEST(wc3_terrain, special_doodad_path_inherits_map_custom_id) {
    w3CutoutDoodadRow_t parent = { .id = MAKEFOURCC('L','O','f','l'),
                                    .pathTex = "PathTextures\\CavePath.tga" };
    uint32_t key = parent.id;
    void *value = &parent;
    slkIndex_t slk = { .keys = &key, .rows = &value, .count = 1 };
    w3CutoutDoodadOverride_t overrides[] = {
        { .target = MAKEFOURCC('D','0','0','E'), .base = parent.id },
    };
    T_STREQ(R_W3CutoutPath(overrides[0].target, &slk, overrides, 1), parent.pathTex);
    overrides[0].overrides_path = true;
    strlcpy(overrides[0].pathTex, "PathTextures/Changed.tga", sizeof(overrides[0].pathTex));
    T_STREQ(R_W3CutoutPath(overrides[0].target, &slk, overrides, 1), "PathTextures/Changed.tga");
    T_NULL(R_W3CutoutPath(MAKEFOURCC('D','0','0','F'), &slk, overrides, 1));
}

TEST(wc3_terrain, special_doodad_path_follows_map_custom_inheritance) {
    w3CutoutDoodadRow_t parent = { .id = MAKEFOURCC('L','O','f','l'),
                                    .pathTex = "PathTextures\\CavePath.tga" };
    uint32_t key = parent.id;
    void *value = &parent;
    slkIndex_t slk = { .keys = &key, .rows = &value, .count = 1 };
    w3CutoutDoodadOverride_t overrides[] = {
        { .target = MAKEFOURCC('D','0','0','E'), .base = MAKEFOURCC('D','0','0','D') },
        { .target = MAKEFOURCC('D','0','0','D'), .base = parent.id },
    };

    T_STREQ(R_W3CutoutPath(overrides[0].target, &slk, overrides, 2), parent.pathTex);
    overrides[1].overrides_path = true;
    strlcpy(overrides[1].pathTex, "PathTextures/Changed.tga", sizeof(overrides[1].pathTex));
    T_STREQ(R_W3CutoutPath(overrides[0].target, &slk, overrides, 2), "PathTextures/Changed.tga");
    overrides[1].overrides_path = false;
    overrides[1].base = overrides[0].target;
    T_NULL(R_W3CutoutPath(overrides[0].target, &slk, overrides, 2));
}

TEST(wc3_terrain, special_cliff_cutout_preserves_side_and_rear_rim) {
    war3map_t map = { .width = 12, .height = 12 };
    war3map_t const *saved_world = tr.world;
    tr.world = &map;
    T_ASSERT(R_W3BeginSpecialCutouts(&map));
    R_W3MarkSpecialCutout(2, 3, 5, 4);
    /* Mouth/front edge stays unobstructed, including its two corners. */
    T_ASSERT(R_W3SpecialCellCovered(&map, 2, 3));
    T_ASSERT(R_W3SpecialCellCovered(&map, 6, 3));
    /* Cliff faces beside the inner rows are retained for natural joins. */
    T_ASSERT(!R_W3SpecialCellCovered(&map, 2, 4));
    T_ASSERT(R_W3SpecialCellCovered(&map, 3, 4));
    T_ASSERT(R_W3SpecialCellCovered(&map, 5, 5));
    T_ASSERT(!R_W3SpecialCellCovered(&map, 6, 5));
    /* Restore the whole rear row over the model's upper rectangle. */
    T_ASSERT(!R_W3SpecialCellCovered(&map, 3, 6));
    T_ASSERT(!R_W3SpecialCellCovered(&map, 5, 6));
    T_ASSERT(!R_W3SpecialCellCovered(&map, 1, 3));
    /* Edge clipping and tiny footprints retain the old safe behaviour. */
    R_W3MarkSpecialCutout(10, 10, UINT32_MAX, UINT32_MAX);
    T_ASSERT(R_W3SpecialCellCovered(&map, 10, 10));
    T_ASSERT(!R_W3SpecialCellCovered(&map, 9, 10));
    R_W3MarkSpecialCutout(0, 0, 2, 1);
    T_ASSERT(R_W3SpecialCellCovered(&map, 0, 0));
    T_ASSERT(R_W3SpecialCellCovered(&map, 1, 0));
    R_W3ClearSpecialCutouts();
    T_ASSERT(!R_W3SpecialCellCovered(&map, 3, 4));
    tr.world = saved_world;
}
#endif
