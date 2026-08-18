#ifndef war3map_h
#define war3map_h

#include "common.h"

struct War3MapVertex {
    USHORT accurate_height;
    USHORT waterlevel;
    BYTE mapedge;
    BYTE ground;
    BYTE ramp;
    BYTE blight;
    BYTE water;
    BYTE boundary;
    BYTE groundVariation;
    BYTE cliffVariation; // used also to mark mid-ramp
    BYTE level;
    BYTE cliff;
};

struct war3map {
    DWORD header;
    DWORD version;
    BYTE tileset;
    DWORD custom;
    LPDWORD grounds;
    LPDWORD cliffs;
    VECTOR2 center;
    DWORD width;
    DWORD height;
    HANDLE vertices;
    DWORD num_grounds;
    DWORD num_cliffs;
};

bool CM_LoadMap(LPCSTR mapFilename);
BOOL CM_IsMapLoaded(LPCSTR mapFilename);
float CM_GetHeightAtPoint(float sx, float sy);
LPDOODAD CM_GetDoodads(void);
//LPCMAPPLAYER CM_GetPlayer(DWORD index);
DWORD CM_GetLocalPlayerNumber(void);
LPCMAPINFO CM_GetMapInfo(void);
VECTOR2 CM_GetNormalizedMapPosition(float x, float y);
VECTOR2 CM_GetDenormalizedMapPosition(float x, float y);
BOOL CM_ClosestPathablePoint(LPCVECTOR2 location, LPVECTOR2 out);
BOOL CM_ClosestPathablePointForRadius(LPCVECTOR2 location, FLOAT radius, LPVECTOR2 out);
BOOL CM_PointIsPathableForRadius(LPCVECTOR2 location, FLOAT radius);
BOOL CM_LineIsWalkable(LPCVECTOR2 a, LPCVECTOR2 b);
BOX2 CM_GetWorldBounds(void);

/* WoW-only: all WorldSafeLocs entries for the current map.  Populated during
 * CM_LoadMap; null until a WoW map is loaded.  Callers must not free. */
#ifdef WOW
#define WOW_ADT_SIZE 533.333313f
#define WOW_ADT_TILES 64
static inline VECTOR3 CM_WowObjectPoint(FLOAT x, FLOAT y, FLOAT z) {
    return (VECTOR3){ WOW_ADT_TILES * 0.5f * WOW_ADT_SIZE - z, WOW_ADT_TILES * 0.5f * WOW_ADT_SIZE - x, y };
}
DWORD CM_WowGetMapId(void);
DWORD CM_WowGetAllSpawnCount(void);
LPCVECTOR3 CM_WowGetSpawnPos(DWORD index);
LPCSTR CM_WowGetSpawnName(DWORD index);
LPCSTR CM_WowAdtPath(int tile_x, int tile_y, LPSTR out, DWORD out_size);
FLOAT CM_WowFloorHeight(FLOAT x, FLOAT y, FLOAT ref_z, FLOAT step_up);
BOOL CM_WowRayTriangle(LPCVECTOR3 start, LPCVECTOR3 end, LPCVECTOR3 a, LPCVECTOR3 b, LPCVECTOR3 c, FLOAT *fraction);
#ifdef BZ_TESTS
BOOL CM_WowTestBspRay(LPCVECTOR3 start, LPCVECTOR3 end, FLOAT *fraction);
#endif
#endif
void CM_BakeStaticObstacles(void);
void CM_InvalidatePathCache(void);
void CM_SetupPathMap(DWORD width, DWORD height, BYTE const *cells);

#endif
