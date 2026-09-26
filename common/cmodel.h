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
    vector2_t const *from, *target;
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
    uint32_t *grounds;
    uint32_t *cliffs;
    vector2_t center;
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
doodad_t *CM_GetDoodads(void);
//mapPlayer_t const *CM_GetPlayer(uint32_t index);
uint32_t CM_GetLocalPlayerNumber(void);
mapInfo_t const *CM_GetMapInfo(void);
bool CM_ReadMapInfo(cstring_t filename, mapInfo_t *info);
void CM_FreeMapInfo(mapInfo_t *info);
void CM_ReadAbilities(handle_t archive);
vector2_t CM_GetNormalizedMapPosition(float x, float y);
vector2_t CM_GetDenormalizedMapPosition(float x, float y);
bool CM_ClosestPathablePoint(vector2_t const *location, vector2_t *out);
bool CM_ClosestPathablePointForRadius(vector2_t const *location, float radius, vector2_t *out);
bool CM_ClosestPathablePointForRadiusFlags(vector2_t const *location, float radius, uint8_t blocked_flags,
                                           vector2_t *out);
bool CM_ClosestReachablePointForRadius(vector2_t const *from, vector2_t const *target, float radius, vector2_t *out);
bool CM_ClosestReachablePointForRadiusFlags(vector2_t const *from, vector2_t const *target, float radius,
                                            uint8_t blocked_flags, vector2_t *out);
bool CM_PointIsPathableForRadius(vector2_t const *location, float radius);
bool CM_PointIsPathableForRadiusFlags(vector2_t const *location, float radius, uint8_t blocked_flags);
bool CM_LineIsWalkable(vector2_t const *a, vector2_t const *b);
/* Optional byte-mask pathing sample used by generic local presentation.
 * Backends without a compatible cell mask return false and clear flags. */
bool CM_GetPathingFlagsAt(vector2_t const *location, uint8_t *flags);
bool CM_TerrainPointIsWalkable(vector2_t const *location);
bool CM_TerrainPointIsSwimmable(vector2_t const *location);
bool CM_LineIsWalkableForRadius(vector2_t const *a, vector2_t const *b, float radius);
bool CM_LineIsPathableForRadiusFlags(vector2_t const *a, vector2_t const *b, float radius, uint8_t blocked_flags);
bool CM_FindPathWaypoint(pathAccelParams_t const *params, vector2_t *out);
bool CM_FindDirectApproachPointForRadius(vector2_t const *from, vector2_t const *target, float range, float radius, vector2_t *out);
float CM_PathCellWorldSize(void);
uint32_t CM_RequestHeatmapForRadius(struct edict_s *goalentity, float radius);
uint32_t CM_RequestHeatmapForRadiusFlags(struct edict_s *goalentity, float radius, uint8_t blocked_flags);
bool CM_ActivateCachedFlowForFlags(uint32_t generation, uint8_t blocked_flags);
void CM_ProcessPathJobs(uint32_t work_budget);
bool CM_FindApproachPointToFootprintForRadius(struct edict_s const *target, vector2_t const *from, float range, float radius, vector2_t *out);
bool CM_FindInnerApproachPointToFootprintForRadius(struct edict_s const *target, vector2_t const *from, float range, float radius, vector2_t *out);
/* Distance from a world point to the target entity's authored no-walk
 * pathing footprint. Returns FLT_MAX when the target has no usable footprint. */
float CM_DistanceToPathingFootprint(struct edict_s const *target, vector2_t const *point);
box2_t CM_GetWorldBounds(void);

/* WoW-only: all WorldSafeLocs entries for the current map.  Populated during
 * CM_LoadMap; null until a WoW map is loaded.  Callers must not free. */
#ifdef WOW
uint32_t CM_WowGetMapId(void);
uint32_t CM_WowGetAllSpawnCount(void);
vector3_t const *CM_WowGetSpawnPos(uint32_t index);
cstring_t CM_WowGetSpawnName(uint32_t index);
cstring_t CM_WowAdtPath(int tile_x, int tile_y, string_t out, uint32_t out_size);
float CM_WowFloorHeight(float x, float y, float ref_z, float step_up);
bool CM_WowMoveBlocked(vector3_t const *from, vector3_t const *to);
bool CM_WowRayTriangle(vector3_t const *start, vector3_t const *end, vector3_t const *a, vector3_t const *b, vector3_t const *c, float *fraction);
#ifdef BZ_TESTS
bool CM_WowTestBspRay(vector3_t const *start, vector3_t const *end, float *fraction);
bool CM_WowTestWallRay(bool wall);
#endif
#endif
void CM_BakeStaticObstacles(void);
void CM_InvalidatePathCache(void);
void CM_SetupPathMap(uint32_t width, uint32_t height, uint8_t const *cells);

#endif
