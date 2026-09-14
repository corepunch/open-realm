#include "renderer/r_game.h"
#include "mdx/r_mdx.h"
#include "w3m/r_war3map.h"
#include "r_weather.h"
#include "common/stb_slk.h"
#include "games/warcraft-3/common/minimap.h"

void _W3M_RegisterMap(LPCSTR mapFileName);
void _W3M_DrawWorld(void);
void _W3M_DrawTerrainShadows(void);
void _W3M_DrawAlphaSurfaces(void);
bool _W3M_TraceLocation(viewDef_t const *viewdef, FLOAT x, FLOAT y, LPVECTOR3 output);

/* MMP v0 is a fixed little-endian header followed by 16-byte icon records. */
typedef struct { DWORD kind, x, y; COLOR32 bgra; } MMPICON;
typedef struct { DWORD version, count; MMPICON icons[]; } MMP;
static struct { PATHSTR map; LPCTEXTURE image, icons[3]; MMP *mmp; } preview;
static LPCSTR const preview_art[] = {
    "UI\\Minimap\\minimap-gold.blp",
    "UI\\Minimap\\minimap-neutralbuilding.blp",
    "UI\\Minimap\\MinimapIconCircleOfPower.blp",
};
#define BZ_MMP_CANVAS 256.0f // pixels; World Editor MMP coordinates use this square; projects preview markers
#define BZ_MMP_ICON_SIZE 16.0f // pixels; native minimap marker footprint in the 256-pixel preview

static LPCSTR selCirclesNames[NUM_SELECTION_CIRCLES] = {
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

static LPCSTR modelNames[MODEL_COUNT] = {
    "UI\\Feedback\\SelectionCircle\\SelectionCircle.mdx"
};

static LPCSTR const cursor_model_name = "UI\\Cursor\\HumanCursor.mdx";
static LPMODEL cursor_model;
static BOOL cursor_load_attempted;

static w3TerrainArt_t *g_terrain_rows; static DWORD g_terrain_count; static slkIndex_t g_terrain_idx;
static w3CliffType_t *g_cliff_rows;   static DWORD g_cliff_count;   static slkIndex_t g_cliff_idx;

typedef struct {
    LPMODEL model;
    DWORD count;
    char paths[256][512];
} model_texture_cache_t;

static model_texture_cache_t model_texture_cache = { 0 };

typedef struct {
    LPMODEL model;
    PATHSTR path;
} wc3AttachmentModel_t;

static wc3AttachmentModel_t wc3_attachment_models[32];
static DWORD wc3_attachment_model_count;

/* Cache authored MDX attachment children because they can be visited every frame while a unit animates. */
static LPMODEL R_W3AttachmentModel(LPCSTR path) {
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

static mdxSequence_t const *R_W3AttachmentSequence(mdxModel_t const *model, LPCSTR name) {
    mdxSequence_t const *seq;
    if (!model || !name) return NULL;
    seq = MDLX_FindSequenceByName(model, name);
    if (seq) return seq;
    FOR_LOOP(i, model->num_sequences)
        if (!strcasecmp(model->sequences[i].name, name)) return &model->sequences[i];
    return NULL;
}

/* Map the parent's authored Birth progress into a child Birth sequence with independent timing. */
static DWORD R_W3AttachmentFrame(mdxModel_t const *parent, mdxModel_t const *child, DWORD frame) {
    mdxSequence_t const *src = R_W3AttachmentSequence(parent, "Birth");
    mdxSequence_t const *dst = R_W3AttachmentSequence(child, "Birth");
    FLOAT ratio;
    DWORD span;

    if (!src || !dst) return frame;
    span = MAX(1, src->interval[1] - src->interval[0]);
    ratio = MAX(0.0f, MIN(1.0f, (FLOAT)(frame - src->interval[0]) / (FLOAT)span));
    span = MAX(1, dst->interval[1] - dst->interval[0]);
    return dst->interval[0] + MIN(span - 1, (DWORD)(ratio * (FLOAT)span));
}

static void R_W3RenderAttachmentModels(renderEntity_t const *entity, LPCMATRIX4 transform) {
    mdxAttachmentPosition_t attachments[32];
    DWORD count;

    if (!entity || !entity->model || !entity->model->mdx || !transform) return;
    count = MDLX_CollectAttachmentPositions(entity->model->mdx, transform, entity->frame,
                                             entity->oldframe, NULL, attachments,
                                             sizeof(attachments) / sizeof(*attachments));
    FOR_LOOP(i, count) {
        renderEntity_t child = *entity;
        LPMODEL model;

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

static BOOL R_W3PathHasExtension(LPCSTR path, LPCSTR extension) {
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
    MDLX_Init();
}

void R_Shutdown(void) {
    _W3M_ClearMap();
    if (preview.mmp) ri.FS_FreeFile(preview.mmp);
    memset(&preview, 0, sizeof(preview));
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
    R_WeatherShutdown();
    MDLX_Shutdown();
}

w3TerrainArt_t const *R_TerrainArt(DWORD id) {
    static w3TerrainArt_t zero;
    w3TerrainArt_t *row = FS_SLKLookup(&g_terrain_idx, id);
    return row ? row : &zero;
}

w3CliffType_t const *R_CliffType(DWORD id) {
    static w3CliffType_t zero;
    w3CliffType_t *row = FS_SLKLookup(&g_cliff_idx, id);
    return row ? row : &zero;
}

void R_SetupTextureMatrix(void) {
    if (tr.world) {
        VECTOR2 s = GetWar3MapSize(tr.world);
        VECTOR2 c = tr.world->center;
        Matrix4_ortho(&tr.viewDef.textureMatrix, -s.x+c.x, s.x+c.x, -s.y+c.y, s.y+c.y, 0.0f, 100.0f);
    } else {
        Matrix4_identity(&tr.viewDef.textureMatrix);
    }
}

/* Loading cannot depend on terrain registration; cache only the destination archive's authored preview assets. */
static void load_preview(LPCSTR map) {
    PATHSTR path;
    void *blob = NULL;
    int size;
    if (!strcmp(preview.map, map)) return;
    if (preview.mmp) ri.FS_FreeFile(preview.mmp);
    memset(&preview, 0, sizeof(preview));
    strlcpy(preview.map, map, sizeof(preview.map));
    static LPCSTR const images[] = { "war3mapMap.blp", "war3mapMap.tga" };
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
    MMP *mmp = blob;
    if (size < sizeof(MMP) || mmp->version != 0 || mmp->count > (size - sizeof(MMP)) / sizeof(MMPICON)) {
        fprintf(stderr, "Minimap preview: invalid MMP header/length in %s\n", path);
        ri.FS_FreeFile(blob);
        return;
    }
    preview.mmp = mmp;
    FOR_LOOP(i, mmp->count) {
        DWORD kind = mmp->icons[i].kind;
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
static void draw_preview(LPCRECT screen, LPCSTR map) {
    load_preview(map);
    if (preview.image) R_DrawImage(preview.image, screen, &MAKE(RECT, 0, 0, 1, 1), COLOR32_WHITE);
    if (!preview.mmp) return;
    FOR_LOOP(i, preview.mmp->count) {
        MMPICON const *icon = &preview.mmp->icons[i];
        if (icon->kind >= sizeof(preview_art) / sizeof(preview_art[0]) || !preview.icons[icon->kind]) continue;
        RECT rect = { screen->x + (icon->x - BZ_MMP_ICON_SIZE / 2) / BZ_MMP_CANVAS * screen->w,
                      screen->y + (icon->y - BZ_MMP_ICON_SIZE / 2) / BZ_MMP_CANVAS * screen->h,
                      BZ_MMP_ICON_SIZE / BZ_MMP_CANVAS * screen->w, BZ_MMP_ICON_SIZE / BZ_MMP_CANVAS * screen->h };
        COLOR32 color = { icon->bgra.b, icon->bgra.g, icon->bgra.r, icon->bgra.a };
        R_DrawImage(preview.icons[icon->kind], &rect, &MAKE(RECT, 0, 0, 1, 1), color);
    }
}

void R_DrawMinimap(LPCRECT screen, LPCSTR map) {
    if (map) { draw_preview(screen, map); return; }
    LPCTEXTURE tex = tr.minimap ? tr.minimap : tr.texture[TEX_WHITE];
    VECTOR2 const map_size = R_WorldSize();
    RECT const content = WC3_MinimapContentRect(screen, &map_size);

    /* The authored war3mapMap texture fills the frame. World-space overlays
     * (fog, camera, pings, click projection) use the centred map-aspect area. */
    R_DrawImage(tex, screen, &MAKE(RECT, 0, 0, 1, 1), COLOR32_WHITE);
    tr.minimapRect = content;

    if (tr.world && tr.shader_minimapFog.prog.progid) {
        DWORD const fow_texid = R_GetMinimapFogOfWarTexture();
        if (fow_texid && (!tr.texture[TEX_WHITE] || fow_texid != tr.texture[TEX_WHITE]->texid)) {
            TEXTURE fog_texture = {
                .texid = fow_texid,
                .width = (tr.world->width - 1) * 4,
                .height = (tr.world->height - 1) * 4,
            };
            R_DrawImageEx(&MAKE(drawImage_t,
                                .texture = &fog_texture,
                                .screen = content,
                                .uv = MAKE(RECT, 0, 0, 1, 1),
                                .color = MAKE(COLOR32, 0, 0, 0, 230),
                                .shader = SHADER_MINIMAP_FOG,
                                .alphamode = BLEND_MODE_BLEND));
        }
    }

    R_DrawMinimapCameraRect(&content);
}

void R_RegisterMap(LPCSTR mapFileName) {
    R_SetMapAssetScope(mapFileName);
    memset(&model_texture_cache, 0, sizeof(model_texture_cache));
    R_WeatherRegisterMap();
    _W3M_RegisterMap(mapFileName);
}

void R_SetupEnvironmentLighting(void) {
    RMODELLIGHT light;
    tr.viewDef.terrainLight = (ENVIRONLIGHT){0};
    tr.viewDef.entityLight = (ENVIRONLIGHT){0};
    if (MDLX_SampleFirstLight(tr.viewDef.terrainLightModel, tr.viewDef.environmentPhase, &light))
        tr.viewDef.terrainLight = R_EnvironLightFromModel(&light);
    if (MDLX_SampleFirstLight(tr.viewDef.entityLightModel, tr.viewDef.environmentPhase, &light))
        tr.viewDef.entityLight = R_EnvironLightFromModel(&light);
}

void R_DrawWorld(void) {
    _W3M_DrawWorld();
}

void R_DrawTerrainShadows(void) {
    _W3M_DrawTerrainShadows();
}

void R_DrawAlphaSurfaces(void) {
    _W3M_DrawAlphaSurfaces();
    R_WeatherEmit();
}

bool R_TraceLocation(viewDef_t const *viewdef, float x, float y, LPVECTOR3 point) {
    return _W3M_TraceLocation(viewdef, x, y, point);
}

FLOAT R_GetHeightAtPoint(FLOAT x, FLOAT y) {
    return R_W3TerrainHeightAtPoint(x, y);
}

FLOAT R_GetCameraHeightAtPoint(FLOAT x, FLOAT y) { return R_W3CameraHeightAtPoint(x, y); }
BOOL R_CameraUsesTerrainHeight(void) { return true; }


static BOOL R_W3WalkableSurfaceHit(renderEntity_t const *surface, FLOAT x, FLOAT y, LPFLOAT z) {
    LINE3 line;
    VECTOR3 hit;

    if (!surface || !z || !(surface->flags & RF_GROUND_SURFACE) ||
        (surface->flags & RF_HIDDEN) || !surface->model) {
        return false;
    }

    line = (LINE3){
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
        FLOAT authored_support = 0.0f;
        BOOL found_surface = false;

        if (!(ent->flags & RF_GROUND_CONFORM) || (ent->flags & RF_HIDDEN) ||
            (ent->flags & RF_GROUND_SURFACE) || !ent->model) {
            continue;
        }

        FOR_LOOP(j, viewdef->num_entities) {
            renderEntity_t const *surface = &viewdef->entities[j];
            FLOAT hit_z;

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

VECTOR2 R_WorldSize(void) {
    return tr.world ? GetWar3MapSize(tr.world) : (VECTOR2){ 0 };
}

LPMODEL R_LoadModel(LPCSTR modelFilename) {
    void *buffer = NULL;
    int fileSize = ri.FS_ReadFile(modelFilename, &buffer);
    LPMODEL model = NULL;

    /* WC3 data files reference models with any extension (.MDL, .MDX, variant
     * digits, etc.).  Strip to the stem via the last dot, optionally remove a
     * trailing digit (WC3 convention: HeroArcher1.mdl → HeroArcher.mdx), then
     * retry with .mdx.  Using strrchr avoids the strcasestr case-sensitivity
     * issue and handles extensions of any length. */
    if (fileSize < 0) {
        PATHSTR tempFileName = { 0 };
        LPCSTR dot = strrchr(modelFilename, '.');
        LPCSTR stem_end = dot ? dot : modelFilename + strlen(modelFilename);
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
    if (*(DWORD *)buffer == ID_MDLX) {
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
        fprintf(stderr, "Unknown model format %.4s in file %s\n", (LPSTR)buffer, modelFilename);
    }
    ri.FS_FreeFile(buffer);
    return model;
}

void R_ReleaseModel(LPMODEL model) {
    if (model->modeltype == ID_MDLX) {
        MDLX_Release(model->mdx);
    }
    ri.MemFree(model);
}

void R_RenderModel(renderEntity_t const *entity) {
    MATRIX4 transform;

    if (!entity || !entity->model || entity->model->modeltype != ID_MDLX) {
        return;
    }
    R_GetEntityMatrix(entity, &transform);
    MDX_RenderModel(entity, entity->model->mdx, &transform);
    R_W3RenderAttachmentModels(entity, &transform);

    if ((entity->effect_flags & EFX_MODEL) && (entity->effect_flags & EFX_ATTACH_SLOTS) &&
        entity->effect_model && tr.render_phase != RENDER_PHASE_LIGHTS) {
        mdxAttachmentPosition_t attachments[16];
        DWORD attachment_count;
        DWORD slot_mask;
        LPCMODEL fire_model = entity->effect_model;

        if (!fire_model || fire_model->modeltype != ID_MDLX || !fire_model->mdx) {
            return;
        }

        slot_mask = (entity->effect_flags & EFX_SLOT_MASK) >> EFX_SLOT_SHIFT;

        attachment_count = MDLX_CollectAttachmentPositions(entity->model->mdx, &transform,
                                                            entity->frame, entity->oldframe,
                                                            "Sprite ", attachments,
                                                            sizeof(attachments) / sizeof(*attachments));
        FOR_LOOP(i, attachment_count) {
            static LPCSTR const names[5] = {
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

bool R_TraceModel(renderEntity_t const *entity, LPCLINE3 line, LPFLOAT distance) {
    VECTOR3 intersection;

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
bool R_GetEntityBounds(renderEntity_t const *entity, LPBOX3 bounds) {
    if (!entity || !bounds || !entity->model || entity->model->modeltype != ID_MDLX || !entity->model->mdx)
        return false;
    *bounds = entity->model->mdx->bounds.box;
    return true;
}

bool R_EntityMatrix(renderEntity_t const *entity, LPMATRIX4 matrix) {
    (void)entity;
    (void)matrix;
    return false;
}

bool R_RenderShadow(renderEntity_t const *entity, LPCVECTOR2 origin) {
    (void)entity;
    (void)origin;
    return false;
}

FLOAT R_SelectionRadius(renderEntity_t const *entity) {
    return entity->radius;
}

FLOAT R_EntityHeight(renderEntity_t const *entity) {
    mdxModel_t const *mdx;
    mdxSequence_t const *seq;
    if (!entity || !entity->model || entity->model->modeltype != ID_MDLX || !entity->model->mdx) return 0.0f;
    mdx = entity->model->mdx;
    /* Retail reads the authored per-sequence bounds, not a transform of the whole model;
     * a single static extent spans every animation and misplaces mines/heroes' overhead UI. */
    seq = R_FindSequenceAtTime(mdx, entity->frame);
    return (seq ? seq->bounds.box.max.z : mdx->info.bounds.box.max.z) * entity->scale;
}
BOOL R_EntityOverheadPosition(renderEntity_t const *entity, LPVECTOR3 out) {
    if (!entity || !out) return false;
    /* Match Warsmash: the status stack is centered on the transformed model-bounds maximum. */
    *out = entity->origin; out->z += R_EntityHeight(entity);
    return true;
}

BOOL R_EntityAttachmentPosition(renderEntity_t const *entity, LPCSTR prefix, LPVECTOR3 out) {
    MATRIX4 transform;
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

static void R_W3TextureCacheAdd(LPCSTR path) {
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

static void R_W3BuildModelTextureCache(LPMODEL model) {
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

bool R_GetModelInfo(LPMODEL model, LPMODELINFO info) {
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

bool R_SetEntityAnimFrame(LPCMODEL model, LPCSTR anim, renderEntity_t *entity) {
    return MDLX_SetEntityAnimationFrame(model, anim, entity);
}

void R_DrawSprite(drawSprite_t const *sprite) {
    MDLX_DrawSpriteInstance(sprite, COLOR32_WHITE);
}

/* Warcraft III can replace the platform cursor with its authored animated MDX cursor. */
bool R_DrawCursor(float x, float y, COLOR32 tint) {
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
