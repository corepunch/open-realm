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

VECTOR3 lightAngles = {-40,0,60};

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

static void Matrix4_fromViewAngles(LPCVECTOR3 target, LPCVECTOR3 angles, FLOAT distance, LPMATRIX4 output) {
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

static void Matrix4_getLightMatrix(LPCVECTOR3 sunangles, FLOAT scale, LPMATRIX4 output) {
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

static void Matrix4_getPreviewLightMatrix(LPCVECTOR3 sunangles, LPCVECTOR3 target, float scale, LPMATRIX4 output) {
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
    re.health = ent->current.stats[ENT_HEALTH];
    re.mana   = ent->current.stats[ENT_MANA];
    re.splat = cl.pics[ent->current.splat & 0xffff];
    re.splatsize = ent->current.splat >> 16;
    re.shadow = cl.pics[ent->current.shadow];
    re.shadow_rect = MAKE(RECT,
                          ShadowUnpackRectComponent((BYTE)(ent->current.shadow_rect & 0xff)),
                          ShadowUnpackRectComponent((BYTE)((ent->current.shadow_rect >> 8) & 0xff)),
                          ShadowUnpackRectComponent((BYTE)((ent->current.shadow_rect >> 16) & 0xff)),
                          ShadowUnpackRectComponent((BYTE)((ent->current.shadow_rect >> 24) & 0xff)));
    if (!Cvar_Integer("r_unit_shadows", 1)) {
        re.flags |= RF_NO_SHADOW;
    }
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
}

void CL_PrepRefresh(void) {
    if (!*cl.configstrings[CS_WORLD]) {
        world_loaded = false;
        begin_sent = false;
        return;
    }

    if (!world_loaded) {
        if (!CM_IsMapLoaded(cl.configstrings[CS_WORLD])) {
            CM_LoadMap(cl.configstrings[CS_WORLD]);
        }
        re.RegisterMap(cl.configstrings[CS_WORLD]);
        world_loaded = true;
    }

#ifdef SC2
    if (world_loaded && cls.state != ca_active) {
        sc2MapCamera_t map_camera;
        viewCamera_t camera = { 0 };

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
#endif

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
    }

    for (DWORD i = 1; i < MAX_IMAGES; i++) {
        if (!*cl.configstrings[CS_IMAGES + i])
            continue;
        if (cl.pics[i])
            continue;
        cl.pics[i] = re.LoadTexture(cl.configstrings[CS_IMAGES + i]);
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
    }

    if (world_loaded && !begin_sent) {
        CL_SendBegin();
        begin_sent = true;
    }

    if (world_loaded && !cl.refresh_prepped) {
        cl.refresh_prepped = true;
    }
}

void V_RenderView(void) {
#ifdef DEBUG_PATHFINDING
    extern LPCOLOR32 pathDebug;
    if (pathDebug)
    re.SetPathTexture(pathDebug);
#endif
    
    static DWORD lastTime = 0;
    if (!world_loaded || cls.state != ca_active) {
        VECTOR3 target = { 0, 0, 90 };

        cl.viewDef.viewport = (RECT) { 0, 0, 1, 1 };
        cl.viewDef.scissor = (RECT) { 0, 0, 1, 1 };
        cl.viewDef.time = cl.time;
        cl.viewDef.deltaTime = cl.time - lastTime;
        cl.viewDef.rdflags = RDF_NOWORLDMODEL | RDF_NOFRUSTUMCULL | RDF_NOFOG;
    cl.viewDef.player = cl.playerstate.number;
    cl.viewDef.hover_entity = cl.hover_entity;

        V_ClearScene();
        Matrix4_getPreviewCameraMatrix(&target, &cl.viewDef.viewProjectionMatrix);
        Matrix4_getPreviewLightMatrix(&lightAngles, &target, VIEW_SHADOW_SIZE, &cl.viewDef.lightMatrix);
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
    if (CL_AltModifierDown()) {
        cl.viewDef.rdflags |= RDF_SHOW_ALL_HEALTHBARS; /* ALT: bars on every unit */
    }
    cl.viewDef.player = cl.playerstate.number;
    
    Matrix4_getCameraMatrix(&cl.viewDef.viewProjectionMatrix);
    Matrix4_getLightMatrix(&lightAngles, VIEW_SHADOW_SIZE, &cl.viewDef.lightMatrix);

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
