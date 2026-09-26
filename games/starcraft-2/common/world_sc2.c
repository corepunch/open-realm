#include "sc2_map.h"
#include "common/ui_constants.h"

uint32_t SC2_MapObjectClassId(sc2MapObject_t const *object);

bool CL_GameDefaultCamera(gameCamera_t *camera) {
    sc2MapCamera_t source;
    if (!camera || !SC2_MapDefaultCamera(&source)) return false;
    vector3_t const euler = SC2_EulerFromCamera(source.pitch, source.yaw);
    *camera = (gameCamera_t){ .target = source.target, .distance = source.distance,
        .pitch = euler.x, .yaw = euler.z, .fov = source.fov,
        .znear = source.znear, .zfar = source.zfar, .height_offset = source.height_offset };
    return true;
}

bool CL_GameCameraUsesWorldUp(void) { return false; }
UICANVASPOLICY CL_GameCanvasPolicy(void) { return UI_CANVAS_POLICY; }
float CL_GameLerpDegrees(float a, float b, float fraction) { return SC2_LerpDegrees(a, b, fraction); }
cstring_t CL_GameOrderQueueReleaseCommand(void) { return NULL; }
bool CL_GameBuildCursorBlocked(vector3_t const *origin) { (void)origin; return false; }
void CL_GameModifyBuildPathing(vector2_t const *point, uint8_t *flags) { (void)point; (void)flags; }
bool CL_GameBuildSameTypeSelection(gameSameTypeSelection_t *selection) {
    (void)selection;
    return false;
}

bool CM_LoadMapFormat(cstring_t mapFilename, cmLoadYield_t yield) {
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
    yield();

    sc2Map_t const *map = SC2_MapCurrent();
    uint32_t width = map->MapInfo.width;
    uint32_t height = map->MapInfo.height;
    world.map = MemAlloc(sizeof(war3map_t));
    memset(world.map, 0, sizeof(war3map_t));
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
    world.info.players[0].startingPosition = (vector2_t){ 0.0f, 0.0f };
    CM_SetupPathMap(width, height, NULL);
    return true;
}

float CM_GetHeightAtPoint(float sx, float sy) {
    return SC2_MapHeightAtPoint(sx, sy);
}

float CM_GetCameraHeightOffset(void) {
    sc2MapCamera_t camera;

    SC2_MapDefaultCamera(&camera);
    return camera.height_offset;
}

vector2_t CM_GetNormalizedMapPosition(float x, float y) {
    return SC2_MapNormalizedPosition(x, y);
}

vector2_t CM_GetDenormalizedMapPosition(float x, float y) {
    return SC2_MapDenormalizedPosition(x, y);
}

box2_t CM_GetWorldBounds(void) {
    return SC2_MapBounds();
}
