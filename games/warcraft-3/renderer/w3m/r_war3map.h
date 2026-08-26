#ifndef __r_war3map_h__
#define __r_war3map_h__

#include "renderer/r_local.h"
#include "r_terrain_layers.h"

LPMAPLAYER R_BuildMapSegmentLayer(LPCWAR3MAP map, DWORD sx, DWORD sy, DWORD layer);
LPMAPLAYER R_BuildGroundLayerGlobal(LPCWAR3MAP map, DWORD layer);
LPMAPLAYER R_BuildMapSegmentCliffs(LPCWAR3MAP map, DWORD sx, DWORD sy, DWORD cliff);
LPMAPLAYER R_BuildMapSegmentWater(LPCWAR3MAP map, DWORD sx, DWORD sy);

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
