#include "r_war3map.h"
#include "../mdx/r_mdx.h"
#include "renderer/r_cliff.h"

#define NO_CLIFF MAKEFOURCC('C','L','n','o')

static rCliffBakeList_t cliff_bake;
typedef struct CLIFFLAYER {
    LPMAPLAYER layer;
    DWORD first;
    struct CLIFFLAYER *next;
} CLIFFLAYER;
static CLIFFLAYER *cliff_layers;

VECTOR3 R_GetVertexNormal(LPCWAR3MAP map, DWORD x, DWORD y);

typedef struct {
    DWORD cliff;
    LPCSTR texDir;
    LPCSTR texFile;
    DWORD groundTile;
    DWORD upperTile;
    LPCSTR rampModelDir;
    LPCSTR cliffModelDir;
} cliffData_t;

struct tCliff {
    PATHSTR name;
    LPCMODEL model;
    struct tCliff *next;
};

static struct tCliff *g_cliffs = NULL;

struct tCliffTexture {
    DWORD cliffid;
    char tileset;
    LPCTEXTURE texture;
    struct tCliffTexture *next;
};

static struct tCliffTexture *g_cliff_textures = NULL; /* Per-map resolved cliff texture choices; renderer texture storage remains cache-owned. */

void R_ResetCliffCache(void) {
    while (g_cliffs) {
        struct tCliff *next = g_cliffs->next;
        if (g_cliffs->model) R_ReleaseModel((LPMODEL)g_cliffs->model);
        ri.MemFree(g_cliffs);
        g_cliffs = next;
    }
    while (g_cliff_textures) {
        struct tCliffTexture *next = g_cliff_textures->next;
        ri.MemFree(g_cliff_textures);
        g_cliff_textures = next;
    }
}

// HELPERS

static int TileBaseLevel(LPCWAR3MAPVERTEX tile) {
    DWORD minLevel = tile->level;
    FOR_LOOP(tileIndex, 4) {
        minLevel = MIN(minLevel, tile[tileIndex].level);
    }
    return minLevel;
}

static VECTOR3 GetAccurateNormalAtPoint(float sx, float sy) {
    float x = sx / TILE_SIZE;
    float y = sy / TILE_SIZE;
    float fx = floorf(x);
    float fy = floorf(y);
    VECTOR3 a = R_GetVertexNormal(tr.world, fx, fy);
    VECTOR3 b = R_GetVertexNormal(tr.world, fx + 1, fy);
    VECTOR3 c = R_GetVertexNormal(tr.world, fx, fy + 1);
    VECTOR3 d = R_GetVertexNormal(tr.world, fx + 1, fy + 1);
    VECTOR3 ab = Vector3_lerp(&a, &b, x - fx);
    VECTOR3 cd = Vector3_lerp(&c, &d, x - fx);
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
    return DECODE_HEIGHT(LerpNumber(ab, cd, y - fy));
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

static LPCMODEL R_LoadCliffModel(cliffData_t const *data, char const *ccfg, bool ramp) {
    PATHSTR zBuffer;
    LPCSTR dir = ramp ? data->rampModelDir : data->cliffModelDir;
    snprintf(zBuffer, sizeof(zBuffer), "Doodads\\Terrain\\%s\\%s%s0.mdx", dir, dir, ccfg);
    /* Configuration letters alone alias different terrain families (Cliffs vs CityCliffs). */
    for (struct tCliff *it = g_cliffs; it; it = it->next) {
        if (!strcmp(it->name, zBuffer))
            return it->model;
    }
    struct tCliff *cliff = ri.MemAlloc(sizeof(struct tCliff));
    PATHSTR scoped;
    strcpy(cliff->name, zBuffer);
    cliff->model = NULL;
    if (R_MapAssetCandidate(zBuffer, scoped, sizeof(scoped))) cliff->model = R_LoadModel(scoped);
    if (!cliff->model) cliff->model = R_LoadModel(zBuffer);
    ADD_TO_LIST(cliff, g_cliffs);
    return cliff->model;
}

static LPCTEXTURE R_LoadCliffTexture(DWORD cliffID, char tileset, cliffData_t const *data) {
    PATHSTR buffer = { 0 };

    for (struct tCliffTexture *it = g_cliff_textures; it; it = it->next) {
        if (it->cliffid == cliffID && it->tileset == tileset) {
            return it->texture;
        }
    }

    struct tCliffTexture *entry = ri.MemAlloc(sizeof(*entry));
    entry->cliffid = cliffID;
    entry->tileset = tileset;

    snprintf(buffer, sizeof(buffer), "%s\\%c_%s.blp", data->texDir, tileset, data->texFile);
    void *testbuf = NULL;
    if (ri.FS_ReadFile(buffer, &testbuf) >= 0) {
        ri.FS_FreeFile(testbuf);
        entry->texture = R_LoadTexture(buffer);
    } else {
        snprintf(buffer, sizeof(buffer), "%s\\%s.blp", data->texDir, data->texFile);
        entry->texture = R_LoadTexture(buffer);
    }

    ADD_TO_LIST(entry, g_cliff_textures);
    return entry->texture;
}

/* Like SC2, only snap mesh edges that border emitted terrain, leaving stacked/internal faces intact. */
static BOOL R_CliffGroundJoin(LPCWAR3MAP map, LPVECTOR3 pos) {
    FLOAT gx = (pos->x - map->center.x) / TILE_SIZE, gy = (pos->y - map->center.y) / TILE_SIZE;
    int ix = (int)floorf(gx), iy = (int)floorf(gy);
    for (int y = iy - 1; y <= iy; y++) for (int x = ix - 1; x <= ix; x++) {
        if (x < 0 || y < 0 || x + 1 >= map->width || y + 1 >= map->height) continue;
        FLOAT u = gx - x, v = gy - y;
        if (u < -0.0001f || u > 1.0001f || v < -0.0001f || v > 1.0001f) continue;
        if (fabsf(u) > 0.0001f && fabsf(u-1) > 0.0001f && fabsf(v) > 0.0001f && fabsf(v-1) > 0.0001f) continue;
        WAR3MAPVERTEX tile[4]; GetTileVertices(x, y, map, tile);
        if (!R_TileHasGround(tile)) continue;
        FLOAT low = LerpNumber(R_GetVertexPosition(map, x, y, true).z, R_GetVertexPosition(map, x+1, y, true).z, u);
        FLOAT high = LerpNumber(R_GetVertexPosition(map, x, y+1, true).z, R_GetVertexPosition(map, x+1, y+1, true).z, u);
        FLOAT z = LerpNumber(low, high, v);
        if (fabsf(pos->z - z) >= TILE_SIZE * 0.5f) continue;
        pos->z = z;
        return true;
    }
    return false;
}

static void R_MakeCliff(LPCWAR3MAP map, DWORD x, DWORD y, cliffData_t const *data) {
    struct War3MapVertex tile[4];
    GetTileVertices(x, y, map, tile);

    if (GetTileRamps(tile) == 4 || !IsTileCliff(tile) || R_CliffTexture(map, x, y) != data->cliff)
        return;

    char cliffcfg[5] = { 0 };
    /* Equal-height ramp flags mark the adjoining cliff, not a sloped transition (e.g. HABH). */
    bool const is_ramp = R_IsCliffRamp(tile);
    int const baselevel = TileBaseLevel(tile);

    FOR_LOOP(index, 4) {
        LPCWAR3MAPVERTEX vert = &tile[r_cliff_corners[index]];
        int const diff = vert->level - baselevel;
        if (diff == 0) {
            cliffcfg[index] = (is_ramp && vert->ramp) ? 'L' : 'A';
        } else if (diff == 1) {
            cliffcfg[index] = (is_ramp && vert->ramp) ? 'H' : 'B';
        } else {
            cliffcfg[index] = (is_ramp && vert->ramp) ? 'X' : 'C';
        }
    }
    
    LPCMODEL pModel = R_LoadCliffModel(data, cliffcfg, is_ramp);
    if (!pModel || pModel->modeltype != ID_MDLX || !pModel->mdx || !pModel->mdx->geosets) {
        fprintf(stderr, "Model %.4s not found\n", (LPCSTR)&cliffcfg);
        return;
    }
    mdxGeoset_t *pGeoset = pModel->mdx->geosets;
    if (!pGeoset->triangles || !pGeoset->vertices || !pGeoset->normals || !pGeoset->texcoord) {
        fprintf(stderr, "Model %.4s has incomplete cliff geometry\n", (LPCSTR)&cliffcfg);
        return;
    }
    
    VECTOR2 offset = { x * TILE_SIZE, y * TILE_SIZE };
    
    if (is_ramp) {
        VECTOR2 shift = R_CliffRampOffset(tile, &pModel->mdx->bounds.box);
        offset = Vector2_add(&offset, &shift);
    }

    DWORD const ground_key = data->groundTile ? data->groundTile : data->upperTile;
    if (ground_key) {
        VECTOR3 span = Vector3_sub(&pModel->mdx->bounds.box.max, &pModel->mdx->bounds.box.min);
        int sx = offset.x / TILE_SIZE, sy = offset.y / TILE_SIZE;
        int nx = is_ramp && span.y > span.x ? 2 : 1, ny = is_ramp && span.x >= span.y ? 2 : 1;
        FOR_LOOP(gindx, map->num_grounds) {
            if (map->grounds[gindx] != ground_key) continue;
            /* Ramp models cover two cells: their low-side corners need the same cliff ground texture. */
            for (int px = MAX(0, sx); px <= sx + nx && px < map->width; px++)
                for (int py = MAX(0, sy); py <= sy + ny && py < map->height; py++)
                    ((LPWAR3MAPVERTEX)GetWar3MapVertex(map, px, py))->ground = gindx;
            break;
        }
    }

    cliff_bake.current_group++;
    FOR_LOOP(t, pGeoset->num_triangles) {
        const int i = pGeoset->triangles[t];
        VECTOR3 pos = Matrix4_multiply_vector3(&r_cliff_axes, &pGeoset->vertices[i]);
        const float fx = pos.x + offset.x;
        const float fy = pos.y + offset.y;
        const float fh = GetAccurateHeightAtPoint(fx, fy);
        const float fw = GetAccurateWaterLevelAtPoint(fx, fy);
        const float fz = pGeoset->vertices[i].z + baselevel * TILE_SIZE + fh - HEIGHT_COR;
        const float dp = GetTileDepth(fw, fz);
        struct vertex *v = R_CliffBakeVertex(&cliff_bake);
        VECTOR3 fn = Matrix4_multiply_vector3(&r_cliff_axes, &pGeoset->normals[i]);
        VECTOR3 an = GetAccurateNormalAtPoint(fx, fy);
        v->color = MakeColor(dp, LerpNumber(dp, 1, 0.25), LerpNumber(dp, 1, 0.5), 1);
        v->position.x = map->center.x + fx;
        v->position.y = map->center.y + fy;
        v->position.z = fz;
        BOOL join = R_CliffGroundJoin(map, &v->position);
        v->texcoord = pGeoset->texcoord[i];
        v->normal = join && fn.z > 0 ? an : Vector3_mad(&(VECTOR3){fn.x,fn.y,0}, fn.z, &an);
        Vector3_normalize(&v->normal);
    }
}

LPMAPLAYER R_BuildMapSegmentCliffs(LPCWAR3MAP map, DWORD sx, DWORD sy, DWORD cliff) {
    DWORD cliffID = map->cliffs[cliff];
    if (cliffID == NO_CLIFF) {
        return NULL;
    }

    LPMAPLAYER mapLayer = ri.MemAlloc(sizeof(MAPLAYER));
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
    mapLayer->type = MAPLAYERTYPE_CLIFF;
    DWORD first = cliff_bake.num_vertices;
    for (DWORD x = sx * SEGMENT_SIZE; x < (sx + 1) * SEGMENT_SIZE; x++) {
        for (DWORD y = sy * SEGMENT_SIZE; y < (sy + 1) * SEGMENT_SIZE; y++) {
            R_MakeCliff(map, x, y, &data);
        }
    }
    mapLayer->num_vertices = cliff_bake.num_vertices - first;
    if (!mapLayer->num_vertices) {
        ri.MemFree(mapLayer);
        return NULL;
    }
    mapLayer->texture = R_LoadCliffTexture(cliffID, map->tileset, &data);
    CLIFFLAYER *pending = ri.MemAlloc(sizeof(*pending));
    *pending = (CLIFFLAYER){ .layer = mapLayer, .first = first };
    ADD_TO_LIST(pending, cliff_layers);
    return mapLayer;
}

/* Weld before uploading any segment/material batch so their boundaries cannot retain lighting seams. */
void R_FinishCliffs(void) {
    R_CliffWeldNormals(&cliff_bake, 0.01f);
    while (cliff_layers) {
        CLIFFLAYER *part = cliff_layers; cliff_layers = part->next;
        part->layer->buffer = R_MakeVertexArrayObject(cliff_bake.vertices + part->first, part->layer->num_vertices);
        ri.MemFree(part);
    }
    ri.MemFree(cliff_bake.vertices); ri.MemFree(cliff_bake.groups);
    memset(&cliff_bake, 0, sizeof(cliff_bake));
}
