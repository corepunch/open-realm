#include "r_war3map.h"
#include "../mdx/r_mdx.h"
#include "renderer/r_cliff.h"

#define NO_CLIFF MAKEFOURCC('C','L','n','o')

static rCliffBakeList_t cliff_bake;
typedef struct cliffLayer_s {
    maplayer_t *layer;
    uint32_t first;
    struct cliffLayer_s *next;
} cliffLayer_t;
static cliffLayer_t *cliff_layers;
static vertex_t *cliff_vertex_samples;
static uint32_t *cliff_vertex_sample_generation;
static uint32_t cliff_vertex_sample_capacity, cliff_vertex_generation;
static bool cliff_warn_sample_alloc, cliff_warn_model_cache_alloc;
static bool cliff_warn_texture_cache_alloc, cliff_warn_layer_alloc, cliff_warn_pending_alloc;

vec3_t R_GetVertexNormal(war3map_t const *map, uint32_t x, uint32_t y);

typedef struct {
    uint32_t cliff;
    cstring_t texDir;
    cstring_t texFile;
    uint32_t groundTile;
    uint32_t upperTile;
    cstring_t rampModelDir;
    cstring_t cliffModelDir;
} cliffData_t;

struct tCliff {
    PATHSTR name;
    model_t const *model;
    bool warned;
    struct tCliff *next;
};

static struct tCliff *g_cliffs = NULL;

struct tCliffTexture {
    uint32_t cliffid;
    char tileset;
    texture_t const *texture;
    struct tCliffTexture *next;
};

static struct tCliffTexture *g_cliff_textures = NULL; /* Per-map resolved cliff texture choices; renderer texture storage remains cache-owned. */

/* Test-only work counts let renderer tests assert that indexed MDX vertices are sampled once. */
#ifdef WC3_CLIFF_TESTS
static uint32_t cliff_vertex_sample_count, cliff_vertex_cache_hit_count;
static uint32_t cliff_model_warning_count;
#endif

void R_ResetCliffCache(void) {
    while (g_cliffs) {
        struct tCliff *next = g_cliffs->next;
        if (g_cliffs->model) R_ReleaseModel((model_t *)g_cliffs->model);
        ri.MemFree(g_cliffs);
        g_cliffs = next;
    }
    while (g_cliff_textures) {
        struct tCliffTexture *next = g_cliff_textures->next;
        ri.MemFree(g_cliff_textures);
        g_cliff_textures = next;
    }
    SAFE_DELETE(cliff_vertex_samples, ri.MemFree);
    SAFE_DELETE(cliff_vertex_sample_generation, ri.MemFree);
    cliff_vertex_sample_capacity = cliff_vertex_generation = 0;
    cliff_warn_sample_alloc = false;
    cliff_warn_model_cache_alloc = cliff_warn_texture_cache_alloc = false;
    cliff_warn_layer_alloc = cliff_warn_pending_alloc = false;
}

// HELPERS

static int TileBaseLevel(war3mapVertex_t const *tile) {
    uint32_t minLevel = tile->level;
    FOR_LOOP(tileIndex, 4) {
        minLevel = MIN(minLevel, tile[tileIndex].level);
    }
    return minLevel;
}

static vec3_t GetAccurateNormalAtPoint(float sx, float sy) {
    float x = sx / TILE_SIZE;
    float y = sy / TILE_SIZE;
    float fx = floorf(x);
    float fy = floorf(y);
    vec3_t a = R_GetVertexNormal(tr.world, fx, fy);
    vec3_t b = R_GetVertexNormal(tr.world, fx + 1, fy);
    vec3_t c = R_GetVertexNormal(tr.world, fx, fy + 1);
    vec3_t d = R_GetVertexNormal(tr.world, fx + 1, fy + 1);
    vec3_t ab = Vector3_lerp(&a, &b, x - fx);
    vec3_t cd = Vector3_lerp(&c, &d, x - fx);
    return Vector3_lerp(&ab, &cd, y - fy);
}

float GetAccurateHeightAtPoint(float sx, float sy) {
    if (!tr.world) return 0;
    float x = sx / TILE_SIZE;
    float y = sy / TILE_SIZE;
    float fx = floorf(x);
    float fy = floorf(y);
    float a = GetWar3MapVertex(tr.world, fx, fy)->accurate_height;
    float b = GetWar3MapVertex(tr.world, fx + 1, fy)->accurate_height;
    float c = GetWar3MapVertex(tr.world, fx, fy + 1)->accurate_height;
    float d = GetWar3MapVertex(tr.world, fx + 1, fy + 1)->accurate_height;
    float ab = LerpNumber(a, b, x - fx);
    float cd = LerpNumber(c, d, x - fx);
    return DECODE_HEIGHT(LerpNumber(ab, cd, y - fy)) + R_W3TerrainOffsetAtPoint(sx, sy);
}

static float GetAccurateWaterLevelAtPoint(float sx, float sy) {
    float x = sx / TILE_SIZE;
    float y = sy / TILE_SIZE;
    float fx = floorf(x);
    float fy = floorf(y);
    float a = GetWar3MapVertex(tr.world, fx, fy)->waterlevel;
    float b = GetWar3MapVertex(tr.world, fx + 1, fy)->waterlevel;
    float c = GetWar3MapVertex(tr.world, fx, fy + 1)->waterlevel;
    float d = GetWar3MapVertex(tr.world, fx + 1, fy + 1)->waterlevel;
    float ab = LerpNumber(a, b, x - fx);
    float cd = LerpNumber(c, d, x - fx);
    return DECODE_HEIGHT(LerpNumber(ab, cd, y - fy));
}

// FUNCTIONS

static model_t const *R_LoadCliffModel(cliffData_t const *data, char const *ccfg, bool ramp) {
    PATHSTR zBuffer;
    cstring_t dir = ramp ? data->rampModelDir : data->cliffModelDir;
    snprintf(zBuffer, sizeof(zBuffer), "Doodads\\Terrain\\%s\\%s%s0.mdx", dir, dir, ccfg);
    /* Configuration letters alone alias different terrain families (Cliffs vs CityCliffs). */
    for (struct tCliff *it = g_cliffs; it; it = it->next) {
        if (!strcmp(it->name, zBuffer)) {
            if (!it->model && !it->warned) {
                fprintf(stderr, "WC3 renderer: unable to load cliff model %s\n", it->name);
#ifdef WC3_CLIFF_TESTS
                cliff_model_warning_count++;
#endif
                it->warned = true;
            }
            return it->model;
        }
    }
    struct tCliff *cliff = ri.MemAlloc(sizeof(struct tCliff));
    if (!cliff) {
        if (!cliff_warn_model_cache_alloc) fprintf(stderr, "WC3 renderer: unable to allocate model cache entry for %s; skipping model\n", zBuffer);
        cliff_warn_model_cache_alloc = true;
        return NULL;
    }
    PATHSTR scoped;
    strcpy(cliff->name, zBuffer);
    cliff->model = NULL;
    if (R_MapAssetCandidate(zBuffer, scoped, sizeof(scoped))) cliff->model = R_LoadModel(scoped);
    if (!cliff->model) cliff->model = R_LoadModel(zBuffer);
    cliff->warned = false;
    ADD_TO_LIST(cliff, g_cliffs);
    if (!cliff->model) {
        fprintf(stderr, "WC3 renderer: unable to load cliff model %s\n", cliff->name);
#ifdef WC3_CLIFF_TESTS
        cliff_model_warning_count++;
#endif
        cliff->warned = true;
    }
    return cliff->model;
}

static struct tCliff *R_FindCliffModel(cliffData_t const *data, char const *ccfg, bool ramp) {
    PATHSTR name;
    cstring_t dir = ramp ? data->rampModelDir : data->cliffModelDir;
    snprintf(name, sizeof(name), "Doodads\\Terrain\\%s\\%s%s0.mdx", dir, dir, ccfg);
    for (struct tCliff *it = g_cliffs; it; it = it->next) {
        if (!strcmp(it->name, name)) return it;
    }
    return NULL;
}

static void R_WarnCliffModelGeometry(cliffData_t const *data, char const *ccfg, bool ramp, char const *reason) {
    struct tCliff *it = R_FindCliffModel(data, ccfg, ramp);
    if (it && !it->warned) {
        fprintf(stderr, "WC3 renderer: cliff model %s %s\n", it->name, reason);
#ifdef WC3_CLIFF_TESTS
        cliff_model_warning_count++;
#endif
        it->warned = true;
    }
}

static texture_t const *R_LoadCliffTexture(uint32_t cliffID, char tileset, cliffData_t const *data) {
    PATHSTR buffer = { 0 };
    struct tCliffTexture *entry;

    for (struct tCliffTexture *it = g_cliff_textures; it; it = it->next) {
        if (it->cliffid == cliffID && it->tileset == tileset) {
            return it->texture;
        }
    }

    /* Tileset variants come from the "<tileset>.mpq" layer (R_GameAssetCandidate), not a filename prefix. */
    snprintf(buffer, sizeof(buffer), "%s\\%s.blp", data->texDir, data->texFile);

    entry = ri.MemAlloc(sizeof(*entry));
    if (!entry) {
        if (!cliff_warn_texture_cache_alloc) fprintf(stderr, "WC3 renderer: unable to allocate texture cache entry for %s; using uncached texture\n", buffer);
        cliff_warn_texture_cache_alloc = true;
        return R_LoadTexture(buffer);
    }
    entry->cliffid = cliffID;
    entry->tileset = tileset;
    entry->texture = R_LoadTexture(buffer);

    ADD_TO_LIST(entry, g_cliff_textures);
    return entry->texture;
}

/* Like SC2, only snap mesh edges that border emitted terrain, leaving stacked/internal faces intact. */
static bool R_CliffGroundJoin(war3map_t const *map, vec3_t *pos) {
    float gx = (pos->x - map->center.x) / TILE_SIZE, gy = (pos->y - map->center.y) / TILE_SIZE;
    int ix = (int)floorf(gx), iy = (int)floorf(gy);
    for (int y = iy - 1; y <= iy; y++) for (int x = ix - 1; x <= ix; x++) {
        if (x < 0 || y < 0 || x + 1 >= map->width || y + 1 >= map->height) continue;
        float u = gx - x, v = gy - y;
        if (u < -0.0001f || u > 1.0001f || v < -0.0001f || v > 1.0001f) continue;
        if (fabsf(u) > 0.0001f && fabsf(u-1) > 0.0001f && fabsf(v) > 0.0001f && fabsf(v-1) > 0.0001f) continue;
        war3mapVertex_t tile[4]; GetTileVertices(x, y, map, tile);
        if (!R_TileHasGround(tile)) continue;
        float low = LerpNumber(R_GetVertexPosition(map, x, y, true).z, R_GetVertexPosition(map, x+1, y, true).z, u);
        float high = LerpNumber(R_GetVertexPosition(map, x, y+1, true).z, R_GetVertexPosition(map, x+1, y+1, true).z, u);
        float z = LerpNumber(low, high, v);
        if (fabsf(pos->z - z) >= TILE_SIZE * 0.5f) continue;
        pos->z = z;
        return true;
    }
    return false;
}

static bool R_GrowCliffVertexSamples(uint32_t count) {
    vertex_t *samples;
    uint32_t *generation;
    uint32_t capacity;
    if (count <= cliff_vertex_sample_capacity) return true;
    capacity = MAX(64u, cliff_vertex_sample_capacity);
    while (capacity < count) {
        if (capacity > UINT32_MAX / 2) { capacity = count; break; }
        capacity *= 2;
    }
    if ((size_t)capacity > SIZE_MAX / sizeof(*samples) || (size_t)capacity > SIZE_MAX / sizeof(*generation))
        return false;
    samples = ri.MemAlloc((size_t)capacity * sizeof(*samples));
    generation = ri.MemAlloc((size_t)capacity * sizeof(*generation));
    if (!samples || !generation) {
        if (samples) ri.MemFree(samples);
        if (generation) ri.MemFree(generation);
        if (!cliff_warn_sample_alloc) {
            fprintf(stderr, "WC3 renderer: unable to allocate cliff vertex sample cache; baking without cache\n");
            cliff_warn_sample_alloc = true;
        }
        return false;
    }
    memset(generation, 0, (size_t)capacity * sizeof(*generation));
    SAFE_DELETE(cliff_vertex_samples, ri.MemFree);
    SAFE_DELETE(cliff_vertex_sample_generation, ri.MemFree);
    cliff_vertex_samples = samples;
    cliff_vertex_sample_generation = generation;
    cliff_vertex_sample_capacity = capacity;
    return true;
}

static uint32_t R_NextCliffVertexGeneration(void) {
    if (++cliff_vertex_generation == 0) {
        memset(cliff_vertex_sample_generation, 0,
            (size_t)cliff_vertex_sample_capacity * sizeof(*cliff_vertex_sample_generation));
        cliff_vertex_generation = 1;
    }
    return cliff_vertex_generation;
}

static vertex_t R_BakeCliffVertex(war3map_t const *map, mdxGeoset_t const *geoset,
                                  vec2_t const *offset, int baselevel, uint32_t index) {
    vec3_t pos = Matrix4_multiply_vector3(&r_cliff_axes, &geoset->vertices[index]);
    float const fx = pos.x + offset->x, fy = pos.y + offset->y;
    float const fh = GetAccurateHeightAtPoint(fx, fy);
    float const fw = GetAccurateWaterLevelAtPoint(fx, fy);
    float const fz = geoset->vertices[index].z + baselevel * TILE_SIZE + fh - HEIGHT_COR;
    float const dp = GetTileDepth(fw, fz);
    vec3_t fn = Matrix4_multiply_vector3(&r_cliff_axes, &geoset->normals[index]);
    vec3_t an = GetAccurateNormalAtPoint(fx, fy);
    vertex_t vertex = { 0 };
    vertex.color = MakeColor(dp, LerpNumber(dp, 1, 0.25), LerpNumber(dp, 1, 0.5), 1);
    vertex.position.x = map->center.x + fx;
    vertex.position.y = map->center.y + fy;
    vertex.position.z = fz;
    bool join = R_CliffGroundJoin(map, &vertex.position);
    vertex.texcoord = geoset->texcoord[index];
    vertex.normal = join && fn.z > 0 ? an : Vector3_mad(&(vec3_t){fn.x,fn.y,0}, fn.z, &an);
    Vector3_normalize(&vertex.normal);
    return vertex;
}

/* CliffTypes.slk writes "_" for an absent tile; a real tile ID has four characters. */
static bool R_CliffTileIsSet(uint32_t tile) {
    return (tile & 0xff) && ((tile >> 8) & 0xff) && ((tile >> 16) & 0xff) && (tile >> 24);
}

/* Map ground-palette index of a tile ID, or -1 when the map does not use that tile. */
static int R_CliffGroundIndex(war3map_t const *map, uint32_t tile) {
    if (!R_CliffTileIsSet(tile)) return -1;
    FOR_LOOP(i, map->num_grounds) if (map->grounds[i] == tile) return (int)i;
    return -1;
}

static void R_MakeCliff(war3map_t const *map, uint32_t x, uint32_t y, cliffData_t const *data) {
    struct War3MapVertex tile[4];
    GetTileVertices(x, y, map, tile);

    if (GetTileRamps(tile) == 4 || !IsTileCliff(tile) || R_CliffTexture(map, x, y) != data->cliff)
        return;

    char cliffcfg[5] = { 0 };
    /* Equal-height ramp flags mark the adjoining cliff, not a sloped transition (e.g. HABH). */
    bool const is_ramp = R_IsCliffRamp(tile);
    int const baselevel = TileBaseLevel(tile);

    FOR_LOOP(index, 4) {
        war3mapVertex_t const *vert = &tile[r_cliff_corners[index]];
        int const diff = vert->level - baselevel;
        if (diff == 0) {
            cliffcfg[index] = (is_ramp && vert->ramp) ? 'L' : 'A';
        } else if (diff == 1) {
            cliffcfg[index] = (is_ramp && vert->ramp) ? 'H' : 'B';
        } else {
            cliffcfg[index] = (is_ramp && vert->ramp) ? 'X' : 'C';
        }
    }
    
    model_t const *pModel = R_LoadCliffModel(data, cliffcfg, is_ramp);
    if (!pModel || pModel->modeltype != ID_MDLX || !pModel->mdx || !pModel->mdx->geosets) {
        if (pModel) R_WarnCliffModelGeometry(data, cliffcfg, is_ramp, "has an unsupported or incomplete model header");
        return;
    }
    mdxGeoset_t *pGeoset = pModel->mdx->geosets;
    if (!pGeoset->triangles || !pGeoset->vertices || !pGeoset->normals || !pGeoset->texcoord) {
        R_WarnCliffModelGeometry(data, cliffcfg, is_ramp, "has incomplete cliff geometry");
        return;
    }
    
    vec2_t offset = { x * TILE_SIZE, y * TILE_SIZE };
    
    if (is_ramp) {
        vec2_t shift = R_CliffRampOffset(tile, &pModel->mdx->bounds.box);
        offset = Vector2_add(&offset, &shift);
    }

    /* CliffTypes.slk groundTile is the low side's tile and upperTile ("_" = none) the high side's; only Outland's
     * abyss cliff (COrd: Oaby below, Osmb above) has both. Painting the abyss onto high corners turned plateau
     * rims into solid black tiles (issue #597). */
    bool const has_upper = R_CliffTileIsSet(data->upperTile);
    int const low_ground = R_CliffGroundIndex(map, data->groundTile);
    int const high_ground = has_upper ? R_CliffGroundIndex(map, data->upperTile) : low_ground;
    if (low_ground >= 0 || high_ground >= 0) {
        vec3_t span = Vector3_sub(&pModel->mdx->bounds.box.max, &pModel->mdx->bounds.box.min);
        int sx = offset.x / TILE_SIZE, sy = offset.y / TILE_SIZE;
        int nx = is_ramp && span.y > span.x ? 2 : 1, ny = is_ramp && span.x >= span.y ? 2 : 1;
        /* Ramp models cover two cells: their low-side corners need the same cliff ground texture. */
        for (int px = MAX(0, sx); px <= sx + nx && px < map->width; px++)
            for (int py = MAX(0, sy); py <= sy + ny && py < map->height; py++) {
                war3mapVertex_t *vert = (war3mapVertex_t *)GetWar3MapVertex(map, px, py);
                int const ground = vert->level > baselevel ? high_ground : low_ground;
                if (ground >= 0) vert->ground = ground;
            }
    }

    cliff_bake.current_group++;
    uint32_t const piece_first = cliff_bake.num_vertices;
    bool cache_samples = R_GrowCliffVertexSamples((uint32_t)pGeoset->num_vertices);
    uint32_t cache_generation = cache_samples ? R_NextCliffVertexGeneration() : 0;
    FOR_LOOP(t, pGeoset->num_triangles) {
        const int i = pGeoset->triangles[t];
        vertex_t sample;
        if (i < 0 || i >= pGeoset->num_vertices) {
            struct tCliff *cliff = R_FindCliffModel(data, (cstring_t)&cliffcfg, is_ramp);
            if (cliff && !cliff->warned) {
                fprintf(stderr, "WC3 renderer: cliff model %s has triangle index %d outside %d vertices; skipping malformed piece\n",
                        cliff->name, i, pGeoset->num_vertices);
#ifdef WC3_CLIFF_TESTS
                cliff_model_warning_count++;
#endif
                cliff->warned = true;
            }
            cliff_bake.num_vertices = piece_first;
            return;
        }
        if (cache_samples && cliff_vertex_sample_generation[i] == cache_generation) {
            sample = cliff_vertex_samples[i];
#ifdef WC3_CLIFF_TESTS
            cliff_vertex_cache_hit_count++;
#endif
        } else {
            sample = R_BakeCliffVertex(map, pGeoset, &offset, baselevel, (uint32_t)i);
            if (cache_samples) {
                cliff_vertex_samples[i] = sample;
                cliff_vertex_sample_generation[i] = cache_generation;
            }
#ifdef WC3_CLIFF_TESTS
            cliff_vertex_sample_count++;
#endif
        }
        vertex_t *output = R_CliffBakeVertex(&cliff_bake);
        if (!output) {
            cliff_bake.num_vertices = piece_first;
            return;
        }
        *output = sample;
    }
}

static maplayer_t *R_BuildMapSegmentCliffsInternal(war3map_t const *map, uint32_t sx, uint32_t sy,
                                                    uint32_t cliff, bool keep_layer) {
    uint32_t cliffID = map->cliffs[cliff];
    if (cliffID == NO_CLIFF) {
        return NULL;
    }

    maplayer_t *mapLayer = keep_layer ? ri.MemAlloc(sizeof(maplayer_t)) : NULL;
    if (keep_layer && !mapLayer) {
        if (!cliff_warn_layer_alloc) fprintf(stderr, "WC3 renderer: unable to allocate cliff layer for segment %u,%u; skipping layer\n", sx, sy);
        cliff_warn_layer_alloc = true;
        return NULL;
    }
    w3CliffType_t const *row = R_CliffType(cliffID);
    cliffData_t data = {
        .cliff = cliff,
        .texDir = row->texDir,
        .texFile = row->texFile,
        .groundTile = row->groundTile,
        .upperTile = row->upperTile,
        .rampModelDir = row->rampModelDir,
        .cliffModelDir = row->cliffModelDir,
    };
    uint32_t first = cliff_bake.num_vertices;
    for (uint32_t x = sx * SEGMENT_SIZE; x < (sx + 1) * SEGMENT_SIZE; x++) {
        for (uint32_t y = sy * SEGMENT_SIZE; y < (sy + 1) * SEGMENT_SIZE; y++) {
            R_MakeCliff(map, x, y, &data);
        }
    }
    if (!keep_layer || cliff_bake.num_vertices == first) {
        if (mapLayer) ri.MemFree(mapLayer);
        return NULL;
    }
    mapLayer->type = MAPLAYERTYPE_CLIFF;
    mapLayer->num_vertices = cliff_bake.num_vertices - first;
    if (!mapLayer->num_vertices) {
        ri.MemFree(mapLayer);
        return NULL;
    }
    mapLayer->texture = R_LoadCliffTexture(cliffID, map->tileset, &data);
    cliffLayer_t *pending = ri.MemAlloc(sizeof(*pending));
    if (!pending) {
        if (!cliff_warn_pending_alloc) fprintf(stderr, "WC3 renderer: unable to allocate pending layer for segment %u,%u; discarding layer\n", sx, sy);
        cliff_warn_pending_alloc = true;
        ri.MemFree(mapLayer);
        return NULL;
    }
    *pending = (cliffLayer_t){ .layer = mapLayer, .first = first };
    ADD_TO_LIST(pending, cliff_layers);
    return mapLayer;
}

maplayer_t *R_BuildMapSegmentCliffs(war3map_t const *map, uint32_t sx, uint32_t sy, uint32_t cliff) {
    return R_BuildMapSegmentCliffsInternal(map, sx, sy, cliff, true);
}

/* Emit neighboring cliff vertices for normal welding without creating a layer or GPU buffer. */
void R_BakeMapSegmentCliffsForWeld(war3map_t const *map, uint32_t sx, uint32_t sy, uint32_t cliff) {
    R_BuildMapSegmentCliffsInternal(map, sx, sy, cliff, false);
}

/* Weld before uploading any segment/material batch so their boundaries cannot retain lighting seams. */
void R_FinishCliffs(void) {
    R_CliffWeldNormals(&cliff_bake, 0.01f);
    while (cliff_layers) {
        cliffLayer_t *part = cliff_layers; cliff_layers = part->next;
        part->layer->buffer = R_MakeVertexArrayObject(cliff_bake.vertices + part->first, part->layer->num_vertices);
        ri.MemFree(part);
    }
    ri.MemFree(cliff_bake.vertices); ri.MemFree(cliff_bake.groups);
    memset(&cliff_bake, 0, sizeof(cliff_bake));
}
