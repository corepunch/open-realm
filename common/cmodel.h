#ifndef war3map_h
#define war3map_h

#include "common.h"

/* cmodel.h is included by common.h before common.h reaches its own edict
 * forward declaration, so declare the tag here before using it in prototypes. */
struct edict_s;

typedef void (*cmLoadYield_t)(void);

/* Warcraft III pathing bits used by movement-class-aware routing.  Existing
 * walkability APIs keep their historical UNWALKABLE behavior; callers that
 * need another movement class pass the appropriate blocked bit explicitly. */
#define CM_PATHING_UNWALKABLE 0x02
#define CM_PATHING_UNFLYABLE  0x04

typedef struct {
    LPCVECTOR2 from, target;
    float radius;
    uint8_t blocked_flags; /* 0 preserves the legacy UNWALKABLE contract */
} pathAccelParams_t;

struct War3MapVertex {
    uint16_t accurate_height;
    uint16_t waterlevel;
    uint8_t mapedge;
    uint8_t ground;
    uint8_t ramp;
    uint8_t blight;
    uint8_t water;
    uint8_t boundary;
    uint8_t groundVariation;
    uint8_t cliffVariation; // used also to mark mid-ramp
    uint8_t level;
    uint8_t cliff;
};

struct war3map {
    uint32_t header;
    uint32_t version;
    uint8_t tileset;
    uint32_t custom;
    uint32_t * grounds;
    uint32_t * cliffs;
    VECTOR2 center;
    uint32_t width;
    uint32_t height;
    handle_t vertices;
    uint32_t num_grounds;
    uint32_t num_cliffs;
};

/* Synchronous format parsers cooperatively yield through the caller-owned callback. */
bool CM_LoadMap(cstring_t mapFilename, cmLoadYield_t yield);
uint32_t CM_GetMapChecksum(void);
bool CM_IsMapLoaded(cstring_t mapFilename);
float CM_GetHeightAtPoint(float sx, float sy);
float CM_GetWaterHeightAtPoint(float sx, float sy);
LPDOODAD CM_GetDoodads(void);
//LPCMAPPLAYER CM_GetPlayer(uint32_t index);
uint32_t CM_GetLocalPlayerNumber(void);
LPCMAPINFO CM_GetMapInfo(void);
bool CM_ReadMapInfo(cstring_t filename, LPMAPINFO info);
void CM_FreeMapInfo(LPMAPINFO info);
void CM_ReadAbilities(handle_t archive);
VECTOR2 CM_GetNormalizedMapPosition(float x, float y);
VECTOR2 CM_GetDenormalizedMapPosition(float x, float y);
bool CM_ClosestPathablePoint(LPCVECTOR2 location, LPVECTOR2 out);
bool CM_ClosestPathablePointForRadius(LPCVECTOR2 location, float radius, LPVECTOR2 out);
bool CM_ClosestPathablePointForRadiusFlags(LPCVECTOR2 location, float radius, uint8_t blocked_flags,
                                           LPVECTOR2 out);
bool CM_ClosestReachablePointForRadius(LPCVECTOR2 from, LPCVECTOR2 target, float radius, LPVECTOR2 out);
bool CM_ClosestReachablePointForRadiusFlags(LPCVECTOR2 from, LPCVECTOR2 target, float radius,
                                            uint8_t blocked_flags, LPVECTOR2 out);
bool CM_PointIsPathableForRadius(LPCVECTOR2 location, float radius);
bool CM_PointIsPathableForRadiusFlags(LPCVECTOR2 location, float radius, uint8_t blocked_flags);
bool CM_LineIsWalkable(LPCVECTOR2 a, LPCVECTOR2 b);
/* Optional byte-mask pathing sample used by generic local presentation.
 * Backends without a compatible cell mask return false and clear flags. */
bool CM_GetPathingFlagsAt(LPCVECTOR2 location, uint8_t * flags);
bool CM_TerrainPointIsWalkable(LPCVECTOR2 location);
bool CM_TerrainPointIsSwimmable(LPCVECTOR2 location);
bool CM_LineIsWalkableForRadius(LPCVECTOR2 a, LPCVECTOR2 b, float radius);
bool CM_LineIsPathableForRadiusFlags(LPCVECTOR2 a, LPCVECTOR2 b, float radius, uint8_t blocked_flags);
bool CM_FindPathWaypoint(pathAccelParams_t const *params, LPVECTOR2 out);
bool CM_FindDirectApproachPointForRadius(LPCVECTOR2 from, LPCVECTOR2 target, float range, float radius, LPVECTOR2 out);
float CM_PathCellWorldSize(void);
uint32_t CM_RequestHeatmapForRadius(struct edict_s *goalentity, float radius);
uint32_t CM_RequestHeatmapForRadiusFlags(struct edict_s *goalentity, float radius, uint8_t blocked_flags);
bool CM_ActivateCachedFlowForFlags(uint32_t generation, uint8_t blocked_flags);
void CM_ProcessPathJobs(uint32_t work_budget);
bool CM_FindApproachPointToFootprintForRadius(struct edict_s const *target, LPCVECTOR2 from, float range, float radius, LPVECTOR2 out);
bool CM_FindInnerApproachPointToFootprintForRadius(struct edict_s const *target, LPCVECTOR2 from, float range, float radius, LPVECTOR2 out);
/* Distance from a world point to the target entity's authored no-walk
 * pathing footprint. Returns FLT_MAX when the target has no usable footprint. */
float CM_DistanceToPathingFootprint(struct edict_s const *target, LPCVECTOR2 point);
BOX2 CM_GetWorldBounds(void);

/* WoW-only: all WorldSafeLocs entries for the current map.  Populated during
 * CM_LoadMap; null until a WoW map is loaded.  Callers must not free. */
#ifdef WOW
uint32_t CM_WowGetMapId(void);
uint32_t CM_WowGetAllSpawnCount(void);
LPCVECTOR3 CM_WowGetSpawnPos(uint32_t index);
cstring_t CM_WowGetSpawnName(uint32_t index);
cstring_t CM_WowAdtPath(int tile_x, int tile_y, string_t out, uint32_t out_size);
float CM_WowFloorHeight(float x, float y, float ref_z, float step_up);
bool CM_WowMoveBlocked(LPCVECTOR3 from, LPCVECTOR3 to);
bool CM_WowRayTriangle(LPCVECTOR3 start, LPCVECTOR3 end, LPCVECTOR3 a, LPCVECTOR3 b, LPCVECTOR3 c, float *fraction);
#ifdef BZ_TESTS
bool CM_WowTestBspRay(LPCVECTOR3 start, LPCVECTOR3 end, float *fraction);
bool CM_WowTestWallRay(bool wall);
#endif
#endif
void CM_BakeStaticObstacles(void);
void CM_InvalidatePathCache(void);
void CM_SetupPathMap(uint32_t width, uint32_t height, uint8_t const *cells);

#endif
