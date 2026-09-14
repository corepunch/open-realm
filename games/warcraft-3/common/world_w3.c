#include "common/common.h"
#include "games/warcraft-3/common/terrain.h"
#include "common/ui_constants.h"
#include <float.h>

typedef void (*cmW3Read_t)(HANDLE archive);

void CM_ReadPathMap(HANDLE archive);
static void CM_ReadDoodads(HANDLE archive);
static void CM_ReadUnitDoodads(HANDLE archive);
static void CM_ReadHeightmap(HANDLE archive);
static void CM_ReadInfo(HANDLE archive);
static void CM_ReadWeather(HANDLE archive);
void CM_ReadUnits(HANDLE archive);
void CM_ReadStrings(HANDLE archive);
void CM_ReadMapScript(HANDLE archive);

static cmW3Read_t const cm_w3_readers[] = {
    CM_ReadPathMap,
    CM_ReadDoodads,
    CM_ReadUnitDoodads,
    CM_ReadHeightmap,
    CM_ReadInfo,
    CM_ReadWeather,
    CM_ReadUnits,
    CM_ReadStrings,
    CM_ReadMapScript,
};

#ifdef BZ_CLIENT_WORLD
/* The engine needs terrain pathing for placement previews, without game-owned routing jobs or imports. */
static struct {
    DWORD width, height;
    BYTE *cells;
} cl_path;

/* Keep only client terrain cells; routing work buffers belong to the game module. */
void CM_SetupPathMap(DWORD width, DWORD height, BYTE const *cells) {
    SAFE_DELETE(cl_path.cells, MemFree);
    cl_path.width = width; cl_path.height = height;
    if (!width || !height) return;
    cl_path.cells = MemAlloc(width * height);
    if (cells) memcpy(cl_path.cells, cells, width * height);
    else memset(cl_path.cells, 0, width * height);
}

/* Client collision circles add live blockers; this lookup supplies the map's authored terrain flags. */
BOOL CM_GetPathingFlagsAt(LPCVECTOR2 pos, LPBYTE flags) {
    if (flags) *flags = 0;
    if (!pos || !flags || !cl_path.cells) return false;
    VECTOR2 n = CM_GetNormalizedMapPosition(pos->x, pos->y);
    int x = (int)floorf(n.x * cl_path.width), y = (int)floorf(n.y * cl_path.height);
    if (x < 0 || y < 0 || x >= cl_path.width || y >= cl_path.height) return false;
    *flags = cl_path.cells[x + y * cl_path.width];
    return true;
}
#endif

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

BOOL CL_GameCameraUsesWorldUp(void) { return true; }
FLOAT CL_GameLerpDegrees(FLOAT a, FLOAT b, FLOAT fraction) {
    FLOAT delta = fmodf(b - a, 360.0f);
    if (delta > 180.0f)
        delta -= 360.0f;
    else if (delta < -180.0f)
        delta += 360.0f;
    return a + delta * fraction;
}
FLOAT CM_GetCameraHeightOffset(void) {
    return -48.0f; // world units; retail target reference is 48 below sampled terrain
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

static FLOAT CM_GetWar3MapVertexWaterHeight(LPCWAR3MAPVERTEX vert) {
    if (!vert) return -FLT_MAX;
    return DECODE_HEIGHT(vert->waterlevel) - WATER_HEIGHT_COR;
}

/* war3map.w3r v5 stores editor regions.  Weather is one field on each region;
 * keep only the bounds + weather rawcode in collision-model state because the
 * remaining name/sound/color metadata belongs to other presentation systems. */
static BOOL CM_W3SkipCString(HANDLE file) {
    BYTE ch = 0;
    do {
        if (!SFileReadFile(file, &ch, sizeof(ch), NULL, NULL)) return false;
    } while (ch != 0);
    return true;
}

static BOOL CM_W3ReadWeatherRegions(HANDLE archive) {
    HANDLE file;
    DWORD version = 0, count = 0, stored = 0;
    mapWeatherRegion_t *regions = NULL;

    if (!archive || !SFileOpenFileEx(archive, "war3map.w3r", SFILE_OPEN_FROM_MPQ, &file)) return true;
    if (!SFileReadFile(file, &version, sizeof(version), NULL, NULL) ||
        !SFileReadFile(file, &count, sizeof(count), NULL, NULL)) {
        SFileCloseFile(file);
        return false;
    }
    if (version != 5) {
        SFileCloseFile(file);
        return false;
    }
    /* The smallest v5 record is 30 bytes (two empty C strings); reject corrupt
     * counts before allocating from map-controlled input. */
    {
        DWORD pos = SFileSetFilePointer(file, 0, 0, FILE_CURRENT);
        DWORD size = SFileGetFileSize(file, NULL);
        DWORD remaining = pos < size ? size - pos : 0;
        if (count > remaining / 30u) {
            SFileCloseFile(file);
            return false;
        }
    }
    if (count) {
        regions = MemAlloc(sizeof(*regions) * count);
        if (!regions) {
            SFileCloseFile(file);
            return false;
        }
        memset(regions, 0, sizeof(*regions) * count);
    }
    FOR_LOOP(i, count) {
        BOX2 bounds;
        DWORD region_id, weather_id;
        BYTE color[4];

        if (!SFileReadFile(file, &bounds.min.x, sizeof(FLOAT), NULL, NULL) ||
            !SFileReadFile(file, &bounds.min.y, sizeof(FLOAT), NULL, NULL) ||
            !SFileReadFile(file, &bounds.max.x, sizeof(FLOAT), NULL, NULL) ||
            !SFileReadFile(file, &bounds.max.y, sizeof(FLOAT), NULL, NULL) ||
            !CM_W3SkipCString(file) ||
            !SFileReadFile(file, &region_id, sizeof(region_id), NULL, NULL) ||
            !SFileReadFile(file, &weather_id, sizeof(weather_id), NULL, NULL) ||
            !CM_W3SkipCString(file) ||
            !SFileReadFile(file, color, sizeof(color), NULL, NULL)) {
            MemFree(regions);
            SFileCloseFile(file);
            return false;
        }
        (void)region_id;
        if (!weather_id) continue;
        regions[stored++] = (mapWeatherRegion_t){ .bounds = bounds, .weatherID = weather_id };
    }
    SFileCloseFile(file);
    if (!stored) {
        MemFree(regions);
        regions = NULL;
    }
    world.info.weatherRegions = regions;
    world.info.num_weatherRegions = stored;
    return true;
}

static void CM_ReadWeather(HANDLE archive) { CM_W3ReadWeatherRegions(archive); }

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

bool CM_LoadMapFormat(LPCSTR mapFilename, cmLoadYield_t yield) {
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
    FOR_LOOP(i, sizeof(cm_w3_readers) / sizeof(*cm_w3_readers)) {
        cm_w3_readers[i](mapArchive);
        yield();
    }
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
    LPCWAR3MAPVERTEX va = CM_GetWar3MapVertex(fx, fy);
    LPCWAR3MAPVERTEX vb = CM_GetWar3MapVertex(fx + 1, fy);
    LPCWAR3MAPVERTEX vc = CM_GetWar3MapVertex(fx, fy + 1);
    LPCWAR3MAPVERTEX vd = CM_GetWar3MapVertex(fx + 1, fy + 1);
    FLOAT a = CM_GetWar3MapVertexHeight(va);
    FLOAT b = CM_GetWar3MapVertexHeight(vb);
    FLOAT c = CM_GetWar3MapVertexHeight(vc);
    FLOAT d = CM_GetWar3MapVertexHeight(vd);
    FLOAT ab = LerpNumber(a, b, x - fx);
    FLOAT cd = LerpNumber(c, d, x - fx);
    return LerpNumber(ab, cd, y - fy);
}

FLOAT CM_GetWaterHeightAtPoint(FLOAT sx, FLOAT sy) {
    if (!world.map || !world.map->vertices) return -FLT_MAX;
    FLOAT x = (sx - world.map->center.x) / TILE_SIZE;
    FLOAT y = (sy - world.map->center.y) / TILE_SIZE;
    FLOAT fx = floorf(x);
    FLOAT fy = floorf(y);
    FLOAT a = CM_GetWar3MapVertexWaterHeight(CM_GetWar3MapVertex(fx, fy));
    FLOAT b = CM_GetWar3MapVertexWaterHeight(CM_GetWar3MapVertex(fx + 1, fy));
    FLOAT c = CM_GetWar3MapVertexWaterHeight(CM_GetWar3MapVertex(fx, fy + 1));
    FLOAT d = CM_GetWar3MapVertexWaterHeight(CM_GetWar3MapVertex(fx + 1, fy + 1));
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

#ifndef TOOL_COMMON_NO_MPQ
/* Both worlds read the same WPM bytes; each module owns its path-data consumer. */
void CM_ReadPathMap(HANDLE archive) {
    HANDLE file;
    DWORD header, version;
    DWORD width, height;
    LPBYTE cells;
    if (!SFileOpenFileEx(archive, "war3map.wpm", SFILE_OPEN_FROM_MPQ, &file)) {
        CM_SetupPathMap(world.map ? world.map->width : 0, world.map ? world.map->height : 0, NULL);
        return;
    }
    SFileReadFile(file, &header, 4, NULL, NULL);
    SFileReadFile(file, &version, 4, NULL, NULL);
    SFileReadFile(file, &width, 4, NULL, NULL);
    SFileReadFile(file, &height, 4, NULL, NULL);
    if (!width || !height) {
        SFileCloseFile(file);
        CM_SetupPathMap(0, 0, NULL);
        return;
    }
    cells = MemAlloc(width * height);
    SFileReadFile(file, cells, width * height, 0, 0);
    SFileCloseFile(file);
    CM_SetupPathMap(width, height, cells);
    MemFree(cells);
}
#endif /* !TOOL_COMMON_NO_MPQ */

#if defined(BZ_CLIENT_WORLD) && defined(BZ_TESTS)
#include "shared/test.h"

/* Client path queries must use their own cells and replace them cleanly between maps. */
TEST(client_world, terrain_path_flags_survive_load_replace_and_clear) {
    BYTE cells[] = { 2, 4, 8, 16 }, flags = 0;
    CM_SetupTestWorldBounds(&(BOX2){ .min = { 0, 0 }, .max = { 64, 64 } });
    CM_SetupPathMap(2, 2, cells);
    T_ASSERT(CM_GetPathingFlagsAt(&(VECTOR2){ 48, 16 }, &flags)); T_EQ(flags, 4);
    T_ASSERT(CM_GetPathingFlagsAt(&(VECTOR2){ 16, 48 }, &flags)); T_EQ(flags, 8);
    T_ASSERT(!CM_GetPathingFlagsAt(&(VECTOR2){ 64, 16 }, &flags));
    CM_SetupPathMap(1, 1, cells);
    T_ASSERT(CM_GetPathingFlagsAt(&(VECTOR2){ 48, 48 }, &flags)); T_EQ(flags, 2);
    CM_SetupPathMap(0, 0, NULL);
    T_ASSERT(!CM_GetPathingFlagsAt(&(VECTOR2){ 16, 16 }, &flags));
    CM_SetupTestWorldBounds(NULL);
}
#endif
