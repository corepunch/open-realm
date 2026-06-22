#include <stdlib.h> // atoi()

#include "client.h"
#include "tr_public.h"
#ifdef SC2
#include "games/starcraft-2/common/sc2_map.h"
#endif

static struct {
    renderEntity_t entities[MAX_CLIENT_ENTITIES];
    renderDecal_t decals[MAX_RENDER_DECALS];
    int num_entities;
    int num_decals;
} view_state;

static bool world_loaded = false;
static bool begin_sent = false;

#ifdef WOW
#define CL_MODEL_LOADS_PER_FRAME 32
#else
#define CL_MODEL_LOADS_PER_FRAME 8
#endif

VECTOR3 lightAngles = {-40,0,60};

/* Lighting direction is not the shadow projection; keep it explicit for shaders. */
static VECTOR3 V_LightDirForAngles(LPCVECTOR3 angles) {
    FLOAT yaw = (FLOAT)DEG2RAD(angles->y), pitch = (FLOAT)DEG2RAD(angles->x);
    VECTOR3 dir = { cosf(pitch) * cosf(yaw), cosf(pitch) * sinf(yaw), -sinf(pitch) };
    Vector3_normalize(&dir);
    return dir;
}

static DWORD CL_CountConfigstrings(DWORD start, DWORD max) {
    DWORD count = 0;

    for (DWORD i = 1; i < max && start + i < MAX_CONFIGSTRINGS; i++) {
        if (*cl.configstrings[start + i]) {
            count++;
        }
    }
    return count;
}

static DWORD CL_CountLoadedConfigstrings(DWORD start, DWORD max, HANDLE const *handles) {
    DWORD count = 0;

    for (DWORD i = 1; i < max && start + i < MAX_CONFIGSTRINGS; i++) {
        if (*cl.configstrings[start + i] && handles[i]) {
            count++;
        }
    }
    return count;
}

static void CL_UpdateAssetLoadingProgress(LPCSTR status, DWORD loaded, DWORD total) {
    FLOAT progress;

    if (!total) {
        total = 1;
    }
    progress = (FLOAT)loaded / (FLOAT)total;
    CL_LoadingUpdate(status, progress);
}

static void CL_SendBegin(void) {
    fprintf(stderr,
            "CL_SendBegin: sending begin world=\"%s\" state=%d player=%u team=%u race=%u color=%u\n",
            cl.configstrings[CS_WORLD],
            cls.state,
            (unsigned)cl.playerstate.number,
            (unsigned)cl.playerstate.team,
            (unsigned)cl.playerstate.race,
            (unsigned)cl.playerstate.color);
    MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
    MSG_WriteString(&cls.netchan.message, "begin");
}

void Matrix4_fromViewAngles(LPCVECTOR3 target, LPCVECTOR3 angles, FLOAT distance, LPMATRIX4 output) {
    VECTOR3 const vieworg = Vector3_unm(target);
    Matrix4_identity(output);
    Matrix4_translate(output, &(VECTOR3){0, 0, -distance});
    Matrix4_rotate(output, angles, ROTATE_ZYX);
    Matrix4_translate(output, &vieworg);
}

void Matrix4_fromViewQuat(LPCVECTOR3 target, LPCQUATERNION quat, FLOAT distance, LPMATRIX4 output) {
    VECTOR3 const vieworg = Vector3_unm(target);
    Matrix4_identity(output);
    Matrix4_translate(output, &(VECTOR3){0, 0, -distance});
    Matrix4_rotateQuat(output, quat);
    Matrix4_translate(output, &vieworg);
}

#ifdef WOW
static FLOAT LerpDegrees(FLOAT a, FLOAT b, FLOAT t) {
    FLOAT delta = fmodf(b - a, 360.0f);
    if (delta > 180.0f) {
        delta -= 360.0f;
    } else if (delta < -180.0f) {
        delta += 360.0f;
    }
    return a + delta * t;
}

static void Wow_AngleVectors(LPCVECTOR3 angles, LPVECTOR3 forward, LPVECTOR3 right, LPVECTOR3 up) {
    FLOAT yaw = (FLOAT)DEG2RAD(angles->y);
    FLOAT pitch = (FLOAT)DEG2RAD(angles->x);
    FLOAT roll = (FLOAT)DEG2RAD(angles->z);
    FLOAT sy = sinf(yaw);
    FLOAT cy = cosf(yaw);
    FLOAT sp = sinf(pitch);
    FLOAT cp = cosf(pitch);
    FLOAT sr = sinf(roll);
    FLOAT cr = cosf(roll);

    if (forward) {
        *forward = (VECTOR3){ cp * cy, cp * sy, -sp };
    }
    if (right) {
        *right = (VECTOR3){
            -sr * sp * cy + cr * sy,
            -sr * sp * sy - cr * cy,
            -sr * cp,
        };
    }
    if (up) {
        *up = (VECTOR3){
            cr * sp * cy + sr * sy,
            cr * sp * sy - sr * cy,
            cr * cp,
        };
    }
}
#endif

#ifdef SC2
static void Matrix4_getSc2CameraMatrix(LPCVECTOR3 origin,
                                       LPCVECTOR3 angles,
                                       FLOAT distance,
                                       FLOAT height_offset,
                                       FLOAT fov,
                                       FLOAT aspect,
                                       FLOAT znear,
                                       FLOAT zfar,
                                       LPMATRIX4 output) {
    FLOAT const pitch = (FLOAT)DEG2RAD(angles && angles->x != 0.0f ? angles->x : 56.0f);
    FLOAT const yaw = (FLOAT)DEG2RAD(angles && angles->y != 0.0f ? angles->y : 180.0f);
    FLOAT const horizontal = cosf(pitch);
    VECTOR3 target = *origin;
    VECTOR3 dir = {
        sinf(yaw) * horizontal,
        cosf(yaw) * horizontal,
        -sinf(pitch),
    };
    VECTOR3 eye;
    MATRIX4 proj, view;

    distance = distance > 0.0f ? distance : 34.07f;
    fov = fov > 0.0f ? fov : 27.8f;
    znear = znear > 0.0f ? znear : 0.1f;
    zfar = zfar > 0.0f ? zfar : 400.0f;
    target.z = height_offset;
    eye = Vector3_sub(&target, &(VECTOR3){ dir.x * distance, dir.y * distance, dir.z * distance });
    Matrix4_perspective(&proj, fov, aspect, znear, zfar);
    Matrix4_lookAt(&view, &eye, &dir, &(VECTOR3){ 0.0f, 0.0f, 1.0f });
    Matrix4_multiply(&proj, &view, output);
}
#endif

void Matrix4_getWorldShadowMatrix(LPCVECTOR3 sunangles, FLOAT scale, LPMATRIX4 output) {
    MATRIX4 proj, view, tmp1, tmp2;
    viewCamera_t const *a = cl.viewDef.camerastate+1;
    viewCamera_t const *b = cl.viewDef.camerastate+0;
    VECTOR3 const target = Vector3_lerp(&a->origin, &b->origin, cl.viewDef.lerpfrac);
    Matrix4_ortho(&proj, -scale, scale, -scale, scale, -1000.0, 3000.0);
    Matrix4_identity(&tmp1);
    Matrix4_rotate(&tmp1, &(VECTOR3){0,0,45}, ROTATE_XYZ);
    Matrix4_fromViewAngles(&target, sunangles, 1000, &tmp2);
    Matrix4_multiply(&tmp1, &tmp2, &view);
    Matrix4_translate(&view, &(VECTOR3){0,-500,0});
    Matrix4_multiply(&proj, &view, output);
}

static void Matrix4_getPreviewCameraMatrix(LPCVECTOR3 target, LPMATRIX4 output) {
    MATRIX4 proj, view;
    size2_t windowSize = re.GetWindowSize();
    VECTOR3 eye = { 520.0f, -420.0f, 220.0f };
    VECTOR3 dir = Vector3_sub(target, &eye);
    FLOAT aspect = (FLOAT)windowSize.width / (FLOAT)windowSize.height;

    Matrix4_perspective(&proj, 35.0f, aspect, 10.0f, 4000.0f);
    Matrix4_lookAt(&view, &eye, &dir, &(VECTOR3){0, 0, 1});
    Matrix4_multiply(&proj, &view, output);
}

static void Matrix4_getPreviewShadowMatrix(LPCVECTOR3 sunangles, LPCVECTOR3 target, float scale, LPMATRIX4 output) {
    MATRIX4 proj, view;
    Matrix4_ortho(&proj, -scale, scale, -scale, scale, -1000.0, 3000.0);
    Matrix4_fromViewAngles(target, sunangles, 1000, &view);
    Matrix4_multiply(&proj, &view, output);
}

void Matrix4_getCameraMatrix(LPMATRIX4 output) {
    if (!world_loaded) {
        Matrix4_identity(output);
        return;
    }
    MATRIX4 proj, view;
    size2_t windowSize = re.GetWindowSize();
    viewCamera_t *a = cl.viewDef.camerastate+1;
    viewCamera_t *b = cl.viewDef.camerastate+0;
    VECTOR3 origin = Vector3_lerp(&a->origin, &b->origin, cl.viewDef.lerpfrac);
#if !defined(WOW) && !defined(SC2)
    QUATERNION quat = Quaternion_slerp(&a->viewquat, &b->viewquat, cl.viewDef.lerpfrac);
#endif
    FLOAT distance = LerpNumber(a->distance, b->distance, cl.viewDef.lerpfrac);
    FLOAT fov = LerpNumber(a->fov, b->fov, cl.viewDef.lerpfrac);
    FLOAT aspect = (FLOAT)windowSize.width / (FLOAT)windowSize.height;
    FLOAT znear = LerpNumber(a->znear, b->znear, cl.viewDef.lerpfrac);
    FLOAT zfar = LerpNumber(a->zfar, b->zfar, cl.viewDef.lerpfrac);
    
#ifdef WOW
    VECTOR3 angles = {
        LerpDegrees(a->viewangles.x, b->viewangles.x, cl.viewDef.lerpfrac),
        LerpDegrees(a->viewangles.y, b->viewangles.y, cl.viewDef.lerpfrac),
        LerpDegrees(a->viewangles.z, b->viewangles.z, cl.viewDef.lerpfrac),
    };
    VECTOR3 forward;
    VECTOR3 offset;
    VECTOR3 eye;

    origin.z = CM_GetHeightAtPoint(origin.x, origin.y) + 1.6f;
    Wow_AngleVectors(&angles, &forward, NULL, NULL);
    offset = Vector3_scale(&forward, -distance);
    eye = Vector3_add(&origin, &offset);

    Matrix4_perspective(&proj, fov, aspect, znear, zfar);
    Matrix4_lookAt(&view, &eye, &forward, &(VECTOR3){ 0.0f, 0.0f, 1.0f });
#else
#ifdef SC2
    (void)proj;
    (void)view;
    Matrix4_getSc2CameraMatrix(&origin, &b->viewangles, distance, CM_GetCameraHeightOffset(), fov, aspect, znear, zfar, output);
    return;
#else
    origin.z = CM_GetHeightAtPoint(origin.x, origin.y) - 128;

    Matrix4_perspective(&proj, fov, aspect, znear, zfar);
    Matrix4_fromViewQuat(&origin, &quat, distance, &view);
#endif
#endif
    Matrix4_multiply(&proj, &view, output);
}

FLOAT LerpRotation(FLOAT a, FLOAT b, FLOAT t) {
    if (b < 0) {
        b = b + 2 * M_PI;
    }
    FLOAT apos = a + 2 * M_PI;
    FLOAT aneg = a - 2 * M_PI;
    if (fabs(a - b) < fabs(apos - b) && fabs(a - b) < fabs(aneg - b)) {
        return LerpNumber(a, b, t);
    } else if (fabs(apos - b) < fabs(aneg - b)) {
        return LerpNumber(apos, b, t);
    } else {
        return LerpNumber(aneg, b, t);
    }
}

static void V_AddClientEntity(centity_t const *ent) {
    renderEntity_t re = { 0 };
    if (view_state.num_entities >= MAX_CLIENT_ENTITIES) {
        return;
    }
    if (ent->current.model >= MAX_MODELS) {
        return;
    }
    
    re.origin = Vector3_lerp(&ent->prev.origin, &ent->current.origin, cl.viewDef.lerpfrac);
    re.angle = LerpRotation(ent->prev.angle, ent->current.angle, cl.viewDef.lerpfrac);
    re.rotation = Vector3_lerp(&ent->prev.rotation, &ent->current.rotation, cl.viewDef.lerpfrac);
    re.scale = LerpNumber(ent->prev.scale, ent->current.scale, cl.viewDef.lerpfrac);
    re.frame = ent->current.frame;
    re.oldframe = ent->prev.frame;
    re.model = cl.models[ent->current.model];
    re.skin = cl.pics[ent->current.image];
    re.team = ent->current.player;
#ifdef WOW
    re.appearance = ent->current.appearance;
    re.equipment = ent->current.equipment;
#endif
    re.flags = ent->current.renderfx;
    if (ent->current.flags & EF_GROUND_ANCHOR) {
        re.flags |= RF_GROUND_ANCHOR;
    }
    if (ent->current.flags & EF_FOW_BLOCKER) {
        re.flags |= RF_FOW_BLOCKER;
    }
    if (ent->current.flags & EF_FOW_REVEALER) {
        re.flags |= RF_FOW_REVEALER;
    }
    re.radius = ent->current.radius;
    re.number = ent->current.number;
    re.splat = cl.pics[ent->current.splat & 0xffff];
    re.splatsize = ent->current.splat >> 16;
#ifdef WOW
    if (ent->current.model2 > 0 &&
        ent->current.model2 < MAX_MODELS &&
        !(ent->current.renderfx & RF_ATTACH_OVERHEAD)) {
        re.attached_model = cl.models[ent->current.model2];
    }
#endif

    view_state.entities[view_state.num_entities++] = re;
    
    if (ent->current.model2 > 0) {
#ifdef WOW
        if (re.attached_model && !(ent->current.renderfx & RF_ATTACH_OVERHEAD)) {
            return;
        }
#endif
        if (ent->current.model2 >= MAX_MODELS) {
            return;
        }
        if (view_state.num_entities >= MAX_CLIENT_ENTITIES) {
            return;
        }
        re.model = cl.models[ent->current.model2];
        re.skin = 0;
        re.frame = 0;
        re.oldframe = 0;
        re.scale = 1;
        re.flags |= RF_NO_SHADOW;
        if (ent->current.renderfx & RF_ATTACH_OVERHEAD) {
            re.origin.z += re.radius * 2.5;
        }
        view_state.entities[view_state.num_entities++] = re;
    }
}

static void V_ClearScene(void) {
    view_state.num_entities = 0;
    view_state.num_decals = 0;
    cl.viewDef.num_entities = 0;
    cl.viewDef.num_decals = 0;
}

static void CL_DebugEntityView(void) {
    static BYTE prev_snapshot[MAX_CLIENT_ENTITIES];
    static BYTE prev_view[MAX_CLIENT_ENTITIES];
    static BYTE prev_mismatch[MAX_CLIENT_ENTITIES];
    static BOOL initialized = false;
    BYTE snapshot[MAX_CLIENT_ENTITIES];
    BYTE view[MAX_CLIENT_ENTITIES];
    BYTE mismatch[MAX_CLIENT_ENTITIES];
    int debug_entities = Cvar_Integer("cl_debug_entities", 0);
    DWORD snapshot_count = 0;
    DWORD view_count = 0;
    DWORD missing_model_handles = 0;

    if (!debug_entities) {
        initialized = false;
        return;
    }

    memset(snapshot, 0, sizeof(snapshot));
    memset(view, 0, sizeof(view));
    memset(mismatch, 0, sizeof(mismatch));

    FOR_LOOP(index, MAX_CLIENT_ENTITIES) {
        centity_t const *ce = &cl.ents[index];

        if (!ce->current.model) {
            continue;
        }
        snapshot[index] = 1;
        snapshot_count++;
        if (ce->current.model >= MAX_MODELS || !cl.models[ce->current.model]) {
            missing_model_handles++;
        }
    }
    FOR_LOOP(index, view_state.num_entities) {
        renderEntity_t const *ent = &view_state.entities[index];

        if (ent->number < MAX_CLIENT_ENTITIES) {
            view[ent->number] = 1;
        }
        view_count++;
    }
    FOR_LOOP(index, MAX_CLIENT_ENTITIES) {
        centity_t const *ce = &cl.ents[index];

        mismatch[index] = snapshot[index] && !view[index];
        if (!initialized) {
            continue;
        }
        if (prev_snapshot[index] && !snapshot[index]) {
            fprintf(stderr, "CL view snapshot lost frame=%d ent=%u\n", cl.frame.serverframe, (unsigned)index);
        } else if (!prev_snapshot[index] && snapshot[index]) {
            fprintf(stderr,
                    "CL view snapshot gained frame=%d ent=%u model=%u class=%u origin=(%.1f %.1f %.1f)\n",
                    cl.frame.serverframe,
                    (unsigned)index,
                    (unsigned)ce->current.model,
                    (unsigned)ce->current.class_id,
                    ce->current.origin.x,
                    ce->current.origin.y,
                    ce->current.origin.z);
        }
        if (prev_view[index] && !view[index]) {
            fprintf(stderr,
                    "CL view render lost frame=%d ent=%u snapshot=%d model=%u class=%u\n",
                    cl.frame.serverframe,
                    (unsigned)index,
                    snapshot[index] ? 1 : 0,
                    (unsigned)ce->current.model,
                    (unsigned)ce->current.class_id);
        } else if (!prev_view[index] && view[index]) {
            fprintf(stderr,
                    "CL view render gained frame=%d ent=%u model=%u class=%u\n",
                    cl.frame.serverframe,
                    (unsigned)index,
                    (unsigned)ce->current.model,
                    (unsigned)ce->current.class_id);
        }
        if (debug_entities > 1 && mismatch[index] && !prev_mismatch[index]) {
            fprintf(stderr,
                    "CL view mismatch frame=%d ent=%u model=%u class=%u model_handle=%d\n",
                    cl.frame.serverframe,
                    (unsigned)index,
                    (unsigned)ce->current.model,
                    (unsigned)ce->current.class_id,
                    ce->current.model < MAX_MODELS && cl.models[ce->current.model] ? 1 : 0);
        }
    }
    if (debug_entities > 1) {
        fprintf(stderr,
                "CL view summary frame=%d snapshot=%u view=%u missing_model_handles=%u\n",
                cl.frame.serverframe,
                (unsigned)snapshot_count,
                (unsigned)view_count,
                (unsigned)missing_model_handles);
    }

    memcpy(prev_snapshot, snapshot, sizeof(prev_snapshot));
    memcpy(prev_view, view, sizeof(prev_view));
    memcpy(prev_mismatch, mismatch, sizeof(prev_mismatch));
    initialized = true;
}

static void CL_AddBuilding(void) {
    if (!cl.cursorEntity)
        return;
    if (view_state.num_entities >= MAX_CLIENT_ENTITIES)
        return;
    if (cl.cursorEntity->model >= MAX_MODELS)
        return;

    renderEntity_t ent;
    memset(&ent, 0, sizeof(renderEntity_t));
    
    re.TraceLocation(&cl.viewDef, mouse.origin.x, mouse.origin.y, &ent.origin);

    ent.origin.x = floor(ent.origin.x / 32) * 32;
    ent.origin.y = floor(ent.origin.y / 32) * 32;
    ent.origin.z = CM_GetHeightAtPoint(ent.origin.x, ent.origin.y);
    ent.scale = cl.cursorEntity->scale;
    ent.frame = cl.cursorEntity->frame;
    ent.oldframe = cl.cursorEntity->frame;
    ent.model = cl.models[cl.cursorEntity->model];
    
    cl.cursorEntity->origin = ent.origin;
    
    view_state.entities[view_state.num_entities++] = ent;
}

static void CL_AddCursorSplat(void) {
    renderDecal_t decal;
    VECTOR3 point;

    if (!cl.cursor_splat.image || cl.cursor_splat.image >= MAX_IMAGES ||
        cl.cursor_splat.radius <= 0.0f) {
        return;
    }
    if (CL_MouseOverGameplayUI()) {
        return;
    }
    if (!re.TraceLocation(&cl.viewDef, mouse.origin.x, mouse.origin.y, &point)) {
        return;
    }

    memset(&decal, 0, sizeof(decal));
    decal.origin = (VECTOR2){ point.x, point.y };
    decal.radius = cl.cursor_splat.radius;
    decal.texture = cl.pics[cl.cursor_splat.image];
    decal.color = (COLOR32){ 255, 255, 255, 180 };
    V_AddDecal(&decal);
}

static void CL_AddEntities(void) {
    FOR_LOOP(index, MAX_CLIENT_ENTITIES) {
        centity_t const *ce = &cl.ents[index];
        if (!ce->current.model)
            continue;
        V_AddClientEntity(ce);
    }
    
    CL_AddTEnts();
    
    CL_AddBuilding();
    CL_AddCursorSplat();

    cl.viewDef.num_entities = view_state.num_entities;
    cl.viewDef.entities = view_state.entities;
    cl.viewDef.num_decals = view_state.num_decals;
    cl.viewDef.decals = view_state.decals;
    CL_DebugEntityView();
}

void CL_PrepRefresh(void) {
    static bool map_registered = false;
    static bool map_load_announced = false;
    static bool loading_complete_displayed = false;
    static DWORD loading_settle_frames = 0;
    static PATHSTR registered_map;
    DWORD total_assets;
    DWORD loaded_assets;

    if (!*cl.configstrings[CS_WORLD]) {
        map_registered = false;
        map_load_announced = false;
        loading_complete_displayed = false;
        loading_settle_frames = 0;
        world_loaded = false;
        begin_sent = false;
        registered_map[0] = '\0';
        CL_LoadingUpdate("Awaiting configstrings", 0.0f);
        return;
    }

    if (strcmp(registered_map, cl.configstrings[CS_WORLD])) {
        map_registered = false;
        map_load_announced = false;
        loading_complete_displayed = false;
        loading_settle_frames = 0;
        world_loaded = false;
        begin_sent = false;
        snprintf(registered_map, sizeof(registered_map), "%s", cl.configstrings[CS_WORLD]);
    }

    total_assets = 1 +
                   CL_CountConfigstrings(CS_MODELS, MAX_MODELS) +
                   CL_CountConfigstrings(CS_IMAGES, MAX_IMAGES) +
                   CL_CountConfigstrings(CS_FONTS, MAX_FONTSTYLES);
    loaded_assets = (map_registered ? 1 : 0) +
                    CL_CountLoadedConfigstrings(CS_MODELS, MAX_MODELS, (HANDLE const *)cl.models) +
                    CL_CountLoadedConfigstrings(CS_IMAGES, MAX_IMAGES, (HANDLE const *)cl.pics) +
                    CL_CountLoadedConfigstrings(CS_FONTS, MAX_FONTSTYLES, (HANDLE const *)cl.fonts);

    if (loaded_assets < total_assets) {
        loading_complete_displayed = false;
        loading_settle_frames = 0;
    }
    
    if (!map_registered) {
        if (!map_load_announced) {
            CL_UpdateAssetLoadingProgress("Loading world", loaded_assets, total_assets);
            map_load_announced = true;
            return;
        }
        if (!CM_IsMapLoaded(cl.configstrings[CS_WORLD])) {
            CM_LoadMap(cl.configstrings[CS_WORLD]);
        }
        re.RegisterMap(cl.configstrings[CS_WORLD]);
        map_registered = true;
        world_loaded = true;
        CL_UpdateAssetLoadingProgress("Loading world", loaded_assets + 1, total_assets);
        return;
    }

#ifdef SC2
    if (world_loaded && cls.state != ca_active) {
        sc2MapCamera_t map_camera;
        viewCamera_t camera = { 0 };

        cls.state = ca_active;
        SC2_MapDefaultCamera(&map_camera);
        camera.origin = map_camera.target;
        camera.viewangles = (VECTOR3){ map_camera.pitch, map_camera.yaw, 0.0f };
        camera.fov = map_camera.fov;
        camera.distance = map_camera.distance;
        camera.znear = map_camera.znear;
        camera.zfar = map_camera.zfar;
        cl.viewDef.camerastate[0] = camera;
        cl.viewDef.camerastate[1] = camera;
        cl.playerstate.origin = (VECTOR2){ camera.origin.x, camera.origin.y };
        cl.playerstate.fov = camera.fov;
        cl.playerstate.distance = camera.distance;
        cl.playerstate.viewangles = camera.viewangles;
        cl.playerstate.viewquat = Quaternion_fromEuler(&camera.viewangles, ROTATE_ZYX);
    }

    if (world_loaded && !begin_sent) {
        CL_LoadingUpdate("Starting game", 1.0f);
        CL_SendBegin();
        begin_sent = true;
        loading_complete_displayed = false;
        loading_settle_frames = 0;
        return;
    }

    if (world_loaded && cl.playerstate.client_ui_state == CLIENT_UI_LOADING) {
        CL_LoadingUpdate("Entering game", 1.0f);
        cl.playerstate.client_ui_state = CLIENT_UI_GAME;
        CL_SetGameplayInput();
    }
#endif
    
    DWORD loaded_models_this_frame = 0;
    for (DWORD i = 1; i < MAX_MODELS; i++) {
        if (!*cl.configstrings[CS_MODELS + i])
            continue;
        if (cl.models[i])
            continue;
        LPCSTR filename = cl.configstrings[CS_MODELS + i];
        PATHSTR portrait = { 0 };
        LPCSTR ext = strstr(filename, ".m");
        if (ext) {
            memcpy(portrait, filename, ext - filename);
            sprintf(portrait + strlen(portrait), "_Portrait%s", ext);
        }
        cl.models[i] = re.LoadModel(filename);
        if (!cl.models[i]) {
            fprintf(stderr,
                    "CL_PrepRefresh: model configstring %u failed to load: %s\n",
                    (unsigned)i,
                    filename);
        }
        if (portrait[0] && FS_FileExists(portrait)) {
            cl.portraits[i] = re.LoadModel(portrait);
        }
        loaded_assets++;
        loaded_models_this_frame++;
        if (loaded_models_this_frame >= CL_MODEL_LOADS_PER_FRAME) {
            CL_UpdateAssetLoadingProgress("Loading models", loaded_assets, total_assets);
            return;
        }
    }
    if (loaded_models_this_frame > 0) {
        CL_UpdateAssetLoadingProgress("Loading models", loaded_assets, total_assets);
        return;
    }
    
    for (DWORD i = 1; i < MAX_IMAGES; i++) {
        if (!*cl.configstrings[CS_IMAGES + i])
            continue;
        if (cl.pics[i])
            continue;
        cl.pics[i] = re.LoadTexture(cl.configstrings[CS_IMAGES + i]);
        CL_UpdateAssetLoadingProgress("Loading textures", loaded_assets + 1, total_assets);
        return;
    }
    
    for (DWORD i = 1; i < MAX_FONTSTYLES; i++) {
        if (!*cl.configstrings[CS_FONTS + i])
            continue;
        if (cl.fonts[i])
            continue;
        LPCSTR fontspec = cl.configstrings[CS_FONTS + i];
        LPCSTR split = strstr(fontspec, ",");
        if (split) {
            PATHSTR filename = { 0 };
            memcpy(filename, fontspec, split - fontspec);
            DWORD fontsize = atoi(split+1);
            cl.fonts[i] = re.LoadFont(filename, fontsize);
        } else {
            cl.fonts[i] = re.LoadFont(cl.configstrings[CS_FONTS + i], 16);
        }
        CL_UpdateAssetLoadingProgress("Loading fonts", loaded_assets + 1, total_assets);
        return;
    }

    /* Quake II sends "begin" after the client finishes precache. */
    if (world_loaded && !begin_sent) {
        CL_LoadingUpdate("Starting game", 1.0f);
        CL_SendBegin();
        begin_sent = true;
        loading_complete_displayed = false;
        loading_settle_frames = 0;
        return;
    }

    if (world_loaded && cls.state == ca_active &&
        cl.playerstate.client_ui_state == CLIENT_UI_LOADING) {
        if (!loading_complete_displayed) {
            CL_LoadingUpdate("Finishing", 1.0f);
            loading_complete_displayed = true;
            return;
        }
        if (loading_settle_frames < 3) {
            loading_settle_frames++;
            return;
        }
        CL_LoadingUpdate("Entering game", 1.0f);
        cl.playerstate.client_ui_state = CLIENT_UI_GAME;
        CL_SetGameplayInput();
    }
}

void V_RenderView(void) {
    BOOL loading_screen = cl.playerstate.client_ui_state == CLIENT_UI_LOADING;

#ifdef DEBUG_PATHFINDING
    extern LPCOLOR32 pathDebug;
    if (pathDebug)
    re.SetPathTexture(pathDebug);
#endif
    
    static DWORD lastTime = 0;
    if (!world_loaded || cls.state != ca_active || loading_screen) {
        VECTOR3 target = { 0, 0, 90 };

        cl.viewDef.viewport = (RECT) { 0, 0, 1, 1 };
        cl.viewDef.scissor = (RECT) { 0, 0, 1, 1 };
        cl.viewDef.time = cl.time;
        cl.viewDef.deltaTime = cl.time - lastTime;
        cl.viewDef.rdflags = RDF_NOWORLDMODEL | RDF_NOFRUSTUMCULL | RDF_NOFOG;
        cl.viewDef.player = cl.playerstate.number;

        V_ClearScene();
        Matrix4_getPreviewCameraMatrix(&target, &cl.viewDef.viewProjectionMatrix);
        Matrix4_getPreviewShadowMatrix(&lightAngles, &target, VIEW_SHADOW_SIZE, &cl.viewDef.lightMatrix);
        cl.viewDef.lightDir = V_LightDirForAngles(&lightAngles);
        Matrix4_identity(&cl.viewDef.textureMatrix);

        re.RenderFrame(&cl.viewDef);
        lastTime = cl.time;
        return;
    }

    cl.viewDef.lerpfrac = (FLOAT)(cl.time - cl.frame.servertime) / FRAMETIME;
    cl.viewDef.lerpfrac = MAX(0.0f, MIN(1.0f, cl.viewDef.lerpfrac));
    cl.viewDef.viewport = (RECT) { 0, 0, 1, 1 };
#if defined(WOW) || defined(SC2)
    cl.viewDef.scissor = (RECT) { 0, 0, 1, 1 };
#else
    cl.viewDef.scissor = (RECT) { 0, 0.22, 1, 0.76 };
#endif
    cl.viewDef.time = cl.time;
    cl.viewDef.deltaTime = cl.time - lastTime;
    cl.viewDef.rdflags = cl.playerstate.rdflags;
    cl.viewDef.player = cl.playerstate.number;
    
    Matrix4_getCameraMatrix(&cl.viewDef.viewProjectionMatrix);
    Matrix4_getWorldShadowMatrix(&lightAngles, VIEW_SHADOW_SIZE, &cl.viewDef.lightMatrix);
    cl.viewDef.lightDir = V_LightDirForAngles(&lightAngles);

    V_ClearScene();
    CL_AddEntities();

    re.RenderFrame(&cl.viewDef);
    
//    re.DrawPic(tex1, 0, 0);
//    re.DrawPic(tex2, 512, 0);

    if (cl.selection.in_progress) {
        re.DrawSelectionRect(&cl.selection.rect, (COLOR32){0,255,0,255});
    }
    
    lastTime = cl.time;
}

void V_AddEntity(renderEntity_t *ent) {
    if (view_state.num_entities >= MAX_CLIENT_ENTITIES) {
        return;
    }
    view_state.entities[view_state.num_entities++] = *ent;
}

void V_AddDecal(renderDecal_t *decal) {
    if (view_state.num_decals >= MAX_RENDER_DECALS) {
        return;
    }
    view_state.decals[view_state.num_decals++] = *decal;
}

void V_Shutdown(void) {
}
