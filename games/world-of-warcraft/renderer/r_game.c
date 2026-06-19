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
        Matrix4_rotate(matrix, &(VECTOR3){ 0.0f, 0.0f, (entity->angle * 180.0f / (FLOAT)M_PI) + 180.0f }, ROTATE_XYZ);
    }

    Matrix4_identity(&adt_to_world_basis);
    adt_to_world_basis.v[0] = 0.0f;
    adt_to_world_basis.v[1] = -1.0f;
    adt_to_world_basis.v[2] = 0.0f;
    adt_to_world_basis.v[4] = 0.0f;
    adt_to_world_basis.v[5] = 0.0f;
    adt_to_world_basis.v[6] = 1.0f;
    adt_to_world_basis.v[8] = -1.0f;
    adt_to_world_basis.v[9] = 0.0f;
    adt_to_world_basis.v[10] = 0.0f;
    Matrix4_multiply(matrix, &adt_to_world_basis, &tmp);
    *matrix = tmp;

    Matrix4_scale(matrix, &(VECTOR3){ -1.0f, 1.0f, -1.0f });
    Matrix4_rotate(matrix, &(VECTOR3){ 0.0f, entity->rotation.y - 270.0f, 0.0f }, ROTATE_XYZ);
    if (!(entity->flags & RF_GROUND_ANCHOR)) {
        Matrix4_rotate(matrix, &(VECTOR3){ 0.0f, 0.0f, -entity->rotation.x }, ROTATE_XYZ);
    }
    Matrix4_rotate(matrix, &(VECTOR3){ entity->rotation.z - 90.0f, 0.0f, 0.0f }, ROTATE_XYZ);
    Matrix4_scale(matrix, &(VECTOR3){entity->scale, entity->scale, entity->scale});
    return true;
}

void R_GameRenderModel(renderEntity_t const *entity) {
    MATRIX4 transform;
    MATRIX4 attached_transform;
    renderEntity_t attached_entity;

    if (!entity || !entity->model || entity->model->modeltype != ID_MD20) {
        return;
    }
    R_GetEntityMatrix(entity, &transform);
    M2_RenderModel(entity, entity->model->m2, &transform);
    if (entity->attached_model &&
        entity->attached_model->modeltype == ID_MD20 &&
#ifdef USE_SHADOWMAPS
        !is_rendering_lights &&
#endif
        M2_AttachmentMatrix(entity->model->m2, 1, &transform, &attached_transform)) {
        attached_entity = *entity;
        attached_entity.model = entity->attached_model;
        attached_entity.attached_model = NULL;
        attached_entity.frame = 0;
        attached_entity.oldframe = 0;
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

void R_GameDrawPortrait(LPCPORTRAITDEF params) {
    renderEntity_t entity;
    MATRIX4 transform;
    MATRIX4 proj_matrix;
    MATRIX4 view_matrix;
    viewDef_t saved_viewdef;
    int saved_viewport[4];
    BOX3 const *bounds;
    VECTOR3 center;
    VECTOR3 eye;
    VECTOR3 target;
    VECTOR3 dir;
    float radius;
    float distance;
    float aspect;
    float fov = 35.0f;
    float znear = 1.0f;
    float zfar = 4000.0f;
    LPCMODEL model = params->model;
    LPCRECT viewport = params->viewport;

    (void)params;
    (void)params->fog.has_fog;
    (void)params->fog.fog_color;
    (void)params->fog.fog_near;
    (void)params->fog.fog_far;

    if (!model || !model->m2 || !viewport) {
        return;
    }

    bounds = &model->m2->bounds;

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

    memset(&entity, 0, sizeof(entity));
    entity.model = model;
    entity.frame = params->frame;
    entity.oldframe = 0;
    entity.flags = RF_NO_SHADOW | RF_NO_FOGOFWAR | RF_NO_LIGHTING | RDF_NOFRUSTUMCULL;
    entity.rotation = (VECTOR3){0, 0, 0};
    entity.scale = 1.0f;
    entity.origin = (VECTOR3){0, 0, 0};

    Matrix4_identity(&transform);
    Matrix4_translate(&transform, &entity.origin);
    Matrix4_scale(&transform, &(VECTOR3){entity.scale, entity.scale, entity.scale});

    saved_viewdef = tr.viewDef;
    glGetIntegerv(GL_VIEWPORT, saved_viewport);

    glViewport((GLint)(viewport->x * tr.drawableSize.width),
               (GLint)(viewport->y * tr.drawableSize.height),
               (GLint)(viewport->w * tr.drawableSize.width),
               (GLint)(viewport->h * tr.drawableSize.height));
    R_Call(glClear, GL_DEPTH_BUFFER_BIT);

    aspect = viewport->h > 0.0f ? viewport->w / viewport->h : 1.0f;
    if (!M2_CameraView(model->m2, 0, &eye, &target, &fov, &znear, &zfar)) {
        distance = radius / tanf((fov * (FLOAT)M_PI / 180.0f) * 0.5f);
        eye = (VECTOR3){ center.x, center.y - distance * 1.35f, center.z + radius * 0.25f };
        target = center;
        zfar = MAX(zfar, distance + radius * 4.0f);
    }
    dir = Vector3_sub(&target, &eye);
    if (Vector3_len(&dir) <= 0.001f) {
        dir = (VECTOR3){ 0.0f, 1.0f, 0.0f };
    }
    Matrix4_perspective(&proj_matrix, fov, aspect, znear, zfar);
    Matrix4_lookAt(&view_matrix, &eye, &dir, &(VECTOR3){0, 0, 1});

    Matrix4_multiply(&proj_matrix, &view_matrix, &tr.viewDef.viewProjectionMatrix);
    Frustum_Calculate(&tr.viewDef.viewProjectionMatrix, &tr.viewDef.frustum);
    Matrix4_identity(&tr.viewDef.textureMatrix);
    Matrix4_identity(&tr.viewDef.lightMatrix);

    M2_RenderModel(&entity, model->m2, &transform);

    tr.viewDef = saved_viewdef;
    glViewport(saved_viewport[0], saved_viewport[1], saved_viewport[2], saved_viewport[3]);
}

void R_GameDrawSprite(LPCMODEL model, LPCSTR anim, float x, float y) {
    (void)model;
    (void)anim;
    (void)x;
    (void)y;
}
