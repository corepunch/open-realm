#include "sc2_map.h"

DWORD SC2_MapObjectClassId(sc2MapObject_t const *object);

BOOL CL_GameDefaultCamera(gameCamera_t *camera) {
    sc2MapCamera_t source;
    if (!camera || !SC2_MapDefaultCamera(&source)) return false;
    VECTOR3 const euler = SC2_EulerFromCamera(source.pitch, source.yaw);
    *camera = (gameCamera_t){ .target = source.target, .distance = source.distance,
        .pitch = euler.x, .yaw = euler.z, .fov = source.fov,
        .znear = source.znear, .zfar = source.zfar, .height_offset = source.height_offset };
    return true;
}

FLOAT CL_GameCameraHeightAtPoint(FLOAT x, FLOAT y) { return SC2_MapCameraHeightAtPoint(x, y); }
FLOAT CL_GameLerpDegrees(FLOAT a, FLOAT b, FLOAT fraction) { return SC2_LerpDegrees(a, b, fraction); }

bool CM_LoadMapFormat(LPCSTR mapFilename) {
    memset(&world, 0, sizeof(world));
    SC2_MapSetHost(&(sc2MapHost_t){
        .read_file = FS_ReadFile,
        .free_file = FS_FreeFile,
        .mem_alloc = MemAlloc,
        .mem_free = MemFree,
        .cvar_string = gi.CvarString,
    });
    if (!SC2_MapLoad(mapFilename))
        return false;

    sc2Map_t const *map = SC2_MapCurrent();
    DWORD width = map->MapInfo.width;
    DWORD height = map->MapInfo.height;
    world.map = MemAlloc(sizeof(WAR3MAP));
    memset(world.map, 0, sizeof(WAR3MAP));
    world.map->width = width + 1;
    world.map->height = height + 1;
    world.map->center = map->origin;
    world.info.mapName = MemAlloc(strlen(map->map_name) + 1);
    memcpy(world.info.mapName, map->map_name, strlen(map->map_name) + 1);
    world.info.playableArea.width = width;
    world.info.playableArea.height = height;
    world.info.players[0].used = true;
    world.info.players[0].playerType = kPlayerTypeHuman;
    world.info.players[0].playerRace = kPlayerRaceHuman;
    world.info.players[0].startingPosition = (VECTOR2){ 0.0f, 0.0f };
    CM_SetupPathMap(width, height, NULL);
    return true;
}

FLOAT CM_GetHeightAtPoint(FLOAT sx, FLOAT sy) {
    return SC2_MapHeightAtPoint(sx, sy);
}

FLOAT CM_GetCameraHeightOffset(void) {
    sc2MapCamera_t camera;

    SC2_MapDefaultCamera(&camera);
    return camera.height_offset;
}

VECTOR2 CM_GetNormalizedMapPosition(FLOAT x, FLOAT y) {
    return SC2_MapNormalizedPosition(x, y);
}

VECTOR2 CM_GetDenormalizedMapPosition(FLOAT x, FLOAT y) {
    return SC2_MapDenormalizedPosition(x, y);
}

BOX2 CM_GetWorldBounds(void) {
    return SC2_MapBounds();
}
