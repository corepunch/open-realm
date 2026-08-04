#include "common/common.h"

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

bool CM_LoadMapFormat(LPCSTR mapFilename) {
    HANDLE mapArchive;
    HANDLE mapData;
    DWORD mapSize = 0;

    memset(&world, 0, sizeof(world));
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
	if (!world.map) return (VECTOR2){0, 0};
	FLOAT _x = (x - world.map->center.x) / ((world.map->width - 1) * TILE_SIZE);
	FLOAT _y = (y - world.map->center.y) / ((world.map->height - 1) * TILE_SIZE);
	return (VECTOR2){ _x, _y };
}

VECTOR2 CM_GetDenormalizedMapPosition(FLOAT x, FLOAT y) {
	if (!world.map) return (VECTOR2){0, 0};
	FLOAT _x = x * (world.map->width - 1) * TILE_SIZE + world.map->center.x;
	FLOAT _y = y * (world.map->height - 1) * TILE_SIZE + world.map->center.y;
	return (VECTOR2){ _x, _y };
}

BOX2 CM_GetWorldBounds(void) {
    return MAKE(BOX2,
        .min = world.map->center,
        .max = {
            .x = (world.map->width - 1)  * TILE_SIZE + world.map->center.x,
            .y = (world.map->height - 1) * TILE_SIZE + world.map->center.y,
        });
}
