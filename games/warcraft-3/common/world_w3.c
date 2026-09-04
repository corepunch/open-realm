#include "common/common.h"
#include "common/ui_constants.h"

BOOL CL_GameDefaultCamera(gameCamera_t *camera) {
    if (!camera) return false;
    *camera = (gameCamera_t){
        .distance = WC3_CAMERA_DEFAULT_DISTANCE,
        .pitch = WC3_CAMERA_DEFAULT_PITCH,
        .yaw = WC3_CAMERA_DEFAULT_YAW,
        .fov = WC3_CAMERA_DEFAULT_FOV,
        .znear = WC3_CAMERA_DEFAULT_NEAR_Z,
        .zfar = WC3_CAMERA_DEFAULT_FAR_Z,
    };
    return true;
}

FLOAT CL_GameCameraHeightAtPoint(FLOAT x, FLOAT y) { return CM_GetHeightAtPoint(x, y); }
FLOAT CL_GameLerpDegrees(FLOAT a, FLOAT b, FLOAT fraction) { return a + (b - a) * fraction; }
FLOAT CM_GetCameraHeightOffset(void) {
    return -TILE_SIZE; // orbit look-at sits one tile below sampled terrain
}

#ifdef BZ_TESTS
static BOX2 test_world_bounds;
static BOOL test_world_bounds_set;

void CM_SetupTestWorldBounds(LPCBOX2 bounds) {
	test_world_bounds_set = bounds != NULL;
	if (bounds) test_world_bounds = *bounds;
}
#endif

static LPCWAR3MAPVERTEX CM_GetWar3MapVertex(DWORD x, DWORD y) {
	if (!world.map || !world.map->vertices) return NULL;
	int const index = x + y * world.map->width;
	char const *ptr = ((char const *)world.map->vertices) + index * sizeof(WAR3MAPVERTEX);
	return (LPCWAR3MAPVERTEX)ptr;
}

static FLOAT CM_GetWar3MapVertexHeight(LPCWAR3MAPVERTEX vert) {
	if (!vert) return 0;
	return DECODE_HEIGHT(vert->accurate_height) + vert->level * TILE_SIZE - HEIGHT_COR;
}

void CM_ReadPathMap(HANDLE archive);
static void CM_ReadDoodads(HANDLE archive);
static void CM_ReadUnitDoodads(HANDLE archive);
static void CM_ReadHeightmap(HANDLE archive);
static void CM_ReadInfo(HANDLE archive);
void CM_ReadUnits(HANDLE archive);
void CM_ReadStrings(HANDLE archive);
void CM_ReadMapScript(HANDLE archive);

static void CM_W3FreeUnitOverrides(DWORD count, unitData_t **units_ptr) {
    unitData_t *units = units_ptr ? *units_ptr : NULL;

    if (!units) return;
    FOR_LOOP(i, count) {
        FOR_LOOP(j, units[i].numbeOfModifications)
            SAFE_DELETE(units[i].modifications[j].data, MemFree);
        SAFE_DELETE(units[i].modifications, MemFree);
    }
    MemFree(units);
    *units_ptr = NULL;
}

static void CM_W3FreeDroppedItemSets(DWORD num_sets, droppableItemSet_t *sets) {
    if (!sets) return;
    FOR_LOOP(i, num_sets)
        SAFE_DELETE(sets[i].droppableItems, MemFree);
    MemFree(sets);
}

static void CM_W3FreeDoodadPlacement(LPDOODAD doodad) {
    if (!doodad) return;
    CM_W3FreeDroppedItemSets(doodad->num_droppedItemSets, doodad->droppableItemSets);
    SAFE_DELETE(doodad->inventoryItems, MemFree);
    SAFE_DELETE(doodad->modifiedAbilities, MemFree);
    SAFE_DELETE(doodad->diffAvailUnits, MemFree);
}

static void CM_W3ClearMapData(void) {
    CM_W3FreeUnitOverrides(world.info.num_originalUnits, &world.info.originalUnits);
    CM_W3FreeUnitOverrides(world.info.num_userCreatedUnits, &world.info.userCreatedUnits);
    CM_ReleaseModel();
    while (world.doodads) {
        LPDOODAD doodad = world.doodads;
        world.doodads = doodad->next;
        CM_W3FreeDoodadPlacement(doodad);
        MemFree(doodad);
    }
    if (world.map) {
        SAFE_DELETE(world.map->grounds, MemFree);
        SAFE_DELETE(world.map->cliffs, MemFree);
        SAFE_DELETE(world.map->vertices, MemFree);
        MemFree(world.map);
    }
    CM_SetupPathMap(0, 0, NULL);
    memset(&world, 0, sizeof(world));
}

bool CM_LoadMapFormat(LPCSTR mapFilename) {
    HANDLE mapArchive;
    HANDLE mapData;
    DWORD mapSize = 0;

    CM_W3ClearMapData();
    mapData = FS_ReadFile(mapFilename, &mapSize);
    if (!mapData || mapSize == 0) {
        Com_Error(ERR_DROP, "CM_LoadMap: failed to read map %s\n", mapFilename);
        return false;
    }
    if (!SFileOpenArchiveFromMemory(mapData, mapSize, 0, &mapArchive)) {
        MemFree(mapData);
        Com_Error(ERR_DROP, "CM_LoadMap: failed to open map archive %s\n", mapFilename);
        return false;
    }
    CM_ReadPathMap(mapArchive);
    CM_ReadDoodads(mapArchive);
    CM_ReadUnitDoodads(mapArchive);
    CM_ReadHeightmap(mapArchive);
    CM_ReadInfo(mapArchive);
    CM_ReadUnits(mapArchive);
    CM_ReadStrings(mapArchive);
    CM_ReadMapScript(mapArchive);
    SFileCloseArchive(mapArchive);
    MemFree(mapData);
    return true;
}

FLOAT CM_GetHeightAtPoint(FLOAT sx, FLOAT sy) {
	if (!world.map || !world.map->vertices) return 0;
	FLOAT x = (sx - world.map->center.x) / TILE_SIZE;
    FLOAT y = (sy - world.map->center.y) / TILE_SIZE;
    FLOAT fx = floorf(x);
    FLOAT fy = floorf(y);
    FLOAT a = CM_GetWar3MapVertexHeight(CM_GetWar3MapVertex(fx, fy));
    FLOAT b = CM_GetWar3MapVertexHeight(CM_GetWar3MapVertex(fx + 1, fy));
    FLOAT c = CM_GetWar3MapVertexHeight(CM_GetWar3MapVertex(fx, fy + 1));
    FLOAT d = CM_GetWar3MapVertexHeight(CM_GetWar3MapVertex(fx + 1, fy + 1));
    FLOAT ab = LerpNumber(a, b, x - fx);
    FLOAT cd = LerpNumber(c, d, x - fx);
    return LerpNumber(ab, cd, y - fy);
}

VECTOR2 CM_GetNormalizedMapPosition(FLOAT x, FLOAT y) {
#ifdef BZ_TESTS
	if (test_world_bounds_set) {
		FLOAT width = test_world_bounds.max.x - test_world_bounds.min.x;
		FLOAT height = test_world_bounds.max.y - test_world_bounds.min.y;
		return (VECTOR2){ width ? (x - test_world_bounds.min.x) / width : 0,
		                  height ? (y - test_world_bounds.min.y) / height : 0 };
	}
#endif
	if (!world.map) return (VECTOR2){0, 0};
	FLOAT _x = (x - world.map->center.x) / ((world.map->width - 1) * TILE_SIZE);
	FLOAT _y = (y - world.map->center.y) / ((world.map->height - 1) * TILE_SIZE);
	return (VECTOR2){ _x, _y };
}

VECTOR2 CM_GetDenormalizedMapPosition(FLOAT x, FLOAT y) {
#ifdef BZ_TESTS
	if (test_world_bounds_set)
		return (VECTOR2){ x * (test_world_bounds.max.x - test_world_bounds.min.x) + test_world_bounds.min.x,
		                  y * (test_world_bounds.max.y - test_world_bounds.min.y) + test_world_bounds.min.y };
#endif
	if (!world.map) return (VECTOR2){0, 0};
	FLOAT _x = x * (world.map->width - 1) * TILE_SIZE + world.map->center.x;
	FLOAT _y = y * (world.map->height - 1) * TILE_SIZE + world.map->center.y;
	return (VECTOR2){ _x, _y };
}

BOX2 CM_GetWorldBounds(void) {
#ifdef BZ_TESTS
    if (test_world_bounds_set) return test_world_bounds;
#endif
    return MAKE(BOX2,
        .min = world.map->center,
        .max = {
            .x = (world.map->width - 1)  * TILE_SIZE + world.map->center.x,
            .y = (world.map->height - 1) * TILE_SIZE + world.map->center.y,
        });
}
