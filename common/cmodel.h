#ifndef war3map_h
#define war3map_h

#include "common.h"

/* cmodel.h is included by common.h before common.h reaches its own edict
 * forward declaration, so declare the tag here before using it in prototypes. */
struct edict_s;

typedef struct {
    LPCVECTOR2 from, target;
    FLOAT radius;
} pathAccelParams_t;

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
DWORD CM_GetMapChecksum(void);
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
BOOL CM_ClosestReachablePointForRadius(LPCVECTOR2 from, LPCVECTOR2 target, FLOAT radius, LPVECTOR2 out);
BOOL CM_PointIsPathableForRadius(LPCVECTOR2 location, FLOAT radius);
BOOL CM_LineIsWalkable(LPCVECTOR2 a, LPCVECTOR2 b);
BOOL CM_GetPathingFlagsAt(LPCVECTOR2 location, LPBYTE flags);
BOOL CM_LineIsWalkableForRadius(LPCVECTOR2 a, LPCVECTOR2 b, FLOAT radius);
BOOL CM_FindPathWaypoint(pathAccelParams_t const *params, LPVECTOR2 out);
BOOL CM_FindDirectApproachPointForRadius(LPCVECTOR2 from, LPCVECTOR2 target, FLOAT range, FLOAT radius, LPVECTOR2 out);
FLOAT CM_PathCellWorldSize(void);
DWORD CM_RequestHeatmapForRadius(struct edict_s *goalentity, FLOAT radius);
void CM_ProcessPathJobs(DWORD work_budget);
BOOL CM_FindApproachPointToFootprintForRadius(struct edict_s const *target, LPCVECTOR2 from, FLOAT range, FLOAT radius, LPVECTOR2 out);
/* Distance from a world point to the target entity's authored no-walk
 * pathing footprint. Returns FLT_MAX when the target has no usable footprint. */
FLOAT CM_DistanceToPathingFootprint(struct edict_s const *target, LPCVECTOR2 point);
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
BOOL CM_WowMoveBlocked(LPCVECTOR3 from, LPCVECTOR3 to);
BOOL CM_WowRayTriangle(LPCVECTOR3 start, LPCVECTOR3 end, LPCVECTOR3 a, LPCVECTOR3 b, LPCVECTOR3 c, FLOAT *fraction);
#ifdef BZ_TESTS
BOOL CM_WowTestBspRay(LPCVECTOR3 start, LPCVECTOR3 end, FLOAT *fraction);
BOOL CM_WowTestWallRay(BOOL wall);
#endif
#endif
void CM_BakeStaticObstacles(void);
void CM_InvalidatePathCache(void);
void CM_SetupPathMap(DWORD width, DWORD height, BYTE const *cells);

#endif
