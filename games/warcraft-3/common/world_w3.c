#include "common/common.h"
#include "games/warcraft-3/common/terrain.h"
#include "common/ui_constants.h"
#include <float.h>
#include <math.h>

#ifdef BZ_CLIENT_WORLD
#define WC3_VS_FIXEDFILEINFO_SIGNATURE 0xFEEF04BDu
#define WC3_VS_FIXEDFILEINFO_VERSION   0x00010000u
#define WC3_VS_FIXEDFILEINFO_SIZE      52u

typedef struct {
    uint16_t file[4];
    uint16_t product[4];
} wc3ExecutableVersion_t;

static uint16_t W3_ReadLE16(uint8_t const *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t W3_ReadLE32(uint8_t const *p) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

/* The executable version resource begins with a VS_VERSION_INFO block whose
 * key is UTF-16LE and whose binary value is VS_FIXEDFILEINFO.  We deliberately
 * avoid parsing the PE resource tree: the UTF-16 key plus the fixed-info magic,
 * structure version and block bounds are a sufficiently strong opt-in diagnostic
 * fingerprint while keeping this code dependency-free and small. */
static bool W3_ParseExecutableVersion(uint8_t const *data,
                                      size_t size,
                                      wc3ExecutableVersion_t *version) {
    static char const key[] = "VS_VERSION_INFO";
    size_t const key_chars = sizeof(key) - 1;
    size_t const key_bytes = key_chars * 2;

    if (!data || !version || size < 6 + key_bytes + 2 + WC3_VS_FIXEDFILEINFO_SIZE)
        return false;

    for (size_t key_offset = 6; key_offset + key_bytes + 2 <= size; key_offset++) {
        bool matches = true;
        size_t block_offset, block_end, value_offset;
        uint16_t block_length, value_length, type;
        uint8_t const *fixed;

        for (size_t i = 0; i < key_chars; i++) {
            if (data[key_offset + i * 2] != (uint8_t)key[i] ||
                data[key_offset + i * 2 + 1] != 0) {
                matches = false;
                break;
            }
        }
        if (!matches || data[key_offset + key_bytes] != 0 ||
            data[key_offset + key_bytes + 1] != 0)
            continue;

        block_offset = key_offset - 6;
        if (block_offset & 3u)
            continue;
        block_length = W3_ReadLE16(data + block_offset);
        value_length = W3_ReadLE16(data + block_offset + 2);
        type = W3_ReadLE16(data + block_offset + 4);
        if (type != 0 || value_length < WC3_VS_FIXEDFILEINFO_SIZE ||
            block_length > size - block_offset)
            continue;
        block_end = block_offset + block_length;

        value_offset = (key_offset + key_bytes + 2 + 3u) & ~(size_t)3u;
        if (value_offset > block_end ||
            WC3_VS_FIXEDFILEINFO_SIZE > block_end - value_offset)
            continue;

        fixed = data + value_offset;
        if (W3_ReadLE32(fixed) != WC3_VS_FIXEDFILEINFO_SIGNATURE ||
            W3_ReadLE32(fixed + 4) != WC3_VS_FIXEDFILEINFO_VERSION)
            continue;

        {
            uint32_t const file_ms = W3_ReadLE32(fixed + 8);
            uint32_t const file_ls = W3_ReadLE32(fixed + 12);
            uint32_t const product_ms = W3_ReadLE32(fixed + 16);
            uint32_t const product_ls = W3_ReadLE32(fixed + 20);
            version->file[0] = (uint16_t)(file_ms >> 16);
            version->file[1] = (uint16_t)file_ms;
            version->file[2] = (uint16_t)(file_ls >> 16);
            version->file[3] = (uint16_t)file_ls;
            version->product[0] = (uint16_t)(product_ms >> 16);
            version->product[1] = (uint16_t)product_ms;
            version->product[2] = (uint16_t)(product_ls >> 16);
            version->product[3] = (uint16_t)product_ls;
        }
        return true;
    }
    return false;
}

static bool W3_VersionEqual(uint16_t const a[4], uint16_t const b[4]) {
    return !memcmp(a, b, sizeof(uint16_t) * 4);
}

static bool W3_ReportExecutableVersion(cstring_t filename, bool *found_file) {
    uint32_t size = 0;
    uint8_t *data = FS_ReadLooseFile(filename, &size, 0);
    wc3ExecutableVersion_t version;

    if (!data)
        return false;
    if (found_file)
        *found_file = true;
    if (!W3_ParseExecutableVersion(data, size, &version)) {
        FS_FreeFile(data);
        return false;
    }
    FS_FreeFile(data);

    fprintf(stderr, "WC3 data executable: %s\n", filename);
    fprintf(stderr, "WC3 executable version: %u.%u.%u.%u\n",
            version.file[0], version.file[1], version.file[2], version.file[3]);
    if (!W3_VersionEqual(version.file, version.product)) {
        fprintf(stderr, "WC3 product version: %u.%u.%u.%u\n",
                version.product[0], version.product[1], version.product[2], version.product[3]);
    }
    return true;
}

void Game_StartupDiagnostics(void) {
    static cstring_t const executable_names[] = {
        "Warcraft III.exe",
        "war3.exe",
    };
    bool found_file = false;

    Cvar_GetD("wc3_report_data_version", "0", 0,
              "report the Warcraft III executable version resource at startup");
    if (!Cvar_Integer("wc3_report_data_version", 0))
        return;

    FOR_LOOP(i, sizeof(executable_names) / sizeof(executable_names[0])) {
        if (W3_ReportExecutableVersion(executable_names[i], &found_file))
            return;
    }

    if (found_file)
        fprintf(stderr, "WC3 executable version: unavailable (version resource not found)\n");
    else
        fprintf(stderr, "WC3 executable version: unavailable (Warcraft III.exe/war3.exe not found)\n");
}

#endif /* BZ_CLIENT_WORLD */

typedef void (*cmW3Read_t)(handle_t archive);

void CM_ReadPathMap(handle_t archive);
static void CM_ReadDoodads(handle_t archive);
static void CM_ReadUnitDoodads(handle_t archive);
static void CM_ReadHeightmap(handle_t archive);
static void CM_ReadInfo(handle_t archive);
static void CM_ReadWeather(handle_t archive);
void CM_ReadUnits(handle_t archive);
void CM_ReadItems(handle_t archive);
void CM_ReadAbilities(handle_t archive);
void CM_ReadDestructables(handle_t archive);
void CM_ReadStrings(handle_t archive);
void CM_ReadMapScript(handle_t archive);

static handle_t cm_w3_map_archive;
static handle_t cm_w3_map_data;

static cmW3Read_t const cm_w3_readers[] = {
    CM_ReadPathMap,
    CM_ReadDoodads,
    CM_ReadUnitDoodads,
    CM_ReadHeightmap,
    CM_ReadInfo,
    CM_ReadWeather,
    CM_ReadUnits,
    CM_ReadItems,
    CM_ReadAbilities,
    CM_ReadDestructables,
    CM_ReadStrings,
    CM_ReadMapScript,
};

#ifdef BZ_CLIENT_WORLD
#include "client/client.h"

/* The engine needs terrain pathing for placement previews, without game-owned routing jobs or imports. */
static struct {
    uint32_t width, height;
    uint8_t *cells;
} cl_path;

/* Keep only client terrain cells; routing work buffers belong to the game module. */
void CM_SetupPathMap(uint32_t width, uint32_t height, uint8_t const *cells) {
    SAFE_DELETE(cl_path.cells, MemFree);
    cl_path.width = width; cl_path.height = height;
    if (!width || !height) return;
    cl_path.cells = MemAlloc(width * height);
    if (cells) memcpy(cl_path.cells, cells, width * height);
    else memset(cl_path.cells, 0, width * height);
}

/* Client collision circles add live blockers; this lookup supplies the map's authored terrain flags. */
bool CM_GetPathingFlagsAt(vec2_t const *pos, uint8_t *flags) {
    if (flags) *flags = 0;
    if (!pos || !flags || !cl_path.cells) return false;
    vec2_t n = CM_GetNormalizedMapPosition(pos->x, pos->y);
    int x = (int)floorf(n.x * cl_path.width), y = (int)floorf(n.y * cl_path.height);
    if (x < 0 || y < 0 || x >= cl_path.width || y >= cl_path.height) return false;
    *flags = cl_path.cells[x + y * cl_path.width];
    return true;
}

#define WC3_GOLD_MINE_MIN_DISTANCE 512.0f

cstring_t CL_GameOrderQueueReleaseCommand(void) { return "orderqueuerelease"; }

bool CL_GameBuildCursorBlocked(vec3_t const *origin) {
    float const min_dist_sq = WC3_GOLD_MINE_MIN_DISTANCE * WC3_GOLD_MINE_MIN_DISTANCE;
    if (!origin || !cl.cursorEntity || !(cl.cursorEntity->flags & EF_RESOURCE_RETURN)) return false;
    FOR_LOOP(i, cl.num_active) {
        uint32_t const number = cl.active_entities[i];
        entityState_t const *state;
        float dx, dy;
        if (!number || number >= MAX_CLIENT_ENTITIES) continue;
        state = &cl.ents[number].current;
        if (!(state->flags & EF_RESOURCE_SOURCE) || (state->flags & EF_NOT_SELECTABLE)) continue;
        dx = state->origin.x - origin->x;
        dy = state->origin.y - origin->y;
        if (dx * dx + dy * dy < min_dist_sq) return true;
    }
    return false;
}

void CL_GameModifyBuildPathing(vec2_t const *point, uint8_t *flags) {
    uint32_t x, y;

    if (!point || !flags || !cl.terrain_mask.cells) return;
    if (!TerrainMask_CellForPoint(cl.terrain_mask.origin, cl.terrain_mask.cell_size, cl.terrain_mask.width, cl.terrain_mask.height, point, &x, &y)) {
        *flags &= ~WC3_PATH_BLIGHTED;
        return;
    }
    if (cl.terrain_mask.cells[x + y * cl.terrain_mask.width]) *flags |= WC3_PATH_BLIGHTED;
    else *flags &= ~WC3_PATH_BLIGHTED;
}

bool CL_GameBuildSameTypeSelection(gameSameTypeSelection_t *selection) {
    uint32_t count = 0;
    uint32_t class_id;

    if (!selection || !Cvar_Integer("cl_same_type_select", 0) || !selection->command ||
        selection->command_size < 2) return false;
    class_id = cl.ents[selection->anchor].current.class_id;
    snprintf(selection->command, selection->command_size, "select %u sametype", selection->anchor);
    FOR_LOOP(i, selection->visible_count) {
        uint32_t const number = selection->visible[i];
        size_t used;
        if (!number || number == selection->anchor || number >= MAX_CLIENT_ENTITIES ||
            cl.ents[number].current.class_id != class_id || count >= MIN(selection->limit, 61)) continue;
        used = strlen(selection->command);
        if (used + 12 >= selection->command_size) break;
        snprintf(selection->command + used, selection->command_size - used, " %u", number);
        count++;
    }
    return true;
}
#endif

static float W3_ClampCameraZoom(float value) {
    return MAX(WC3_CAMERA_MIN_DISTANCE, MIN(WC3_CAMERA_MAX_DISTANCE, value));
}

static gameCameraZoomPolicy_t W3_CameraZoomPolicy(float user_default, float user_maximum) {
    gameCameraZoomPolicy_t policy = {
        .minimum = WC3_CAMERA_MIN_DISTANCE,
        .default_distance = W3_ClampCameraZoom(user_default),
        .maximum = W3_ClampCameraZoom(user_maximum),
    };
    mapInfo_t const *info = &world.info;

    /* The options UI prevents max < default.  Keep direct cvar edits sane too. */
    if (policy.maximum < policy.default_distance)
        policy.maximum = policy.default_distance;

    if (info->fileFormat >= 32 && (info->flags & force_default_camera_zoom))
        policy.default_distance = W3_ClampCameraZoom((float)info->defaultZoomOverride);
    if (info->fileFormat >= 32 && (info->flags & force_maximum_camera_zoom))
        policy.maximum = W3_ClampCameraZoom((float)info->maximumZoomOverride);
    if (info->fileFormat >= 33 && (info->flags & force_minimum_camera_zoom))
        policy.minimum = W3_ClampCameraZoom((float)info->minimumZoomOverride);

    /* Individual map force flags can make a player's remaining values invalid;
     * normalize the effective policy without changing the authored W3I data. */
    if (policy.maximum < policy.minimum)
        policy.maximum = policy.minimum;
    policy.default_distance = MAX(policy.minimum, MIN(policy.maximum, policy.default_distance));
    return policy;
}

bool CL_GameCameraZoomPolicy(gameCameraZoomPolicy_t *policy, float user_default, float user_maximum) {
    if (!policy) return false;
    *policy = W3_CameraZoomPolicy(user_default, user_maximum);
    return true;
}

bool CL_GameDefaultCamera(gameCamera_t *camera) {
    gameCameraZoomPolicy_t const zoom = W3_CameraZoomPolicy(WC3_CAMERA_DEFAULT_DISTANCE, WC3_CAMERA_MAX_DISTANCE);
    if (!camera) return false;
    *camera = (gameCamera_t){
        .distance = zoom.default_distance,
        .pitch = WC3_CAMERA_DEFAULT_PITCH,
        .yaw = WC3_CAMERA_DEFAULT_YAW,
        .fov = WC3_CAMERA_DEFAULT_FOV,
        .znear = WC3_CAMERA_DEFAULT_NEAR_Z,
        .zfar = WC3_CAMERA_DEFAULT_FAR_Z,
    };
    return true;
}

bool CL_GameCameraUsesWorldUp(void) { return true; }

/* Retail 1.30+ ConsoleUI.fdf authors ConsoleTexture05/06 tiles beside the 4:3 root, so the canvas widens
 * and centers the HUD; classic archives author none, so the scene stretches like classic retail. */
UICANVASPOLICY CL_GameCanvasPolicy(void) {
    uint32_t size = 0;
    string_t text = FS_ReadFile("UI\\FrameDef\\UI\\ConsoleUI.fdf", &size);
    UICANVASPOLICY policy = UI_CANVAS_POLICY;
    if (!text) {
        fprintf(stderr, "WC3: ConsoleUI.fdf unavailable; keeping the classic stretched canvas\n");
        return policy;
    }
    if (W3_FdfReferencesFile(text, "ConsoleTexture05")) policy = UI_CANVAS_EXPAND_CENTER;
    FS_FreeFile(text);
    return policy;
}
float CL_GameLerpDegrees(float a, float b, float fraction) {
    float delta = fmodf(b - a, 360.0f);
    if (delta > 180.0f)
        delta -= 360.0f;
    else if (delta < -180.0f)
        delta += 360.0f;
    return a + delta * fraction;
}
float CM_GetCameraHeightOffset(void) {
    return -48.0f; // world units; retail target reference is 48 below sampled terrain
}

#ifdef BZ_TESTS
static box2_t test_world_bounds;
static bool test_world_bounds_set;

void CM_SetupTestWorldBounds(box2_t const *bounds) {
	test_world_bounds_set = bounds != NULL;
	if (bounds) test_world_bounds = *bounds;
}
#endif

static war3mapVertex_t const *CM_GetWar3MapVertex(uint32_t x, uint32_t y) {
	if (!world.map || !world.map->vertices) return NULL;
	int const index = x + y * world.map->width;
	char const *ptr = ((char const *)world.map->vertices) + index * sizeof(war3mapVertex_t);
	return (war3mapVertex_t const *)ptr;
}

static float CM_GetWar3MapVertexHeight(war3mapVertex_t const *vert) {
	if (!vert) return 0;
	return DECODE_HEIGHT(vert->accurate_height) + vert->level * TILE_SIZE - HEIGHT_COR;
}

static float CM_GetWar3MapVertexWaterHeight(war3mapVertex_t const *vert) {
    if (!vert) return -FLT_MAX;
    return DECODE_HEIGHT(vert->waterlevel) - WATER_HEIGHT_COR;
}

/* war3map.w3r v5 stores editor regions.  Weather is one field on each region;
 * keep only the bounds + weather rawcode in collision-model state because the
 * remaining name/sound/color metadata belongs to other presentation systems. */
static bool CM_W3SkipCString(handle_t file) {
    uint8_t ch = 0;
    do {
        if (!SFileReadFile(file, &ch, sizeof(ch), NULL, NULL)) return false;
    } while (ch != 0);
    return true;
}

static bool CM_W3ReadWeatherRegions(handle_t archive) {
    handle_t file;
    uint32_t version = 0, count = 0, stored = 0;
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
        uint32_t pos = SFileSetFilePointer(file, 0, 0, FILE_CURRENT);
        uint32_t size = SFileGetFileSize(file, NULL);
        uint32_t remaining = pos < size ? size - pos : 0;
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
        box2_t bounds;
        uint32_t region_id, weather_id;
        uint8_t color[4];

        if (!SFileReadFile(file, &bounds.min.x, sizeof(float), NULL, NULL) ||
            !SFileReadFile(file, &bounds.min.y, sizeof(float), NULL, NULL) ||
            !SFileReadFile(file, &bounds.max.x, sizeof(float), NULL, NULL) ||
            !SFileReadFile(file, &bounds.max.y, sizeof(float), NULL, NULL) ||
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

static void CM_ReadWeather(handle_t archive) { CM_W3ReadWeatherRegions(archive); }

static void CM_W3FreeUnitOverrides(uint32_t count, unitData_t **units_ptr) {
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

static void CM_W3FreeDroppedItemSets(uint32_t num_sets, droppableItemSet_t *sets) {
    if (!sets) return;
    FOR_LOOP(i, num_sets)
        SAFE_DELETE(sets[i].droppableItems, MemFree);
    MemFree(sets);
}

static void CM_W3FreeDoodadPlacement(doodad_t *doodad) {
    if (!doodad) return;
    CM_W3FreeDroppedItemSets(doodad->num_droppedItemSets, doodad->droppableItemSets);
    SAFE_DELETE(doodad->inventoryItems, MemFree);
    SAFE_DELETE(doodad->modifiedAbilities, MemFree);
    SAFE_DELETE(doodad->diffAvailUnits, MemFree);
}

static void CM_W3ReleaseMapArchive(void) {
    FS_SetPriorityArchive(NULL);
    if (cm_w3_map_archive) {
        SFileCloseArchive(cm_w3_map_archive);
        cm_w3_map_archive = NULL;
    }
    SAFE_DELETE(cm_w3_map_data, MemFree);
}

static void CM_W3ClearMapData(void) {
    CM_W3ReleaseMapArchive();
    CM_W3FreeUnitOverrides(world.info.num_originalUnits, &world.info.originalUnits);
    CM_W3FreeUnitOverrides(world.info.num_userCreatedUnits, &world.info.userCreatedUnits);
    CM_W3FreeUnitOverrides(world.info.num_originalItems, &world.info.originalItems);
    CM_W3FreeUnitOverrides(world.info.num_userCreatedItems, &world.info.userCreatedItems);
    CM_W3FreeUnitOverrides(world.info.num_originalAbilities, &world.info.originalAbilities);
    CM_W3FreeUnitOverrides(world.info.num_userCreatedAbilities, &world.info.userCreatedAbilities);
    CM_W3FreeUnitOverrides(world.info.num_originalDestructables, &world.info.originalDestructables);
    CM_W3FreeUnitOverrides(world.info.num_userCreatedDestructables, &world.info.userCreatedDestructables);
    CM_ReleaseModel();
    while (world.doodads) {
        doodad_t *doodad = world.doodads;
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

bool CM_LoadMapFormat(cstring_t mapFilename, cmLoadYield_t yield) {
    uint32_t mapSize = 0;

    CM_W3ClearMapData();
    cm_w3_map_data = FS_ReadFile(mapFilename, &mapSize);
    if (!cm_w3_map_data || mapSize == 0) {
        SAFE_DELETE(cm_w3_map_data, MemFree);
        Com_Error(ERR_DROP, "CM_LoadMap: failed to read map %s\n", mapFilename);
        return false;
    }
    if (!SFileOpenArchiveFromMemory(cm_w3_map_data, mapSize, 0, &cm_w3_map_archive)) {
        SAFE_DELETE(cm_w3_map_data, MemFree);
        Com_Error(ERR_DROP, "CM_LoadMap: failed to open map archive %s\n", mapFilename);
        return false;
    }
    /* Keep the open map as the highest-priority FS source so sheet/INI loaders
     * (G_ReadGameDataFile) see map-imported Units\*.txt and war3mapMisc.txt. */
    FS_SetPriorityArchive(cm_w3_map_archive);
    FOR_LOOP(i, sizeof(cm_w3_readers) / sizeof(*cm_w3_readers)) {
        cm_w3_readers[i](cm_w3_map_archive);
        yield();
    }
    return true;
}

float CM_GetHeightAtPoint(float sx, float sy) {
	if (!world.map || !world.map->vertices) return 0;
	float x = (sx - world.map->center.x) / TILE_SIZE;
    float y = (sy - world.map->center.y) / TILE_SIZE;
    float fx = floorf(x);
    float fy = floorf(y);
    war3mapVertex_t const *va = CM_GetWar3MapVertex(fx, fy);
    war3mapVertex_t const *vb = CM_GetWar3MapVertex(fx + 1, fy);
    war3mapVertex_t const *vc = CM_GetWar3MapVertex(fx, fy + 1);
    war3mapVertex_t const *vd = CM_GetWar3MapVertex(fx + 1, fy + 1);
    float a = CM_GetWar3MapVertexHeight(va);
    float b = CM_GetWar3MapVertexHeight(vb);
    float c = CM_GetWar3MapVertexHeight(vc);
    float d = CM_GetWar3MapVertexHeight(vd);
    float ab = LerpNumber(a, b, x - fx);
    float cd = LerpNumber(c, d, x - fx);
    return LerpNumber(ab, cd, y - fy);
}

float CM_GetWaterHeightAtPoint(float sx, float sy) {
    if (!world.map || !world.map->vertices) return -FLT_MAX;
    float x = (sx - world.map->center.x) / TILE_SIZE;
    float y = (sy - world.map->center.y) / TILE_SIZE;
    float fx = floorf(x);
    float fy = floorf(y);
    float a = CM_GetWar3MapVertexWaterHeight(CM_GetWar3MapVertex(fx, fy));
    float b = CM_GetWar3MapVertexWaterHeight(CM_GetWar3MapVertex(fx + 1, fy));
    float c = CM_GetWar3MapVertexWaterHeight(CM_GetWar3MapVertex(fx, fy + 1));
    float d = CM_GetWar3MapVertexWaterHeight(CM_GetWar3MapVertex(fx + 1, fy + 1));
    float ab = LerpNumber(a, b, x - fx);
    float cd = LerpNumber(c, d, x - fx);
    return LerpNumber(ab, cd, y - fy);
}

vec2_t CM_GetNormalizedMapPosition(float x, float y) {
#ifdef BZ_TESTS
	if (test_world_bounds_set) {
		float width = test_world_bounds.max.x - test_world_bounds.min.x;
		float height = test_world_bounds.max.y - test_world_bounds.min.y;
		return (vec2_t){ width ? (x - test_world_bounds.min.x) / width : 0,
		                  height ? (y - test_world_bounds.min.y) / height : 0 };
	}
#endif
	if (!world.map) return (vec2_t){0, 0};
	float _x = (x - world.map->center.x) / ((world.map->width - 1) * TILE_SIZE);
	float _y = (y - world.map->center.y) / ((world.map->height - 1) * TILE_SIZE);
	return (vec2_t){ _x, _y };
}

vec2_t CM_GetDenormalizedMapPosition(float x, float y) {
#ifdef BZ_TESTS
	if (test_world_bounds_set)
		return (vec2_t){ x * (test_world_bounds.max.x - test_world_bounds.min.x) + test_world_bounds.min.x,
		                  y * (test_world_bounds.max.y - test_world_bounds.min.y) + test_world_bounds.min.y };
#endif
	if (!world.map) return (vec2_t){0, 0};
	float _x = x * (world.map->width - 1) * TILE_SIZE + world.map->center.x;
	float _y = y * (world.map->height - 1) * TILE_SIZE + world.map->center.y;
	return (vec2_t){ _x, _y };
}

box2_t CM_GetWorldBounds(void) {
#ifdef BZ_TESTS
    if (test_world_bounds_set) return test_world_bounds;
#endif
    return MAKE(box2_t,
        .min = world.map->center,
        .max = {
            .x = (world.map->width - 1)  * TILE_SIZE + world.map->center.x,
            .y = (world.map->height - 1) * TILE_SIZE + world.map->center.y,
        });
}

#ifndef TOOL_COMMON_NO_MPQ
/* Both worlds read the same WPM bytes; each module owns its path-data consumer. */
void CM_ReadPathMap(handle_t archive) {
    handle_t file;
    uint32_t header, version;
    uint32_t width, height;
    uint8_t *cells;
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

static void W3_TestWriteLE16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void W3_TestWriteLE32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static size_t W3_TestVersionBlob(uint8_t *blob, size_t size) {
    static char const key[] = "VS_VERSION_INFO";
    size_t const block_offset = 8;
    size_t const key_offset = block_offset + 6;
    size_t const key_bytes = (sizeof(key) - 1) * 2;
    size_t const value_offset = (key_offset + key_bytes + 2 + 3u) & ~(size_t)3u;
    size_t const block_end = value_offset + WC3_VS_FIXEDFILEINFO_SIZE;

    if (size < block_end) return 0;
    memset(blob, 0, size);
    W3_TestWriteLE16(blob + block_offset, (uint16_t)(block_end - block_offset));
    W3_TestWriteLE16(blob + block_offset + 2, WC3_VS_FIXEDFILEINFO_SIZE);
    W3_TestWriteLE16(blob + block_offset + 4, 0);
    for (size_t i = 0; i < sizeof(key) - 1; i++)
        blob[key_offset + i * 2] = (uint8_t)key[i];

    W3_TestWriteLE32(blob + value_offset, WC3_VS_FIXEDFILEINFO_SIGNATURE);
    W3_TestWriteLE32(blob + value_offset + 4, WC3_VS_FIXEDFILEINFO_VERSION);
    W3_TestWriteLE32(blob + value_offset + 8, (1u << 16) | 29u);
    W3_TestWriteLE32(blob + value_offset + 12, (2u << 16) | 9231u);
    W3_TestWriteLE32(blob + value_offset + 16, (1u << 16) | 29u);
    W3_TestWriteLE32(blob + value_offset + 20, (2u << 16) | 9231u);
    return value_offset;
}

TEST(wc3_data_version, parses_version_info_without_pe_headers) {
    uint8_t blob[128];
    wc3ExecutableVersion_t version = { 0 };
    W3_TestVersionBlob(blob, sizeof(blob));

    T_ASSERT(W3_ParseExecutableVersion(blob, sizeof(blob), &version));
    T_EQ(version.file[0], 1); T_EQ(version.file[1], 29);
    T_EQ(version.file[2], 2); T_EQ(version.file[3], 9231);
    T_EQ(version.product[0], 1); T_EQ(version.product[1], 29);
    T_EQ(version.product[2], 2); T_EQ(version.product[3], 9231);
}

TEST(wc3_data_version, rejects_false_or_truncated_version_resources) {
    uint8_t blob[128];
    wc3ExecutableVersion_t version = { 0 };
    size_t value_offset = W3_TestVersionBlob(blob, sizeof(blob));

    W3_TestWriteLE32(blob + value_offset, 0xFEEF04BCu);
    T_ASSERT(!W3_ParseExecutableVersion(blob, sizeof(blob), &version));

    value_offset = W3_TestVersionBlob(blob, sizeof(blob));
    (void)value_offset;
    T_ASSERT(!W3_ParseExecutableVersion(blob, 72, &version));
}

/* Client path queries must use their own cells and replace them cleanly between maps. */
TEST(client_world, terrain_path_flags_survive_load_replace_and_clear) {
    uint8_t cells[] = { 2, 4, 8, 16 }, flags = 0;
    CM_SetupTestWorldBounds(&(box2_t){ .min = { 0, 0 }, .max = { 64, 64 } });
    CM_SetupPathMap(2, 2, cells);
    T_ASSERT(CM_GetPathingFlagsAt(&(vec2_t){ 48, 16 }, &flags)); T_EQ(flags, 4);
    T_ASSERT(CM_GetPathingFlagsAt(&(vec2_t){ 16, 48 }, &flags)); T_EQ(flags, 8);
    T_ASSERT(!CM_GetPathingFlagsAt(&(vec2_t){ 64, 16 }, &flags));
    CM_SetupPathMap(1, 1, cells);
    T_ASSERT(CM_GetPathingFlagsAt(&(vec2_t){ 48, 48 }, &flags)); T_EQ(flags, 2);
    CM_SetupPathMap(0, 0, NULL);
    T_ASSERT(!CM_GetPathingFlagsAt(&(vec2_t){ 16, 16 }, &flags));
    CM_SetupTestWorldBounds(NULL);
}
#endif
