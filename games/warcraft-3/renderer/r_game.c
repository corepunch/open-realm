#include "renderer/r_game.h"
#include "mdx/r_mdx.h"
#include "w3m/r_war3map.h"

void R_RegisterMap(LPCSTR mapFileName);
void R_DrawWorld(void);
void R_DrawTerrainShadows(void);
void R_DrawAlphaSurfaces(void);
bool R_TraceLocation(viewDef_t const *viewdef, FLOAT x, FLOAT y, LPVECTOR3 output);
float GetAccurateHeightAtPoint(float sx, float sy);

static LPCSTR selCirclesNames[NUM_SELECTION_CIRCLES] = {
    "ReplaceableTextures\\Selection\\SelectionCircleSmall.blp",
    "ReplaceableTextures\\Selection\\SelectionCircleMed.blp",
    "ReplaceableTextures\\Selection\\SelectionCircleLarge.blp",
};

static LPCSTR sheetNames[SHEET_COUNT] = {
    "TerrainArt\\Terrain.slk",
    "TerrainArt\\CliffTypes.slk",
};

static LPCSTR modelNames[MODEL_COUNT] = {
    "UI\\Feedback\\SelectionCircle\\SelectionCircle.mdx"
};

typedef struct {
    LPMODEL model;
    DWORD count;
    char paths[256][512];
} model_texture_cache_t;

static model_texture_cache_t model_texture_cache = { 0 };

static BOOL R_GamePathHasExtension(LPCSTR path, LPCSTR extension) {
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

void R_GameLoadAssets(void) {
    FOR_LOOP(i, MODEL_COUNT) {
        tr.model[i] = R_LoadModel(modelNames[i]);
    }
    FOR_LOOP(i, SHEET_COUNT) {
        tr.sheet[i] = ri.ReadSheet(sheetNames[i]);
    }
    FOR_LOOP(i, NUM_SELECTION_CIRCLES) {
        tr.texture[TEX_SELECTION_CIRCLE+i] = R_LoadTexture(selCirclesNames[i]);
    }
    FOR_LOOP(team, MAX_TEAMS) {
        PATHSTR glowFilename, colorFilename;
        sprintf(glowFilename, "ReplaceableTextures\\TeamGlow\\TeamGlow%02d.blp", team);
        sprintf(colorFilename, "ReplaceableTextures\\TeamColor\\TeamColor%02d.blp", team);
        tr.texture[TEX_TEAM_GLOW + team] = R_LoadTexture(glowFilename);
        tr.texture[TEX_TEAM_COLOR + team] = R_LoadTexture(colorFilename);
    }
    tr.texture[TEX_WATER] = R_LoadTexture("ReplaceableTextures\\Water\\Water12.blp");
}

void R_GameInit(void) {
    MDLX_Init();
}

void R_GameShutdown(void) {
    MDLX_Shutdown();
}

void R_GameSetupTextureMatrix(void) {
    if (tr.world) {
        VECTOR2 s = GetWar3MapSize(tr.world);
        VECTOR2 c = tr.world->center;
        Matrix4_ortho(&tr.viewDef.textureMatrix, -s.x+c.x, s.x+c.x, -s.y+c.y, s.y+c.y, 0.0f, 100.0f);
    } else {
        Matrix4_identity(&tr.viewDef.textureMatrix);
    }
}

void R_GameRegisterMap(LPCSTR mapFileName) {
    R_RegisterMap(mapFileName);
}

void R_GameDrawWorld(void) {
    R_DrawWorld();
}

void R_GameDrawTerrainShadows(void) {
    R_DrawTerrainShadows();
}

void R_GameDrawAlphaSurfaces(void) {
    R_DrawAlphaSurfaces();
}

bool R_GameTraceLocation(viewDef_t const *viewdef, float x, float y, LPVECTOR3 point) {
    return R_TraceLocation(viewdef, x, y, point);
}

FLOAT R_GameGetHeightAtPoint(FLOAT x, FLOAT y) {
    return GetAccurateHeightAtPoint(x, y);
}

VECTOR2 R_GameWorldSize(void) {
    return tr.world ? GetWar3MapSize(tr.world) : (VECTOR2){ 0 };
}

LPMODEL R_GameLoadModel(LPCSTR modelFilename) {
    void *buffer = NULL;
    int fileSize = ri.FS_ReadFile(modelFilename, &buffer);
    LPMODEL model = NULL;

    if (fileSize < 0 && R_GamePathHasExtension(modelFilename, ".mdl")) {
        /* R_GamePathHasExtension uses strcasecmp, so the extension may be any
         * case variant (e.g. ".MDL").  strstr() is case-sensitive, so we must
         * not use it here; instead compute the stem length directly. */
        PATHSTR tempFileName = { 0 };
        size_t stemLen = strlen(modelFilename) - 4; /* ".mdl" = 4 chars */

        strncpy(tempFileName, modelFilename, stemLen);
        strcpy(tempFileName + stemLen, ".mdx");
        fileSize = ri.FS_ReadFile(tempFileName, &buffer);
    }
    if (fileSize < 0) {
        size_t nameLen = strlen(modelFilename);
        if (nameLen >= 4) {
            PATHSTR tempFileName = { 0 };
            LPCSTR end = modelFilename + nameLen - 4;
            size_t stemLen;

            if (end > modelFilename && isdigit((unsigned char)*(end - 1))) {
                end--;
            }
            stemLen = (size_t)(end - modelFilename);
            strncpy(tempFileName, modelFilename, stemLen);
            strcpy(tempFileName + stemLen, ".mdx");
            fileSize = ri.FS_ReadFile(tempFileName, &buffer);
        }
    }
    if (fileSize < 0 || !buffer) {
        return NULL;
    }
    if (*(DWORD *)buffer == ID_MDLX) {
        model = ri.MemAlloc(sizeof(model_t));
        model->mdx = R_LoadModelMDLX(buffer, fileSize);
        model->modeltype = ID_MDLX;
    } else if (R_GamePathHasExtension(modelFilename, ".mdl")) {
        /* Same case-insensitive issue: use stem length, not strstr. */
        PATHSTR tempFileName = { 0 };
        size_t stemLen = strlen(modelFilename) - 4;

        strncpy(tempFileName, modelFilename, stemLen);
        strcpy(tempFileName + stemLen, ".mdx");
        ri.FS_FreeFile(buffer);
        return R_LoadModel(tempFileName);
    } else {
        fprintf(stderr, "Unknown model format %.4s in file %s\n", (LPSTR)buffer, modelFilename);
    }
    ri.FS_FreeFile(buffer);
    return model;
}

void R_GameReleaseModel(LPMODEL model) {
    if (model->modeltype == ID_MDLX) {
        MDLX_Release(model->mdx);
    }
    ri.MemFree(model);
}

void R_GameRenderModel(renderEntity_t const *entity) {
    MATRIX4 transform;

    if (!entity || !entity->model || entity->model->modeltype != ID_MDLX) {
        return;
    }
    R_GetEntityMatrix(entity, &transform);
    MDX_RenderModel(entity, entity->model->mdx, &transform);
}

bool R_GameTraceModel(renderEntity_t const *entity, LPCLINE3 line, LPFLOAT distance) {
    if (!entity || !entity->model || entity->model->modeltype != ID_MDLX) {
        return false;
    }
    if (!MDLX_TraceModel(entity, line)) {
        return false;
    }
    if (distance) {
        *distance = 0.0f;
    }
    return true;
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
    if (!model || model->modeltype != ID_MDLX || !model->mdx || !model->mdx->textures) {
        return;
    }
    FOR_LOOP(i, model->mdx->num_textures) {
        R_GameTextureCacheAdd(model->mdx->textures[i].path);
    }
}

bool R_GameGetModelInfo(LPMODEL model, LPMODELINFO info) {
    bool found = false;
    float min_u = 1.0f, min_v = 1.0f, max_u = 0.0f, max_v = 0.0f;

    if (!model || !info || model->modeltype != ID_MDLX || !model->mdx) {
        return false;
    }

    R_GameBuildModelTextureCache(model);
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

void R_GameDrawPortrait(LPCMODEL model, LPCRECT viewport, LPCSTR anim) {
    MDLX_DrawPortrait(model, viewport, anim);
}

bool R_GameExtractEntityCamera(renderEntity_t const *entity, float aspect, viewDef_t *viewdef) {
    if (!entity || !entity->model || entity->model->modeltype != ID_MDLX || !viewdef)
        return false;
    mdxModel_t const *mdx = entity->model->mdx;
    if (!mdx || !mdx->cameras)
        return false;

    mdxCamera_t const *camera = mdx->cameras;
    VECTOR3 eye = camera->pivot;
    VECTOR3 target = camera->targetPivot;
    VECTOR3 up = { 0, 0, 1 };
    float fov_deg = camera->fieldOfView * (180.0f / (float)M_PI);
    float near_clip = camera->nearClip;
    float far_clip = camera->farClip;
    float roll = 0.0f;

    if (!isfinite(fov_deg) || fov_deg <= 1.0f || fov_deg >= 179.0f)
        fov_deg = 35.0f;
    if (!isfinite(near_clip) || near_clip < 0.01f)
        near_clip = 1.0f;
    if (!isfinite(far_clip) || far_clip <= near_clip + 1.0f)
        far_clip = near_clip + 5000.0f;
    if (!isfinite(aspect) || aspect <= 0.0f)
        aspect = 1.0f;

    if (camera->translation) {
        VECTOR3 translation = { 0, 0, 0 };
        MDLX_GetModelKeytrackValue(mdx, camera->translation, entity->frame, &translation);
        eye = Vector3_add(&eye, &translation);
    }
    if (camera->targetTranslation) {
        VECTOR3 targetTranslation = { 0, 0, 0 };
        MDLX_GetModelKeytrackValue(mdx, camera->targetTranslation, entity->frame, &targetTranslation);
        target = Vector3_add(&target, &targetTranslation);
    }
    VECTOR3 dir = Vector3_sub(&target, &eye);
    if (Vector3_len(&dir) < 0.001f)
        return false;
    if (camera->roll) {
        MDLX_GetModelKeytrackValue(mdx, camera->roll, entity->frame, &roll);
        if (isfinite(roll) && fabsf(roll) > 0.0001f)
            up = Vector3_rotateAroundAxis(&up, &dir, roll);
    }

    float const camera_aspect = 1.66f;
    fov_deg = 2.0f * atanf(tanf(fov_deg * (float)M_PI / 360.0f) / camera_aspect) * 180.0f / (float)M_PI;

    MATRIX4 proj_matrix, view_matrix;
    Matrix4_perspective(&proj_matrix, fov_deg, aspect, near_clip, far_clip);
    Matrix4_lookAt(&view_matrix, &eye, &dir, &up);
    Matrix4_multiply(&proj_matrix, &view_matrix, &viewdef->viewProjectionMatrix);
    Matrix4_identity(&viewdef->textureMatrix);

    VECTOR3 lightAngles = { 10, 270, 0 };
    Matrix4_getLightMatrix(&lightAngles, &target, PORTRAIT_SHADOW_SIZE, &viewdef->lightMatrix);
    viewdef->rdflags |= RDF_PORTRAIT_LIGHTING;
    return true;
}

bool R_GameSetEntityAnimFrame(LPCMODEL model, LPCSTR anim, renderEntity_t *entity) {
    if (!model || model->modeltype != ID_MDLX || !model->mdx || !anim || !entity)
        return false;
    mdxModel_t const *mdx = model->mdx;
    mdxSequence_t const *seq = NULL;
    LPCSTR sequence = anim;

    if (sequence && sequence[0] == '#' && sequence[1] == '!')
        sequence += 2;
    else if (sequence && sequence[0] == '#')
        sequence++;

    if (anim[0] == '#') {
        char *end = NULL;
        unsigned long index = strtoul(sequence, &end, 10);
        if (end && (*end == '\0' || *end == '@') && index < (unsigned long)mdx->num_sequences)
            seq = &mdx->sequences[index];
    } else if (anim && *anim) {
        LPCSTR ratio = strchr(anim, '@');
        char sequence_name[sizeof(mdxObjectName_t) + 1];
        size_t len = ratio ? (size_t)(ratio - anim) : strlen(anim);

        if (len >= sizeof(sequence_name))
            len = sizeof(sequence_name) - 1;
        memcpy(sequence_name, anim, len);
        sequence_name[len] = '\0';
        seq = MDLX_FindSequenceByName(mdx, sequence_name);
    }
    if (!seq)
        return false;

    DWORD seq_len = seq->interval[1] - seq->interval[0];
    if (seq_len == 0)
        seq_len = 1;
    entity->frame = seq->interval[0] + (entity->frame % seq_len);
    entity->oldframe = entity->frame;
    return true;
}

void R_GameDrawSprite(LPCMODEL model, LPCSTR anim, float x, float y) {
    MDLX_DrawSprite(model, anim, x, y);
}
