#include "renderer/r_game.h"
#include "m3/r_m3.h"
#include "games/starcraft-2/common/sc2_map.h"
#include "sc2/r_sc2map.h"

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

static float R_GameLerp(float left, float right, float t) {
    return left * (1 - t) + right * t;
}

static float R_GameBezier(float left, float outTan, float inTan, float right, float t) {
    float const inverseFactor = 1 - t,
        inverseFactorTimesTwo = inverseFactor * inverseFactor,
        factorTimes2 = t * t,
        factor1 = inverseFactorTimesTwo * inverseFactor,
        factor2 = 3 * t * inverseFactorTimesTwo,
        factor3 = 3 * factorTimes2 * inverseFactor,
        factor4 = factorTimes2 * t;
    return left * factor1 + outTan * factor2 + inTan * factor3 + right * factor4;
}

static float R_GameHermite(float left, float outTan, float inTan, float right, float t) {
    float const factorTimes2 = t * t,
        factor1 = factorTimes2 * (2 * t - 3) + 1,
        factor2 = factorTimes2 * (t - 2) + t,
        factor3 = factorTimes2 * (t - 1),
        factor4 = factorTimes2 * (3 - 2 * t);
    return left * factor1 + outTan * factor2 + inTan * factor3 + right * factor4;
}

static int R_GameInterpInt(int const *left, int const *right, float t, MODELKEYTRACKTYPE lineType) {
    switch (lineType) {
        case TRACK_NO_INTERP: return left[KEY_VALUE];
        case TRACK_BEZIER: return R_GameBezier(left[KEY_VALUE], left[KEY_OUTTAN], right[KEY_INTAN], right[KEY_VALUE], t);
        case TRACK_HERMITE: return R_GameHermite(left[KEY_VALUE], left[KEY_OUTTAN], right[KEY_INTAN], right[KEY_VALUE], t);
        default: return R_GameLerp(left[KEY_VALUE], right[KEY_VALUE], t);
    }
}

static float R_GameInterpFloat(float const *left, float const *right, float t, MODELKEYTRACKTYPE lineType) {
    switch (lineType) {
        case TRACK_NO_INTERP: return left[KEY_VALUE];
        case TRACK_BEZIER: return R_GameBezier(left[KEY_VALUE], left[KEY_OUTTAN], right[KEY_INTAN], right[KEY_VALUE], t);
        case TRACK_HERMITE: return R_GameHermite(left[KEY_VALUE], left[KEY_OUTTAN], right[KEY_INTAN], right[KEY_VALUE], t);
        default: return R_GameLerp(left[KEY_VALUE], right[KEY_VALUE], t);
    }
}

static VECTOR3 R_GameInterpVec3(LPCVECTOR3 left, LPCVECTOR3 right, float t, MODELKEYTRACKTYPE lineType) {
    switch (lineType) {
        case TRACK_NO_INTERP: return left[KEY_VALUE];
        case TRACK_BEZIER: return Vector3_bezier(&left[KEY_VALUE], &left[KEY_OUTTAN], &right[KEY_INTAN], &right[KEY_VALUE], t);
        case TRACK_HERMITE: return Vector3_hermite(&left[KEY_VALUE], &left[KEY_OUTTAN], &right[KEY_INTAN], &right[KEY_VALUE], t);
        default: return Vector3_lerp(&left[KEY_VALUE], &right[KEY_VALUE], t);
    }
}

static QUATERNION R_GameInterpQuat(LPCQUATERNION left, LPCQUATERNION right, float t, MODELKEYTRACKTYPE lineType) {
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
        case TDATA_INT1: *((int *)out) = R_GameInterpInt(left, right, t, linetype); return;
        case TDATA_FLOAT1: *((float *)out) = R_GameInterpFloat(left, right, t, linetype); return;
        case TDATA_FLOAT3: *((VECTOR3 *)out) = R_GameInterpVec3(left, right, t, linetype); return;
        case TDATA_FLOAT4: *((QUATERNION *)out) = R_GameInterpQuat(left, right, t, linetype); return;
    }
}

void R_RenderFlatRectSplat(LPCVECTOR2 mins,
                           LPCVECTOR2 maxs,
                           FLOAT z,
                           LPCTEXTURE texture,
                           LPCSHADER shader,
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
    R_Call(glUseProgram, shader->progid);
    R_Call(glUniformMatrix4fv, shader->uViewProjectionMatrix, 1, GL_FALSE, tr.viewDef.viewProjectionMatrix.v);
    R_Call(glUniformMatrix4fv, shader->uModelMatrix, 1, GL_FALSE, model_matrix.v);
    R_Call(glEnable, GL_BLEND);
    R_Call(glBlendFunc, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    R_Call(glDepthMask, GL_FALSE);
    R_Call(glBindVertexArray, tr.buffer[RBUF_TEMP1]->vao);
    R_Call(glBindBuffer, GL_ARRAY_BUFFER, tr.buffer[RBUF_TEMP1]->vbo);
    R_Call(glBufferData, GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW);
    R_Call(glDrawArrays, GL_TRIANGLES, 0, sizeof(vertices) / sizeof(vertices[0]));
    R_Call(glDepthMask, GL_TRUE);
}

void R_RenderRectSplat(LPCVECTOR2 mins,
                       LPCVECTOR2 maxs,
                       LPCTEXTURE texture,
                       LPCSHADER shader,
                       COLOR32 color)
{
    R_RenderFlatRectSplat(mins, maxs, 0.0f, texture, shader, color);
}

void R_RenderSplat(LPCVECTOR2 position,
                   FLOAT radius,
                   LPCTEXTURE texture,
                   LPCSHADER shader,
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

void R_GameLoadAssets(void) {
}

void R_GameInit(void) {
    M3_Init();
}

void R_GameShutdown(void) {
    M3_Shutdown();
}

void R_GameSetupTextureMatrix(void) {
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

void R_GameRegisterMap(LPCSTR mapFileName) {
    R_SC2RegisterMap(mapFileName);
}

void R_GameDrawWorld(void) {
    R_SC2DrawWorld();
}

void R_GameDrawTerrainShadows(void) {
}

void R_GameDrawAlphaSurfaces(void) {
}

bool R_GameTraceLocation(viewDef_t const *viewdef, float x, float y, LPVECTOR3 point) {
    return R_SC2TraceLocation(viewdef, x, y, point);
}

FLOAT R_GameGetHeightAtPoint(FLOAT x, FLOAT y) {
    return R_SC2GetHeightAtPoint(x, y);
}

VECTOR2 R_GameWorldSize(void) {
    return R_SC2WorldSize();
}

LPMODEL R_GameLoadModel(LPCSTR modelFilename) {
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

void R_GameReleaseModel(LPMODEL model) {
    ri.MemFree(model);
}

void R_GameRenderModel(renderEntity_t const *entity) {
    MATRIX4 transform;

    if (!entity || !entity->model || entity->model->modeltype != ID_43DM || !entity->model->m3) {
        return;
    }
    R_GetEntityMatrix(entity, &transform);
    M3_RenderModel(entity, entity->model->m3, &transform);
}

bool R_GameTraceModel(renderEntity_t const *entity, LPCLINE3 line, LPFLOAT distance) {
    (void)entity;
    (void)line;
    (void)distance;
    return false;
}

bool R_GameEntityMatrix(renderEntity_t const *entity, LPMATRIX4 matrix) {
    (void)entity;
    (void)matrix;
    return false;
}

bool R_GameRenderShadow(renderEntity_t const *entity, LPCVECTOR2 origin) {
    (void)entity;
    (void)origin;
    return false;
}

static void R_GameTextureCacheAdd(LPCSTR path) {
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

static void R_GameBuildModelTextureCache(LPMODEL model) {
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
                R_GameTextureCacheAdd(layer->imagePath);
            }
        }
    }
}

bool R_GameGetModelInfo(LPMODEL model, LPMODELINFO info) {
    if (!model || !info || model->modeltype != ID_43DM) {
        return false;
    }
    R_GameBuildModelTextureCache(model);
    if (model_texture_cache.model == model) {
        info->textureCount = MIN(model_texture_cache.count, MODELINFO_MAX_TEXTURES);
        FOR_LOOP(i, info->textureCount) {
            info->texturePaths[i] = model_texture_cache.paths[i];
        }
    }
    return true;
}

void R_GameDrawPortrait(LPCMODEL model, LPCRECT viewport, LPCSTR anim) {
    (void)model;
    (void)viewport;
    (void)anim;
}

bool R_GameExtractEntityCamera(renderEntity_t const *entity, float aspect, viewDef_t *viewdef) {
    (void)entity; (void)aspect; (void)viewdef;
    return false;
}

bool R_GameSetEntityAnimFrame(LPCMODEL model, LPCSTR anim, renderEntity_t *entity) {
    (void)model; (void)anim; (void)entity;
    return false;
}

void R_GameDrawSprite(LPCMODEL model, LPCSTR anim, float x, float y) {
    (void)model;
    (void)anim;
    (void)x;
    (void)y;
}
