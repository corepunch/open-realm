#include "renderer/r_game.h"
#include "games/warcraft-3/common/wc3_coords.h"
#include "r_lightning.h"
#include "mdx/r_mdx.h"
#include "w3m/r_war3map.h"
#include "r_weather.h"
#include "common/stb_slk.h"
#include "games/warcraft-3/common/minimap_render.h"
#include <ctype.h>

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

static cstring_t modelNames[MODEL_COUNT] = {
    "UI\\Feedback\\SelectionCircle\\SelectionCircle.mdx"
};

static cstring_t const cursor_model_name = "UI\\Cursor\\HumanCursor.mdx";
static model_t *cursor_model;
static bool cursor_load_attempted;

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
    model_t * model;
} wc3SpawnData_t;

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
typedef struct { model_t const *model; uint32_t frame, render_time; bool valid; } wc3EventSoundState_t;
static wc3EventSoundState_t event_sound_state[MAX_GAME_ENTITIES];
#define WC3_EVENT_SPAWN_MAX 128
typedef struct {
    model_t *model; mat4_t transform;
    uint32_t team, flags, start_time, frame, serial;
    float scale; bool active;
} wc3EventSpawn_t;
static wc3EventSpawn_t event_spawns[WC3_EVENT_SPAWN_MAX];
static uint32_t event_spawn_serial;
static void R_W3DrawEventSpawns(void);

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

static void R_W3FreeSpawnData(bool release_models) {
    if (release_models) R_W3ReleaseSpawnModels();
    FS_SLKFreeRows(spawn_data_schema, spawn_data_rows, spawn_data_count, sizeof(wc3SpawnData_t));
    spawn_data_rows = NULL; spawn_data_count = 0;
}

static void R_W3LoadSpawnData(void) {
    PATHSTR scoped;

    R_W3FreeSpawnData(true);
    if (ri.LoadSlk && R_MapAssetCandidate("Splats\\SpawnData.slk", scoped, sizeof(scoped)))
        spawn_data_count = ri.LoadSlk(scoped, spawn_data_schema,
                                      (void **)&spawn_data_rows, sizeof(wc3SpawnData_t));
    if (!spawn_data_count && ri.LoadSlk)
        spawn_data_count = ri.LoadSlk("Splats\\SpawnData.slk", spawn_data_schema,
                                      (void **)&spawn_data_rows, sizeof(wc3SpawnData_t));
    if (ri.LoadSlk && !spawn_data_count)
        fprintf(stderr, "WC3 renderer: failed to load Splats\\SpawnData.slk for MDX SPN events\n");
}

static wc3SpawnData_t *R_W3SpawnData(cstring_t id) {
    if (!id || !*id) return NULL;
    FOR_LOOP(i, spawn_data_count)
        if (spawn_data_rows[i].name && !strcasecmp(spawn_data_rows[i].name, id))
            return spawn_data_rows + i;
    return NULL;
}

static model_t * R_W3SpawnModel(wc3SpawnData_t *row) {
    if (!row || !row->model_path || !row->model_path[0]) return NULL;
    if (!row->model) row->model = R_LoadRegisteredModel(row->model_path);
    return row->model && row->model->modeltype == ID_MDLX && row->model->mdx ? row->model : NULL;
}

static void R_W3ClearEventSpawns(void) {
    memset(event_spawns, 0, sizeof(event_spawns));
    event_spawn_serial = 0;
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
    R_W3LoadSpawnData();
    memset(event_sound_state, 0, sizeof(event_sound_state));
    R_W3ClearEventSpawns();

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
    tr.texture[TEX_WATER] = R_LoadTexture("ReplaceableTextures\\Water\\Water12.blp");
}

void R_Init(void) {
    cursor_model = NULL; cursor_load_attempted = false;
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
    cursor_model = NULL; cursor_load_attempted = false;
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
    R_W3FreeSpawnData(false);
    memset(event_sound_state, 0, sizeof(event_sound_state));
    R_W3ClearEventSpawns();
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
    R_SetMapAssetScope(mapFileName);
    R_AdvanceTextureGeneration();
    R_W3LoadSpawnData();
    R_W3ClearEventSpawns();
    memset(event_sound_state, 0, sizeof(event_sound_state));
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
    R_W3DrawEventSpawns();
}

void R_DrawTerrainShadows(void) {
    _W3M_DrawTerrainShadows();
}

void R_DrawAlphaSurfaces(void) {
    _W3M_DrawAlphaSurfaces();
    R_LightningDraw();
    R_WeatherEmit();
}

bool R_TraceLocation(viewDef_t const *viewdef, float x, float y, vec3_t *point) {
    return _W3M_TraceLocation(viewdef, x, y, point);
}

float R_GetHeightAtPoint(float x, float y) {
    return R_W3TerrainHeightAtPoint(x, y);
}

float R_GetCameraHeightAtPoint(float x, float y) { return R_W3CameraHeightAtPoint(x, y); }
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
    if (!viewdef || (viewdef->rdflags & RDF_NOWORLDMODEL)) return;

    FOR_LOOP(i, viewdef->num_entities) {
        renderEntity_t *ent = &viewdef->entities[i];
        float authored_support = 0.0f;
        bool found_surface = false;

        if (!(ent->flags & RF_GROUND_CONFORM) || (ent->flags & RF_HIDDEN) ||
            (ent->flags & RF_GROUND_SURFACE) || !ent->model) {
            continue;
        }

        FOR_LOOP(j, viewdef->num_entities) {
            renderEntity_t const *surface = &viewdef->entities[j];
            float hit_z;

            if (!(surface->flags & RF_GROUND_SURFACE)) continue;
            if (!R_W3WalkableSurfaceHit(surface, ent->origin.x, ent->origin.y, &hit_z)) continue;
            if (!found_surface || hit_z > authored_support) {
                authored_support = hit_z;
                found_surface = true;
            }
        }

        if (found_surface)
            ent->origin.z = authored_support + ent->ground_offset;
    }
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

static void R_W3EmitSoundEvent(renderEntity_t const *entity, mdxModel_t const *model,
                               mdxEvent_t const *event, uint32_t key, mat4_t const *transform) {
    cstring_t id;
    cstring_t label;
    wc3AnimSound_t const *row;
    uint32_t count, pick;
    char path[512];
    vec3_t origin;

    if (!ri.PlaySoundAt || !MDLX_EventObjectId(event, "SND", id, sizeof(id))) return;
    label = R_W3AnimLookupLabel(id);
    row = R_W3AnimSound(label ? label : id);
    if (!row) return;
    if (!(count = R_W3SoundVariantCount(row))) return;
    pick = R_W3PresentationPick(entity->number, key, tr.viewDef.time, count);
    if (!R_W3SoundPath(row, pick, path, sizeof(path))) return;
    {
        mat4_t event_transform;
        if (!MDLX_EventWorldTransform(model, event, entity, transform, &event_transform)) return;
        origin = MAKE(vec3_t, event_transform.v[12], event_transform.v[13], event_transform.v[14]);
    }
    ri.PlaySoundAt(path, &origin, MAX(0.0f, MIN(1.0f, row->volume / 127.0f)));
}

static wc3EventSpawn_t *R_W3AllocEventSpawn(void) {
    wc3EventSpawn_t *oldest = event_spawns;

    FOR_LOOP(i, WC3_EVENT_SPAWN_MAX) {
        if (!event_spawns[i].active) return event_spawns + i;
        if (event_spawns[i].serial < oldest->serial) oldest = event_spawns + i;
    }
    return oldest;
}

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
    child.team = spawn->team;
    child.flags = spawn->flags | RF_NO_SHADOW | RF_NO_UBERSPLAT;
    child.scale = spawn->scale;
    child.frame = frame;
    child.oldframe = spawn->frame;
    child.tint = COLOR32_WHITE;
    MDX_RenderModel(&child, spawn->model->mdx, &spawn->transform);
    R_W3RenderAttachmentModels(&child, &spawn->transform);
    spawn->frame = frame;
    return true;
}

static void R_W3EmitSpawnEvent(renderEntity_t const *entity, mdxModel_t const *model,
                               mdxEvent_t const *event, mat4_t const * transform) {
    char id[sizeof(event->node.name) + 1];
    wc3SpawnData_t *row;
    wc3EventSpawn_t *spawn;
    model_t * child_model;
    mdxSequence_t const *seq;
    uint32_t slot;

    if (!MDLX_EventObjectId(event, "SPN", id, sizeof(id))) return;
    row = R_W3SpawnData(id);
    if (!row) { fprintf(stderr, "WC3 renderer: MDX SPN event '%s' has no SpawnData row\n", id); return; }
    child_model = R_W3SpawnModel(row);
    if (!child_model) { fprintf(stderr, "WC3 renderer: MDX SPN '%s' model '%s' did not resolve to MDLX\n", id, row->model_path ? row->model_path : "(empty)"); return; }
    if (!child_model->mdx->sequences || child_model->mdx->num_sequences < 1) {
        fprintf(stderr, "WC3 renderer: MDX SPN '%s' model '%s' has no sequences\n", id, row->model_path);
        return;
    }
    spawn = R_W3AllocEventSpawn();
    slot = (uint32_t)(spawn - event_spawns);
    seq = child_model->mdx->sequences;
    *spawn = (wc3EventSpawn_t){
        .model = child_model, .team = entity->team,
        .flags = entity->flags & (RF_NO_FOGOFWAR | RF_NO_LIGHTING | RF_PORTRAIT_LIGHTING),
        .start_time = tr.viewDef.time, .frame = seq->interval[0],
        .serial = ++event_spawn_serial, .scale = entity->scale > 0.0f ? entity->scale : 1.0f,
        .active = true,
    };
    if (!MDLX_EventWorldTransform(model, event, entity, transform, &spawn->transform)) {
        spawn->active = false;
        fprintf(stderr, "WC3 renderer: failed to transform MDX SPN event '%s'\n", id);
        return;
    }
    /* Render the event on the crossing frame; the retained slot continues from
     * the same sequence on later frames after the parent entity is gone. */
    R_W3RenderEventSpawn(spawn, slot);
}

static void R_W3DrawEventSpawns(void) {
    if (tr.render_phase != RENDER_PHASE_SOLID) return;
    FOR_LOOP(i, WC3_EVENT_SPAWN_MAX) R_W3RenderEventSpawn(event_spawns + i, i);
}

static void R_W3UpdateModelEvents(renderEntity_t const *entity) {
    mdxModel_t const *model;
    wc3EventSoundState_t *state;
    mat4_t transform;

    /* Presentation events belong to the color pass, not the shadow-map pass. */
    if (tr.render_phase == RENDER_PHASE_LIGHTS) return;
    if (!entity || (entity->flags & RF_HIDDEN) || !entity->model || entity->model->modeltype != ID_MDLX ||
        !entity->model->mdx || entity->number >= MAX_GAME_ENTITIES) return;
    model = entity->model->mdx;
    if (!model->events) return;
    state = event_sound_state + entity->number;
    if (!state->valid || state->model != entity->model) {
        *state = (wc3EventSoundState_t){ .model = entity->model, .frame = entity->frame,
                                   .render_time = tr.viewDef.time, .valid = true };
        return;
    }
    if (state->frame == entity->frame && state->render_time == tr.viewDef.time) return;

    R_GetEntityMatrix(entity, &transform);
    FOR_EACH_LIST(mdxEvent_t, event, model->events) {
        if (!event->num_keys || (strncmp(event->node.name, "SND", 3) && strncmp(event->node.name, "SPN", 3))) continue;
        FOR_LOOP(i, event->num_keys) {

            uint32_t key = event->keys[i];
            if (!MDLX_EventKeyCrossed(model, event, key, state->frame, entity->frame,
                                      state->render_time, tr.viewDef.time)) continue;
            if (!strncmp(event->node.name, "SND", 3)) R_W3EmitSoundEvent(entity, model, event, key, &transform);
            else R_W3EmitSpawnEvent(entity, model, event, &transform);
        }
    }
    state->frame = entity->frame;
    state->render_time = tr.viewDef.time;
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
    bool ok = MDLX_ExtractCamera(entity->model->mdx, entity->frame, aspect, &viewdef->viewProjectionMatrix,
                                 &viewdef->lightMatrix);
    Matrix4_identity(&viewdef->textureMatrix);
    return ok;
}

bool R_SetEntityAnimFrame(model_t const *model, cstring_t anim, renderEntity_t *entity) {
    return MDLX_SetEntityAnimationFrame(model, anim, entity);
}

void R_DrawSprite(drawSprite_t const *sprite) {
    MDLX_DrawSpriteInstance(sprite, COLOR32_WHITE);
}

/* Warcraft III can replace the platform cursor with its authored animated MDX cursor. */
bool R_DrawCursor(float x, float y, color32_t tint) {
    renderEntity_t probe = {0};

    if (!cursor_model && !cursor_load_attempted) {
        cursor_load_attempted = true;
        cursor_model = R_LoadModel(cursor_model_name);
        if (!cursor_model || !R_SetEntityAnimFrame(cursor_model, "Normal", &probe)) {
            fprintf(stderr, "WC3 cursor unavailable: %s\n", cursor_model_name);
            if (cursor_model) R_ReleaseModel(cursor_model);
            cursor_model = NULL;
        }
    }
    if (!cursor_model) return false;
    MDLX_DrawSpriteTinted(cursor_model, "Normal", x, y, tint);
    return true;
}
