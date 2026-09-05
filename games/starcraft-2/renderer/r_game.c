#include "renderer/r_game.h"
#include "renderer/r_shader.h"
#include "m3/r_m3.h"
#include "games/starcraft-2/common/sc2_map.h"
#include "sc2/r_sc2map.h"
#include <math.h>

void M3_Init(void);
void M3_Shutdown(void);
void M3_RenderModel(renderEntity_t const *entity, m3Model_t const *model, LPCMATRIX4 transform);

typedef struct {
    LPMODEL model;
    DWORD count;
    char paths[256][512];
} model_texture_cache_t;

static model_texture_cache_t model_texture_cache = { 0 };

enum {
    KEY_VALUE,
    KEY_INTAN,
    KEY_OUTTAN,
};

static float R_M3Lerp(float left, float right, float t) {
    return left * (1 - t) + right * t;
}

static float R_M3Bezier(float left, float outTan, float inTan, float right, float t) {
    float const inverseFactor = 1 - t,
        inverseFactorTimesTwo = inverseFactor * inverseFactor,
        factorTimes2 = t * t,
        factor1 = inverseFactorTimesTwo * inverseFactor,
        factor2 = 3 * t * inverseFactorTimesTwo,
        factor3 = 3 * factorTimes2 * inverseFactor,
        factor4 = factorTimes2 * t;
    return left * factor1 + outTan * factor2 + inTan * factor3 + right * factor4;
}

static float R_M3Hermite(float left, float outTan, float inTan, float right, float t) {
    float const factorTimes2 = t * t,
        factor1 = factorTimes2 * (2 * t - 3) + 1,
        factor2 = factorTimes2 * (t - 2) + t,
        factor3 = factorTimes2 * (t - 1),
        factor4 = factorTimes2 * (3 - 2 * t);
    return left * factor1 + outTan * factor2 + inTan * factor3 + right * factor4;
}

static int R_M3InterpInt(int const *left, int const *right, float t, MODELKEYTRACKTYPE lineType) {
    switch (lineType) {
        case TRACK_NO_INTERP: return left[KEY_VALUE];
        case TRACK_BEZIER: return R_M3Bezier(left[KEY_VALUE], left[KEY_OUTTAN], right[KEY_INTAN], right[KEY_VALUE], t);
        case TRACK_HERMITE: return R_M3Hermite(left[KEY_VALUE], left[KEY_OUTTAN], right[KEY_INTAN], right[KEY_VALUE], t);
        default: return R_M3Lerp(left[KEY_VALUE], right[KEY_VALUE], t);
    }
}

static float R_M3InterpFloat(float const *left, float const *right, float t, MODELKEYTRACKTYPE lineType) {
    switch (lineType) {
        case TRACK_NO_INTERP: return left[KEY_VALUE];
        case TRACK_BEZIER: return R_M3Bezier(left[KEY_VALUE], left[KEY_OUTTAN], right[KEY_INTAN], right[KEY_VALUE], t);
        case TRACK_HERMITE: return R_M3Hermite(left[KEY_VALUE], left[KEY_OUTTAN], right[KEY_INTAN], right[KEY_VALUE], t);
        default: return R_M3Lerp(left[KEY_VALUE], right[KEY_VALUE], t);
    }
}

static VECTOR3 R_M3InterpVec3(LPCVECTOR3 left, LPCVECTOR3 right, float t, MODELKEYTRACKTYPE lineType) {
    switch (lineType) {
        case TRACK_NO_INTERP: return left[KEY_VALUE];
        case TRACK_BEZIER: return Vector3_bezier(&left[KEY_VALUE], &left[KEY_OUTTAN], &right[KEY_INTAN], &right[KEY_VALUE], t);
        case TRACK_HERMITE: return Vector3_hermite(&left[KEY_VALUE], &left[KEY_OUTTAN], &right[KEY_INTAN], &right[KEY_VALUE], t);
        default: return Vector3_lerp(&left[KEY_VALUE], &right[KEY_VALUE], t);
    }
}

static QUATERNION R_M3InterpQuat(LPCQUATERNION left, LPCQUATERNION right, float t, MODELKEYTRACKTYPE lineType) {
    switch (lineType) {
        case TRACK_NO_INTERP: return left[KEY_VALUE];
        case TRACK_BEZIER:
        case TRACK_HERMITE: return Quaternion_sqlerp(&left[KEY_VALUE], &left[KEY_OUTTAN], &right[KEY_INTAN], &right[KEY_VALUE], t);
        default: return Quaternion_slerp(&left[KEY_VALUE], &right[KEY_VALUE], t);
    }
}

void R_EvalKeyframeValue(void const *left,
                         void const *right,
                         float t,
                         MODELKEYTRACKDATATYPE datatype,
                         MODELKEYTRACKTYPE linetype,
                         HANDLE out)
{
    switch (datatype) {
        case TDATA_INT1: *((int *)out) = R_M3InterpInt(left, right, t, linetype); return;
        case TDATA_FLOAT1: *((float *)out) = R_M3InterpFloat(left, right, t, linetype); return;
        case TDATA_FLOAT3: *((VECTOR3 *)out) = R_M3InterpVec3(left, right, t, linetype); return;
        case TDATA_FLOAT4: *((QUATERNION *)out) = R_M3InterpQuat(left, right, t, linetype); return;
    }
}

void R_RenderFlatRectSplat(LPCVECTOR2 mins,
                           LPCVECTOR2 maxs,
                           FLOAT z,
                           LPCTEXTURE texture,
                           splat_shader_t *shader,
                           COLOR32 color)
{
    MATRIX4 model_matrix;
    FLOAT const width = maxs->x - mins->x;
    FLOAT const height = maxs->y - mins->y;
    if (!texture || width <= 0 || height <= 0) {
        return;
    }

    VERTEX vertices[6] = {
        { .position = { mins->x, mins->y, z }, .texcoord = { 0, 1 }, .normal = { 0, 0, 1 }, .color = color },
        { .position = { maxs->x, mins->y, z }, .texcoord = { 1, 1 }, .normal = { 0, 0, 1 }, .color = color },
        { .position = { maxs->x, maxs->y, z }, .texcoord = { 1, 0 }, .normal = { 0, 0, 1 }, .color = color },
        { .position = { mins->x, mins->y, z }, .texcoord = { 0, 1 }, .normal = { 0, 0, 1 }, .color = color },
        { .position = { maxs->x, maxs->y, z }, .texcoord = { 1, 0 }, .normal = { 0, 0, 1 }, .color = color },
        { .position = { mins->x, maxs->y, z }, .texcoord = { 0, 0 }, .normal = { 0, 0, 1 }, .color = color },
    };

    Matrix4_identity(&model_matrix);
    R_BindTexture(texture, 0);

    shader->state.viewProjection = tr.viewDef.viewProjectionMatrix;
    shader->state.model = model_matrix;
    R_Call(glEnable, GL_BLEND);
    R_Call(glBlendFunc, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    R_Call(glDepthMask, GL_FALSE);
    R_Call(glBindVertexArray, tr.buffer[RBUF_TEMP1]->vao);
    R_Call(glBindBuffer, GL_ARRAY_BUFFER, tr.buffer[RBUF_TEMP1]->vbo);
    R_Call(glBufferData, GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW);
    R_StatsDraw(GL_TRIANGLES, sizeof(vertices) / sizeof(vertices[0]), 1);
    R_ApplyShader(shader);
    R_Call(glDrawArrays, GL_TRIANGLES, 0, sizeof(vertices) / sizeof(vertices[0]));
    R_Call(glDepthMask, GL_TRUE);
}

void R_RenderRectSplat(LPCVECTOR2 mins,
                       LPCVECTOR2 maxs,
                       LPCTEXTURE texture,
                       splat_shader_t *shader,
                       COLOR32 color)
{
    R_RenderFlatRectSplat(mins, maxs, 0.0f, texture, shader, color);
}

void R_RenderSplat(LPCVECTOR2 position,
                   FLOAT radius,
                   LPCTEXTURE texture,
                   splat_shader_t *shader,
                   COLOR32 color)
{
    VECTOR2 mins = {
        .x = position->x - radius,
        .y = position->y - radius,
    };
    VECTOR2 maxs = {
        .x = position->x + radius,
        .y = position->y + radius,
    };

    R_RenderRectSplat(&mins, &maxs, texture, shader, color);
}

/* SC2 splats are flat quads; the shared batch API stays immediate (a batching
 * pass exists only in the WC3 terrain-conforming renderer). */
static splat_shader_t *sc2_batch_shader;
void R_BeginSplatBatch(splat_shader_t *shader) { sc2_batch_shader = shader; }
void R_AddRectSplat(LPCVECTOR2 mins, LPCVECTOR2 maxs, LPCTEXTURE texture, COLOR32 color) {
    R_RenderRectSplat(mins, maxs, texture, sc2_batch_shader, color);
}
void R_EndSplatBatch(void) { }

void R_LoadAssets(void) {
}

void R_Init(void) {
    M3_Init();
}

void R_Shutdown(void) {
    R_SC2ShutdownShaders();
    M3_Shutdown();
}

void R_SetupTextureMatrix(void) {
    sc2Map_t const *map = SC2_MapCurrent();
    if (map && map->MapInfo.width && map->MapInfo.height) {
        BOX2 bounds = SC2_MapBounds();
        Matrix4_ortho(&tr.viewDef.textureMatrix,
                      bounds.min.x,
                      bounds.max.x,
                      bounds.min.y,
                      bounds.max.y,
                      0.0f,
                      100.0f);
    } else {
        Matrix4_identity(&tr.viewDef.textureMatrix);
    }
}

void R_DrawMinimap(LPCRECT screen) {
    LPCTEXTURE tex = tr.minimap ? tr.minimap : tr.texture[TEX_WHITE];
    R_DrawImage(tex, screen, &MAKE(RECT, 0, 0, 1, 1), COLOR32_WHITE);
}

void R_WeatherCommand(void const *data, DWORD size) { (void)data; (void)size; }

void R_RegisterMap(LPCSTR mapFileName) {
    R_SC2RegisterMap(mapFileName);
}

void R_SetupEnvironmentLighting(void) {
    sc2Map_t const *map = SC2_MapCurrent();
    sc2MapLighting_t const *src = map ? &map->lighting : NULL;
    sc2DirectionalLight_t const *key = src && src->enabled ? &src->directional[SC2_LIGHT_KEY] : NULL;
    tr.viewDef.terrainLight = (ENVIRONLIGHT){0};
    tr.viewDef.entityLight = (ENVIRONLIGHT){0};
    if (!key || !key->enabled) return;
    tr.viewDef.terrainLight = tr.viewDef.entityLight = (ENVIRONLIGHT){
        .dir = Vector3_unm(&key->direction),
        .color = key->color,
        .ambient = sc2_light_ambient(src),
        .intensity = key->color_multiplier,
        .ambient_intensity = 1.0f,
        .type = R_MODEL_LIGHT_DIRECT,
        .valid = true,
    };
}

void R_DrawWorld(void) {
    R_SC2DrawWorld();
}

void R_DrawTerrainShadows(void) {
}

void R_DrawAlphaSurfaces(void) {
}

bool R_TraceLocation(viewDef_t const *viewdef, float x, float y, LPVECTOR3 point) {
    return R_SC2TraceLocation(viewdef, x, y, point);
}

FLOAT R_GetHeightAtPoint(FLOAT x, FLOAT y) {
    return R_SC2GetHeightAtPoint(x, y);
}

VECTOR2 R_WorldSize(void) {
    return R_SC2WorldSize();
}

LPMODEL R_LoadModel(LPCSTR modelFilename) {
    void *buffer = NULL;
    int fileSize = ri.FS_ReadFile(modelFilename, &buffer);
    LPMODEL model = NULL;

    if (fileSize < 0 || !buffer) {
        return NULL;
    }
    if (*(DWORD *)buffer == ID_43DM) {
        model = ri.MemAlloc(sizeof(model_t));
        model->m3 = R_LoadModelM3(buffer, fileSize);
        model->modeltype = ID_43DM;
    } else {
        fprintf(stderr, "Unknown model format %.4s in file %s\n", (LPSTR)buffer, modelFilename);
    }
    ri.FS_FreeFile(buffer);
    return model;
}

void R_ReleaseModel(LPMODEL model) {
    ri.MemFree(model);
}

void R_RenderModel(renderEntity_t const *entity) {
    MATRIX4 transform;

    if (!entity || !entity->model || entity->model->modeltype != ID_43DM || !entity->model->m3) {
        return;
    }
    R_GetEntityMatrix(entity, &transform);
    M3_RenderModel(entity, entity->model->m3, &transform);
}

bool R_TraceModel(renderEntity_t const *entity, LPCLINE3 line, LPFLOAT distance) {
    (void)entity;
    (void)line;
    (void)distance;
    return false;
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

FLOAT R_EntityHeight(renderEntity_t const *entity) { return entity ? entity->radius * 2.0f : 0.0f; }
BOOL R_EntityOverheadPosition(renderEntity_t const *entity, LPVECTOR3 out) {
    if (!entity || !out) return false;
    *out = entity->origin; out->z += R_EntityHeight(entity);
    return true;
}
BOOL R_EntityAttachmentPosition(renderEntity_t const *entity, LPCSTR prefix, LPVECTOR3 out) {
    (void)entity; (void)prefix; (void)out;
    return false;
}

static void R_M3TextureCacheAdd(LPCSTR path) {
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

static void R_M3BuildModelTextureCache(LPMODEL model) {
    if (model_texture_cache.model == model) {
        return;
    }
    model_texture_cache.model = model;
    model_texture_cache.count = 0;
    if (!model || model->modeltype != ID_43DM || !model->m3 || !model->m3->materialStandard) {
        return;
    }
    FOR_LOOP(i, model->m3->materialStandardNum) {
        m3Material_t const *material = &model->m3->materialStandard[i];
        m3Layer_t const *layers[] = {
            material->diffuseLayer,
            material->decalLayer,
            material->specularLayer,
            material->glossLayer,
            material->emissiveLayer,
            material->emissive2Layer,
            material->evioLayer,
            material->evioMaskLayer,
            material->alphaMaskLayer,
            material->alphaMask2Layer,
            material->normalLayer,
            material->heightLayer,
            material->lightMapLayer,
            material->ambientOcclusionLayer,
        };
        FOR_LOOP(layerIndex, sizeof(layers) / sizeof(layers[0])) {
            m3Layer_t const *layer = layers[layerIndex];
            if (layer && layer->imagePath && *layer->imagePath) {
                R_M3TextureCacheAdd(layer->imagePath);
            }
        }
    }
}

bool R_GetModelInfo(LPMODEL model, LPMODELINFO info) {
    if (!model || !info || model->modeltype != ID_43DM) {
        return false;
    }
    memset(info, 0, sizeof(*info));
    R_M3BuildModelTextureCache(model);
    if (model_texture_cache.model == model) {
        info->textureCount = MIN(model_texture_cache.count, MODELINFO_MAX_TEXTURES);
        FOR_LOOP(i, info->textureCount) {
            info->texturePaths[i] = model_texture_cache.paths[i];
        }
    }
    return true;
}

/* Unit portraits use a bounds-derived perspective camera; layout models supply their own camera payload. */
bool R_ExtractEntityCamera(renderEntity_t const *entity, float aspect, viewDef_t *viewdef) {
    if (!entity || !entity->model || entity->model->modeltype != ID_43DM || !entity->model->m3 || !viewdef)
        return false;

    m3Model_t const *m3 = entity->model->m3;
    BoundingSphere const *bs = &m3->boundings;
    VECTOR3 center = {
        (bs->max.x + bs->min.x) * 0.5f,
        (bs->max.y + bs->min.y) * 0.5f,
        (bs->max.z + bs->min.z) * 0.5f
    };
    float radius = (bs->radius > 0.1f) ? bs->radius : 1.5f;
    MATRIX4 transform;
    R_GetEntityMatrix(entity, &transform);

    MATRIX4 proj, view;
    {
        float fov  = 35.0f;
        float dist = radius / tanf((fov * (float)M_PI / 180.0f) * 0.5f);
        VECTOR3 eye = { center.x, center.y - dist * 0.9f, center.z + radius * 0.25f };
        VECTOR3 tgt = center;
        eye = Matrix4_multiply_vector3(&transform, &eye);
        tgt = Matrix4_multiply_vector3(&transform, &tgt);
        VECTOR3 model_z = Matrix4_multiply_vector3(&transform, &(VECTOR3){0, 0, 1});
        VECTOR3 model_o = Matrix4_multiply_vector3(&transform, &(VECTOR3){0, 0, 0});
        VECTOR3 up      = Vector3_sub(&model_z, &model_o);
        if (Vector3_len(&up) <= 0.001f)
            up = (VECTOR3){ 0.0f, 0.0f, 1.0f };
        VECTOR3 dir = Vector3_sub(&tgt, &eye);
        if (Vector3_len(&dir) <= 0.001f)
            dir = (VECTOR3){ 0.0f, 1.0f, 0.0f };
        Matrix4_perspective(&proj, fov, aspect, MAX(0.05f, dist * 0.02f), MAX(dist + radius * 4.0f, 100.0f));
        Matrix4_lookAt(&view, &eye, &dir, &up);
    }
    Matrix4_multiply(&proj, &view, &viewdef->viewProjectionMatrix);
    Matrix4_identity(&viewdef->textureMatrix);
    Matrix4_identity(&viewdef->lightMatrix);
    return true;
}

/* Retained SC2 UI models use a stable final pose from the requested sequence. */
bool R_SetEntityAnimFrame(LPCMODEL model, LPCSTR anim, renderEntity_t *entity) {
    if (!model || model->modeltype != ID_43DM || !model->m3 || !entity) return false;
    m3Model_t const *m3 = model->m3;
    DWORD time = 0;
    M3_FOR_EACH(Sequence, seq, m3->sequences) {
        if (seq->name && anim && !strcasecmp(seq->name, anim)) {
            /* End minus one remains inside this sequence in M3_FindAnimationAtTime. */
            entity->frame = entity->oldframe = time + (seq->interval[1] ? seq->interval[1] - 1 : 0);
            return true;
        }
        time += seq->interval[1];
    }
    entity->frame = entity->oldframe = 0;
    return false;
}

void R_DrawSprite(LPCMODEL model, LPCSTR anim, float x, float y) {
    (void)model;
    (void)anim;
    (void)x;
    (void)y;
}

/* TODO: SC2 authored cursor assets are not wired to the renderer yet;
 * returning false keeps SDL's native platform cursor visible. */
bool R_DrawCursor(float x, float y, COLOR32 tint) {
    (void)x; (void)y; (void)tint;
    return false;
}
