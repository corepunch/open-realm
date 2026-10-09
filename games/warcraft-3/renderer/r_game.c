#include "games/warcraft-3/common/cursor.h"
#include "renderer/r_game.h"
#include "games/warcraft-3/common/wc3_coords.h"
#include "r_lightning.h"
#include "mdx/r_mdx.h"
#include "w3m/r_war3map.h"
#include "r_weather.h"
#include "common/stb_slk.h"
#include "games/warcraft-3/common/minimap_render.h"
#include <ctype.h>
#include <limits.h>
#include <math.h>

void _W3M_RegisterMap(cstring_t mapFileName);
void _W3M_DrawWorld(void);
void _W3M_DrawTerrainShadows(void);
void _W3M_DrawAlphaSurfaces(void);
bool _W3M_TraceLocation(viewDef_t const *viewdef, float x, float y, vec3_t *output);

/* MMP v0 is a fixed little-endian header followed by 16-byte icon records. */
typedef struct { uint32_t kind, x, y; color32_t bgra; } mmpIcon_t;
typedef struct { uint32_t version, count; mmpIcon_t icons[]; } mmp_t;
static struct { PATHSTR map; texture_t const *image, *icons[3]; mmp_t *mmp; } preview;

static texture_t const *minimap_special[WC3_MINIMAP_CONTACT_NEUTRAL_BUILDING + 1];
static stbIniCache_t minimap_theme, minimap_map_skin;

static cstring_t const preview_art[] = {
    "UI\\Minimap\\minimap-gold.blp",
    "UI\\Minimap\\minimap-neutralbuilding.blp",
    "UI\\Minimap\\MinimapIconCircleOfPower.blp",
};
#define BZ_MMP_CANVAS 256.0f // pixels; World Editor MMP coordinates use this square; projects preview markers
#define BZ_MMP_ICON_SIZE 16.0f // pixels; native minimap marker footprint in the 256-pixel preview

static cstring_t selCirclesNames[NUM_SELECTION_CIRCLES] = {
    "ReplaceableTextures\\Selection\\SelectionCircleSmall.blp",
    "ReplaceableTextures\\Selection\\SelectionCircleMed.blp",
    "ReplaceableTextures\\Selection\\SelectionCircleLarge.blp",
};

static slkField_t const terrain_schema[] = {
    { "", offsetof(w3TerrainArt_t, id), STB_SLK_FOURCC },
    { "dir", offsetof(w3TerrainArt_t, dir), STB_SLK_STR },
    { "file", offsetof(w3TerrainArt_t, file), STB_SLK_STR },
    { NULL, 0, 0 },
};

static slkField_t const cliff_schema[] = {
    { "", offsetof(w3CliffType_t, id), STB_SLK_FOURCC },
    { "texDir", offsetof(w3CliffType_t, texDir), STB_SLK_STR },
    { "texFile", offsetof(w3CliffType_t, texFile), STB_SLK_STR },
    { "groundTile", offsetof(w3CliffType_t, groundTile), STB_SLK_FOURCC },
    { "upperTile", offsetof(w3CliffType_t, upperTile), STB_SLK_FOURCC },
    { "rampModelDir", offsetof(w3CliffType_t, rampModelDir), STB_SLK_STR },
    { "cliffModelDir", offsetof(w3CliffType_t, cliffModelDir), STB_SLK_STR },
    { NULL, 0, 0 },
};

typedef struct {
    uint32_t id;
    float height;
    cstring_t texFile;
    uint32_t numTex;
    float texRate;
    uint32_t color[4][4]; /* Smin, Smax, Dmin, Dmax; each R, G, B, A. */
} w3WaterRow_t;

#define WATER_COLOR_FIELDS(prefix, band) \
    { prefix "_R", offsetof(w3WaterRow_t, color[band][0]), STB_SLK_INT }, \
    { prefix "_G", offsetof(w3WaterRow_t, color[band][1]), STB_SLK_INT }, \
    { prefix "_B", offsetof(w3WaterRow_t, color[band][2]), STB_SLK_INT }, \
    { prefix "_A", offsetof(w3WaterRow_t, color[band][3]), STB_SLK_INT }

static slkField_t const water_schema[] = {
    { "", offsetof(w3WaterRow_t, id), STB_SLK_FOURCC },
    { "height", offsetof(w3WaterRow_t, height), STB_SLK_FLOAT },
    { "texFile", offsetof(w3WaterRow_t, texFile), STB_SLK_STR },
    { "numTex", offsetof(w3WaterRow_t, numTex), STB_SLK_INT },
    { "texRate", offsetof(w3WaterRow_t, texRate), STB_SLK_FLOAT },
    WATER_COLOR_FIELDS("Smin", 0),
    WATER_COLOR_FIELDS("Smax", 1),
    WATER_COLOR_FIELDS("Dmin", 2),
    WATER_COLOR_FIELDS("Dmax", 3),
    { NULL, 0, 0 },
};
#undef WATER_COLOR_FIELDS

static cstring_t modelNames[MODEL_COUNT] = {
    "UI\\Feedback\\SelectionCircle\\SelectionCircle.mdx"
};

static PATHSTR cursor_model_name;
static model_t const *cursor_active_model;
static cstring_t cursor_bad_anim;
static model_t *cursor_model;
static bool cursor_load_attempted;
static struct {
    cstring_t anim;
    color32_t tint;
} const cursor_modes[WC3_CURSOR_COUNT] = {
    [WC3_CURSOR_NORMAL] = { "Normal", {255,255,255,255} },
    [WC3_CURSOR_SELECT_YELLOW] = { "Select", {255,255,0,255} },
    [WC3_CURSOR_SELECT_RED] = { "Select", {255,0,0,255} },
    [WC3_CURSOR_SELECT_GREEN] = { "Select", {0,255,0,255} },
    [WC3_CURSOR_TARGET] = { "Target", {255,255,255,255} },
    [WC3_CURSOR_TARGET_SELECT_YELLOW] = { "TargetSelect", {255,255,0,255} },
    [WC3_CURSOR_TARGET_SELECT_RED] = { "TargetSelect", {255,0,0,255} },
    [WC3_CURSOR_TARGET_SELECT_GREEN] = { "TargetSelect", {0,255,0,255} },
    [WC3_CURSOR_SIGNAL] = { "TargetSelect", {0,0,0,0} }, /* Dynamic skin/player color. */
    [WC3_CURSOR_HOLD_ITEM] = { "HoldItem", {255,255,255,255} },
    [WC3_CURSOR_SCROLL_LEFT] = { "Scroll Left", {255,255,255,255} },
    [WC3_CURSOR_SCROLL_RIGHT] = { "Scroll Right", {255,255,255,255} },
    [WC3_CURSOR_SCROLL_UP] = { "Scroll Up", {255,255,255,255} },
    [WC3_CURSOR_SCROLL_DOWN] = { "Scroll Down", {255,255,255,255} },
    [WC3_CURSOR_SCROLL_UP_LEFT] = { "Scroll Up Left", {255,255,255,255} },
    [WC3_CURSOR_SCROLL_UP_RIGHT] = { "Scroll Up Right", {255,255,255,255} },
    [WC3_CURSOR_SCROLL_DOWN_LEFT] = { "Scroll Down Left", {255,255,255,255} },
    [WC3_CURSOR_SCROLL_DOWN_RIGHT] = { "Scroll Down Right", {255,255,255,255} },
};

static wc3CursorMode_t R_ResolveCursorMode(drawCursor_t const *cursor) {
    static wc3CursorMode_t const scroll[3][3] = {
        {WC3_CURSOR_SCROLL_UP_LEFT, WC3_CURSOR_SCROLL_UP, WC3_CURSOR_SCROLL_UP_RIGHT},
        {WC3_CURSOR_SCROLL_LEFT, WC3_CURSOR_NORMAL, WC3_CURSOR_SCROLL_RIGHT},
        {WC3_CURSOR_SCROLL_DOWN_LEFT, WC3_CURSOR_SCROLL_DOWN, WC3_CURSOR_SCROLL_DOWN_RIGHT},
    };
    /* CSignalMode locks the owner until exit; neither hover nor scroll replaces it. */
    if (cursor->interaction == WC3_POINTER_SIGNALING) return WC3_CURSOR_SIGNAL;
    if (cursor->scroll.x || cursor->scroll.y)
        return scroll[1 - (cursor->scroll.y > 0) + (cursor->scroll.y < 0)]
                     [1 + (cursor->scroll.x > 0) - (cursor->scroll.x < 0)];
    if (cursor->interaction == WC3_POINTER_HOLDING) return WC3_CURSOR_HOLD_ITEM;
    bool const target = cursor->interaction == WC3_POINTER_TARGETING;
    if (!cursor->hover) return target ? WC3_CURSOR_TARGET : WC3_CURSOR_NORMAL;
    wc3CursorMode_t const selection = cursor->hostile ? WC3_CURSOR_SELECT_RED :
        cursor->owned ? WC3_CURSOR_SELECT_GREEN : WC3_CURSOR_SELECT_YELLOW;
    return (wc3CursorMode_t)(selection + (target ? WC3_CURSOR_TARGET : 0));
}
static cstring_t cursor_anim;
static uint32_t cursor_time, cursor_frame;
static bool cursor_restart;

static w3TerrainArt_t *g_terrain_rows; static uint32_t g_terrain_count; static slkIndex_t g_terrain_idx;
static w3CliffType_t *g_cliff_rows;   static uint32_t g_cliff_count;   static slkIndex_t g_cliff_idx;
static texture_t const *g_blight_texture;

typedef struct {
    cstring_t name;
    cstring_t sound_label;
} wc3AnimLookup_t;
typedef struct {
    cstring_t name;
    cstring_t files;
    cstring_t directory;
    float volume, pitch, pitch_variance, min_distance, max_distance, distance_cutoff;
} wc3AnimSound_t;
typedef struct {
    cstring_t name;
    cstring_t model_path;
    model_t *model;
} wc3SpawnData_t;
typedef struct {
    cstring_t name;
    cstring_t dir;
    cstring_t file;
    cstring_t blend_mode;
    int rows, columns;
    float scale, lifespan, decay_time;
    int uv_lifespan_start, uv_lifespan_end, lifespan_repeat;
    int uv_decay_start, uv_decay_end, decay_repeat;
    float start_r, start_g, start_b, start_a;
    float middle_r, middle_g, middle_b, middle_a;
    float end_r, end_g, end_b, end_a;
    int water;
    cstring_t sound;
    texture_t const *texture;
    bool texture_attempted, unsupported_warned, atlas_warned, zero_duration_warned;
} wc3SplatData_t;
typedef struct {
    cstring_t name;
    cstring_t dir;
    cstring_t file;
    cstring_t blend_mode, sound;
    float scale, birth_time, pause_time, decay_time;
    float start_r, start_g, start_b, start_a;
    float middle_r, middle_g, middle_b, middle_a;
    float end_r, end_g, end_b, end_a;
    texture_t const *texture;
    bool texture_attempted, unsupported_warned, zero_duration_warned;
} wc3UberSplatData_t;

typedef struct {
    cstring_t family, name, dir, file;
    texture_t const **texture;
    bool *attempted;
} wc3SplatTextureParams_t;
typedef struct {
    cstring_t path, family;
    slkField_t const *schema;
    void **rows;
    uint32_t size;
} wc3EventTable_t;

static slkField_t const anim_lookup_schema[] = {
    { "", offsetof(wc3AnimLookup_t, name), STB_SLK_STR },
    { "SoundLabel", offsetof(wc3AnimLookup_t, sound_label), STB_SLK_STR },
    { NULL, 0, 0 },
};
static slkField_t const anim_sound_schema[] = {
    { "", offsetof(wc3AnimSound_t, name), STB_SLK_STR },
    { "FileNames", offsetof(wc3AnimSound_t, files), STB_SLK_STR },
    { "DirectoryBase", offsetof(wc3AnimSound_t, directory), STB_SLK_STR },
    { "Volume", offsetof(wc3AnimSound_t, volume), STB_SLK_FLOAT },
    { "Pitch", offsetof(wc3AnimSound_t, pitch), STB_SLK_FLOAT },
    { "PitchVariance", offsetof(wc3AnimSound_t, pitch_variance), STB_SLK_FLOAT },
    { "MinDistance", offsetof(wc3AnimSound_t, min_distance), STB_SLK_FLOAT },
    { "MaxDistance", offsetof(wc3AnimSound_t, max_distance), STB_SLK_FLOAT },
    { "DistanceCutoff", offsetof(wc3AnimSound_t, distance_cutoff), STB_SLK_FLOAT },
    { NULL, 0, 0 },
};

static wc3AnimLookup_t *anim_lookup_rows; static uint32_t anim_lookup_count;
static wc3AnimSound_t *anim_sound_rows; static uint32_t anim_sound_count;
static slkField_t const spawn_data_schema[] = {
    { "", offsetof(wc3SpawnData_t, name), STB_SLK_STR },
    { "Model", offsetof(wc3SpawnData_t, model_path), STB_SLK_STR },
    { NULL, 0, 0 },
};
static wc3SpawnData_t *spawn_data_rows; static uint32_t spawn_data_count;
static slkField_t const splat_data_schema[] = {
    { "", offsetof(wc3SplatData_t, name), STB_SLK_STR },
    { "Dir", offsetof(wc3SplatData_t, dir), STB_SLK_STR },
    { "file", offsetof(wc3SplatData_t, file), STB_SLK_STR },
    { "BlendMode", offsetof(wc3SplatData_t, blend_mode), STB_SLK_STR },
    { "Rows", offsetof(wc3SplatData_t, rows), STB_SLK_INT },
    { "Columns", offsetof(wc3SplatData_t, columns), STB_SLK_INT },
    { "Scale", offsetof(wc3SplatData_t, scale), STB_SLK_FLOAT },
    { "Lifespan", offsetof(wc3SplatData_t, lifespan), STB_SLK_FLOAT },
    { "Decay", offsetof(wc3SplatData_t, decay_time), STB_SLK_FLOAT },
    { "UVLifespanStart", offsetof(wc3SplatData_t, uv_lifespan_start), STB_SLK_INT },
    { "UVLifespanEnd", offsetof(wc3SplatData_t, uv_lifespan_end), STB_SLK_INT },
    { "LifespanRepeat", offsetof(wc3SplatData_t, lifespan_repeat), STB_SLK_INT },
    { "UVDecayStart", offsetof(wc3SplatData_t, uv_decay_start), STB_SLK_INT },
    { "UVDecayEnd", offsetof(wc3SplatData_t, uv_decay_end), STB_SLK_INT },
    { "UVDecayRepeat", offsetof(wc3SplatData_t, decay_repeat), STB_SLK_INT },
    { "StartR", offsetof(wc3SplatData_t, start_r), STB_SLK_FLOAT },
    { "StartG", offsetof(wc3SplatData_t, start_g), STB_SLK_FLOAT },
    { "StartB", offsetof(wc3SplatData_t, start_b), STB_SLK_FLOAT },
    { "StartA", offsetof(wc3SplatData_t, start_a), STB_SLK_FLOAT },
    { "MiddleR", offsetof(wc3SplatData_t, middle_r), STB_SLK_FLOAT },
    { "MiddleG", offsetof(wc3SplatData_t, middle_g), STB_SLK_FLOAT },
    { "MiddleB", offsetof(wc3SplatData_t, middle_b), STB_SLK_FLOAT },
    { "MiddleA", offsetof(wc3SplatData_t, middle_a), STB_SLK_FLOAT },
    { "EndR", offsetof(wc3SplatData_t, end_r), STB_SLK_FLOAT },
    { "EndG", offsetof(wc3SplatData_t, end_g), STB_SLK_FLOAT },
    { "EndB", offsetof(wc3SplatData_t, end_b), STB_SLK_FLOAT },
    { "EndA", offsetof(wc3SplatData_t, end_a), STB_SLK_FLOAT },
    { "Water", offsetof(wc3SplatData_t, water), STB_SLK_INT },
    { "Sound", offsetof(wc3SplatData_t, sound), STB_SLK_STR },
    { NULL, 0, 0 },
};
static wc3SplatData_t *splat_data_rows; static uint32_t splat_data_count;
static slkField_t const uber_splat_data_schema[] = {
    { "", offsetof(wc3UberSplatData_t, name), STB_SLK_STR },
    { "Dir", offsetof(wc3UberSplatData_t, dir), STB_SLK_STR },
    { "file", offsetof(wc3UberSplatData_t, file), STB_SLK_STR },
    { "BlendMode", offsetof(wc3UberSplatData_t, blend_mode), STB_SLK_STR },
    { "Scale", offsetof(wc3UberSplatData_t, scale), STB_SLK_FLOAT },
    { "BirthTime", offsetof(wc3UberSplatData_t, birth_time), STB_SLK_FLOAT },
    { "PauseTime", offsetof(wc3UberSplatData_t, pause_time), STB_SLK_FLOAT },
    { "Decay", offsetof(wc3UberSplatData_t, decay_time), STB_SLK_FLOAT },
    { "StartR", offsetof(wc3UberSplatData_t, start_r), STB_SLK_FLOAT },
    { "StartG", offsetof(wc3UberSplatData_t, start_g), STB_SLK_FLOAT },
    { "StartB", offsetof(wc3UberSplatData_t, start_b), STB_SLK_FLOAT },
    { "StartA", offsetof(wc3UberSplatData_t, start_a), STB_SLK_FLOAT },
    { "MiddleR", offsetof(wc3UberSplatData_t, middle_r), STB_SLK_FLOAT },
    { "MiddleG", offsetof(wc3UberSplatData_t, middle_g), STB_SLK_FLOAT },
    { "MiddleB", offsetof(wc3UberSplatData_t, middle_b), STB_SLK_FLOAT },
    { "MiddleA", offsetof(wc3UberSplatData_t, middle_a), STB_SLK_FLOAT },
    { "EndR", offsetof(wc3UberSplatData_t, end_r), STB_SLK_FLOAT },
    { "EndG", offsetof(wc3UberSplatData_t, end_g), STB_SLK_FLOAT },
    { "EndB", offsetof(wc3UberSplatData_t, end_b), STB_SLK_FLOAT },
    { "EndA", offsetof(wc3UberSplatData_t, end_a), STB_SLK_FLOAT },
    { "Sound", offsetof(wc3UberSplatData_t, sound), STB_SLK_STR },
    { NULL, 0, 0 },
};
static wc3UberSplatData_t *uber_splat_rows; static uint32_t uber_splat_count;
typedef struct { model_t const *model; uint32_t frame, render_time, generation; bool valid; } wc3EventState_t;
typedef struct { uintptr_t instance_id; wc3EventState_t state; } wc3CameraEventState_t;
typedef enum {
    WC3_EVENT_NONE, WC3_EVENT_SOUND, WC3_EVENT_SPAWN, WC3_EVENT_SPLAT, WC3_EVENT_FOOTPRINT, WC3_EVENT_UBER_SPLAT,
} wc3EventKind_t;
typedef struct { cstring_t prefix; wc3EventKind_t kind; } wc3EventFamily_t;
static wc3EventFamily_t const event_families[] = {
    { "SND", WC3_EVENT_SOUND },
    { "SPN", WC3_EVENT_SPAWN },
    { "SPL", WC3_EVENT_SPLAT },
    { "FPT", WC3_EVENT_FOOTPRINT },
    { "UBR", WC3_EVENT_UBER_SPLAT },
};
typedef struct {
    renderEntity_t const *entity;
    mdxModel_t const *model;
    mdxEvent_t const *event;
    mat4_t const *transform;
    wc3EventFamily_t const *family;
    uint32_t depth;
    bool entity_camera;
    bool ui_sprite;
    model_t const *source_model;
    uintptr_t source_instance_id;
} wc3EventParams_t;
typedef struct {
    renderEntity_t const *entity;
    mdxModel_t const *model;
    wc3EventState_t *state;
    mat4_t const *transform;
    uint32_t depth;
    bool entity_camera;
    bool ui_sprite;
    model_t const *source_model;
    uintptr_t source_instance_id;
} wc3EventDispatchParams_t;
static wc3EventState_t event_state[MAX_GAME_ENTITIES];
static wc3CameraEventState_t *camera_event_states;
static size_t camera_event_state_count, camera_event_state_capacity;
#define WC3_EVENT_WARNING_MAX 128
typedef struct { char kind[32], name[80]; } wc3EventWarning_t;
static wc3EventWarning_t event_warnings[WC3_EVENT_WARNING_MAX];
static uint32_t event_warning_count;
static bool event_warning_overflow_logged;
#define WC3_EVENT_SPAWN_MAX 128 // effects; bounds renderer-owned SPN children without heap growth
typedef struct {
    model_t *model; model_t const *source_model; uintptr_t source_instance_id; mat4_t transform;
    uint32_t team, flags, start_time, frame, serial, depth;
    uint32_t event_frame, event_render_time;
    float scale; bool active, event_valid, entity_camera;
} wc3EventSpawn_t;
static wc3EventSpawn_t event_spawns[WC3_EVENT_SPAWN_MAX];
static uint32_t event_spawn_serial;
#define WC3_EVENT_SPLAT_MAX 128 // effects; bounds transient SPL/FPT/UBR decals without heap growth
#define WC3_EVENT_MAX_DEPTH 4 // model levels; caps recursive SPN child-event graphs
typedef enum { WC3_EVENT_SPLAT_SPLAT, WC3_EVENT_SPLAT_UBER } wc3EventSplatKind_t;
typedef struct { float r, g, b, a; } wc3SplatColorValue_t;
typedef struct { vec2_t mins, maxs; } wc3SplatUV_t;
typedef struct {
    wc3EventSplatKind_t kind;
    wc3SplatData_t *splat_row;
    wc3UberSplatData_t *uber_row;
    vec2_t origin;
    uint32_t start_time, serial;
    bool active;
} wc3EventSplat_t;
static wc3EventSplat_t event_splats[WC3_EVENT_SPLAT_MAX];
static uint32_t event_splat_serial;
static void R_W3DrawEventSpawns(bool entity_camera, model_t const *source_model, uintptr_t source_instance_id);
static void R_W3DrawEventSplats(void);
static bool R_W3RenderEventSplat(wc3EventSplat_t *splat);
static void R_W3DispatchModelEvents(wc3EventDispatchParams_t const *params);
static bool R_W3EventWarningShouldLog(cstring_t kind, cstring_t name);
static void R_W3ClearEventWarnings(void);
static void R_W3ClearCameraSpawns(model_t const *source_model, uintptr_t source_instance_id);
static void R_W3ClearCameraEventStates(void);

/* WorldEditData is the authoritative tileset-to-Blight-art mapping.  Keep the
 * lookup data-driven because custom/expansion tilesets can add rows there. */
void R_LoadBlightTexture(uint8_t tileset) {
    uint8_t *data;
    uint32_t size;
    PATHSTR path = { 0 };
    char const *cursor;
    bool in_tilesets = false;

    g_blight_texture = NULL;
    size = (uint32_t)ri.FS_ReadFile("UI\\WorldEditData.txt", (void **)&data);
    if (!data) {
        fprintf(stderr, "WC3 renderer: failed to load UI\\WorldEditData.txt for Blight tileset %c\n", tileset);
        return;
    }
    cursor = (char const *)data;
    while ((uint32_t)(cursor - (char const *)data) < size) {
        char line[sizeof(PATHSTR) + 128];
        char key = 0;
        PATHSTR value = { 0 };
        size_t remaining = size - (uint32_t)(cursor - (char const *)data);
        char const *end = memchr(cursor, '\n', remaining);
        size_t length = end ? (size_t)(end - cursor) : remaining;
        length = MIN(length, sizeof(line) - 1);
        memcpy(line, cursor, length); line[length] = 0;
        if (line[0] == '[') in_tilesets = !strncasecmp(line, "[TileSets]", 10);
        else if (in_tilesets && WC3_ParseBlightTilesetLine(line, &key, value) && key == tileset) {
            snprintf(path, sizeof(path), "%s", value);
            break;
        }
        cursor += end ? length + 1 : length;
    }
    ri.FS_FreeFile(data);
    if (!path[0]) {
        fprintf(stderr, "WC3 renderer: no Blight texture mapping for tileset %c\n", tileset);
        return;
    }
    strlcat(path, ".blp", sizeof(path));
    g_blight_texture = R_LoadTexture(path);
    if (!g_blight_texture || g_blight_texture == tr.texture[TEX_PLACEHOLDER])
        fprintf(stderr, "WC3 renderer: failed to load Blight texture %s for tileset %c\n", path, tileset);
    else BLIGHT_LOG("texture tileset=%c path=%s id=%u size=%ux%u\n", tileset, path,
                 (unsigned)g_blight_texture->texid, (unsigned)g_blight_texture->width,
                 (unsigned)g_blight_texture->height);
}

texture_t const *R_BlightTexture(void) {
    return g_blight_texture;
}

static wc3WaterStyle_t g_water_style;

static color32_t R_WaterRowColor(uint32_t const *rgba) {
    return (color32_t){ .r = (uint8_t)MIN(rgba[0], 255), .g = (uint8_t)MIN(rgba[1], 255),
                        .b = (uint8_t)MIN(rgba[2], 255), .a = (uint8_t)MIN(rgba[3], 255) };
}

/* Water.slk is the authoritative per-tileset water art: Outland's "OSha" row is an opaque black
 * TeamColor surface (the Abyss), not the Lordaeron Water texture. Keyed by "<tileset>Sha". Frames resolve
 * through the tileset archive layer, so Ashenvale's A.mpq water replaces the base frames. */
void R_LoadWaterStyle(uint8_t tileset) {
    w3WaterRow_t *rows = NULL;
    w3WaterRow_t const *row = NULL;
    uint32_t const id = MAKEFOURCC(tileset, 'S', 'h', 'a');
    uint32_t count;

    memset(&g_water_style, 0, sizeof(g_water_style));
    count = ri.LoadSlk("TerrainArt\\Water.slk", water_schema, (void **)&rows, sizeof(w3WaterRow_t));
    FOR_LOOP(i, count) if (rows[i].id == id) { row = &rows[i]; break; }
    if (!row) {
        fprintf(stderr, "WC3 renderer: no TerrainArt\\Water.slk row %cSha for tileset %c; water is not drawn\n",
                tileset, tileset);
        FS_SLKFreeRows(water_schema, rows, count, sizeof(w3WaterRow_t));
        return;
    }
    g_water_style.height = row->height;
    g_water_style.frame_rate = row->texRate;
    g_water_style.shallow_min = R_WaterRowColor(row->color[0]);
    g_water_style.shallow_max = R_WaterRowColor(row->color[1]);
    g_water_style.deep_min = R_WaterRowColor(row->color[2]);
    g_water_style.deep_max = R_WaterRowColor(row->color[3]);
    if (!row->texFile || !row->texFile[0] || !row->numTex) {
        fprintf(stderr, "WC3 renderer: Water.slk row %cSha has no texture frames; water is not drawn\n", tileset);
    } else {
        if (row->numTex > WC3_MAX_WATER_FRAMES)
            fprintf(stderr, "WC3 renderer: Water.slk row %cSha has %u frames; animating the first %u\n",
                    tileset, row->numTex, WC3_MAX_WATER_FRAMES);
        g_water_style.num_frames = MIN(row->numTex, WC3_MAX_WATER_FRAMES);
        FOR_LOOP(i, g_water_style.num_frames) {
            PATHSTR path;
            snprintf(path, sizeof(path), "%s%02u.blp", row->texFile, i);
            g_water_style.frames[i] = R_LoadTexture(path);
            if (!g_water_style.frames[i] || g_water_style.frames[i] == tr.texture[TEX_PLACEHOLDER])
                fprintf(stderr, "WC3 renderer: failed to load water frame %s for tileset %c\n", path, tileset);
        }
    }
    FS_SLKFreeRows(water_schema, rows, count, sizeof(w3WaterRow_t));
}

/* Water.slk texRate is frames per second over numTex frames (Warsmash advances index += texRate * dt). */
texture_t const *R_WaterFrame(wc3WaterStyle_t const *style, uint32_t time_ms) {
    uint64_t frame;
    if (!style || !style->num_frames) return NULL;
    frame = style->frame_rate > 0 ? (uint64_t)((double)time_ms * style->frame_rate / 1000.0) : 0;
    return style->frames[frame % style->num_frames];
}

wc3WaterStyle_t const *R_WaterStyle(void) {
    return &g_water_style;
}

typedef struct {
    model_t *model;
    uint32_t count;
    char paths[256][512];
} model_texture_cache_t;

static model_texture_cache_t model_texture_cache = { 0 };

typedef struct {
    model_t *model;
    PATHSTR path;
} wc3AttachmentModel_t;

static wc3AttachmentModel_t wc3_attachment_models[32];
static uint32_t wc3_attachment_model_count;

/* Cache authored MDX attachment children because they can be visited every frame while a unit animates. */
static model_t *R_W3AttachmentModel(cstring_t path) {
    if (!path || !*path) return NULL;
    FOR_LOOP(i, wc3_attachment_model_count)
        if (!strcasecmp(wc3_attachment_models[i].path, path)) return wc3_attachment_models[i].model;
    if (wc3_attachment_model_count >= sizeof(wc3_attachment_models) / sizeof(*wc3_attachment_models)) {
        fprintf(stderr, "WC3 renderer: attachment model cache full for %s\n", path);
        return NULL;
    }
    wc3_attachment_models[wc3_attachment_model_count].model = R_LoadRegisteredModel(path);
    if (!wc3_attachment_models[wc3_attachment_model_count].model ||
        wc3_attachment_models[wc3_attachment_model_count].model->modeltype != ID_MDLX) {
        fprintf(stderr, "WC3 renderer: unable to load MDX attachment model %s\n", path);
        wc3_attachment_models[wc3_attachment_model_count].model = NULL;
    }
    strlcpy(wc3_attachment_models[wc3_attachment_model_count].path, path,
            sizeof(wc3_attachment_models[wc3_attachment_model_count].path));
    wc3_attachment_model_count++;
    return wc3_attachment_models[wc3_attachment_model_count - 1].model;
}

static mdxSequence_t const *R_W3AttachmentSequence(mdxModel_t const *model, cstring_t name) {
    mdxSequence_t const *seq;
    if (!model || !name) return NULL;
    seq = MDLX_FindSequenceByName(model, name);
    if (seq) return seq;
    FOR_LOOP(i, model->num_sequences)
        if (!strcasecmp(model->sequences[i].name, name)) return &model->sequences[i];
    return NULL;
}

/* Map the parent's authored Birth progress into a child Birth sequence with independent timing. */
static uint32_t R_W3AttachmentFrame(mdxModel_t const *parent, mdxModel_t const *child, uint32_t frame) {
    mdxSequence_t const *src = R_W3AttachmentSequence(parent, "Birth");
    mdxSequence_t const *dst = R_W3AttachmentSequence(child, "Birth");
    float ratio;
    uint32_t span;

    if (!src || !dst) return frame;
    span = MAX(1, src->interval[1] - src->interval[0]);
    ratio = MAX(0.0f, MIN(1.0f, (float)(frame - src->interval[0]) / (float)span));
    span = MAX(1, dst->interval[1] - dst->interval[0]);
    return dst->interval[0] + MIN(span - 1, (uint32_t)(ratio * (float)span));
}

static void R_W3RenderAttachmentModels(renderEntity_t const *entity, mat4_t const *transform) {
    mdxAttachmentPosition_t attachments[32];
    uint32_t count;

    if (!entity || !entity->model || !entity->model->mdx || !transform) return;
    count = MDLX_CollectAttachmentPositions(entity->model->mdx, transform, entity->frame,
                                             entity->oldframe, NULL, attachments,
                                             sizeof(attachments) / sizeof(*attachments));
    FOR_LOOP(i, count) {
        renderEntity_t child = *entity;
        model_t *model;

        if (!attachments[i].path[0]) continue;
        model = R_W3AttachmentModel(attachments[i].path);
        if (!model) continue;
        child.model = model;
        child.effect_model = NULL;
        child.frame = R_W3AttachmentFrame(entity->model->mdx, model->mdx, entity->frame);
        child.oldframe = R_W3AttachmentFrame(entity->model->mdx, model->mdx, entity->oldframe);
        child.flags |= RF_NO_SHADOW | RF_NO_FOGOFWAR | RF_NO_UBERSPLAT;
        MDX_RenderModel(&child, model->mdx, &attachments[i].transform);
    }
}

static bool R_W3PathHasExtension(cstring_t path, cstring_t extension) {
    size_t pathLen;
    size_t extLen;

    if (!path || !extension) {
        return false;
    }
    pathLen = strlen(path);
    extLen = strlen(extension);
    if (pathLen < extLen) {
        return false;
    }
    return !strcasecmp(path + pathLen - extLen, extension);
}

static void R_W3ReleaseSpawnModels(void) {
    FOR_LOOP(i, spawn_data_count) {
        if (!spawn_data_rows[i].model) continue;
        R_ReleaseRegisteredModel(spawn_data_rows[i].model);
        spawn_data_rows[i].model = NULL;
    }
}

/* MDX event-object tables (SPN/SPL/FPT/UBR) share one contract: the map archive's copy overrides the retail
 * table, and rows are keyed by the name in their first field. One loader and one lookup serve every family. */
static uint32_t R_W3LoadEventTable(wc3EventTable_t const *table) {
    PATHSTR scoped;
    uint32_t count = 0;

    if (R_MapAssetCandidate(table->path, scoped, sizeof(scoped)))
        count = ri.LoadSlk(scoped, table->schema, table->rows, table->size);
    if (!count) count = ri.LoadSlk(table->path, table->schema, table->rows, table->size);
    if (!count) fprintf(stderr, "WC3 renderer: failed to load %s for MDX %s events\n", table->path, table->family);
    return count;
}

static void *R_W3EventRow(void *rows, uint32_t count, uint32_t size, cstring_t id) {
    if (!id || !*id) return NULL;
    FOR_LOOP(i, count) {
        cstring_t const *name = (cstring_t const *)((uint8_t *)rows + (size_t)i * size);
        if (*name && !strcasecmp(*name, id)) return (void *)name;
    }
    return NULL;
}

static void R_W3FreeSpawnData(bool release_models) {
    if (release_models) R_W3ReleaseSpawnModels();
    FS_SLKFreeRows(spawn_data_schema, spawn_data_rows, spawn_data_count, sizeof(wc3SpawnData_t));
    spawn_data_rows = NULL; spawn_data_count = 0;
}

static void R_W3LoadSpawnData(void) {
    R_W3FreeSpawnData(true);
    spawn_data_count = R_W3LoadEventTable(&MAKE(wc3EventTable_t, .path = "Splats\\SpawnData.slk", .family = "SPN",
        .schema = spawn_data_schema, .rows = (void **)&spawn_data_rows, .size = sizeof(wc3SpawnData_t)));
}

static wc3SpawnData_t *R_W3SpawnData(cstring_t id) {
    return R_W3EventRow(spawn_data_rows, spawn_data_count, sizeof(wc3SpawnData_t), id);
}

static model_t *R_W3SpawnModel(wc3SpawnData_t *row) {
    if (!row || !row->model_path || !row->model_path[0]) return NULL;
    if (!row->model) row->model = R_LoadRegisteredModel(row->model_path);
    return row->model && row->model->modeltype == ID_MDLX && row->model->mdx ? row->model : NULL;
}

static void R_W3FreeSplatData(void) {
    FS_SLKFreeRows(splat_data_schema, splat_data_rows, splat_data_count, sizeof(wc3SplatData_t));
    splat_data_rows = NULL; splat_data_count = 0;
}

static void R_W3LoadSplatData(void) {
    R_W3FreeSplatData();
    splat_data_count = R_W3LoadEventTable(&MAKE(wc3EventTable_t, .path = "Splats\\SplatData.slk", .family = "SPL/FPT",
        .schema = splat_data_schema, .rows = (void **)&splat_data_rows, .size = sizeof(wc3SplatData_t)));
}

static wc3SplatData_t *R_W3SplatData(cstring_t id) {
    return R_W3EventRow(splat_data_rows, splat_data_count, sizeof(wc3SplatData_t), id);
}

/* Resolve and cache one data-row texture; R_LoadTexture owns missing-asset placeholders/logging. */
static texture_t const *R_W3LoadSplatTexture(wc3SplatTextureParams_t const *params) {
    PATHSTR path;

    if (!params || !params->texture || !params->attempted) return NULL;
    if (*params->texture || *params->attempted) return *params->texture;
    *params->attempted = true;
    if (!params->dir || !params->file || !params->dir[0] || !params->file[0]) {
        fprintf(stderr, "WC3 renderer: %s row '%s' has no splat texture path\n",
                params->family ? params->family : "splat", params->name ? params->name : "(unnamed)");
        return NULL;
    }
    snprintf(path, sizeof(path), "%s\\%s.blp", params->dir, params->file);
    *params->texture = R_LoadTexture(path);
    return *params->texture;
}

static texture_t const *R_W3SplatTexture(wc3SplatData_t *row) {
    if (!row) return NULL;
    return R_W3LoadSplatTexture(&MAKE(wc3SplatTextureParams_t,
        .family = "SplatData", .name = row->name, .dir = row->dir, .file = row->file,
        .texture = &row->texture, .attempted = &row->texture_attempted));
}

static bool R_W3SplatHasSound(cstring_t sound) {
    return sound && sound[0] && strcasecmp(sound, "NULL") && strcmp(sound, "-") && strcmp(sound, "_");
}

/* TODO: Repeat counts, non-default blend modes, water placement, and row sounds
 * need verified retail contracts. Preserve them and warn once instead of silently
 * pretending the implemented atlas/lifetime subset is complete. */
static void R_W3WarnUnsupportedSplatFields(wc3SplatData_t *row) {
    bool unsupported_blend;

    if (!row || row->unsupported_warned) return;
    unsupported_blend = row->blend_mode && row->blend_mode[0] && strcmp(row->blend_mode, "0");
    if (!row->lifespan_repeat && !row->decay_repeat && !row->water &&
        !R_W3SplatHasSound(row->sound) && !unsupported_blend) return;
    row->unsupported_warned = true;
    fprintf(stderr, "WC3 renderer: SplatData '%s' uses unsupported retained fields\n",
            row->name ? row->name : "(unnamed)");
}

static void R_W3WarnZeroSplatLifetime(wc3SplatData_t *row) {
    if (!row || row->zero_duration_warned || row->lifespan > 0.0f || row->decay_time > 0.0f) return;
    row->zero_duration_warned = true;
    if (R_W3EventWarningShouldLog("zero-splat-lifetime", row->name ? row->name : "(unnamed)"))
        fprintf(stderr, "WC3 renderer: SplatData '%s' has zero lifetime; one-frame display is unverified\n",
                row->name ? row->name : "(unnamed)");
}

static void R_W3FreeUberSplatData(void) {
    FS_SLKFreeRows(uber_splat_data_schema, uber_splat_rows, uber_splat_count, sizeof(wc3UberSplatData_t));
    uber_splat_rows = NULL; uber_splat_count = 0;
}

static void R_W3LoadUberSplatData(void) {
    R_W3FreeUberSplatData();
    uber_splat_count = R_W3LoadEventTable(&MAKE(wc3EventTable_t, .path = "Splats\\UberSplatData.slk", .family = "UBR",
        .schema = uber_splat_data_schema, .rows = (void **)&uber_splat_rows, .size = sizeof(wc3UberSplatData_t)));
}

static wc3UberSplatData_t *R_W3UberSplatData(cstring_t id) {
    return R_W3EventRow(uber_splat_rows, uber_splat_count, sizeof(wc3UberSplatData_t), id);
}

static texture_t const *R_W3UberSplatTexture(wc3UberSplatData_t *row) {
    if (!row) return NULL;
    return R_W3LoadSplatTexture(&MAKE(wc3SplatTextureParams_t,
        .family = "UberSplatData", .name = row->name, .dir = row->dir, .file = row->file,
        .texture = &row->texture, .attempted = &row->texture_attempted));
}

/* TODO: UBR BlendMode values other than the existing alpha-blend path and the
 * optional Sound field need a verified retail renderer/audio mapping. */
static void R_W3WarnUnsupportedUberSplatFields(wc3UberSplatData_t *row) {
    bool unsupported_blend;

    if (!row || row->unsupported_warned) return;
    unsupported_blend = row->blend_mode && row->blend_mode[0] && strcmp(row->blend_mode, "0");
    if (!R_W3SplatHasSound(row->sound) && !unsupported_blend) return;
    row->unsupported_warned = true;
    fprintf(stderr, "WC3 renderer: UberSplatData '%s' uses unsupported BlendMode/Sound fields\n",
            row->name ? row->name : "(unnamed)");
}

static void R_W3WarnZeroUberSplatLifetime(wc3UberSplatData_t *row) {
    if (!row || row->zero_duration_warned || row->birth_time > 0.0f ||
        row->pause_time > 0.0f || row->decay_time > 0.0f) return;
    row->zero_duration_warned = true;
    if (R_W3EventWarningShouldLog("zero-uber-splat-lifetime", row->name ? row->name : "(unnamed)"))
        fprintf(stderr, "WC3 renderer: UberSplatData '%s' has zero lifetime; one-frame display is unverified\n",
                row->name ? row->name : "(unnamed)");
}

static void R_W3ClearEventSplats(void) {
    memset(event_splats, 0, sizeof(event_splats));
    event_splat_serial = 0;
}

static void R_W3ClearEventSpawns(void) {
    memset(event_spawns, 0, sizeof(event_spawns));
    event_spawn_serial = 0;
}

static void R_W3ClearCameraSpawns(model_t const *source_model, uintptr_t source_instance_id) {
    FOR_LOOP(i, WC3_EVENT_SPAWN_MAX) {
        wc3EventSpawn_t *spawn = event_spawns + i;
        if (spawn->active && spawn->entity_camera && spawn->source_model == source_model &&
            spawn->source_instance_id == source_instance_id) spawn->active = false;
    }
}

static void R_W3ClearCameraEventStates(void) {
    if (camera_event_states) {
        if (!ri.MemFree) {
            fprintf(stderr, "WC3 renderer: cannot free entity-camera event states without MemFree\n");
            memset(camera_event_states, 0, camera_event_state_capacity * sizeof(*camera_event_states));
            camera_event_state_count = 0;
            return;
        }
        ri.MemFree(camera_event_states);
    }
    camera_event_states = NULL;
    camera_event_state_count = camera_event_state_capacity = 0;
}

static wc3EventState_t *R_W3CameraEventState(renderEntity_t const *entity) {
    wc3CameraEventState_t *state;

    if (!entity || !entity->instance_id) {
        static bool missing_identity_logged;
        if (!missing_identity_logged) {
            fprintf(stderr, "WC3 renderer: entity-camera MDX events require a stable instance_id\n");
            missing_identity_logged = true;
        }
        return NULL;
    }
    FOR_LOOP(i, camera_event_state_count) {
        state = camera_event_states + i;
        if (state->instance_id != entity->instance_id) continue;
        if (state->state.model != entity->model || state->state.generation != entity->generation) {
            R_W3ClearCameraSpawns(state->state.model, state->instance_id);
            state->state = (wc3EventState_t){0};
        }
        return &state->state;
    }
    if (camera_event_state_count == camera_event_state_capacity) {
        size_t capacity = camera_event_state_capacity ? camera_event_state_capacity * 2 : 16;
        if (capacity < camera_event_state_capacity ||
            capacity > (size_t)LONG_MAX / sizeof(*camera_event_states)) {
            fprintf(stderr, "WC3 renderer: entity-camera event-state capacity overflow\n");
            return NULL;
        }
        if (!ri.MemAlloc || !ri.MemFree) {
            fprintf(stderr, "WC3 renderer: entity-camera event states require MemAlloc and MemFree\n");
            return NULL;
        }
        wc3CameraEventState_t *states = ri.MemAlloc((long)(capacity * sizeof(*states)));
        if (!states) {
            fprintf(stderr, "WC3 renderer: failed to grow entity-camera event states to %zu entries\n", capacity);
            return NULL;
        }
        memset(states, 0, capacity * sizeof(*states));
        if (camera_event_state_count)
            memcpy(states, camera_event_states, camera_event_state_count * sizeof(*states));
        if (camera_event_states) ri.MemFree(camera_event_states);
        camera_event_states = states;
        camera_event_state_capacity = capacity;
    }
    state = camera_event_states + camera_event_state_count++;
    /* A compacted table slot owns no clock until this new instance seeds it. */
    *state = (wc3CameraEventState_t){ .instance_id = entity->instance_id, .state = { .model = entity->model, .generation = entity->generation } };
    return &state->state;
}

void R_ReleaseGameEntityCameraEvents(uintptr_t instance_id) {
    FOR_LOOP(i, camera_event_state_count) {
        wc3CameraEventState_t *state = camera_event_states + i;
        if (state->instance_id != instance_id) continue;
        R_W3ClearCameraSpawns(state->state.model, state->instance_id);
        camera_event_state_count--;
        if (i != camera_event_state_count)
            camera_event_states[i] = camera_event_states[camera_event_state_count];
        return;
    }
}

/* Keep repeated authored-event failures visible without printing on every animation loop. */
static bool R_W3EventWarningShouldLog(cstring_t kind, cstring_t name) {
    if (!kind || !name) return false;
    FOR_LOOP(i, event_warning_count)
        if (!strcmp(event_warnings[i].kind, kind) && !strcmp(event_warnings[i].name, name)) return false;
    if (event_warning_count >= WC3_EVENT_WARNING_MAX) {
        if (!event_warning_overflow_logged) {
            fprintf(stderr, "WC3 renderer: MDX event warning cache full; suppressing further event warnings\n");
            event_warning_overflow_logged = true;
        }
        return false;
    }
    strlcpy(event_warnings[event_warning_count].kind, kind, sizeof(event_warnings[0].kind));
    strlcpy(event_warnings[event_warning_count].name, name, sizeof(event_warnings[0].name));
    event_warning_count++;
    return true;
}

static void R_W3ClearEventWarnings(void) {
    memset(event_warnings, 0, sizeof(event_warnings));
    event_warning_count = 0;
    event_warning_overflow_logged = false;
}

void R_LoadAssets(void) {
    FOR_LOOP(i, MODEL_COUNT) {
        tr.model[i] = R_LoadModel(modelNames[i]);
    }
    FS_SLKFreeIndex(&g_terrain_idx);
    FS_SLKFreeRows(terrain_schema, g_terrain_rows, g_terrain_count, sizeof(w3TerrainArt_t));
    g_terrain_count = ri.LoadSlk("TerrainArt\\Terrain.slk", terrain_schema, (void **)&g_terrain_rows, sizeof(w3TerrainArt_t));
    if (!g_terrain_count) fprintf(stderr, "Renderer: failed to load TerrainArt\\Terrain.slk\n");
    FS_SLKBuildIndex(&g_terrain_idx, g_terrain_rows, g_terrain_count, sizeof(w3TerrainArt_t));
    FS_SLKFreeIndex(&g_cliff_idx);
    FS_SLKFreeRows(cliff_schema, g_cliff_rows, g_cliff_count, sizeof(w3CliffType_t));
    g_cliff_count = ri.LoadSlk("TerrainArt\\CliffTypes.slk", cliff_schema, (void **)&g_cliff_rows, sizeof(w3CliffType_t));
    if (!g_cliff_count) fprintf(stderr, "Renderer: failed to load TerrainArt\\CliffTypes.slk\n");
    FS_SLKBuildIndex(&g_cliff_idx, g_cliff_rows, g_cliff_count, sizeof(w3CliffType_t));
    FS_SLKFreeRows(anim_lookup_schema, anim_lookup_rows, anim_lookup_count, sizeof(wc3AnimLookup_t));
    FS_SLKFreeRows(anim_sound_schema, anim_sound_rows, anim_sound_count, sizeof(wc3AnimSound_t));
    anim_lookup_rows = NULL; anim_lookup_count = 0;
    anim_sound_rows = NULL; anim_sound_count = 0;
    anim_lookup_count = ri.LoadSlk("UI\\SoundInfo\\AnimLookups.slk", anim_lookup_schema,
                                   (void **)&anim_lookup_rows, sizeof(wc3AnimLookup_t));
    anim_sound_count = ri.LoadSlk("UI\\SoundInfo\\AnimSounds.slk", anim_sound_schema,
                                  (void **)&anim_sound_rows, sizeof(wc3AnimSound_t));
    R_W3ClearEventSpawns();
    R_W3ClearCameraEventStates();
    R_W3ClearEventSplats();
    R_W3ClearEventWarnings();
    R_W3LoadSpawnData();
    R_W3LoadSplatData();
    R_W3LoadUberSplatData();
    memset(event_state, 0, sizeof(event_state));

    FOR_LOOP(i, NUM_SELECTION_CIRCLES) {
        tr.texture[TEX_SELECTION_CIRCLE+i] = R_LoadTexture(selCirclesNames[i]);
    }
    FOR_LOOP(team, MAX_TEAMS) {
        PATHSTR glowFilename, colorFilename;
        snprintf(glowFilename, sizeof(glowFilename), "ReplaceableTextures\\TeamGlow\\TeamGlow%02d.blp", team);
        snprintf(colorFilename, sizeof(colorFilename), "ReplaceableTextures\\TeamColor\\TeamColor%02d.blp", team);
        tr.texture[TEX_TEAM_GLOW + team] = R_LoadTexture(glowFilename);
        tr.texture[TEX_TEAM_COLOR + team] = R_LoadTexture(colorFilename);
    }
}

void R_Init(void) {
    cursor_model = NULL; cursor_active_model = NULL; cursor_load_attempted = false; cursor_anim = NULL;
    cursor_model_name[0] = 0;
    R_WeatherInit();
    R_LightningInit();
    MDLX_Init();
}

void R_Shutdown(void) {
    _W3M_ClearMap();
    if (preview.mmp) ri.FS_FreeFile(preview.mmp);
    memset(&preview, 0, sizeof(preview));
    Stb_IniCacheFree(&minimap_theme);
    Stb_IniCacheFree(&minimap_map_skin);
    memset(minimap_special, 0, sizeof(minimap_special));
    /* R_ShutdownModels runs first and owns the cached model allocation; only clear our borrowed handle here. */
    cursor_model = NULL; cursor_active_model = NULL; cursor_load_attempted = false; cursor_anim = NULL;
    cursor_model_name[0] = 0;
    memset(wc3_attachment_models, 0, sizeof(wc3_attachment_models));
    wc3_attachment_model_count = 0;
    FS_SLKFreeIndex(&g_terrain_idx);
    FS_SLKFreeRows(terrain_schema, g_terrain_rows, g_terrain_count, sizeof(w3TerrainArt_t));
    g_terrain_rows = NULL; g_terrain_count = 0;
    FS_SLKFreeIndex(&g_cliff_idx);
    FS_SLKFreeRows(cliff_schema, g_cliff_rows, g_cliff_count, sizeof(w3CliffType_t));
    g_cliff_rows = NULL; g_cliff_count = 0;
    FS_SLKFreeRows(anim_lookup_schema, anim_lookup_rows, anim_lookup_count, sizeof(wc3AnimLookup_t));
    FS_SLKFreeRows(anim_sound_schema, anim_sound_rows, anim_sound_count, sizeof(wc3AnimSound_t));
    anim_lookup_rows = NULL; anim_lookup_count = 0;
    anim_sound_rows = NULL; anim_sound_count = 0;
    R_W3ClearEventSpawns();
    R_W3ClearCameraEventStates();
    R_W3ClearEventSplats();
    R_W3ClearEventWarnings();
    R_W3FreeSpawnData(false);
    R_W3FreeSplatData();
    R_W3FreeUberSplatData();
    memset(event_state, 0, sizeof(event_state));
    R_WeatherShutdown();
    R_LightningShutdown();
    MDLX_Shutdown();
}

w3TerrainArt_t const *R_TerrainArt(uint32_t id) {
    static w3TerrainArt_t zero;
    w3TerrainArt_t *row = FS_SLKLookup(&g_terrain_idx, id);
    return row ? row : &zero;
}

w3CliffType_t const *R_CliffType(uint32_t id) {
    static w3CliffType_t zero;
    w3CliffType_t *row = FS_SLKLookup(&g_cliff_idx, id);
    return row ? row : &zero;
}

void R_SetupTextureMatrix(void) {
    if (tr.world) {
        vec2_t s = GetWar3MapSize(tr.world);
        vec2_t c = tr.world->center;
        Matrix4_ortho(&tr.viewDef.textureMatrix, -s.x+c.x, s.x+c.x, -s.y+c.y, s.y+c.y, 0.0f, 100.0f);
    } else {
        Matrix4_identity(&tr.viewDef.textureMatrix);
    }
}

/* Loading cannot depend on terrain registration; cache only the destination archive's authored preview assets. */
static void load_preview(cstring_t map) {
    PATHSTR path;
    void *blob = NULL;
    int size;
    if (!strcmp(preview.map, map)) return;
    if (preview.mmp) ri.FS_FreeFile(preview.mmp);
    memset(&preview, 0, sizeof(preview));
    strlcpy(preview.map, map, sizeof(preview.map));
    static cstring_t const images[] = { "war3mapMap.blp", "war3mapMap.tga" };
    FOR_LOOP(i, sizeof(images) / sizeof(images[0])) {
        snprintf(path, sizeof(path), "%s\\%s", map, images[i]);
        if (ri.FS_ReadFile(path, &blob) < 0 || !blob) continue;
        ri.FS_FreeFile(blob); blob = NULL;
        preview.image = R_LoadTexture(path);
        if (!preview.image) fprintf(stderr, "Minimap preview: failed to load %s\n", path);
        break;
    }
    if (!preview.image) fprintf(stderr, "Minimap preview: %s has no readable war3mapMap image\n", map);
    snprintf(path, sizeof(path), "%s\\war3map.mmp", map);
    size = ri.FS_ReadFile(path, &blob);
    if (size < 0 || !blob) {
        fprintf(stderr, "Minimap preview: %s has no icon data\n", map);
        return;
    }
    mmp_t *mmp = blob;
    if (size < sizeof(mmp_t) || mmp->version != 0 || mmp->count > (size - sizeof(mmp_t)) / sizeof(mmpIcon_t)) {
        fprintf(stderr, "Minimap preview: invalid MMP header/length in %s\n", path);
        ri.FS_FreeFile(blob);
        return;
    }
    preview.mmp = mmp;
    FOR_LOOP(i, mmp->count) {
        uint32_t kind = mmp->icons[i].kind;
        if (kind >= sizeof(preview_art) / sizeof(preview_art[0])) {
            fprintf(stderr, "Minimap preview: unknown icon %u in %s\n", kind, path);
            continue;
        }
        if (!preview.icons[kind]) {
            preview.icons[kind] = R_LoadTexture(preview_art[kind]);
            if (!preview.icons[kind]) fprintf(stderr, "Minimap preview: missing %s\n", preview_art[kind]);
        }
    }
}

/* MMP positions include the thumbnail's letterboxing, so project in image space, without world/fog state. */
static void draw_preview(rect_t const *screen, cstring_t map) {
    load_preview(map);
    if (preview.image) R_DrawImage(preview.image, screen, &MAKE(rect_t, 0, 0, 1, 1), COLOR32_WHITE);
    if (!preview.mmp) return;
    FOR_LOOP(i, preview.mmp->count) {
        mmpIcon_t const *icon = &preview.mmp->icons[i];
        if (icon->kind >= sizeof(preview_art) / sizeof(preview_art[0]) || !preview.icons[icon->kind]) continue;
        rect_t rect = { screen->x + (icon->x - BZ_MMP_ICON_SIZE / 2) / BZ_MMP_CANVAS * screen->w,
                      screen->y + (icon->y - BZ_MMP_ICON_SIZE / 2) / BZ_MMP_CANVAS * screen->h,
                      BZ_MMP_ICON_SIZE / BZ_MMP_CANVAS * screen->w, BZ_MMP_ICON_SIZE / BZ_MMP_CANVAS * screen->h };
        color32_t color = { icon->bgra.b, icon->bgra.g, icon->bgra.r, icon->bgra.a };
        R_DrawImage(preview.icons[icon->kind], &rect, &MAKE(rect_t, 0, 0, 1, 1), color);
    }
}

typedef enum {
    WC3_INI_MISSING = -1,
    WC3_INI_INVALID,
    WC3_INI_LOADED,
} wc3IniLoadResult_t;

static wc3IniLoadResult_t R_LoadIniCachePath(stbIniCache_t *cache, cstring_t path) {
    void *file = NULL;
    string_t text;
    int size;
    bool loaded;

    if (!cache || !path || !*path) return WC3_INI_MISSING;
    size = ri.FS_ReadFile(path, &file);
    if (size < 0 || !file) return WC3_INI_MISSING;
    text = ri.MemAlloc((long)size + 1);
    if (!text) {
        ri.FS_FreeFile(file);
        fprintf(stderr, "WC3 minimap: failed to allocate INI buffer for %s\n", path);
        return WC3_INI_INVALID;
    }
    memcpy(text, file, (size_t)size);
    text[size] = '\0';
    loaded = Stb_IniCacheLoadBuffer(cache, text);
    ri.MemFree(text);
    ri.FS_FreeFile(file);
    if (!loaded) fprintf(stderr, "WC3 minimap: failed to parse %s\n", path);
    return loaded ? WC3_INI_LOADED : WC3_INI_INVALID;
}

/* A map archive replacement wins; missing map data falls back to the base
 * archive. Invalid overrides are diagnosed and returned to the caller so it
 * can apply the field's explicit optional-data policy. */
static wc3IniLoadResult_t R_LoadIniCache(stbIniCache_t *cache, cstring_t path) {
    PATHSTR scoped;
    wc3IniLoadResult_t result;

    if (!cache || !path || !*path) return WC3_INI_MISSING;
    if (R_MapAssetCandidate(path, scoped, sizeof(scoped))) {
        result = R_LoadIniCachePath(cache, scoped);
        if (result != WC3_INI_MISSING) return result;
    }
    return R_LoadIniCachePath(cache, path);
}

static void R_ClearMinimapSpecialAssets(void) {
    Stb_IniCacheFree(&minimap_theme);
    Stb_IniCacheFree(&minimap_map_skin);
    memset(minimap_special, 0, sizeof(minimap_special));
}

static void *R_LoadMinimapTexturePinned(void *context, cstring_t path) {
    (void)context;
    return R_LoadTexture(path);
}

static void *R_LoadMinimapTextureMapScoped(void *context, cstring_t path) {
    (void)context;
    return R_LoadTextureStreamed(path);
}

/* Resolve map CustomSkin before stock defaults. Map overrides are streamable;
 * stock Game Interface textures stay pinned across map registrations. */
static void R_LoadMinimapSpecialAssets(void) {
    wc3IniLoadResult_t const theme_result = R_LoadIniCache(&minimap_theme, "UI\\war3skins.txt");
    wc3MinimapSpecialAsset_t assets[WC3_MINIMAP_CONTACT_NEUTRAL_BUILDING - WC3_MINIMAP_CONTACT_HERO + 1];

    /* A malformed optional map override is diagnosed by R_LoadIniCachePath;
     * its empty cache lets valid stock defaults supply the missing fields. */
    R_LoadIniCache(&minimap_map_skin, "war3mapSkin.txt");
    if (theme_result == WC3_INI_MISSING)
        fprintf(stderr, "WC3 minimap: missing UI\\war3skins.txt\n");

    uint32_t const count = wc3_minimap_special_assets(&minimap_theme, &minimap_map_skin,
                                                    assets, sizeof(assets) / sizeof(assets[0]));
    FOR_LOOP(i, count) {
        wc3MinimapSpecialAsset_t const *asset = &assets[i];
        cstring_t const path = asset->path;
        if (!path || !*path) {
            fprintf(stderr, "WC3 minimap: missing/empty Game Interface key %s\n", asset->key ? asset->key : "<null>");
            minimap_special[asset->contact] = tr.texture[TEX_PLACEHOLDER];
            continue;
        }
        minimap_special[asset->contact] = wc3_minimap_register_special_asset(
            asset, tr.texture[TEX_PLACEHOLDER], NULL,
            R_LoadMinimapTexturePinned, R_LoadMinimapTextureMapScoped);
    }
}

static uint32_t R_MinimapAllyColorFilter(void) {
    return MIN((uint32_t)tr.viewDef.game_variant, (uint32_t)WC3_MINIMAP_ALLY_COLOR_WORLD);
}

static bool R_MinimapUsesAllianceColors(void) {
    return R_MinimapAllyColorFilter() >= WC3_MINIMAP_ALLY_COLOR_MINIMAP;
}

static wc3MinimapColorKind_t R_MinimapColorKind(renderEntity_t const *entity) {
    wc3MinimapColorParams_t const params = {
        .owner = entity ? entity->owner : 0,
        .viewer = tr.viewDef.player,
        .filter = R_MinimapAllyColorFilter(),
        .hostile = entity && (entity->flags & RF_HOSTILE),
    };
    return wc3_minimap_ordinary_color_kind(&params);
}

static color32_t R_MinimapAllianceColor(renderEntity_t const *entity) {
    wc3MinimapColorKind_t const kind = R_MinimapColorKind(entity);
    switch (kind) {
    case WC3_MINIMAP_COLOR_SELF_WHITE: return COLOR32_WHITE;
    case WC3_MINIMAP_COLOR_ENEMY_RED: return MAKE(color32_t, 255, 3, 3, 255);
    case WC3_MINIMAP_COLOR_NEUTRAL_BLACK: return MAKE(color32_t, 0, 0, 0, 255);
    case WC3_MINIMAP_COLOR_ALLY_TEAL: return MAKE(color32_t, 28, 230, 185, 255);
    case WC3_MINIMAP_COLOR_TEAM:
    default:
        return COLOR32_WHITE;
    }
}

static texture_t const *R_MinimapOrdinaryContactTexture(renderEntity_t const *entity, color32_t *color) {
    wc3MinimapColorKind_t const kind = R_MinimapColorKind(entity);
    if (kind == WC3_MINIMAP_COLOR_TEAM) {
        *color = COLOR32_WHITE;
        return tr.texture[TEX_TEAM_COLOR + (entity->team & TEAM_MASK)];
    }
    *color = R_MinimapAllianceColor(entity);
    return tr.texture[TEX_WHITE];
}

/* Draw one automatic WC3 contact from recipient-authored snapshot metadata.
 * Shared/client code carries the game variant opaquely; only this WC3 renderer
 * assigns minimap semantics to it. */
static void R_DrawMinimapEntityMarker(renderEntity_t const *entity) {
    wc3MinimapContact_t const contact = wc3_minimap_contact_get(entity ? entity->effect_flags : 0);
    texture_t const *texture = NULL;
    color32_t color = COLOR32_WHITE;
    vec2_t point, world, size;
    rect_t marker;

    if (!entity || !entity->number || contact == WC3_MINIMAP_CONTACT_NONE ||
        (entity->flags & RF_HIDDEN)) return;
    world = MAKE(vec2_t, entity->origin.x, entity->origin.y);
    if (!R_WorldToMinimap(&world, &point)) return;

    size = wc3_minimap_marker_size(contact);
    switch (contact) {
    case WC3_MINIMAP_CONTACT_HERO:
        texture = minimap_special[contact];
        if (R_MinimapUsesAllianceColors()) color = R_MinimapAllianceColor(entity);
        break;
    case WC3_MINIMAP_CONTACT_GOLD_MINE:
    case WC3_MINIMAP_CONTACT_GOLD_ENTANGLED:
    case WC3_MINIMAP_CONTACT_GOLD_HAUNTED:
    case WC3_MINIMAP_CONTACT_NEUTRAL_BUILDING:
        texture = minimap_special[contact];
        break;
    case WC3_MINIMAP_CONTACT_BUILDING:
    case WC3_MINIMAP_CONTACT_UNIT:
        texture = R_MinimapOrdinaryContactTexture(entity, &color);
        break;
    default:
        return;
    }

    if (!texture || size.x <= 0.0f || size.y <= 0.0f) return;
    marker = wc3_minimap_marker_rect(&point, contact);
    R_DrawImage(texture, &marker, &MAKE(rect_t, 0, 0, 1, 1), color);
}

static void R_DrawMinimapEntityMarkers(void) {
    if (!tr.viewDef.entities) return;
    FOR_LOOP(i, tr.viewDef.num_entities)
        R_DrawMinimapEntityMarker(&tr.viewDef.entities[i]);
}

void R_DrawMinimap(rect_t const *screen, cstring_t map) {
    if (map) { draw_preview(screen, map); return; }
    texture_t const *tex = tr.minimap ? tr.minimap : tr.texture[TEX_WHITE];
    vec2_t const map_size = R_WorldSize();
    rect_t const content = WC3_MinimapContentRect(screen, &map_size);

    /* The authored war3mapMap texture fills the frame. World-space overlays
     * (fog, camera, pings, click projection) use the centred map-aspect area. */
    R_DrawImage(tex, screen, &MAKE(rect_t, 0, 0, 1, 1), COLOR32_WHITE);
    tr.minimapRect = content;

    if (tr.world && tr.shader_minimapFog.prog.progid) {
        uint32_t const fow_texid = R_GetMinimapFogOfWarTexture();
        if (fow_texid && (!tr.texture[TEX_WHITE] || fow_texid != tr.texture[TEX_WHITE]->texid)) {
            texture_t fog_texture = {
                .texid = fow_texid,
                .width = (tr.world->width - 1) * 4,
                .height = (tr.world->height - 1) * 4,
            };
            R_DrawImageEx(&MAKE(drawImage_t,
                                .texture = &fog_texture,
                                .screen = content,
                                .uv = MAKE(rect_t, 0, 0, 1, 1),
                                .color = MAKE(color32_t, 0, 0, 0, 230),
                                .shader = SHADER_MINIMAP_FOG,
                                .alphamode = BLEND_MODE_BLEND));
        }
    }

    /* Contacts draw over the fog texture after the game/server has already
     * decided which entities this recipient is allowed to know about. */
    R_DrawMinimapEntityMarkers();
    R_DrawMinimapCameraRect(&content);
    /* Draw last so the border remains visible over the map and camera overlay.
     * It outlines the actual aspect-preserving map area, not letterbox margins. */
    R_DrawMinimapBorder(&content, MAKE(color32_t, 192, 192, 192, 255));
}

void R_RegisterMap(cstring_t mapFileName) {
    cursor_active_model = NULL; cursor_anim = NULL;
    R_SetMapAssetScope(mapFileName);
    R_AdvanceTextureGeneration();
    R_W3ClearEventSpawns();
    R_W3ClearCameraEventStates();
    R_W3ClearEventSplats();
    R_W3ClearEventWarnings();
    R_W3LoadSpawnData();
    R_W3LoadSplatData();
    R_W3LoadUberSplatData();
    memset(event_state, 0, sizeof(event_state));
    memset(&model_texture_cache, 0, sizeof(model_texture_cache));
    R_ClearMinimapSpecialAssets();
    if (mapFileName && *mapFileName) R_LoadMinimapSpecialAssets();
    R_WeatherRegisterMap();
    R_LightningRegisterMap();
    _W3M_RegisterMap(mapFileName);
    R_ReclaimStreamedTextures(0);
}

void R_SetupEnvironmentLighting(void) {
    rModelLight_t light;
    tr.viewDef.terrainLight = (environLight_t){0};
    tr.viewDef.entityLight = (environLight_t){0};
    if (MDLX_SampleFirstLight(tr.viewDef.terrainLightModel, tr.viewDef.environmentPhase, &light))
        tr.viewDef.terrainLight = R_EnvironLightFromModel(&light);
    if (MDLX_SampleFirstLight(tr.viewDef.entityLightModel, tr.viewDef.environmentPhase, &light))
        tr.viewDef.entityLight = R_EnvironLightFromModel(&light);
}

void R_DrawWorld(void) {
    _W3M_DrawWorld();
    R_W3DrawEventSplats();
    R_W3DrawEventSpawns(false, NULL, 0);
}

void R_DrawEntityCameraEventSpawns(model_t const *source_model, uintptr_t source_instance_id) {
    R_W3DrawEventSpawns(true, source_model, source_instance_id);
}

void R_DrawTerrainShadows(void) {
    _W3M_DrawTerrainShadows();
}

void R_DrawAlphaSurfaces(void) {
    _W3M_DrawAlphaSurfaces();
    R_LightningDraw();
    R_WeatherEmit();

    /* MDX blended geosets must composite over water. Opaque geosets were
     * already rendered during the ordinary entity pass; RENDER_PHASE_ALPHA
     * selects only transparent model layers and the alpha of tinted units. */
    FOR_LOOP(i, tr.viewDef.num_entities) {
        renderEntity_t const *entity = tr.viewDef.entities + i;
        if ((entity->flags & RF_HIDDEN) || !entity->model || entity->model->modeltype != ID_MDLX)
            continue;
        R_RenderModel(entity);
    }
}

bool R_TraceLocation(viewDef_t const *viewdef, float x, float y, vec3_t *point) {
    return _W3M_TraceLocation(viewdef, x, y, point);
}

float R_GetHeightAtPoint(float x, float y) {
    return R_W3TerrainHeightAtPoint(x, y);
}

float R_GetCameraHeightAtPoint(float x, float y) {
    return R_W3CameraHeightAtPoint(x, y);
}
void R_StartTerrainDeformation(terrainDeform_t const *deformation) { R_W3StartTerrainDeformation(deformation); }
void R_StopTerrainDeformation(uint32_t id, uint32_t fade_ms) { R_W3StopTerrainDeformation(id, fade_ms); }
void R_StopAllTerrainDeformations(void) { R_W3StopAllTerrainDeformations(); }
bool R_CameraUsesTerrainHeight(void) { return true; }


static bool R_W3WalkableSurfaceHit(renderEntity_t const *surface, float x, float y, float *z) {
    line3_t line;
    vec3_t hit;

    if (!surface || !z || !(surface->flags & RF_GROUND_SURFACE) ||
        (surface->flags & RF_HIDDEN) || !surface->model) {
        return false;
    }

    line = (line3_t){
        .a = { x, y, surface->origin.z + 4096.0f },
        .b = { x, y, surface->origin.z - 4096.0f },
    };
    if (!MDLX_TraceWalkableSurface(surface, &line, &hit)) {
        return false;
    }
    *z = hit.z;
    return true;
}

/* Warsmash keeps walkable destructable height as presentation state: the
 * simulation decides whether a unit may traverse the bridge, while the model
 * collision/geoset geometry supplies the exact visual support Z. Preserve the
 * explicit server-authored altitude offset (WC3 FlyHeight) and replace only the
 * coarse destructable-origin support height with the highest authored MDX hit. */
void R_ConformGroundSurfaces(viewDef_t *viewdef) {
    renderEntity_t const *local_surfaces[128];
    renderEntity_t const **surfaces = local_surfaces;
    uint32_t count = 0;

    if (!viewdef || (viewdef->rdflags & RDF_NOWORLDMODEL)) return;

    /* Snapshot entities are mostly units and ordinary scenery. Compact the
     * actual support providers once, retaining snapshot order. In particular,
     * a world without bridges costs one linear pass, not units * entities.
     * Keep exact mesh queries: general model bounds need not enclose the
     * selectable geosets used by the walkable-surface trace. */
    FOR_LOOP(i, viewdef->num_entities) {
        renderEntity_t const *ent = &viewdef->entities[i];
        if ((ent->flags & RF_GROUND_SURFACE) && !(ent->flags & RF_HIDDEN) && ent->model)
            count++;
    }
    if (!count) return;
    if (count > sizeof(local_surfaces) / sizeof(*local_surfaces))
        surfaces = ri.MemAlloc((long)((size_t)count * sizeof(*surfaces)));
    if (!surfaces) {
        fprintf(stderr, "WC3 renderer: cannot allocate walkable surface list\n");
        abort();
    }
    count = 0;
    FOR_LOOP(i, viewdef->num_entities) {
        renderEntity_t const *ent = &viewdef->entities[i];
        if ((ent->flags & RF_GROUND_SURFACE) && !(ent->flags & RF_HIDDEN) && ent->model)
            surfaces[count++] = ent;
    }

    FOR_LOOP(i, viewdef->num_entities) {
        renderEntity_t *ent = &viewdef->entities[i];
        float authored_support = 0.0f;
        bool found_surface = false;

        ent->flags &= ~RF_GROUND_SURFACE_SUPPORT;

        if (!(ent->flags & RF_GROUND_CONFORM) || (ent->flags & RF_HIDDEN) ||
            (ent->flags & RF_GROUND_SURFACE) || !ent->model) {
            continue;
        }
        FOR_LOOP(j, count) {
            float hit_z;

            if (!R_W3WalkableSurfaceHit(surfaces[j], ent->origin.x, ent->origin.y, &hit_z)) continue;
            if (!found_surface || hit_z > authored_support) {
                authored_support = hit_z;
                found_surface = true;
            }
        }
        if (found_surface) {
            float support_z = authored_support + ent->ground_offset;
            if (ent->ground_snapshot_valid)
                support_z = MAX(support_z, ent->ground_snapshot_z);
            ent->origin.z = support_z;
            ent->flags |= RF_GROUND_SURFACE_SUPPORT;
        }
    }
    if (surfaces != local_surfaces) ri.MemFree(surfaces);
}

vec2_t R_WorldOrigin(void) {
    return tr.world ? tr.world->center : (vec2_t){0};
}

vec2_t R_WorldSize(void) {
    return tr.world ? GetWar3MapSize(tr.world) : (vec2_t){ 0 };
}

model_t *R_LoadModel(cstring_t modelFilename) {
    void *buffer = NULL;
    int fileSize = ri.FS_ReadFile(modelFilename, &buffer);
    model_t *model = NULL;

    /* WC3 data files reference models with any extension (.MDL, .MDX, variant
     * digits, etc.).  Strip to the stem via the last dot, optionally remove a
     * trailing digit (WC3 convention: HeroArcher1.mdl → HeroArcher.mdx), then
     * retry with .mdx.  Using strrchr avoids the strcasestr case-sensitivity
     * issue and handles extensions of any length. */
    if (fileSize < 0) {
        PATHSTR tempFileName = { 0 };
        cstring_t dot = strrchr(modelFilename, '.');
        cstring_t stem_end = dot ? dot : modelFilename + strlen(modelFilename);
        size_t stemLen;

        if (stem_end > modelFilename && isdigit((unsigned char)*(stem_end - 1))) {
            stem_end--;
        }
        stemLen = (size_t)(stem_end - modelFilename);
        if (stemLen > sizeof(tempFileName) - 5) {
            stemLen = sizeof(tempFileName) - 5;
        }
        memcpy(tempFileName, modelFilename, stemLen);
        memcpy(tempFileName + stemLen, ".mdx", 5);
        fileSize = ri.FS_ReadFile(tempFileName, &buffer);
    }
    if (fileSize < 0 || !buffer) {
        return NULL;
    }
    if (*(uint32_t *)buffer == ID_MDLX) {
        model = ri.MemAlloc(sizeof(model_t));
        model->mdx = R_LoadModelMDLX(buffer, fileSize);
        model->modeltype = ID_MDLX;
    } else if (R_W3PathHasExtension(modelFilename, ".mdl")) {
        /* Same case-insensitive issue: use stem length, not strstr. */
        PATHSTR tempFileName = { 0 };
        size_t stemLen = strlen(modelFilename) - 4;

        if (stemLen > sizeof(tempFileName) - 5) {
            stemLen = sizeof(tempFileName) - 5;
        }
        memcpy(tempFileName, modelFilename, stemLen);
        memcpy(tempFileName + stemLen, ".mdx", 5);
        ri.FS_FreeFile(buffer);
        return R_LoadModel(tempFileName);
    } else {
        fprintf(stderr, "Unknown model format %.4s in file %s\n", (string_t)buffer, modelFilename);
    }
    ri.FS_FreeFile(buffer);
    return model;
}

void R_ReleaseModel(model_t *model) {
    if (model->modeltype == ID_MDLX) {
        MDLX_Release(model->mdx);
    }
    ri.MemFree(model);
}

static cstring_t R_W3AnimLookupLabel(cstring_t id) {
    if (!id || !*id) return NULL;
    FOR_LOOP(i, anim_lookup_count)
        if (anim_lookup_rows[i].name && !strcmp(anim_lookup_rows[i].name, id))
            return anim_lookup_rows[i].sound_label;
    return NULL;
}

static wc3AnimSound_t const *R_W3AnimSound(cstring_t label) {
    if (!label || !*label) return NULL;
    FOR_LOOP(i, anim_sound_count)
        if (anim_sound_rows[i].name && !strcmp(anim_sound_rows[i].name, label))
            return anim_sound_rows + i;
    return NULL;
}

static uint32_t R_W3PresentationPick(uint32_t entity, uint32_t key, uint32_t time, uint32_t count) {
    uint32_t x = entity * 0x9e3779b9u ^ key * 0x85ebca6bu ^ time;
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15;
    return count ? x % count : 0;
}

static bool R_W3SoundPath(wc3AnimSound_t const *row, uint32_t variant, string_t path, size_t path_size) {
    cstring_t chosen;
    cstring_t comma;
    uint32_t count = 1;
    if (!row || !row->files || !row->files[0] || !path || !path_size) return false;
    for (cstring_t p = row->files; (p = strchr(p, ',')) != NULL; p++) count++;
    if (variant >= count) return false;
    chosen = row->files;
    while (variant--) { chosen = strchr(chosen, ','); if (!chosen) return false; chosen++; }
    comma = strchr(chosen, ',');
    if (row->directory && row->directory[0]) {
        size_t n = strlen(row->directory);
        snprintf(path, path_size, "%s%s%.*s", row->directory,
                 row->directory[n - 1] == '\\' || row->directory[n - 1] == '/' ? "" : "\\",
                 comma ? (int)(comma - chosen) : (int)strlen(chosen), chosen);
    } else {
        snprintf(path, path_size, "%.*s", comma ? (int)(comma - chosen) : (int)strlen(chosen), chosen);
    }
    return true;
}

static uint32_t R_W3SoundVariantCount(wc3AnimSound_t const *row) {
    uint32_t count = 0;
    if (!row || !row->files || !row->files[0]) return 0;
    count = 1;
    for (cstring_t p = row->files; (p = strchr(p, ',')) != NULL; p++) count++;
    return count;
}

/* Map the fixed three-byte MDX event prefix through one table shared by all consumers. */
static wc3EventFamily_t const *R_W3EventFamily(mdxEvent_t const *event) {
    if (!event) return NULL;
    FOR_LOOP(i, sizeof(event_families) / sizeof(*event_families))
        if (!strncmp(event->node.name, event_families[i].prefix, 3))
            return event_families + i;
    return NULL;
}

/* Resolve one crossed SND key through AnimLookups/AnimSounds and play it at the event-node world position. */
static void R_W3EmitSoundEvent(wc3EventParams_t const *params, uint32_t key) {
    char id[sizeof(params->event->node.name) + 1];
    cstring_t label;
    wc3AnimSound_t const *row;
    uint32_t count, pick;
    char path[512];
    vec3_t origin;

    if (!params || !params->entity || !params->model || !params->event || !params->transform ||
        !params->family ||
        !MDLX_EventObjectId(params->event, params->family->prefix, id, sizeof(id))) return;
    label = R_W3AnimLookupLabel(id);
    row = R_W3AnimSound(label ? label : id);
    if (!row) {
        if (R_W3EventWarningShouldLog("missing-animation-sound", id))
            fprintf(stderr, "WC3 renderer: MDX SND event '%s' has no AnimSounds row\n", id);
        return;
    }
    if (!(count = R_W3SoundVariantCount(row))) {
        if (R_W3EventWarningShouldLog("empty-animation-sound", id))
            fprintf(stderr, "WC3 renderer: MDX SND event '%s' has no sound variants\n", id);
        return;
    }
    pick = R_W3PresentationPick(params->entity->number, key, tr.viewDef.time, count);
    if (!R_W3SoundPath(row, pick, path, sizeof(path))) {
        if (R_W3EventWarningShouldLog("invalid-animation-sound-path", id))
            fprintf(stderr, "WC3 renderer: MDX SND event '%s' has an invalid AnimSounds path\n", id);
        return;
    }
    /* Glue panels and other UI sprites live in screen space, so their SND keys are interface sounds. */
    if (params->ui_sprite) {
        ri.PlaySoundAt(path, NULL, MAX(0.0f, MIN(1.0f, row->volume / 127.0f)));
        return;
    }
    {
        mat4_t event_transform;
        if (!MDLX_EventWorldTransform(params->model, params->event, params->entity,
                                      params->transform, &event_transform)) return;
        origin = MAKE(vec3_t, event_transform.v[12], event_transform.v[13], event_transform.v[14]);
    }
    ri.PlaySoundAt(path, &origin, MAX(0.0f, MIN(1.0f, row->volume / 127.0f)));
}

/* Reuse an inactive SPN slot, or the oldest transient when the bounded pool is full. */
static wc3EventSpawn_t *R_W3AllocEventSpawn(void) {
    wc3EventSpawn_t *oldest = event_spawns;

    FOR_LOOP(i, WC3_EVENT_SPAWN_MAX) {
        if (!event_spawns[i].active) return event_spawns + i;
        if (event_spawns[i].serial < oldest->serial) oldest = event_spawns + i;
    }
    return oldest;
}

/* Advance one renderer-owned SPN child and preserve its independent nested-event clock. */
static bool R_W3RenderEventSpawn(wc3EventSpawn_t *spawn, uint32_t slot) {
    renderEntity_t child = { 0 };
    mdxSequence_t const *seq;
    uint32_t elapsed, span, frame;

    if (!spawn || !spawn->active || !spawn->model || !spawn->model->mdx ||
        !spawn->model->mdx->sequences || spawn->model->mdx->num_sequences < 1) {
        if (spawn) spawn->active = false;
        return false;
    }
    seq = spawn->model->mdx->sequences;
    span = seq->interval[1] - seq->interval[0];
    if (!span) { spawn->active = false; return false; }
    elapsed = tr.viewDef.time - spawn->start_time;
    if (elapsed >= span) { spawn->active = false; return false; }
    frame = seq->interval[0] + elapsed;

    child.model = spawn->model;
    child.number = MAX_GAME_ENTITIES + slot + 1;
    child.generation = spawn->serial;
    child.instance_id = spawn->source_instance_id;
    child.team = spawn->team;
    child.flags = spawn->flags | RF_NO_SHADOW | RF_NO_UBERSPLAT;
    child.scale = spawn->scale;
    child.frame = frame;
    child.oldframe = spawn->frame;
    child.tint = COLOR32_WHITE;
    {
        uint32_t serial = spawn->serial;
        wc3EventState_t state = {
            .model = spawn->model, .frame = spawn->event_frame,
            .render_time = spawn->event_render_time, .generation = spawn->serial, .valid = spawn->event_valid,
        };
        R_W3DispatchModelEvents(&MAKE(wc3EventDispatchParams_t, .entity = &child,
            .model = spawn->model->mdx, .state = &state, .transform = &spawn->transform,
            .depth = spawn->depth, .entity_camera = spawn->entity_camera,
            .source_model = spawn->source_model, .source_instance_id = spawn->source_instance_id));
        /* A nested SPN may recycle this slot when the bounded transient pool is full.
         * Do not write the parent's event state into the replacement instance. */
        if (spawn->serial != serial) return true;
        spawn->event_frame = state.frame;
        spawn->event_render_time = state.render_time;
        spawn->event_valid = state.valid;
    }
    MDX_RenderModel(&child, spawn->model->mdx, &spawn->transform);
    R_W3RenderAttachmentModels(&child, &spawn->transform);
    spawn->frame = frame;
    return true;
}

/* Snapshot an SPN event transform into a bounded, entity-less child-model presentation. */
static void R_W3EmitSpawnEvent(wc3EventParams_t const *params) {
    char id[sizeof(params->event->node.name) + 1];
    wc3SpawnData_t *row;
    wc3EventSpawn_t *spawn;
    model_t *child_model;
    mdxSequence_t const *seq;
    uint32_t slot;

    if (!params || !params->entity || !params->model || !params->event || !params->transform ||
        !params->family ||
        !MDLX_EventObjectId(params->event, params->family->prefix, id, sizeof(id))) return;
    row = R_W3SpawnData(id);
    if (!row) {
        if (R_W3EventWarningShouldLog("missing-spawn-row", id))
            fprintf(stderr, "WC3 renderer: MDX SPN event '%s' has no SpawnData row\n", id);
        return;
    }
    child_model = R_W3SpawnModel(row);
    if (!child_model) {
        if (R_W3EventWarningShouldLog("unresolved-spawn-model", id))
            fprintf(stderr, "WC3 renderer: MDX SPN '%s' model '%s' did not resolve to MDLX\n",
                    id, row->model_path ? row->model_path : "(empty)");
        return;
    }
    if (!child_model->mdx->sequences || child_model->mdx->num_sequences < 1) {
        if (R_W3EventWarningShouldLog("spawn-model-no-sequence", id))
            fprintf(stderr, "WC3 renderer: MDX SPN '%s' model '%s' has no sequences\n", id, row->model_path);
        return;
    }
    spawn = R_W3AllocEventSpawn();
    slot = (uint32_t)(spawn - event_spawns);
    seq = child_model->mdx->sequences;
    *spawn = (wc3EventSpawn_t){
        .model = child_model, .team = params->entity->team,
        .source_model = params->source_model,
        .source_instance_id = params->source_instance_id,
        .entity_camera = params->entity_camera,
        .flags = params->entity->flags & (RF_NO_FOGOFWAR | RF_NO_LIGHTING | RF_PORTRAIT_LIGHTING),
        .start_time = tr.viewDef.time, .frame = seq->interval[0], .depth = params->depth,
        .serial = ++event_spawn_serial,
        .scale = params->entity->scale > 0.0f ? params->entity->scale : 1.0f,
        .active = true,
    };
    if (!MDLX_EventWorldTransform(params->model, params->event, params->entity,
                                  params->transform, &spawn->transform)) {
        spawn->active = false;
        if (R_W3EventWarningShouldLog("spawn-transform", id))
            fprintf(stderr, "WC3 renderer: failed to transform MDX SPN event '%s'\n", id);
        return;
    }
    /* Render the event on the crossing frame; the retained slot continues from
     * the same sequence on later frames after the parent entity is gone. */
    R_W3RenderEventSpawn(spawn, slot);
}

/* Reuse an inactive decal slot, or the oldest transient when the bounded pool is full. */
static wc3EventSplat_t *R_W3AllocEventSplat(void) {
    wc3EventSplat_t *oldest = event_splats;

    FOR_LOOP(i, WC3_EVENT_SPLAT_MAX) {
        if (!event_splats[i].active) return event_splats + i;
        if (event_splats[i].serial < oldest->serial) oldest = event_splats + i;
    }
    return oldest;
}

static float R_W3SplatChannel(float value) {
    if (!isfinite(value)) return 0.0f;
    if (value <= 1.0f) value *= 255.0f;
    return MAX(0.0f, MIN(255.0f, value));
}

static color32_t R_W3SplatColor(wc3SplatColorValue_t const *value) {
    return MAKE(color32_t,
                (uint8_t)R_W3SplatChannel(value->r),
                (uint8_t)R_W3SplatChannel(value->g),
                (uint8_t)R_W3SplatChannel(value->b),
                (uint8_t)R_W3SplatChannel(value->a));
}

static color32_t R_W3LerpSplatColor(color32_t a, color32_t b, float t) {
    t = MAX(0.0f, MIN(1.0f, t));
    return MAKE(color32_t,
                (uint8_t)LerpNumber(a.r, b.r, t),
                (uint8_t)LerpNumber(a.g, b.g, t),
                (uint8_t)LerpNumber(a.b, b.b, t),
                (uint8_t)LerpNumber(a.a, b.a, t));
}

/* Convert normalized phase progress into the authored inclusive atlas-frame range, including reverse ranges. */
static int R_W3SplatAtlasFrame(int start, int end, float progress) {
    int64_t lo = MIN(start, end), hi = MAX(start, end);
    int64_t count = hi - lo + 1;
    int64_t step;
    if (count <= 1) return start;
    progress = MAX(0.0f, MIN(1.0f, progress));
    step = MIN(count - 1, (int64_t)floor((double)progress * (double)count));
    return (int)(start <= end ? (int64_t)start + step : (int64_t)start - step);
}

static bool R_W3SplatAtlasValid(wc3SplatData_t *row) {
    int64_t frame_count;

    if (!row || row->rows <= 0 || row->columns <= 0) goto invalid;
    frame_count = (int64_t)row->rows * row->columns;
    if (frame_count <= INT_MAX) return true;

invalid:
    if (row && !row->atlas_warned) {
        row->atlas_warned = true;
        fprintf(stderr, "WC3 renderer: SplatData '%s' has invalid atlas dimensions %d x %d\n",
                row->name ? row->name : "(unnamed)", row->rows, row->columns);
    }
    return false;
}

/* Map one clamped atlas frame to the UV rectangle consumed by the terrain splat renderer. */
static wc3SplatUV_t R_W3SplatAtlasUV(wc3SplatData_t const *row, int frame) {
    int rows = row->rows, columns = row->columns;
    int64_t total = (int64_t)rows * columns;
    int64_t index = MAX(0, MIN(total - 1, frame));
    int column = (int)(index % columns), atlas_row = (int)(index / columns);
    float inv_columns = 1.0f / (float)columns, inv_rows = 1.0f / (float)rows;
    return MAKE(wc3SplatUV_t,
        .mins = MAKE(vec2_t, column * inv_columns, atlas_row * inv_rows),
        .maxs = MAKE(vec2_t, (column + 1) * inv_columns, (atlas_row + 1) * inv_rows));
}

/* Snapshot an SPL/FPT event into a terrain-conforming, renderer-owned SplatData transient. */
static void R_W3EmitSplatEvent(wc3EventParams_t const *params) {
    char id[sizeof(params->event->node.name) + 1];
    wc3SplatData_t *row;
    wc3EventSplat_t *splat;
    mat4_t event_transform;

    if (!params || !params->entity || !params->model || !params->event || !params->transform ||
        !params->family ||
        !MDLX_EventObjectId(params->event, params->family->prefix, id, sizeof(id))) return;
    row = R_W3SplatData(id);
    if (!row) {
        if (R_W3EventWarningShouldLog("missing-splat-row", id))
            fprintf(stderr, "WC3 renderer: MDX %s event '%s' has no SplatData row\n",
                    params->family->prefix, id);
        return;
    }
    if (!R_W3SplatAtlasValid(row)) return;
    if (row->scale <= 0.0f) {
        if (R_W3EventWarningShouldLog("invalid-splat-scale", id))
            fprintf(stderr, "WC3 renderer: SplatData '%s' has invalid scale %.3f\n", id, row->scale);
        return;
    }
    R_W3WarnUnsupportedSplatFields(row);
    if (!R_W3SplatTexture(row)) return;
    if (!MDLX_EventWorldTransform(params->model, params->event, params->entity,
                                  params->transform, &event_transform)) {
        if (R_W3EventWarningShouldLog("splat-transform", id))
            fprintf(stderr, "WC3 renderer: failed to transform MDX %s event '%s'\n",
                    params->family->prefix, id);
        return;
    }
    splat = R_W3AllocEventSplat();
    *splat = (wc3EventSplat_t){
        .kind = WC3_EVENT_SPLAT_SPLAT,
        .splat_row = row,
        .origin = MAKE(vec2_t, event_transform.v[12], event_transform.v[13]),
        .start_time = tr.viewDef.time,
        .serial = ++event_splat_serial,
        .active = true,
    };
    R_W3RenderEventSplat(splat);
}

/* Snapshot a UBR event into a renderer-owned UberSplatData lifetime transient. */
static void R_W3EmitUberSplatEvent(wc3EventParams_t const *params) {
    char id[sizeof(params->event->node.name) + 1];
    wc3UberSplatData_t *row;
    wc3EventSplat_t *splat;
    mat4_t event_transform;

    if (!params || !params->entity || !params->model || !params->event || !params->transform ||
        !params->family ||
        !MDLX_EventObjectId(params->event, params->family->prefix, id, sizeof(id))) return;
    row = R_W3UberSplatData(id);
    if (!row) {
        if (R_W3EventWarningShouldLog("missing-uber-splat-row", id))
            fprintf(stderr, "WC3 renderer: MDX UBR event '%s' has no UberSplatData row\n", id);
        return;
    }
    if (row->scale <= 0.0f) {
        if (R_W3EventWarningShouldLog("invalid-uber-splat-scale", id))
            fprintf(stderr, "WC3 renderer: UberSplatData '%s' has invalid scale %.3f\n", id, row->scale);
        return;
    }
    R_W3WarnUnsupportedUberSplatFields(row);
    if (!R_W3UberSplatTexture(row)) return;
    if (!MDLX_EventWorldTransform(params->model, params->event, params->entity,
                                  params->transform, &event_transform)) {
        if (R_W3EventWarningShouldLog("uber-splat-transform", id))
            fprintf(stderr, "WC3 renderer: failed to transform MDX UBR event '%s'\n", id);
        return;
    }
    splat = R_W3AllocEventSplat();
    *splat = (wc3EventSplat_t){
        .kind = WC3_EVENT_SPLAT_UBER,
        .uber_row = row,
        .origin = MAKE(vec2_t, event_transform.v[12], event_transform.v[13]),
        .start_time = tr.viewDef.time,
        .serial = ++event_splat_serial,
        .active = true,
    };
    R_W3RenderEventSplat(splat);
}

/* Render one retained SPL/FPT/UBR transient from its authored timing and captured position. */
static bool R_W3RenderEventSplat(wc3EventSplat_t *splat) {
    uint32_t elapsed;

    if (!splat || !splat->active) return false;
    elapsed = tr.viewDef.time - splat->start_time;

    if (splat->kind == WC3_EVENT_SPLAT_SPLAT) {
        wc3SplatData_t *row = splat->splat_row;
        texture_t const *texture;
        float life_ms, decay_ms, total_ms, phase_progress;
        color32_t start_color, middle_color, end_color, color;
        int frame;
        wc3SplatUV_t uv;
        vec2_t mins, maxs;

        if (!row || !(texture = R_W3SplatTexture(row))) { splat->active = false; return false; }
        R_W3WarnZeroSplatLifetime(row);
        life_ms = MAX(0.0f, row->lifespan) * 1000.0f;
        decay_ms = MAX(0.0f, row->decay_time) * 1000.0f;
        total_ms = life_ms + decay_ms;
        if (total_ms > 0.0f && (float)elapsed >= total_ms) { splat->active = false; return false; }

        start_color = R_W3SplatColor(&MAKE(wc3SplatColorValue_t, .r = row->start_r, .g = row->start_g,
            .b = row->start_b, .a = row->start_a));
        middle_color = R_W3SplatColor(&MAKE(wc3SplatColorValue_t, .r = row->middle_r, .g = row->middle_g,
            .b = row->middle_b, .a = row->middle_a));
        end_color = R_W3SplatColor(&MAKE(wc3SplatColorValue_t, .r = row->end_r, .g = row->end_g,
            .b = row->end_b, .a = row->end_a));
        if (life_ms > 0.0f && (float)elapsed < life_ms) {
            phase_progress = (float)elapsed / life_ms;
            color = R_W3LerpSplatColor(start_color, middle_color, phase_progress);
            frame = R_W3SplatAtlasFrame(row->uv_lifespan_start, row->uv_lifespan_end, phase_progress);
        } else {
            phase_progress = decay_ms > 0.0f ? ((float)elapsed - life_ms) / decay_ms : 1.0f;
            color = R_W3LerpSplatColor(middle_color, end_color, phase_progress);
            frame = R_W3SplatAtlasFrame(row->uv_decay_start, row->uv_decay_end, phase_progress);
        }
        uv = R_W3SplatAtlasUV(row, frame);
        mins = MAKE(vec2_t, splat->origin.x - row->scale, splat->origin.y - row->scale);
        maxs = MAKE(vec2_t, splat->origin.x + row->scale, splat->origin.y + row->scale);
        R_RenderRectSplatUV(&MAKE(rectSplatParams_t, .mins = &mins, .maxs = &maxs, .uv_mins = &uv.mins,
            .uv_maxs = &uv.maxs, .texture = texture, .shader = R_SPLAT_SHADER(&tr.shader_default), .color = color));
        if (total_ms <= 0.0f) splat->active = false;
        return true;
    } else {
        wc3UberSplatData_t *row = splat->uber_row;
        texture_t const *texture;
        float birth_ms, pause_ms, decay_ms, total_ms;
        color32_t start_color, middle_color, end_color, color;

        if (!row || !(texture = R_W3UberSplatTexture(row))) { splat->active = false; return false; }
        R_W3WarnZeroUberSplatLifetime(row);
        birth_ms = MAX(0.0f, row->birth_time) * 1000.0f;
        pause_ms = MAX(0.0f, row->pause_time) * 1000.0f;
        decay_ms = MAX(0.0f, row->decay_time) * 1000.0f;
        total_ms = birth_ms + pause_ms + decay_ms;
        if (total_ms > 0.0f && (float)elapsed >= total_ms) { splat->active = false; return false; }
        start_color = R_W3SplatColor(&MAKE(wc3SplatColorValue_t, .r = row->start_r, .g = row->start_g,
            .b = row->start_b, .a = row->start_a));
        middle_color = R_W3SplatColor(&MAKE(wc3SplatColorValue_t, .r = row->middle_r, .g = row->middle_g,
            .b = row->middle_b, .a = row->middle_a));
        end_color = R_W3SplatColor(&MAKE(wc3SplatColorValue_t, .r = row->end_r, .g = row->end_g,
            .b = row->end_b, .a = row->end_a));
        if (birth_ms > 0.0f && (float)elapsed < birth_ms)
            color = R_W3LerpSplatColor(start_color, middle_color, (float)elapsed / birth_ms);
        else if ((float)elapsed < birth_ms + pause_ms || decay_ms <= 0.0f)
            color = middle_color;
        else
            color = R_W3LerpSplatColor(middle_color, end_color, ((float)elapsed - birth_ms - pause_ms) / decay_ms);
        R_RenderSplat(&splat->origin, row->scale, texture, R_SPLAT_SHADER(&tr.shader_default), color);
        if (total_ms <= 0.0f) splat->active = false;
        return true;
    }
}

static void R_W3DrawEventSplats(void) {
    if (tr.render_phase != RENDER_PHASE_SOLID) return;
    FOR_LOOP(i, WC3_EVENT_SPLAT_MAX) R_W3RenderEventSplat(event_splats + i);
}

static void R_W3DrawEventSpawns(bool entity_camera, model_t const *source_model, uintptr_t source_instance_id) {
    if (tr.render_phase != RENDER_PHASE_SOLID) return;
    FOR_LOOP(i, WC3_EVENT_SPAWN_MAX) {
        wc3EventSpawn_t *spawn = event_spawns + i;
        if (!spawn->active || spawn->entity_camera != entity_camera) continue;
        if (entity_camera && spawn->source_model != source_model) continue;
        if (entity_camera && spawn->source_instance_id != source_instance_id) continue;
        R_W3RenderEventSpawn(spawn, i);
    }
}

/* Dispatch event-key crossings for normal entities and renderer-owned child model instances. */
static void R_W3DispatchModelEvents(wc3EventDispatchParams_t const *params) {
    if (!params || !params->entity || !params->model || !params->state || !params->transform ||
        !params->model->events) return;
    if (!params->state->valid || params->state->model != params->entity->model ||
        params->state->generation != params->entity->generation) {
        *params->state = (wc3EventState_t){ .model = params->entity->model, .frame = params->entity->frame,
            .render_time = tr.viewDef.time, .generation = params->entity->generation, .valid = true };
        /* A glue panel appears on the first frame of its Birth/Morph sequence, where its slide sound is keyed.
         * Treat the first sighting as entering the sequence (no previous sequence) instead of skipping it. */
        if (!params->ui_sprite) return;
        params->state->frame = UINT32_MAX;
    }
    if (params->state->frame == params->entity->frame &&
        params->state->render_time == tr.viewDef.time) return;

    FOR_EACH_LIST(mdxEvent_t, event, params->model->events) {
        wc3EventFamily_t const *family = R_W3EventFamily(event);
        wc3EventParams_t event_params;

        if (!event->num_keys) continue;
        if (!family) {
            char prefix[4] = { event->node.name[0], event->node.name[1], event->node.name[2], '\0' };
            if (R_W3EventWarningShouldLog("unsupported-event-family", prefix))
                fprintf(stderr, "WC3 renderer: unsupported MDX event family '%s' in '%s'\n",
                        prefix, event->node.name);
            continue;
        }
        /* UI sprites have no world to spawn into or splat onto; only their sounds are presentation. */
        if (params->ui_sprite && family->kind != WC3_EVENT_SOUND) continue;
        event_params = MAKE(wc3EventParams_t, .entity = params->entity, .model = params->model,
            .event = event, .transform = params->transform, .family = family, .depth = params->depth,
            .entity_camera = params->entity_camera, .ui_sprite = params->ui_sprite,
            .source_model = params->source_model, .source_instance_id = params->source_instance_id);
        FOR_LOOP(i, event->num_keys) {
            uint32_t key = event->keys[i];
            if (!MDLX_EventKeyCrossed(params->model, event, key, params->state->frame,
                                      params->entity->frame, params->state->render_time,
                                      tr.viewDef.time)) continue;
            switch (family->kind) {
            case WC3_EVENT_SOUND:
                R_W3EmitSoundEvent(&event_params, key);
                break;
            case WC3_EVENT_UBER_SPLAT:
            case WC3_EVENT_SPLAT:
            case WC3_EVENT_FOOTPRINT:
                if (params->entity_camera) {
                    if (R_W3EventWarningShouldLog("camera-splat-event", event->node.name))
                        fprintf(stderr, "WC3 renderer: entity-camera event '%s' cannot emit a world terrain splat\n",
                                event->node.name);
                } else if (family->kind == WC3_EVENT_UBER_SPLAT) {
                    R_W3EmitUberSplatEvent(&event_params);
                } else {
                    R_W3EmitSplatEvent(&event_params);
                }
                break;
            case WC3_EVENT_SPAWN:
                if (params->depth < WC3_EVENT_MAX_DEPTH) {
                    event_params.depth = params->depth + 1;
                    R_W3EmitSpawnEvent(&event_params);
                } else {
                    if (R_W3EventWarningShouldLog("spawn-depth-limit", event->node.name))
                        fprintf(stderr, "WC3 renderer: MDX SPN nesting exceeded %u presentation levels at event '%s'\n",
                                WC3_EVENT_MAX_DEPTH, event->node.name);
                }
                break;
            default:
                break;
            }
        }
    }
    params->state->frame = params->entity->frame;
    params->state->render_time = tr.viewDef.time;
}

static void R_W3UpdateModelEvents(renderEntity_t const *entity) {
    mdxModel_t const *model;
    mat4_t transform;

    /* Presentation events belong to the color pass, not the shadow-map pass. */
    if (tr.render_phase == RENDER_PHASE_LIGHTS) return;
    bool entity_camera = (tr.viewDef.rdflags & RDF_USE_ENTITY_CAMERA) != 0;
    bool ui_sprite = (tr.viewDef.rdflags & RDF_UI_SPRITE) != 0;
    /* Sprites share entity number 0, so like entity-camera views they need per-instance event state. */
    bool instanced = entity_camera || ui_sprite;

    if (!entity) return;
    /* Look the instance state up first: that lookup is what invalidates it when the model changes. */
    wc3EventState_t *state = instanced ? R_W3CameraEventState(entity) : NULL;
    if (instanced && !state) return;
    if ((entity->flags & RF_HIDDEN) || !entity->model || entity->model->modeltype != ID_MDLX ||
        !entity->model->mdx || (!instanced && entity->number >= MAX_GAME_ENTITIES)) return;
    model = entity->model->mdx;
    if (!model->events) return;
    R_GetEntityMatrix(entity, &transform);
    if (!instanced) state = event_state + entity->number;
    R_W3DispatchModelEvents(&MAKE(wc3EventDispatchParams_t, .entity = entity, .model = model,
        .state = state, .transform = &transform, .depth = 0, .entity_camera = entity_camera,
        .ui_sprite = ui_sprite,
        .source_model = entity_camera ? entity->model : NULL,
        .source_instance_id = entity_camera ? entity->instance_id : 0));
}

void R_UpdateEntityPresentation(renderEntity_t const *entity) {
    R_W3UpdateModelEvents(entity);
}

void R_RenderModel(renderEntity_t const *entity) {
    mat4_t transform;

    if (!entity || !entity->model || entity->model->modeltype != ID_MDLX) {
        return;
    }
    MDLX_TickDetachedRibbons(); /* fade trails whose entity stopped drawing before this model draws */
    R_GetEntityMatrix(entity, &transform);
    MDX_RenderModel(entity, entity->model->mdx, &transform);
    R_W3RenderAttachmentModels(entity, &transform);

    if ((entity->effect_flags & EFX_MODEL) && (entity->effect_flags & EFX_ATTACH_SLOTS) &&
        entity->effect_model && tr.render_phase != RENDER_PHASE_LIGHTS) {
        mdxAttachmentPosition_t attachments[16];
        uint32_t attachment_count;
        uint32_t slot_mask;
        model_t const *fire_model = entity->effect_model;

        if (!fire_model || fire_model->modeltype != ID_MDLX || !fire_model->mdx) {
            return;
        }

        slot_mask = (entity->effect_flags & EFX_SLOT_MASK) >> EFX_SLOT_SHIFT;

        attachment_count = MDLX_CollectAttachmentPositions(entity->model->mdx, &transform,
                                                            entity->frame, entity->oldframe,
                                                            "Sprite ", attachments,
                                                            sizeof(attachments) / sizeof(*attachments));
        FOR_LOOP(i, attachment_count) {
            static cstring_t const names[5] = {
                "Sprite First", "Sprite Second", "Sprite Third", "Sprite Fourth", "Sprite Fifth",
            };
            int slot = -1;
            renderEntity_t fire = { 0 };

            FOR_LOOP(j, 5) {
                if (!strncasecmp(attachments[i].name, names[j], strlen(names[j]))) {
                    slot = (int)j;
                    break;
                }
            }
            if (slot < 0 || !(slot_mask & (1u << slot))) {
                continue;
            }

            fire.origin = attachments[i].origin;
            fire.model = fire_model;
            fire.team = entity->team;
            fire.scale = entity->scale;
            fire.angle = entity->angle;
            fire.tint = COLOR32_WHITE;
            fire.flags = RF_NO_SHADOW | RF_NO_UBERSPLAT;
            MDLX_SetEntityAnimationFrame(fire_model, "Stand", &fire);
            R_RenderModel(&fire);
        }
    }
}

bool R_TraceModel(renderEntity_t const *entity, line3_t const *line, float *distance) {
    vec3_t intersection;

    if (!entity || !entity->model || entity->model->modeltype != ID_MDLX) {
        return false;
    }
    if (!MDLX_TraceModel(entity, line, &intersection)) {
        return false;
    }
    if (distance) {
        *distance = Vector3_distance(&line->a, &intersection);
    }
    return true;
}

/* Share MDX authored bounds with the outer renderer culler so tall doodads are
 * not rejected by their smaller gameplay selection radius. */
bool R_GetEntityBounds(renderEntity_t const *entity, box3_t *bounds) {
    if (!entity || !bounds || !entity->model || entity->model->modeltype != ID_MDLX || !entity->model->mdx)
        return false;
    *bounds = entity->model->mdx->bounds.box;
    return true;
}

mat4_t const *R_EntityPose(renderEntity_t const *entity, modelPose_t *pose) {
    (void)entity; (void)pose;
    return &wc3_model_basis;
}

bool R_RenderShadow(renderEntity_t const *entity, vec2_t const *origin) {
    (void)entity;
    (void)origin;
    return false;
}

float R_SelectionRadius(renderEntity_t const *entity) {
    return entity->radius;
}

float R_EntityHeight(renderEntity_t const *entity) {
    mdxModel_t const *mdx;
    mdxSequence_t const *seq;
    if (!entity || !entity->model || entity->model->modeltype != ID_MDLX || !entity->model->mdx) return 0.0f;
    mdx = entity->model->mdx;
    /* Retail reads the authored per-sequence bounds, not a transform of the whole model;
     * a single static extent spans every animation and misplaces mines/heroes' overhead UI. */
    seq = R_FindSequenceAtTime(mdx, entity->frame);
    return (seq ? seq->bounds.box.max.z : mdx->info.bounds.box.max.z) * entity->scale;
}
bool R_EntityOverheadPosition(renderEntity_t const *entity, vec3_t *out) {
    if (!entity || !out) return false;
    /* Match Warsmash: the status stack is centered on the transformed model-bounds maximum. */
    *out = entity->origin; out->z += R_EntityHeight(entity);
    return true;
}

bool R_EntityAttachmentPosition(renderEntity_t const *entity, cstring_t prefix, vec3_t *out) {
    mat4_t transform;
    mdxAttachmentPosition_t attachment;

    if (!entity || !entity->model || entity->model->modeltype != ID_MDLX ||
        !entity->model->mdx || !prefix || !prefix[0] || !out) {
        return false;
    }
    R_GetEntityMatrix(entity, &transform);
    if (!MDLX_CollectAttachmentPositions(entity->model->mdx, &transform,
                                         entity->frame, entity->oldframe,
                                         prefix, &attachment, 1)) {
        return false;
    }
    *out = attachment.origin;
    return true;
}

static void R_W3TextureCacheAdd(cstring_t path) {
    if (!path || !*path || model_texture_cache.count >= 256) {
        return;
    }
    FOR_LOOP(i, model_texture_cache.count) {
        if (!strcmp(model_texture_cache.paths[i], path)) {
            return;
        }
    }
    strncpy(model_texture_cache.paths[model_texture_cache.count], path, sizeof(model_texture_cache.paths[0]) - 1);
    model_texture_cache.paths[model_texture_cache.count][sizeof(model_texture_cache.paths[0]) - 1] = 0;
    model_texture_cache.count++;
}

static void R_W3BuildModelTextureCache(model_t *model) {
    if (model_texture_cache.model == model) {
        return;
    }
    model_texture_cache.model = model;
    model_texture_cache.count = 0;
    if (!model || model->modeltype != ID_MDLX || !model->mdx || !model->mdx->textures) {
        return;
    }
    FOR_LOOP(i, model->mdx->num_textures) {
        R_W3TextureCacheAdd(model->mdx->textures[i].path);
    }
}

bool R_GetModelInfo(model_t *model, modelInfo_t *info) {
    bool found = false;
    float min_u = 1.0f, min_v = 1.0f, max_u = 0.0f, max_v = 0.0f;

    if (!model || !info || model->modeltype != ID_MDLX || !model->mdx) {
        return false;
    }
    memset(info, 0, sizeof(*info));
    info->sequenceCount = (uint32_t)MAX(0, model->mdx->num_sequences);
    FOR_EACH_LIST(mdxCamera_t, camera, model->mdx->cameras) info->cameraCount++;

    R_W3BuildModelTextureCache(model);
    if (model_texture_cache.model == model) {
        info->textureCount = MIN(model_texture_cache.count, MODELINFO_MAX_TEXTURES);
        FOR_LOOP(i, info->textureCount) {
            info->texturePaths[i] = model_texture_cache.paths[i];
        }
    }

    FOR_EACH_LIST(mdxGeoset_t, geoset, model->mdx->geosets) {
        if (!geoset->texcoord || geoset->num_texcoord <= 0) {
            continue;
        }
        FOR_LOOP(i, geoset->num_texcoord) {
            float u = geoset->texcoord[i].x;
            float v = geoset->texcoord[i].y;

            if (!found) {
                min_u = max_u = u;
                min_v = max_v = v;
                found = true;
            } else {
                if (u < min_u) min_u = u;
                if (u > max_u) max_u = u;
                if (v < min_v) min_v = v;
                if (v > max_v) max_v = v;
            }
        }
    }
    if (found) {
        info->textureUVRect.x = min_u;
        info->textureUVRect.y = min_v;
        info->textureUVRect.w = max_u - min_u;
        info->textureUVRect.h = max_v - min_v;
        info->hasTextureUVRect = true;
    }
    return true;
}

bool R_ExtractEntityCamera(renderEntity_t const *entity, float aspect, viewDef_t *viewdef) {
    if (!entity || !entity->model || !entity->model->mdx || !viewdef) {
        return false;
    }
    mdxCameraView_t const view = {
        .model = entity->model->mdx,
        .frame = entity->frame,
        .camera_index = entity->camera_index,
        .aspect = aspect,
        .output = &viewdef->viewProjectionMatrix,
        .light = &viewdef->lightMatrix,
    };
    bool ok = MDLX_ExtractCamera(&view);
    Matrix4_identity(&viewdef->textureMatrix);
    return ok;
}

bool R_SetEntityAnimFrame(model_t const *model, cstring_t anim, renderEntity_t *entity) {
    return MDLX_SetEntityAnimationFrame(model, anim, entity);
}

bool R_GetModelAnimationDuration(model_t const *model, cstring_t anim, uint32_t *duration) {
    mdxSequence_t const *seq;

    if (!model || model->modeltype != ID_MDLX || !model->mdx || !anim || !*anim || !duration)
        return false;
    if (anim[0] == '#') {
        char *end;
        unsigned long index = strtoul(anim + 1, &end, 10);
        if (end == anim + 1 || *end || index >= (unsigned long)model->mdx->num_sequences) return false;
        seq = model->mdx->sequences + index;
    } else {
        seq = MDLX_FindSequenceByName(model->mdx, anim);
    }
    if (!seq || seq->interval[1] < seq->interval[0]) return false;
    *duration = seq->interval[1] - seq->interval[0];
    return true;
}

void R_DrawSprite(drawSprite_t const *sprite) {
    MDLX_DrawSpriteInstance(sprite, COLOR32_WHITE);
}

/* Warcraft III can replace the platform cursor with its authored animated MDX cursor. */
bool R_DrawCursor(drawCursor_t const *cursor) {
    if (cursor->game && !cursor->model) return false;
    if (!cursor->model && !cursor_model && !cursor_load_attempted) {
        stbIniCache_t theme = {0};
        cursor_load_attempted = true;
        R_LoadIniCachePath(&theme, "UI\\war3skins.txt");
        cstring_t path = Stb_IniCacheFind(&theme, "Default", "Cursor");
        if (path && *path) snprintf(cursor_model_name, sizeof(cursor_model_name), "%s", path);
        else fprintf(stderr, "WC3 menu cursor: missing Default/Cursor in UI\\war3skins.txt\n");
        Stb_IniCacheFree(&theme);
        if (cursor_model_name[0]) cursor_model = R_LoadModel(cursor_model_name);
        if (!cursor_model || cursor_model->modeltype != ID_MDLX || !cursor_model->mdx) {
            fprintf(stderr, "WC3 menu cursor unavailable: %s\n", cursor_model_name);
            if (cursor_model) R_ReleaseModel(cursor_model);
            cursor_model = NULL;
        }
    }
    model_t const *model = cursor->model ? cursor->model : cursor_model;
    if (!model) return false;
    if (model != cursor_active_model) {
        cursor_active_model = model;
        cursor_anim = cursor_bad_anim = NULL;
    }
    if (model->modeltype != ID_MDLX || !model->mdx) {
        if (!cursor_bad_anim) fprintf(stderr, "WC3 cursor: selected model has no MDX data\n");
        cursor_bad_anim = "invalid model";
        return false;
    }
    wc3CursorMode_t const mode = R_ResolveCursorMode(cursor);
    cstring_t anim = cursor_modes[mode].anim;
    if (mode == WC3_CURSOR_SIGNAL && (!cursor->skin || !cursor->skin->has_first_pixel)) {
        if (cursor_bad_anim != anim) fprintf(stderr, "WC3 signal cursor: missing decoded player-color image\n");
        cursor_bad_anim = anim;
        return false;
    }
    mdxSequence_t const *sequence = MDLX_FindSequenceByName(model->mdx, anim);
    if (!sequence || sequence->interval[1] <= sequence->interval[0]) {
        if (cursor_bad_anim != anim) fprintf(stderr, "WC3 cursor: missing or invalid sequence %s\n", anim);
        cursor_bad_anim = anim;
        return false;
    }
    cursor_bad_anim = NULL;
    uint32_t const step = cursor_anim ? cursor->time - cursor_time : 0;
    uint32_t const duration = sequence->interval[1] - sequence->interval[0];
    if (anim != cursor_anim || cursor_restart) cursor_frame = 0;
    cursor_anim = anim;
    cursor_time = cursor->time;
    /* CSpriteUber's end callback marks -2. The next update restarts before
     * consuming its step, dropping the preceding loop's wrapped remainder. */
    uint64_t const frame = (uint64_t)cursor_frame + step;
    cursor_restart = !(sequence->flags & 1) && frame >= duration;
    cursor_frame = sequence->flags & 1 ? MIN(frame, duration) : frame % duration;
    /* Retail substitutes the cursor at its hotspot, with untinted authored scroll art. */
    color32_t tint = mode == WC3_CURSOR_SIGNAL ? cursor->skin->first_pixel : cursor_modes[mode].tint;
    drawSprite_t sprite = { .model = model, .anim = anim, .x = cursor->origin.x, .y = cursor->origin.y,
        .skin = mode == WC3_CURSOR_HOLD_ITEM ? cursor->skin : NULL,
        .skin_slot = 21, /* Retail HoldItem replaces ID 21; other authored slots remain intact. */
        .start_time = tr.viewDef.time - cursor_frame, .id = &cursor_model };
    /* D3D9 places pixel centers on integer coordinates. GL's half-integer
     * centers require this physical-pixel offset for the retail hotspot. */
    rect_t const scene = R_UISceneRect();
    if (tr.drawableSize.width && tr.drawableSize.height) {
        sprite.x += scene.w / (2.0f * tr.drawableSize.width);
        sprite.y += scene.h / (2.0f * tr.drawableSize.height);
    }
    MDLX_DrawSpriteInstance(&sprite, tint);
    return true;
}
