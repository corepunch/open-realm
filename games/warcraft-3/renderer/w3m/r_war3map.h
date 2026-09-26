#ifndef __r_war3map_h__
#define __r_war3map_h__

#include "renderer/r_local.h"
#include "renderer/r_camera_height.h"
#include "games/warcraft-3/common/terrain.h"
#include "r_terrain_layers.h"

#define BZ_WC3_NO_CLIFF_TEXTURE 15 // index; no explicit W3E corner texture; resolved from neighbouring vertices

static const uint8_t r_cliff_corners[] = { 1, 0, 2, 3 }; /* Native MDX configuration: NW,NE,SE,SW. */
/* Retail rotates cliff geometry -90 degrees; selecting a rotated filename does not preserve authored UVs/shape. */
static const MATRIX4 r_cliff_axes = { .v = {0,-1,0,0, 1,0,0,0, 0,0,1,0, 0,0,0,1} };

LPMAPLAYER R_BuildMapSegmentLayer(LPCWAR3MAP map, uint32_t sx, uint32_t sy, uint32_t layer);
LPMAPLAYER R_BuildGroundLayerGlobal(LPCWAR3MAP map, uint32_t layer);
LPMAPLAYER R_BuildMapSegmentCliffs(LPCWAR3MAP map, uint32_t sx, uint32_t sy, uint32_t cliff);
LPMAPLAYER R_BuildMapSegmentWater(LPCWAR3MAP map, uint32_t sx, uint32_t sy);
void R_ResetGroundTextures(void);
void R_ResetCliffCache(void);
void R_FinishCliffs(void);
VECTOR3 R_GetVertexPosition(LPCWAR3MAP map, uint32_t x, uint32_t y, bool useLevel);
void R_ResetBlightCache(void);
void R_LoadBlightTexture(uint8_t tileset);
LPCTEXTURE R_BlightTexture(void);
void R_UpdateBlightLayer(void);
void R_DrawBlightLayer(void);
void _W3M_ClearMap(void);
float R_W3CameraHeightAtPoint(float x, float y);
float R_W3TerrainHeightAtPoint(float x, float y);

VECTOR2 GetWar3MapPosition(LPCWAR3MAP war3Map, float x, float y);
float GetTileDepth(float waterlevel, float height);
struct color32 MakeColor(float r, float g, float b, float a);
LPCWAR3MAPVERTEX GetWar3MapVertex(LPCWAR3MAP terrain, uint32_t x, uint32_t y);
uint32_t GetTile(LPCWAR3MAPVERTEX mv, uint32_t ground);
float GetWar3MapVertexHeight(LPCWAR3MAPVERTEX vert);
float GetWar3MapVertexWaterLevel(LPCWAR3MAPVERTEX vert);
void GetTileVertices(uint32_t x, uint32_t y, LPCWAR3MAP terrain, LPWAR3MAPVERTEX vertices);
void SetTileUV(LPCWAR3MAPVERTEX mv, uint32_t tile, LPVERTEX vertices, LPCTEXTURE texture);
uint32_t GetTileRamps(LPCWAR3MAPVERTEX vertices);
uint32_t IsTileCliff(LPCWAR3MAPVERTEX vertices);
uint32_t IsTileWater(LPCWAR3MAPVERTEX vertices);

/* Retail checks SW,SE,NW,NE, then an X-major 5x5 neighbourhood. A fixed slot turns Undead04 grass into dirt. */
static inline uint32_t R_CliffTexture(LPCWAR3MAP map, int x, int y) {
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
static inline VECTOR2 R_CliffRampOffset(LPCWAR3MAPVERTEX tile, LPCBOX3 box) {
    VECTOR3 span = Vector3_sub(&box->max, &box->min);
    if (span.y > span.x) {
        /* After the native -90 degree rotation both ramp axes span [0,256]; extend toward the low side. */
        return (VECTOR2){ .x = tile[3].level + tile[1].level < tile[2].level + tile[0].level ? -TILE_SIZE : 0 };
    }
    return (VECTOR2){ .y = tile[3].level + tile[2].level < tile[1].level + tile[0].level ? -TILE_SIZE : 0 };
}

/* Transition models join two adjacent ramp corners one cliff level apart; tile order is NE,NW,SE,SW. */
static inline bool R_IsCliffRamp(LPCWAR3MAPVERTEX tile) {
    static const uint8_t next[] = { 1, 3, 0, 2 };
    if (tile[0].ramp + tile[1].ramp + tile[2].ramp + tile[3].ramp != 2) return false;
    FOR_LOOP(i, 4)
        if (tile[i].ramp && tile[next[i]].ramp && abs((int)tile[i].level - tile[next[i]].level) == 1) return true;
    return false;
}

/* Ground, splats and cliff joins must agree on the cells owned by transition meshes. */
static inline bool R_TileHasGround(LPCWAR3MAPVERTEX tile) {
    uint32_t ramps = GetTileRamps(tile), mid = 0;
    FOR_LOOP(i, 4) mid += tile[i].ramp && tile[i].cliffVariation;
    return !(IsTileCliff(tile) && ramps < 4) && !(ramps == 2 && mid == 1);
}

/* Preserve every corner of a cliff/transition footprint, including the low ramp neighbour. */
static inline bool R_CliffOwnsCorner(LPCWAR3MAP map, int x, int y) {
    for (int cy = MAX(0, y-1); cy <= y && cy + 1 < map->height; cy++)
        for (int cx = MAX(0, x-1); cx <= x && cx + 1 < map->width; cx++) {
            WAR3MAPVERTEX tile[4]; GetTileVertices(cx, cy, map, tile);
            if (!R_TileHasGround(tile)) return true;
        }
    return false;
}

VECTOR2 GetWar3MapSize(LPCWAR3MAP war3Map);

#endif
