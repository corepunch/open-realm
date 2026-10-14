#include "r_war3map.h"
#include "common/stb_slk.h"
#include "games/warcraft-3/renderer/mdx/r_mdx.h"
#include "renderer/r_game.h"

/* Special terrain models replace the generated cliff across their full footprint.
 * Ground geometry and gameplay pathing are separate from this visual mask. */
static uint8_t *w3_special_cutouts;
static uint32_t w3_cutout_width, w3_cutout_height;

typedef struct w3SpecialMesh_s {
    uint32_t x, y, width, height;
    mdxModel_t const *source;
    mdxModel_t *model;
    mat4_t transform;
    struct w3SpecialMesh_s *next;
} w3SpecialMesh_t;
static w3SpecialMesh_t *w3_special_meshes;
typedef struct {
    war3map_t const *map;
    w3SpecialMesh_t const *mesh;
    mdxModel_t const *source;
    mat4_t const *transform;
} w3SpecialFit_t;

/* Only geometry belongs to the placement. Materials, UV channels, indices and
 * animation data remain owned by the registered model. */
static void R_W3FreeSpecialModel(w3SpecialMesh_t *mesh) {
    if (!mesh->model) return;
    MDX_ReleaseBuffers(mesh->model);
    MDLX_ReleaseSprites(mesh->model);
    MDLX_ForgetRibbonModel(mesh->model);
    SAFE_DELETE(mesh->model->ribbon_states, MDLX_ReleaseRibbonStates);
    mdxGeoset_t *geo = mesh->model->geosets;
    while (geo) {
        mdxGeoset_t *next = geo->next;
        ri.MemFree(geo->vertices); ri.MemFree(geo->normals); ri.MemFree(geo);
        geo = next;
    }
    ri.MemFree(mesh->model);
    mesh->model = NULL; mesh->source = NULL;
}

/* Terrain rebuilds discard fitted storage so the next draw resamples the changed W3E. */
void R_W3InvalidateSpecialModels(void) {
    FOR_EACH_LIST(w3SpecialMesh_t, mesh, w3_special_meshes) R_W3FreeSpecialModel(mesh);
}

/* Apply retail cliff fitting without changing shared geometry or stacked native Z offsets. */
static mdxModel_t *R_W3FitSpecialModel(w3SpecialFit_t const *fit) {
    war3map_t const *map = fit->map;
    w3SpecialMesh_t const *mesh = fit->mesh;
    mdxModel_t const *source = fit->source;
    mat4_t const *transform = fit->transform;
    mdxModel_t *model = ri.MemAlloc(sizeof(*model));
    mdxGeoset_t **tail;
    mat4_t inverse;
    int level = 15;
    if (!model) goto no_model;
    *model = *source;
    model->geosets = NULL;
    model->sprites = NULL; model->ribbon_states = NULL; model->ribbon_tick = 0;
    memset(model->buffers, 0, sizeof(model->buffers));
    tail = &model->geosets;
    Matrix4_inverse(transform, &inverse);
    /* Retail uses the lowest cliff layer across the whole footprint, then
     * adds only raw W3E height to each vertex. Adding interpolated cliff layers
     * would raise the lower vertices of stacked walls onto their roof. */
    for (uint32_t y = mesh->y; y <= mesh->y + MIN(mesh->height, map->height - 1 - mesh->y); y++)
        for (uint32_t x = mesh->x; x <= mesh->x + MIN(mesh->width, map->width - 1 - mesh->x); x++)
            level = MIN(level, GetWar3MapVertex(map, x, y)->level);
    FOR_EACH_LIST(mdxGeoset_t const, original, source->geosets) {
        mdxGeoset_t *geo = ri.MemAlloc(sizeof(*geo));
        if (!geo) goto failed;
        *geo = *original;
        geo->next = NULL; geo->vaos = NULL; ARRAY_COUNT(geo->vaos) = 0;
        geo->matrixPalette = NULL; geo->num_matrixPalette = 0;
        geo->vertices = ri.MemAlloc(geo->num_vertices * sizeof(*geo->vertices));
        geo->normals = ri.MemAlloc(geo->num_vertices * sizeof(*geo->normals));
        *tail = geo; tail = &geo->next;
        if (!geo->vertices || !geo->normals) goto failed;
        FOR_LOOP(i, geo->num_vertices) {
            vec3_t pos = Matrix4_multiply_vector3(transform, &original->vertices[i]);
            float gx = (pos.x - map->center.x) / TILE_SIZE;
            float gy = (pos.y - map->center.y) / TILE_SIZE;
            gx = MAX(0, MIN(gx, map->width - 1));
            gy = MAX(0, MIN(gy, map->height - 1));
            uint32_t x = MIN((uint32_t)floorf(gx), map->width - 2);
            uint32_t y = MIN((uint32_t)floorf(gy), map->height - 2);
            float u = gx - x, v = gy - y;
            float south = LerpNumber(R_GetVertexPosition(map, x, y, false).z, R_GetVertexPosition(map, x+1, y, false).z, u);
            float north = LerpNumber(R_GetVertexPosition(map, x, y+1, false).z, R_GetVertexPosition(map, x+1, y+1, false).z, u);
            /* The entity's rigid center elevation is a transport anchor, not
             * the mesh's terrain baseline. Preserve scale and native Z. */
            pos.z += level * TILE_SIZE - HEIGHT_COR + LerpNumber(south, north, v) - transform->v[14];
            geo->vertices[i] = original->vertices[i];
            geo->vertices[i].z = Matrix4_multiply_vector3(&inverse, &pos).z;
            /* Frustum culling must include the new fitted Z range. XY is unchanged. */
            model->bounds.box.min.z = MIN(model->bounds.box.min.z, geo->vertices[i].z);
            model->bounds.box.max.z = MAX(model->bounds.box.max.z, geo->vertices[i].z);
            geo->normals[i] = original->normals[i];
            if (fabsf(gx - roundf(gx)) < .001f && fabsf(gy - roundf(gy)) < .001f) {
                vec3_t normal = R_GetVertexNormal(map, (uint32_t)roundf(gx), (uint32_t)roundf(gy));
                vec3_t const zero = Matrix4_multiply_vector3(&inverse, &MAKE(vec3_t,0,0,0));
                normal = Matrix4_multiply_vector3(&inverse, &normal);
                geo->normals[i] = Vector3_sub(&normal, &zero);
                Vector3_normalize(&geo->normals[i]);
            }
        }
    }
    vec3_t center = Box3_Center(&model->bounds.box);
    model->bounds.radius = MAX(model->bounds.radius, Vector3_distance(&center, &model->bounds.box.max));
    MDX_BuildBuffers(model);
    return model;
failed:;
    w3SpecialMesh_t partial = {.model=model};
    R_W3FreeSpecialModel(&partial);
no_model:
    fprintf(stderr, "WC3 renderer: unable to allocate fitted terrain doodad at cell (%u,%u)\n", mesh->x, mesh->y);
    return NULL;
}

/* Match the map-authored footprint center; leave ordinary cliff-textured MDX instances untouched. */
mdxModel_t const *R_W3SpecialModel(renderEntity_t const *entity, mat4_t const *transform) {
    mdxModel_t const *source = entity && entity->model ? entity->model->mdx : NULL;
    war3map_t const *map = tr.world;
    bool cliff = false;
    if (!w3_special_meshes || !map || !map->vertices || map->width < 2 || map->height < 2 ||
        !source || !transform || entity->scale <= 0) return source;
    FOR_LOOP(i, source->num_textures) cliff |= source->textures[i].replaceableID == 11;
    if (!cliff) return source;
    FOR_EACH_LIST(w3SpecialMesh_t, mesh, w3_special_meshes) {
        float x = map->center.x + (mesh->x + mesh->width * .5f) * TILE_SIZE;
        float y = map->center.y + (mesh->y + mesh->height * .5f) * TILE_SIZE;
        if (fabsf(entity->origin.x - x) > .01f || fabsf(entity->origin.y - y) > .01f) continue;
        if (mesh->source != source || memcmp(&mesh->transform, transform, sizeof(*transform))) {
            R_W3FreeSpecialModel(mesh);
            mesh->model = R_W3FitSpecialModel(&MAKE(w3SpecialFit_t, .map=map, .mesh=mesh, .source=source, .transform=transform));
            mesh->source = source; mesh->transform = *transform;
        }
        return mesh->model ? mesh->model : source;
    }
    return source;
}

bool R_W3SpecialCellCovered(war3map_t const *map, uint32_t x, uint32_t y) {
    return map && map == tr.world && w3_special_cutouts &&
           x < w3_cutout_width && y < w3_cutout_height &&
           w3_special_cutouts[x + (size_t)y * w3_cutout_width] != 0;
}

void R_W3ClearSpecialCutouts(void) {
    R_W3InvalidateSpecialModels();
    while (w3_special_meshes) {
        w3SpecialMesh_t *next = w3_special_meshes->next;
        ri.MemFree(w3_special_meshes); w3_special_meshes = next;
    }
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
    /* Clip malformed or edge-overlapping footprints without wrapping indexes. */
    uint32_t right = x + MIN(width, w3_cutout_width - x);
    uint32_t bottom = y + MIN(height, w3_cutout_height - y);
    for (uint32_t cy = y; cy < bottom; cy++)
        memset(w3_special_cutouts + (size_t)cy * w3_cutout_width + x, 1, right - x);
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
            !R_W3CutoutU32(r, &mods) || mods > (uint32_t)((r->end - r->p) / 13)) return false;
        w3CutoutDoodadOverride_t item = { .target = target ? target : base, .base = base };
        for (uint32_t m = 0; m < mods; m++) {
            uint32_t field, type, discard;
            cstring_t value = NULL;
            /* W3D is field/type/value/end-ID, without W3A's level/data words.
             * Skipping those words truncated path textures in retail maps.
             * The shortest record is 13 bytes with an empty string value. */
            if (!R_W3CutoutU32(r, &field) || !R_W3CutoutU32(r, &type)) return false;
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
    bool reached_base = false;

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
        if (!override) {
            reached_base = true;
            break;
        }
        if (override->overrides_path) return override->pathTex;
        if (override->base == parent) {
            reached_base = true;
            break;
        }
        parent = override->base;
    }
    if (!reached_base) return NULL;
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
        if (x >= w3_cutout_width || y >= w3_cutout_height) continue;
        w3SpecialMesh_t *mesh = ri.MemAlloc(sizeof(*mesh));
        if (!mesh) {
            fprintf(stderr, "WC3 renderer: unable to allocate terrain doodad footprint at cell (%u,%u)\n", x, y);
            continue;
        }
        *mesh = (w3SpecialMesh_t){.x=x, .y=y, .width=texture->width / 4, .height=texture->height / 4};
        ADD_TO_LIST(mesh, w3_special_meshes);
        R_W3MarkSpecialCutout(x, y, mesh->width, mesh->height);
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
                         MAKEFOURCC('d','p','t','x'), mod_pathingTexture };
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

TEST(wc3_terrain, special_doodad_empty_path_and_scalar_overrides_decode) {
    /* W3D uses field/type/value/end-ID, including an unaligned empty string. */
    uint8_t bytes[64] = {0};
    uint32_t words[] = { 1, MAKEFOURCC('L','O','f','l'), MAKEFOURCC('D','0','0','E'), 2,
                        MAKEFOURCC('d','p','t','x'), mod_pathingTexture };
    memcpy(bytes, words, sizeof(words));
    size_t n = sizeof(words) + 1;
    uint32_t tail[] = { MAKEFOURCC('D','0','0','E'), MAKEFOURCC('d','v','a','r'), mod_int, 3,
                       MAKEFOURCC('D','0','0','E') };
    memcpy(bytes + n, tail, sizeof(tail));
    n += sizeof(tail);
    w3CutoutReader_t reader = { bytes, bytes + n };
    w3CutoutDoodadOverride_t row = {0};
    uint32_t used = 0;
    T_ASSERT(R_W3ReadOverrideTable(&reader, &row, &used, 1));
    T_EQ(used, 1);
    T_ASSERT(row.overrides_path);
    T_STREQ(row.pathTex, "");
    T_EQ(reader.p, reader.end);
    for (size_t length = 0; length < n; length++) {
        reader = (w3CutoutReader_t){ bytes, bytes + length };
        used = 0;
        T_ASSERT(!R_W3ReadOverrideTable(&reader, &row, &used, 1));
    }
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
    overrides[0].base = parent.id;
    overrides[1].target = parent.id;
    overrides[1].base = overrides[0].target;
    T_NULL(R_W3CutoutPath(overrides[0].target, &slk, overrides, 2));
}

static handle_t R_W3TestCutoutAlloc(long size) { return malloc(size); }

TEST(wc3_terrain, special_cliff_cutout_replaces_full_footprint) {
    /* Filtered runs cannot rely on earlier model tests installing imports. */
    refImport_t saved_imports = ri;
    ri.MemAlloc = R_W3TestCutoutAlloc;
    ri.MemFree = free;
    war3map_t map = { .width = 12, .height = 12 };
    war3map_t const *saved_world = tr.world;
    tr.world = &map;
    T_ASSERT(R_W3BeginSpecialCutouts(&map));
    R_W3MarkSpecialCutout(2, 3, 5, 4);
    /* Mouth/front edge stays unobstructed, including its two corners. */
    T_ASSERT(R_W3SpecialCellCovered(&map, 2, 3));
    T_ASSERT(R_W3SpecialCellCovered(&map, 6, 3));
    /* The fitted special mesh owns every cell, including the side and rear joins. */
    T_ASSERT(R_W3SpecialCellCovered(&map, 2, 4));
    T_ASSERT(R_W3SpecialCellCovered(&map, 3, 4));
    T_ASSERT(R_W3SpecialCellCovered(&map, 5, 5));
    T_ASSERT(R_W3SpecialCellCovered(&map, 6, 5));
    /* The full rear row must not retain a second cliff wall. */
    T_ASSERT(R_W3SpecialCellCovered(&map, 3, 6));
    T_ASSERT(R_W3SpecialCellCovered(&map, 5, 6));
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
    ri = saved_imports;
}

TEST(wc3_terrain, orcx01_cave_vertices_follow_raw_height_and_keep_stacked_walls) {
    refImport_t saved_imports = ri;
    war3map_t const *saved_world = tr.world;
    w3SpecialMesh_t *saved_meshes = w3_special_meshes;
    ri.MemAlloc = R_W3TestCutoutAlloc; ri.MemFree = free;
    /* OrcX01 YCr2 (12,160): W3E heights and cliff layers at all 16 footprint corners. */
    float const heights[16] = {463.75f,478.25f,458.25f,460.25f,
        570.5f,522.25f,490.25f,483.75f,583,536.5f,506.75f,534.75f,
        556.75f,538.5f,526.5f,528.75f};
    uint8_t const layers[16] = {0,0,0,0,2,0,0,0,2,0,0,2,2,2,2,2};
    war3mapVertex_t vertices[16];
    FOR_LOOP(i,16) vertices[i] = (war3mapVertex_t){
        .accurate_height = 8192 + heights[i] * 4, .level = layers[i] };
    war3map_t map = {.width=4, .height=4, .center={-4608,12288}, .vertices=vertices};
    vec3_t positions[] = {{-192,192,256}, {-192,192,0}, {192,-192,0}, {0,0,90}};
    vec3_t normals[] = {{0,0,1}, {1,0,0}, {0,0,1}, {1,0,0}};
    vec2_t uvs[8] = {{.998047f,.001953f}, {0,0}, {1,1}, {.2f,.3f},
        {.615494f,.979932f}, {0,1}, {1,0}, {.7f,.8f}};
    short triangles[] = {0,1,2};
    mdxGeoset_t geoset = {.vertices=positions, .num_vertices=4, .normals=normals,
        .texcoord=uvs, .num_texcoord=4, .num_texcoordChannels=2,
        .triangles=triangles, .num_triangles=3};
    mdxTexture_t texture = {.replaceableID=11};
    mdxRibbonInstance_t ribbon = {0};
    mdxModel_t mdx = {.geosets=&geoset, .textures=&texture, .num_textures=1, .ribbon_states=&ribbon,
        .bounds={.box={.min={-192,-192,0}, .max={192,192,256}}, .radius=300}};
    model_t model = {.modeltype=ID_MDLX, .mdx=&mdx};
    renderEntity_t entity = {.model=&model, .origin={-4416,12480,257.5f},
        .angle=DEG2RAD(270), .scale=1};
    quaternion_t rotation = Quaternion_fromOrientation(&MAKE(orientation_t, .yaw=entity.angle));
    mat4_t transform;
    Matrix4_from_rotation_translation_scale_origin(&transform, &rotation, &entity.origin, &MAKE(vec3_t,1,1,1), &MAKE(vec3_t,0,0,0));
    w3SpecialMesh_t placement = {.width=3,.height=3};
    tr.world = &map; w3_special_meshes = &placement;
    mdxModel_t const *fitted = R_W3SpecialModel(&entity, &transform);
    T_NE(fitted, &mdx);
    T_FEQ(fitted->geosets->vertices[0].z + entity.origin.z, 528.75f, .001f);
    T_FEQ(fitted->geosets->vertices[1].z + entity.origin.z, 272.75f, .001f);
    T_FEQ(fitted->geosets->vertices[2].z + entity.origin.z, 207.75f, .001f);
    T_FEQ(fitted->geosets->vertices[3].z + entity.origin.z, 347.9375f, .001f);
    T_FEQ(positions[0].z, 256, .001f); /* Registry model remains reusable by other placements. */
    T_NULL(fitted->ribbon_states);
    T_EQ(mdx.ribbon_states, &ribbon);
    T_ASSERT(fitted->bounds.box.min.z <= fitted->geosets->vertices[2].z);
    T_ASSERT(fitted->bounds.box.max.z >= fitted->geosets->vertices[0].z);
    T_EQ(fitted->geosets->texcoord, uvs);
    T_EQ(fitted->geosets->num_texcoordChannels, 2);
    T_EQ(R_W3SpecialModel(&entity, &transform), fitted);
    T_EQ(fitted->geosets->triangles, triangles);
    T_EQ(ARRAY_COUNT(fitted->geosets->vaos), 2);
    T_NULL(geoset.vaos);
    T_NULL(geoset.matrixPalette);
    vec3_t roof = Matrix4_multiply_vector3(&transform, &fitted->geosets->vertices[0]);
    T_FEQ(roof.x, -4224, .001f); T_FEQ(roof.y, 12672, .001f);
    entity.origin.x += TILE_SIZE;
    T_EQ(R_W3SpecialModel(&entity, &transform), &mdx); /* Ordinary MDX instances do not get fitted. */
    entity.origin.x -= TILE_SIZE;
    /* A later terrain deformation rebuilds from the immutable authored mesh. */
    FOR_LOOP(i,16) vertices[i].accurate_height += 320;
    R_W3InvalidateSpecialModels();
    T_NULL(placement.model);
    fitted = R_W3SpecialModel(&entity, &transform);
    T_FEQ(fitted->geosets->vertices[0].z + entity.origin.z, 608.75f, .001f);
    /* Rebinding the transport anchor must not translate the fitted world roof. */
    entity.origin.z += 40; transform.v[14] += 40;
    fitted = R_W3SpecialModel(&entity, &transform);
    T_FEQ(fitted->geosets->vertices[0].z + entity.origin.z, 608.75f, .001f);
    /* The outer entity culler must use fitted bounds too, before the draw. */
    entity.angle = 0;
    box3_t bounds;
    T_ASSERT(R_GetEntityBounds(&entity, &bounds));
    T_FEQ(bounds.max.z, 339.25f, .001f);
    T_FEQ(bounds.min.z, -13.25f, .001f);
    R_W3FreeSpecialModel(&placement);
    T_NULL(placement.model);
    T_FEQ(positions[0].z, 256, .001f);
    w3_special_meshes = saved_meshes; tr.world = saved_world; ri = saved_imports;
}
#endif
