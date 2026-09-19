#ifndef __r_war3map_h__
#define __r_war3map_h__

#include "renderer/r_local.h"
#include "renderer/r_camera_height.h"
#include "games/warcraft-3/common/terrain.h"
#include "r_terrain_layers.h"

#define BZ_WC3_NO_CLIFF_TEXTURE 15 // index; W3E's non-cliff corner sentinel; excluded from cliff texture selection

static const BYTE r_cliff_corners[] = { 1, 0, 2, 3 }; /* Native MDX configuration: NW,NE,SE,SW. */
/* Retail rotates cliff geometry -90 degrees; selecting a rotated filename does not preserve authored UVs/shape. */
static const MATRIX4 r_cliff_axes = { .v = {0,-1,0,0, 1,0,0,0, 0,0,1,0, 0,0,0,1} };

LPMAPLAYER R_BuildMapSegmentLayer(LPCWAR3MAP map, DWORD sx, DWORD sy, DWORD layer);
LPMAPLAYER R_BuildGroundLayerGlobal(LPCWAR3MAP map, DWORD layer);
LPMAPLAYER R_BuildMapSegmentCliffs(LPCWAR3MAP map, DWORD sx, DWORD sy, DWORD cliff);
LPMAPLAYER R_BuildMapSegmentWater(LPCWAR3MAP map, DWORD sx, DWORD sy);
void R_ResetGroundTextures(void);
void R_ResetCliffCache(void);
void R_ResetBlightCache(void);
void R_RenderBlightMask(void);
void R_UpdateBlightLayer(void);
void R_DrawBlightLayer(void);
void R_ResetBlightLayer(void);
void _W3M_ClearMap(void);
FLOAT R_W3CameraHeightAtPoint(FLOAT x, FLOAT y);
FLOAT R_W3TerrainHeightAtPoint(FLOAT x, FLOAT y);

VECTOR2 GetWar3MapPosition(LPCWAR3MAP war3Map, float x, float y);
float GetTileDepth(float waterlevel, float height);
struct color32 MakeColor(float r, float g, float b, float a);
LPCWAR3MAPVERTEX GetWar3MapVertex(LPCWAR3MAP terrain, DWORD x, DWORD y);
DWORD GetTile(LPCWAR3MAPVERTEX mv, DWORD ground);
float GetWar3MapVertexHeight(LPCWAR3MAPVERTEX vert);
float GetWar3MapVertexWaterLevel(LPCWAR3MAPVERTEX vert);
void GetTileVertices(DWORD x, DWORD y, LPCWAR3MAP terrain, LPWAR3MAPVERTEX vertices);
void SetTileUV(LPCWAR3MAPVERTEX mv, DWORD tile, LPVERTEX vertices, LPCTEXTURE texture);
DWORD GetTileRamps(LPCWAR3MAPVERTEX vertices);
DWORD IsTileCliff(LPCWAR3MAPVERTEX vertices);
DWORD IsTileWater(LPCWAR3MAPVERTEX vertices);

/* A non-cliff SW corner must not discard the face: use the first authored cliff in SW,NW,NE,SE order. */
static inline DWORD R_CliffTexture(LPCWAR3MAPVERTEX tile) {
    static const BYTE order[] = { 3, 1, 0, 2 }; /* Texture priority is independent of model corner order. */
    FOR_LOOP(i, 4)
        if (tile[order[i]].cliff != BZ_WC3_NO_CLIFF_TEXTURE)
            return tile[order[i]].cliff;
    return BZ_WC3_NO_CLIFF_TEXTURE;
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
static inline BOOL R_IsCliffRamp(LPCWAR3MAPVERTEX tile) {
    static const BYTE next[] = { 1, 3, 0, 2 };
    if (tile[0].ramp + tile[1].ramp + tile[2].ramp + tile[3].ramp != 2) return false;
    FOR_LOOP(i, 4)
        if (tile[i].ramp && tile[next[i]].ramp && abs((int)tile[i].level - tile[next[i]].level) == 1) return true;
    return false;
}

VECTOR2 GetWar3MapSize(LPCWAR3MAP war3Map);

#endif
