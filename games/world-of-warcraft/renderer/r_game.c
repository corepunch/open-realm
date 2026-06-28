#include "renderer/r_game.h"
#include "wow/r_wowmap.h"

void R_RegisterMap(LPCSTR mapFileName);
void R_DrawWorld(void);
void R_DrawTerrainShadows(void);
void R_DrawAlphaSurfaces(void);
bool R_TraceLocation(viewDef_t const *viewdef, FLOAT x, FLOAT y, LPVECTOR3 output);
float GetAccurateHeightAtPoint(float sx, float sy);

m2Model_t *R_LoadModelM2(LPCSTR modelFilename, void *buffer, DWORD size);
void M2_RenderModel(renderEntity_t const *entity, m2Model_t const *model, LPCMATRIX4 transform);
BOOL M2_AttachmentMatrix(m2Model_t const *model, DWORD attachment_id, LPCMATRIX4 model_matrix, LPMATRIX4 out);
FLOAT M2_GroundOffset(m2Model_t const *model);
BOOL M2_CameraView(m2Model_t const *model,
                   DWORD camera_index,
                   LPVECTOR3 eye,
                   LPVECTOR3 target,
                   LPFLOAT fov_degrees,
                   LPFLOAT znear,
                   LPFLOAT zfar);
BOOL M2_IsCharacterModel(m2Model_t const *model);
BOOL M2_SetEntitySequenceFrame(m2Model_t const *model, LPCSTR anim, renderEntity_t *entity);
void M2_Release(m2Model_t *model);
void M2_Shutdown(void);

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
}

void R_GameInit(void) {
}

void R_GameShutdown(void) {
    Wow_ShutdownWorldShaders();
    M2_Shutdown();
}

void R_GameSetupTextureMatrix(void) {
    Matrix4_identity(&tr.viewDef.textureMatrix);
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
    return (VECTOR2){ 0 };
}

LPMODEL R_GameLoadModel(LPCSTR modelFilename) {
    void *buffer = NULL;
    PATHSTR load_name;
    int fileSize = ri.FS_ReadFile(modelFilename, &buffer);
    LPMODEL model;

    snprintf(load_name, sizeof(load_name), "%s", modelFilename ? modelFilename : "");
    if ((fileSize < 0 || !buffer) && R_GamePathHasExtension(modelFilename, ".mdx")) {
        PATHSTR tempFileName = { 0 };
        LPSTR ext = strstr(modelFilename, ".mdx");

        strncpy(tempFileName, modelFilename, ext - modelFilename);
        strcpy(tempFileName + strlen(tempFileName), ".m2");
        fileSize = ri.FS_ReadFile(tempFileName, &buffer);
        if (fileSize >= 0 && buffer) {
            snprintf(load_name, sizeof(load_name), "%s", tempFileName);
        } else {
            fprintf(stderr,
                    "R_LoadModel: missing WoW glue model %s and converted path %s\n",
                    modelFilename,
                    tempFileName);
        }
    }
    if ((fileSize < 0 || !buffer) &&
        (R_GamePathHasExtension(modelFilename, ".m2") ||
         R_GamePathHasExtension(modelFilename, ".mdx"))) {
        fprintf(stderr, "R_LoadModel: missing WoW model %s, using fallback\n", modelFilename);
        model = ri.MemAlloc(sizeof(model_t));
        memset(model, 0, sizeof(*model));
        model->m2 = R_LoadModelM2(modelFilename, NULL, 0);
        model->modeltype = ID_MD20;
        return model;
    }
    if (fileSize < 0 || !buffer) {
        return NULL;
    }
    if (*(DWORD *)buffer != ID_MD20 && *(DWORD *)buffer != ID_MD21 && *(DWORD *)buffer != ID_12DM) {
        fprintf(stderr, "Unknown model format %.4s in file %s\n", (LPSTR)buffer, modelFilename);
        ri.FS_FreeFile(buffer);
        return NULL;
    }

    model = ri.MemAlloc(sizeof(model_t));
    model->m2 = R_LoadModelM2(load_name, buffer, fileSize);
    model->modeltype = ID_MD20;
    if (!model->m2) {
        ri.MemFree(model);
        model = NULL;
    }
    ri.FS_FreeFile(buffer);
    return model;
}

void R_GameReleaseModel(LPMODEL model) {
    if (model->modeltype == ID_MD20) {
        M2_Release(model->m2);
    }
    ri.MemFree(model);
}

bool R_GameEntityMatrix(renderEntity_t const *entity, LPMATRIX4 matrix) {
    VECTOR3 origin;
    MATRIX4 adt_to_world_basis;
    MATRIX4 tmp;

    if (!entity || !entity->model || entity->model->modeltype != ID_MD20) {
        return false;
    }

    origin = entity->origin;
    if (entity->flags & RF_GROUND_ANCHOR) {
        origin.z += M2_GroundOffset(entity->model->m2) * entity->scale;
    }

    Matrix4_identity(matrix);
    Matrix4_translate(matrix, &origin);
    if (entity->flags & RF_GROUND_ANCHOR) {
        /* Dynamic grounded actors share the Warcraft III one-dimensional yaw path. */
        Matrix4_rotate(matrix, &(VECTOR3){ 0.0f, entity->angle * 180.0f / (FLOAT)M_PI, 0.0f }, ROTATE_XYZ);
    }

    Matrix4_identity(&adt_to_world_basis);
    adt_to_world_basis.v[0] = 0.0f;
    adt_to_world_basis.v[1] = 1.0f;
    adt_to_world_basis.v[2] = 0.0f;
    adt_to_world_basis.v[4] = 0.0f;
    adt_to_world_basis.v[5] = 0.0f;
    adt_to_world_basis.v[6] = 1.0f;
    adt_to_world_basis.v[8] = 1.0f;
    adt_to_world_basis.v[9] = 0.0f;
    adt_to_world_basis.v[10] = 0.0f;
    Matrix4_multiply(matrix, &adt_to_world_basis, &tmp);
    *matrix = tmp;
    Matrix4_rotate(matrix, &(VECTOR3){ 0.0f, entity->rotation.y - 270.0f, 0.0f }, ROTATE_XYZ);
    if (!(entity->flags & RF_GROUND_ANCHOR)) {
        Matrix4_rotate(matrix, &(VECTOR3){ 0.0f, 0.0f, -entity->rotation.x }, ROTATE_XYZ);
    }
    Matrix4_rotate(matrix, &(VECTOR3){ entity->rotation.z - 90.0f, 0.0f, 0.0f }, ROTATE_XYZ);
    Matrix4_scale(matrix, &(VECTOR3){entity->scale, entity->scale, entity->scale});
    return true;
}

/* Build a stable top/front light for WoW UI model-camera previews. */
static void R_GameEntityCameraLightMatrix(LPCVECTOR3 target, FLOAT radius, LPMATRIX4 output) {
    MATRIX4 proj;
    MATRIX4 view;
    VECTOR3 light_dir = { -0.35f, -0.50f, 0.80f };
    VECTOR3 view_dir;
    VECTOR3 eye;
    FLOAT distance = MAX(1000.0f, radius * 8.0f);
    FLOAT scale = MAX(64.0f, radius * 2.5f);

    Vector3_normalize(&light_dir);
    view_dir = Vector3_unm(&light_dir);
    eye = Vector3_mad(target, distance, &light_dir);
    Matrix4_ortho(&proj, -scale, scale, -scale, scale, -1000.0f, 3000.0f);
    Matrix4_lookAt(&view, &eye, &view_dir, &(VECTOR3){ 0.0f, 0.0f, 1.0f });
    Matrix4_multiply(&proj, &view, output);
}

void R_GameRenderModel(renderEntity_t const *entity) {
    MATRIX4 transform;
    MATRIX4 attached_transform;
    renderEntity_t attached_entity;
    DWORD attachment_id;

    if (!entity || !entity->model || entity->model->modeltype != ID_MD20) {
        return;
    }
    R_GetEntityMatrix(entity, &transform);
    M2_RenderModel(entity, entity->model->m2, &transform);
    attachment_id = (tr.viewDef.rdflags & RDF_USE_ENTITY_CAMERA) ? 0 : 1;
    if (entity->attached_model &&
        entity->attached_model->modeltype == ID_MD20 &&
#ifdef USE_SHADOWMAPS
        !is_rendering_lights &&
#endif
        M2_AttachmentMatrix(entity->model->m2, attachment_id, &transform, &attached_transform)) {
        if (tr.viewDef.rdflags & RDF_USE_ENTITY_CAMERA) {
            Matrix4_rotate(&attached_transform,
                           &(VECTOR3){ 0.0f, 0.0f, entity->angle * 180.0f / (FLOAT)M_PI },
                           ROTATE_XYZ);
        }
        attached_entity = *entity;
        attached_entity.model = entity->attached_model;
        attached_entity.attached_model = NULL;
        if (!(tr.viewDef.rdflags & RDF_USE_ENTITY_CAMERA)) {
            attached_entity.frame = 0;
            attached_entity.oldframe = 0;
        }
        attached_entity.flags &= ~RF_GROUND_ANCHOR;
        attached_entity.flags |= RF_NO_SHADOW;
        M2_RenderModel(&attached_entity, attached_entity.model->m2, &attached_transform);
    }
}

bool R_GameTraceModel(renderEntity_t const *entity, LPCLINE3 line, LPFLOAT distance) {
    VECTOR3 ab;
    VECTOR3 ac;
    VECTOR3 center;
    FLOAT radius;
    FLOAT denom;
    FLOAT t;
    VECTOR3 closest;
    VECTOR3 delta;
    FLOAT dist2;

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

bool R_GameRenderShadow(renderEntity_t const *entity, LPCVECTOR2 origin) {
    LPCTEXTURE shadow;
    BOOL use_fast_blob;
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
                    entity->shadow_w <= 0 &&
                    entity->shadow_h <= 0;

    if (entity->shadow_w > 0 && entity->shadow_h > 0) {
        mins.x = origin->x - entity->shadow_x;
        mins.y = origin->y - entity->shadow_y;
        maxs.x = mins.x + entity->shadow_w;
        maxs.y = mins.y + entity->shadow_h;
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
        shadow_z = R_GameGetHeightAtPoint(origin->x, origin->y) + WOW_SPLAT_Z_BIAS;
    }
    bounds = (BOX3){
        .min = { mins.x, mins.y, shadow_z - 16.0f },
        .max = { maxs.x, maxs.y, shadow_z + 16.0f },
    };
    if (!(tr.viewDef.rdflags & RDF_NOFRUSTUMCULL) &&
        !Frustum_ContainsAABox(&tr.viewDef.frustum, &bounds)) {
        return true;
    }
    if (use_fast_blob) {
        R_RenderFlatRectSplat(&mins, &maxs, shadow_z, shadow, tr.shader[SHADER_SHADOWSPLAT], shadowColor);
    } else {
        R_RenderRectSplat(&mins, &maxs, shadow, tr.shader[SHADER_SHADOWSPLAT], shadowColor);
    }
    return true;
}

bool R_GameGetModelInfo(LPMODEL model, LPMODELINFO info) {
    (void)model;
    (void)info;
    return false;
}

bool R_GameExtractEntityCamera(renderEntity_t const *entity, float aspect, viewDef_t *viewdef) {
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
        distance = radius / tanf((fov * (FLOAT)M_PI / 180.0f) * 0.5f);
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
    R_GameEntityCameraLightMatrix(&target, radius, &viewdef->lightMatrix);
    return true;
}

bool R_GameSetEntityAnimFrame(LPCMODEL model, LPCSTR anim, renderEntity_t *entity) {
    if (!model || model->modeltype != ID_MD20)
        return false;
    return M2_SetEntitySequenceFrame(model->m2, anim, entity);
}

void R_GameDrawSprite(LPCMODEL model, LPCSTR anim, float x, float y) {
    (void)model;
    (void)anim;
    (void)x;
    (void)y;
}
