#ifndef __r_war3map_h__
#define __r_war3map_h__

#include "renderer/r_local.h"
#include "renderer/r_camera_height.h"
#include "games/warcraft-3/common/terrain.h"
#include "r_terrain_layers.h"

#define BZ_WC3_NO_CLIFF_TEXTURE 15 // index; no explicit W3E corner texture; resolved from neighbouring vertices

static const uint8_t r_cliff_corners[] = { 1, 0, 2, 3 }; /* Native MDX configuration: NW,NE,SE,SW. */
/* Retail rotates cliff geometry -90 degrees; selecting a rotated filename does not preserve authored UVs/shape. */
static const mat4_t r_cliff_axes = { .v = {0,-1,0,0, 1,0,0,0, 0,0,1,0, 0,0,0,1} };

maplayer_t *R_BuildGroundLayerGlobal(war3map_t const *map, uint32_t layer);
void R_UpdateGroundSegment(war3map_t const *map, uint32_t sx, uint32_t sy);
maplayer_t *R_BuildMapSegmentCliffs(war3map_t const *map, uint32_t sx, uint32_t sy, uint32_t cliff);
void R_BakeMapSegmentCliffsForWeld(war3map_t const *map, uint32_t sx, uint32_t sy, uint32_t cliff);
maplayer_t *R_BuildMapSegmentWater(war3map_t const *map, uint32_t sx, uint32_t sy);
void R_ResetGroundTextures(void);
void R_BeginTerrainNormalCache(war3map_t const *map);
void R_EndTerrainNormalCache(void);
void R_ResetTerrainNormalCache(void);
void R_ResetCliffCache(void);
/* Visual-only terrain replacement cells covered by special war3map.doo doodads. */
bool R_W3SpecialCellCovered(war3map_t const *map, uint32_t x, uint32_t y);
void R_W3LoadSpecialCutouts(handle_t archive, war3map_t const *map);
void R_W3ClearSpecialCutouts(void);
void R_W3InvalidateSpecialModels(void);
struct mdxModel_s const *R_W3SpecialModel(renderEntity_t const *entity, mat4_t const *transform);
/* Resolve MDX replaceable texture 11 from the local W3E cliff palette. */
texture_t const *R_W3CliffReplacementAt(float world_x, float world_y);
void R_FinishCliffs(void);
vec3_t R_GetVertexPosition(war3map_t const *map, uint32_t x, uint32_t y, bool useLevel);
vec3_t R_GetVertexNormal(war3map_t const *map, uint32_t x, uint32_t y);
void R_ResetBlightCache(void);
void R_LoadBlightTexture(uint8_t tileset);
texture_t const *R_BlightTexture(void);
#define WC3_MAX_WATER_FRAMES 64 // textures; Water.slk numTex is 45 for stock water, 1 for Outland's Abyss
/* One tileset's Water.slk "<tileset>Sha" row: animated surface frames and the shallow/deep depth tint bands. */
typedef struct {
    texture_t const *frames[WC3_MAX_WATER_FRAMES];
    uint32_t num_frames;
    float frame_rate; /* frames per second (texRate) */
    color32_t shallow_min, shallow_max, deep_min, deep_max;
    float height; /* tiles below the W3E water level; see W3_WaterSurfaceHeight */
} wc3WaterStyle_t;
void R_LoadWaterStyle(uint8_t tileset);
texture_t const *R_WaterFrame(wc3WaterStyle_t const *style, uint32_t time_ms);
void R_W3OpenTilesetArchive(uint8_t tileset);
wc3WaterStyle_t const *R_WaterStyle(void);
color32_t R_WaterDepthColor(wc3WaterStyle_t const *style, float depth);
void R_UpdateBlightLayer(void);
void R_InvalidateBlightLayer(void);
void R_DrawBlightLayer(void);
void _W3M_ClearMap(void);
float R_W3CameraHeightAtPoint(float x, float y);
float R_W3TerrainHeightAtPoint(float x, float y);
void R_W3StartTerrainDeformation(terrainDeform_t const *deformation);
void R_W3StopTerrainDeformation(uint32_t id, uint32_t fade_ms);
void R_W3StopAllTerrainDeformations(void);
void R_W3UpdateTerrainDeformations(void);
float R_W3TerrainOffsetAtPoint(float x, float y);

vec2_t GetWar3MapPosition(war3map_t const *war3Map, float x, float y);
float GetTileDepth(float waterlevel, float height);
struct color32 MakeColor(float r, float g, float b, float a);
war3mapVertex_t const *GetWar3MapVertex(war3map_t const *terrain, uint32_t x, uint32_t y);
uint32_t GetTile(war3mapVertex_t const *mv, uint32_t ground);
float GetWar3MapVertexHeight(war3mapVertex_t const *vert);
float GetWar3MapVertexWaterLevel(war3mapVertex_t const *vert);
void GetTileVertices(uint32_t x, uint32_t y, war3map_t const *terrain, war3mapVertex_t *vertices);
void SetTileUV(war3mapVertex_t const *mv, uint32_t tile, vertex_t *vertices, texture_t const *texture);
uint32_t GetTileRamps(war3mapVertex_t const *vertices);
uint32_t IsTileCliff(war3mapVertex_t const *vertices);
uint32_t IsTileWater(war3mapVertex_t const *vertices);

/* Retail checks SW,SE,NW,NE, then an X-major 5x5 neighbourhood. A fixed slot turns Undead04 grass into dirt. */
static inline uint32_t R_CliffTexture(war3map_t const *map, int x, int y) {
    FOR_LOOP(i, 4) {
        uint32_t cliff = GetWar3MapVertex(map, x + (i & 1), y + (i >> 1))->cliff;
        if (cliff != BZ_WC3_NO_CLIFF_TEXTURE) return cliff;
    }
    for (int cx = MAX(0, x-2); cx <= x+2 && cx < map->width; cx++)
        for (int cy = MAX(0, y-2); cy <= y+2 && cy < map->height; cy++) {
            uint32_t cliff = GetWar3MapVertex(map, cx, cy)->cliff;
            if (cliff != BZ_WC3_NO_CLIFF_TEXTURE) return cliff;
        }
    /* Retail's explicit error case: no authored type in range uses slot zero, never slot one. */
    fprintf(stderr, "WC3: no cliff type near cell (%d,%d); using cliff slot 0\n", x, y);
    return 0;
}

/* Extend the two-cell MDX footprint into the low neighbour omitted by the ground baker. */
static inline vec2_t R_CliffRampOffset(war3mapVertex_t const *tile, box3_t const *box) {
    vec3_t span = Vector3_sub(&box->max, &box->min);
    if (span.y > span.x) {
        /* After the native -90 degree rotation both ramp axes span [0,256]; extend toward the low side. */
        return (vec2_t){ .x = tile[3].level + tile[1].level < tile[2].level + tile[0].level ? -TILE_SIZE : 0 };
    }
    return (vec2_t){ .y = tile[3].level + tile[2].level < tile[1].level + tile[0].level ? -TILE_SIZE : 0 };
}

/* Transition models join two adjacent ramp corners one cliff level apart; tile order is NE,NW,SE,SW. */
static inline bool R_IsCliffRamp(war3mapVertex_t const *tile) {
    static const uint8_t next[] = { 1, 3, 0, 2 };
    if (tile[0].ramp + tile[1].ramp + tile[2].ramp + tile[3].ramp != 2) return false;
    FOR_LOOP(i, 4)
        if (tile[i].ramp && tile[next[i]].ramp && abs((int)tile[i].level - tile[next[i]].level) == 1) return true;
    return false;
}

/* Ground, splats and cliff joins must agree on the cells owned by transition meshes. */
static inline bool R_TileHasGround(war3map_t const *map, int x, int y) {
    war3mapVertex_t tile[4]; GetTileVertices(x, y, map, tile);
    uint32_t ramps = GetTileRamps(tile), mid = 0;
    FOR_LOOP(i, 4) mid += tile[i].ramp && tile[i].cliffVariation;
    if (IsTileCliff(tile) && ramps < 4) return false;
    if (ramps != 2 || mid != 1) return true;
    /* A mid-ramp corner can touch an unrelated flat cell (NightElfX04). Only omit the low cell
     * when an adjacent transition actually extends into it; the old corner-only test left holes. */
    static const int step[][2] = { {-1,0}, {1,0}, {0,-1}, {0,1} };
    FOR_LOOP(i, 4) {
        int nx = x + step[i][0], ny = y + step[i][1];
        if (nx < 0 || ny < 0 || nx + 1 >= map->width || ny + 1 >= map->height) continue;
        war3mapVertex_t near[4]; GetTileVertices(nx, ny, map, near);
        if (!IsTileCliff(near) || !R_IsCliffRamp(near)) continue;
        bool alongx = near[0].ramp == near[1].ramp;
        int dx = alongx ? (near[3].level + near[1].level < near[2].level + near[0].level ? -1 : 1) : 0;
        int dy = alongx ? 0 : (near[3].level + near[2].level < near[1].level + near[0].level ? -1 : 1);
        if (nx + dx == x && ny + dy == y) return false;
    }
    return true;
}

/* Preserve every corner of a cliff/transition footprint, including the low ramp neighbour. */
static inline bool R_CliffOwnsCorner(war3map_t const *map, int x, int y) {
    for (int cy = MAX(0, y-1); cy <= y && cy + 1 < map->height; cy++)
        for (int cx = MAX(0, x-1); cx <= x && cx + 1 < map->width; cx++) {
            if (!R_TileHasGround(map, cx, cy)) return true;
        }
    return false;
}

vec2_t GetWar3MapSize(war3map_t const *war3Map);

#endif
