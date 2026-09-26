#include "renderer/r_game.h"
#include "wow_assets.h"
#include "renderer/r_local.h"
#include "renderer/r_shader.h"
#include "common/wow_view.h"
#include "wow/r_wowmap.h"
#include "m2/r_m2_format.h"

void Wow_RegisterMap(cstring_t mapFileName);
void Wow_DrawWorld(void);
void Wow_DrawTerrainShadows(void);
void Wow_DrawAlphaSurfaces(void);
void Wow_DrawMinimap(rect_t const * screen);
float Wow_GetHeightAtPoint(float x, float y);
bool R_TraceLocation(viewDef_t const *viewdef, float x, float y, LPVECTOR3 output);
float GetAccurateHeightAtPoint(float sx, float sy);

static LPTEXTURE s_quest_active_icon;

m2Model_t *R_LoadModelM2(cstring_t modelFilename, void *buffer, uint32_t size, bool *buffer_owned);
void M2_Init(void);
void M2_RenderModel(renderEntity_t const *entity, m2Model_t const *model, LPCMATRIX4 transform);
void M2_RenderInstanced(m2Model_t const *model, LPCINSTANCEBUFFER instances, uint32_t flags);
bool M2_CanStaticInstance(m2Model_t const *model);
bool M2_AttachmentMatrix(m2Model_t const *model, uint32_t attachment_id, LPCMATRIX4 model_matrix, LPMATRIX4 out);
bool M2_EntityAttachmentPosition(m2Model_t const *model, renderEntity_t const *entity, uint32_t attachment_id,
                                 LPCMATRIX4 model_matrix, LPVECTOR3 out);
bool M2_PosedAttachmentPosition(m2Model_t const *model, uint32_t attachment_id, LPCMATRIX4 model_matrix, LPVECTOR3 out);
float M2_GroundOffset(m2Model_t const *model);
float M2_HeadHeight(m2Model_t const *model);
float M2_VisibleBottom(m2Model_t const *model);
bool M2_CameraView(m2Model_t const *model, uint32_t camera_index, LPVECTOR3 eye, LPVECTOR3 target, float * fov_degrees, float * znear, float * zfar);
bool M2_IsCharacterModel(m2Model_t const *model);
bool M2_SetEntitySequenceFrame(m2Model_t const *model, cstring_t anim, renderEntity_t *entity);
void M2_Release(m2Model_t *model);
void M2_Shutdown(void);

typedef struct {
    LPCMODEL model;
    VECTOR3 origin, rotation, point;
    uint32_t time, frame, oldframe, flags;
    float angle, scale;
    bool valid, found;
} wowOverheadCache_t;

static wowOverheadCache_t s_overhead[MAX_GAME_ENTITIES];

/* Entity transforms can change without server time advancing, so every pose input participates in the cache key. */
static bool R_WowOverheadCacheMatch(wowOverheadCache_t const *cache, renderEntity_t const *entity) {
    return cache->valid && cache->model == entity->model && cache->time == tr.viewDef.time &&
           cache->frame == entity->frame && cache->oldframe == entity->oldframe && cache->flags == entity->flags &&
           cache->angle == entity->angle && cache->scale == entity->scale &&
           !memcmp(&cache->origin, &entity->origin, sizeof(cache->origin)) &&
           !memcmp(&cache->rotation, &entity->rotation, sizeof(cache->rotation));
}

/* Preserve the model-authored attachment result before another M2 draw overwrites the shared bone palette. */
static void R_WowCacheOverhead(renderEntity_t const *entity, LPCMATRIX4 transform) {
    wowOverheadCache_t *cache;
    uint32_t attachment;
    if (entity->number >= MAX_GAME_ENTITIES) return;
    cache = &s_overhead[entity->number];
    attachment = (entity->flags & RF_MOUNTED) ? M2_ATTACH_PLAYER_NAME_MOUNTED : M2_ATTACH_PLAYER_NAME;
    cache->model = entity->model; cache->origin = entity->origin; cache->rotation = entity->rotation;
    cache->time = tr.viewDef.time; cache->frame = entity->frame; cache->oldframe = entity->oldframe;
    cache->flags = entity->flags; cache->angle = entity->angle; cache->scale = entity->scale;
    cache->found = M2_PosedAttachmentPosition(entity->model->m2, attachment, transform, &cache->point);
    cache->valid = true;
}

static bool R_WowPathHasExtension(cstring_t path, cstring_t extension) {
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
    /* WoW has no WC3 selection-circle BLPs; generate one ring and share it across the size slots until
     * distinct per-size variants exist. */
    LPTEXTURE ring = R_MakeSelectionCircleTexture();
    FOR_LOOP(i, NUM_SELECTION_CIRCLES)
        tr.texture[TEX_SELECTION_CIRCLE+i] = ring;
    s_quest_active_icon = R_LoadTexture(WOW_QUEST_ACTIVE_ICON);
}

void R_Init(void) {
    M2_Init();
}

void R_Shutdown(void) {
    Wow_ShutdownWorldShaders();
    M2_Shutdown();
}

void R_SetupTextureMatrix(void) {
    Matrix4_identity(&tr.viewDef.textureMatrix);
}

void R_DrawMinimap(rect_t const * screen, cstring_t map) {
    if (map) { fprintf(stderr, "R_DrawMinimap: static preview unsupported for %s\n", map); return; }
    Wow_DrawMinimap(screen);
}


void R_RegisterMap(cstring_t mapFileName) {
    Wow_RegisterMap(mapFileName);
}

void R_SetupEnvironmentLighting(void) {
    VECTOR3 dir;
    ENVIRONLIGHT sun;
    Wow_SunDirection(Wow_DayFraction(), &dir);
    sun = (ENVIRONLIGHT){
        .dir = dir,
        .color = { WOW_LIGHT_DIFFUSE_R, WOW_LIGHT_DIFFUSE_G, WOW_LIGHT_DIFFUSE_B },
        .ambient = { WOW_LIGHT_AMBIENT_R, WOW_LIGHT_AMBIENT_G, WOW_LIGHT_AMBIENT_B },
        .intensity = 1.0f,
        .ambient_intensity = 1.0f,
        .type = R_MODEL_LIGHT_DIRECT,
        .valid = true,
    };
    tr.viewDef.terrainLight = sun;
    tr.viewDef.entityLight = sun;
}

void R_ConformGroundSurfaces(viewDef_t *viewdef) {
    (void)viewdef;
}

void R_DrawWorld(void) {
    Wow_DrawWorld();
}

void R_DrawTerrainShadows(void) {
    Wow_DrawTerrainShadows();
}

void R_DrawAlphaSurfaces(void) {
    Wow_DrawAlphaSurfaces();
}

float R_GetHeightAtPoint(float x, float y) {
    return Wow_GetHeightAtPoint(x, y);
}

float R_GetCameraHeightAtPoint(float x, float y) { return R_GetHeightAtPoint(x, y); }
bool R_CameraUsesTerrainHeight(void) { return false; }

VECTOR2 R_WorldSize(void) {
    return (VECTOR2){ 0 };
}

LPMODEL R_LoadModel(cstring_t modelFilename) {
    void *buffer = NULL;
    PATHSTR load_name;
    int fileSize = ri.FS_ReadFile(modelFilename, &buffer);
    LPMODEL model;

    snprintf(load_name, sizeof(load_name), "%s", modelFilename ? modelFilename : "");
    /* WoW only uses .m2; legacy data files (WMO MODN chunks, early ADTs) may
     * reference models with any extension (.MDL, .MDX, etc.).  If the direct
     * read failed and the path isn't already .m2, strip the extension and retry. */
    if ((fileSize < 0 || !buffer) && !R_WowPathHasExtension(modelFilename, ".m2")) {
        PATHSTR tempFileName = { 0 };
        cstring_t dot = strrchr(modelFilename, '.');
        size_t stemLen = dot ? (size_t)(dot - modelFilename) : strlen(modelFilename);

        if (stemLen > sizeof(tempFileName) - 4) {
            stemLen = sizeof(tempFileName) - 4;
        }
        memcpy(tempFileName, modelFilename, stemLen);
        memcpy(tempFileName + stemLen, ".m2", 4);
        fileSize = ri.FS_ReadFile(tempFileName, &buffer);
        if (fileSize >= 0 && buffer) {
            snprintf(load_name, sizeof(load_name), "%s", tempFileName);
        }
    }
    if (fileSize < 0 || !buffer) {
        model = ri.MemAlloc(sizeof(model_t));
        memset(model, 0, sizeof(*model));
        model->m2 = R_LoadModelM2(load_name, NULL, 0, NULL);
        model->modeltype = ID_MD20;
        return model;
    }
    if (*(uint32_t *)buffer != ID_MD20 && *(uint32_t *)buffer != ID_MD21 && *(uint32_t *)buffer != ID_12DM) {
        fprintf(stderr, "Unknown model format %.4s in file %s\n", (string_t)buffer, modelFilename);
        ri.FS_FreeFile(buffer);
        return NULL;
    }

    model = ri.MemAlloc(sizeof(model_t));
    bool buffer_owned = false;
    model->m2 = R_LoadModelM2(load_name, buffer, fileSize, &buffer_owned);
    model->modeltype = ID_MD20;
    if (!model->m2) {
        ri.MemFree(model);
        model = NULL;
    }
    if (!buffer_owned) ri.FS_FreeFile(buffer);
    return model;
}

void R_ReleaseModel(LPMODEL model) {
    if (model->modeltype == ID_MD20) {
        M2_Release(model->m2);
    }
    ri.MemFree(model);
}

LPCMATRIX4 R_EntityPose(renderEntity_t const *entity, modelPose_t *pose) {
    pose->angles = Wow_DoodadOrientation(entity->rotation);
    pose->angles.yaw += entity->angle;
    if (entity->model && entity->model->modeltype == ID_MD20 && (entity->flags & RF_GROUND_ANCHOR) &&
        M2_IsCharacterModel(entity->model->m2))
        pose->origin.z += M2_GroundOffset(entity->model->m2) * pose->scale;
    return &wow_model_basis;
}

bool R_GetEntityBounds(renderEntity_t const *entity, LPBOX3 bounds) {
    (void)entity; (void)bounds;
    return false;
}

/* Build a stable top/front light for WoW UI model-camera previews. */
static void R_WowEntityCameraLightMatrix(LPCVECTOR3 target, float radius, LPMATRIX4 output) {
    MATRIX4 proj;
    MATRIX4 view;
    VECTOR3 light_dir = { -0.35f, -0.50f, 0.80f };
    VECTOR3 view_dir;
    VECTOR3 eye;
    float distance = MAX(1000.0f, radius * 8.0f);
    float scale = MAX(64.0f, radius * 2.5f);

    Vector3_normalize(&light_dir);
    view_dir = Vector3_unm(&light_dir);
    eye = Vector3_mad(target, distance, &light_dir);
    Matrix4_ortho(&proj, -scale, scale, -scale, scale, -1000.0f, 3000.0f);
    Matrix4_lookAt(&view, &eye, &view_dir, &(VECTOR3){ 0.0f, 0.0f, 1.0f });
    Matrix4_multiply(&proj, &view, output);
}

void R_UpdateEntityPresentation(renderEntity_t const *entity) { (void)entity; }

void R_RenderModel(renderEntity_t const *entity) {
    MATRIX4 transform;
    MATRIX4 attached_transform;
    renderEntity_t attached_entity;
    uint32_t attachment_id;

    if (!entity || !entity->model || entity->model->modeltype != ID_MD20) {
        return;
    }
    R_GetEntityMatrix(entity, &transform);
    M2_RenderModel(entity, entity->model->m2, &transform);
    R_WowCacheOverhead(entity, &transform);
    if (entity->overhead_model && entity->overhead_model->modeltype == ID_MD20) {
        renderEntity_t marker = *entity;
        R_EntityOverheadPosition(entity, &marker.origin);
        marker.model = entity->overhead_model;
        /* A visible name owns the base slot; TalkToMe's authored bottom clearance separates the marker above it. */
        if (entity->name && *entity->name) marker.origin.z += M2_VisibleBottom(marker.model->m2);
        marker.attachment.model = marker.overhead_model = NULL;
        marker.flags &= ~(RF_HAS_QUEST | RF_QUEST_COMPLETE);
        marker.scale = 1.0f;
        /* The parent frame crosses TalkToMe's unrelated sequence every 1533 ms; the marker owns Stand's clock. */
        marker.frame = marker.oldframe = tr.viewDef.time;
        M2_SetEntitySequenceFrame(marker.model->m2, "0", &marker);
        marker.flags &= ~RF_GROUND_ANCHOR;
        marker.flags |= RF_NO_SHADOW;
        R_GetEntityMatrix(&marker, &attached_transform);
        M2_RenderModel(&marker, marker.model->m2, &attached_transform);
    }
    if (s_quest_active_icon && (entity->flags & RF_HAS_QUEST)) {
        VECTOR3 origin = entity->origin;
        origin.z += (M2_GroundOffset(entity->model->m2) + M2_HeadHeight(entity->model->m2)) * entity->scale + 0.25f;
        COLOR32 tint = (entity->flags & RF_QUEST_COMPLETE) ? MAKE(COLOR32, 255, 215, 0, 255) : COLOR32_WHITE;
        R_DrawBillboardSprite(s_quest_active_icon, &origin, 0.5f, tint);
    }
    attachment_id = (tr.viewDef.rdflags & RDF_USE_ENTITY_CAMERA) ? 0 : 1;
    if (entity->attachment.model &&
        entity->attachment.model->modeltype == ID_MD20 &&
#ifdef USE_SHADOWMAPS
        tr.render_phase != RENDER_PHASE_LIGHTS &&
#endif
        M2_AttachmentMatrix(entity->model->m2, attachment_id, &transform, &attached_transform)) {
        R_GetAttachmentMatrix(entity, &attached_transform, &attached_transform);
        attached_entity = *entity;
        attached_entity.model = entity->attachment.model;
        attached_entity.attachment.model = NULL;
        if (!(tr.viewDef.rdflags & RDF_USE_ENTITY_CAMERA)) {
            attached_entity.frame = 0;
            attached_entity.oldframe = 0;
        }
        attached_entity.flags &= ~RF_GROUND_ANCHOR;
        attached_entity.flags |= RF_NO_SHADOW;
        M2_RenderModel(&attached_entity, attached_entity.model->m2, &attached_transform);
    }
}

void R_RenderModelInstanced(LPCMODEL model, LPCINSTANCEBUFFER instances, uint32_t flags) {
    if (!model || model->modeltype != ID_MD20) {
        return;
    }
    M2_RenderInstanced(model->m2, instances, flags);
}

bool R_ModelCanStaticInstance(LPCMODEL model) {
    return model && model->modeltype == ID_MD20 && M2_CanStaticInstance(model->m2);
}

bool R_TraceModel(renderEntity_t const *entity, LPCLINE3 line, float * distance) {
    VECTOR3 ab;
    VECTOR3 ac;
    VECTOR3 center;
    float radius;
    float denom;
    float t;
    VECTOR3 closest;
    VECTOR3 delta;
    float dist2;

    if (!entity || !entity->number || !entity->model) {
        return false;
    }

    ab = Vector3_sub(&line->b, &line->a);
    center = entity->origin;
    radius = MAX(1.5f, entity->radius * MAX(1.0f, entity->scale));
    denom = Vector3_dot(&ab, &ab);
    if (denom <= 0.0001f) {
        return false;
    }

    center.z += radius;
    ac = Vector3_sub(&center, &line->a);
    t = Vector3_dot(&ac, &ab) / denom;
    t = MAX(0.0f, MIN(1.0f, t));
    closest = (VECTOR3){
        line->a.x + ab.x * t,
        line->a.y + ab.y * t,
        line->a.z + ab.z * t,
    };
    delta = Vector3_sub(&center, &closest);
    dist2 = Vector3_dot(&delta, &delta);
    if (dist2 > radius * radius) {
        return false;
    }
    if (distance) {
        *distance = t;
    }
    return true;
}

#ifndef USE_SHADOWMAPS
bool R_RenderShadow(renderEntity_t const *entity, LPCVECTOR2 origin) {
    LPCTEXTURE shadow;
    bool use_fast_blob;
    float shadow_z;
    VECTOR2 mins;
    VECTOR2 maxs;
    BOX3 bounds;
    COLOR32 shadowColor = {0, 0, 0, 128};

    if (!entity || (entity->flags & RF_NO_SHADOW)) {
        return true;
    }

    shadow = entity->shadow ? entity->shadow : tr.texture[TEX_BLOB_SHADOW];
    if (!shadow) {
        return true;
    }

    shadow_z = entity->origin.z + WOW_SPLAT_Z_BIAS;
    use_fast_blob = shadow == tr.texture[TEX_BLOB_SHADOW] &&
                    entity->shadow_rect.w <= 0 &&
                    entity->shadow_rect.h <= 0;

    if (entity->shadow_rect.w > 0 && entity->shadow_rect.h > 0) {
        mins.x = origin->x - entity->shadow_rect.x;
        mins.y = origin->y - entity->shadow_rect.y;
        maxs.x = mins.x + entity->shadow_rect.w;
        maxs.y = mins.y + entity->shadow_rect.h;
    } else {
        float radius = MAX(entity->radius * MAX(entity->scale, 1.0f), 1.0f);
        float width = MAX(radius * 2.4f, 2.0f);
        float height = MAX(radius * 1.6f, 1.5f);
        mins.x = origin->x - width * 0.5f;
        mins.y = origin->y - height * 0.5f;
        maxs.x = mins.x + width;
        maxs.y = mins.y + height;
    }

    if (use_fast_blob) {
        BOX3 pre_bounds = {
            .min = { mins.x, mins.y, entity->origin.z - 16.0f },
            .max = { maxs.x, maxs.y, entity->origin.z + 16.0f },
        };
        /* Entity Z is the server-authored ground position; reject its blob before the terrain height lookup. */
        if (!Wow_ShadowBoundsVisible(&tr.viewDef.frustum, &pre_bounds, !(tr.viewDef.rdflags & RDF_NOFRUSTUMCULL))) return true;
        shadow_z = R_GetHeightAtPoint(origin->x, origin->y) + WOW_SPLAT_Z_BIAS;
    }
    bounds = (BOX3){
        .min = { mins.x, mins.y, shadow_z - 16.0f },
        .max = { maxs.x, maxs.y, shadow_z + 16.0f },
    };
    if (!Wow_ShadowBoundsVisible(&tr.viewDef.frustum, &bounds, !(tr.viewDef.rdflags & RDF_NOFRUSTUMCULL))) return true;
    if (use_fast_blob) {
        R_RenderFlatRectSplat(&mins, &maxs, shadow_z, shadow, R_SPLAT_SHADER(&tr.shader_shadowSplat), shadowColor);
    } else {
        R_RenderRectSplat(&mins, &maxs, shadow, R_SPLAT_SHADER(&tr.shader_shadowSplat), shadowColor);
    }
    return true;
}
#endif

float R_SelectionRadius(renderEntity_t const *entity) {
    /* Fractional WoW collision radii need a minimum visual footprint around the model. */
    return MAX(entity->radius * MAX(entity->scale, 1.0f), 1.0f);
}

/* The PlayerName attachment (mounted variant when riding) is the model-authored name-plate point. */
bool R_EntityOverheadPosition(renderEntity_t const *entity, LPVECTOR3 out) {
    static LPCMODEL last_missing;
    wowOverheadCache_t *cache;
    MATRIX4 transform;
    uint32_t attachment;
    if (!entity || !out) return false;
    *out = entity->origin;
    if (!entity->model || entity->model->modeltype != ID_MD20) {
        out->z += entity->radius * 2.0f;
        return false;
    }
    cache = entity->number < MAX_GAME_ENTITIES ? &s_overhead[entity->number] : NULL;
    if (cache && cache->found && R_WowOverheadCacheMatch(cache, entity)) { *out = cache->point; return true; }
    R_GetEntityMatrix(entity, &transform);
    /* CGUnit_C::GetNamePosition prefers the mounted anchor (29) over the grounded one (18). */
    attachment = (entity->flags & RF_MOUNTED) ? M2_ATTACH_PLAYER_NAME_MOUNTED : M2_ATTACH_PLAYER_NAME;
    if (M2_EntityAttachmentPosition(entity->model->m2, entity, attachment, &transform, out)) {
        if (cache) { R_WowCacheOverhead(entity, &transform); *out = cache->point; }
        return true;
    }
    if (entity->model != last_missing) {
        last_missing = entity->model;
        fprintf(stderr, "WoW renderer: M2 model has no PlayerName attachment %u\n", attachment);
    }
    out->z += (M2_GroundOffset(entity->model->m2) + M2_HeadHeight(entity->model->m2)) * entity->scale;
    return false;
}
bool R_EntityAttachmentPosition(renderEntity_t const *entity, cstring_t prefix, LPVECTOR3 out) {
    (void)entity; (void)prefix; (void)out;
    return false;
}


float R_EntityHeight(renderEntity_t const *entity) {
    VECTOR3 top;
    if (!entity) return 0.0f;
    R_EntityOverheadPosition(entity, &top);
    return top.z - entity->origin.z;
}

bool R_GetModelInfo(LPMODEL model, LPMODELINFO info) {
    if (info) memset(info, 0, sizeof(*info));
    (void)model;
    return false;
}

bool R_ExtractEntityCamera(renderEntity_t const *entity, float aspect, viewDef_t *viewdef) {
    BOX3 const *bounds;
    MATRIX4 transform;
    VECTOR3 center;
    VECTOR3 eye;
    VECTOR3 target;
    VECTOR3 dir;
    VECTOR3 up;
    VECTOR3 model_origin;
    VECTOR3 model_z;
    float radius;
    float distance;
    float fov = 35.0f;
    float znear = 1.0f;
    float zfar = 4000.0f;

    if (!entity || !entity->model || entity->model->modeltype != ID_MD20 || !viewdef) {
        return false;
    }

    m2Model_t const *m2 = entity->model->m2;
    bounds = &m2->bounds;
    R_GetEntityMatrix(entity, &transform);

    center = (VECTOR3){
        (bounds->max.x + bounds->min.x) * 0.5f,
        (bounds->max.y + bounds->min.y) * 0.5f,
        (bounds->max.z + bounds->min.z) * 0.5f
    };
    radius = Vector3_len(&(VECTOR3){
        bounds->max.x - bounds->min.x,
        bounds->max.y - bounds->min.y,
        bounds->max.z - bounds->min.z
    }) * 0.5f;
    if (radius < 1.0f) {
        radius = 32.0f;
    }

    if (!M2_CameraView(m2, 0, &eye, &target, &fov, &znear, &zfar)) {
        distance = radius / tanf((fov * (float)M_PI / 180.0f) * 0.5f);
        if (M2_IsCharacterModel(m2)) {
            target = (VECTOR3){ center.x, center.y, center.z + radius * 0.28f };
            eye = (VECTOR3){ target.x, target.y - distance * 0.52f, target.z + radius * 0.02f };
            znear = MAX(0.1f, distance * 0.02f);
        } else {
            eye = (VECTOR3){ center.x, center.y - distance * 1.35f, center.z + radius * 0.25f };
            target = center;
        }
        zfar = MAX(zfar, distance + radius * 4.0f);
    }
    eye = Matrix4_multiply_vector3(&transform, &eye);
    target = Matrix4_multiply_vector3(&transform, &target);
    model_origin = Matrix4_multiply_vector3(&transform, &(VECTOR3){0, 0, 0});
    model_z = Matrix4_multiply_vector3(&transform, &(VECTOR3){0, 0, 1});
    up = Vector3_sub(&model_z, &model_origin);
    if (Vector3_len(&up) <= 0.001f) {
        up = (VECTOR3){ 0.0f, 0.0f, 1.0f };
    }
    dir = Vector3_sub(&target, &eye);
    if (Vector3_len(&dir) <= 0.001f) {
        dir = (VECTOR3){ 0.0f, 1.0f, 0.0f };
    }

    MATRIX4 proj_matrix, view_matrix;
    Matrix4_perspective(&proj_matrix, fov, aspect, znear, zfar);
    Matrix4_lookAt(&view_matrix, &eye, &dir, &up);
    Matrix4_multiply(&proj_matrix, &view_matrix, &viewdef->viewProjectionMatrix);
    Matrix4_identity(&viewdef->textureMatrix);
    R_WowEntityCameraLightMatrix(&target, radius, &viewdef->lightMatrix);
    return true;
}

bool R_SetEntityAnimFrame(LPCMODEL model, cstring_t anim, renderEntity_t *entity) {
    if (!model || model->modeltype != ID_MD20)
        return false;
    return M2_SetEntitySequenceFrame(model->m2, anim, entity);
}

void R_DrawSprite(drawSprite_t const *sprite) {
    (void)sprite;
}

/* WoW context cursors are native SDL cursors owned by cl_input.c. */
bool R_DrawCursor(float x, float y, COLOR32 tint) {
    (void)x; (void)y; (void)tint;
    return false;
}
