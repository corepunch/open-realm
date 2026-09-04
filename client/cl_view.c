#include <stdlib.h> // atoi()

#include "client.h"
#include "sound/s_local.h"
#include "tr_public.h"

static struct {
    renderEntity_t entities[MAX_CLIENT_ENTITIES];
    renderDecal_t decals[MAX_RENDER_DECALS];
    int num_entities;
    int num_decals;
} view_state;

static bool world_loaded = false;
static bool begin_sent = false;

/* Optional CS_MODELS indices become handles here. Games that do not publish
 * CS_TERRAIN_LIGHT_MODEL / CS_ENTITY_LIGHT_MODEL leave the slots empty. */
static LPCMODEL V_ConfigLightModel(DWORD configstring) {
    LPCSTR value;
    char *end = NULL;
    unsigned long index;

    if (configstring >= MAX_CONFIGSTRINGS) return NULL;
    value = cl.configstrings[configstring];
    if (!value || !*value) return NULL;
    index = strtoul(value, &end, 10);
    if (end == value || !end || *end || index == 0 || index >= MAX_MODELS) return NULL;
    return cl.models[index];
}

static LPCMODEL V_ConfigSkyModel(void) {
    char *end = NULL;
    unsigned long index = strtoul(cl.configstrings[CS_SKY], &end, 10);
    if (!*cl.configstrings[CS_SKY] || end == cl.configstrings[CS_SKY] || *end || index == 0 || index >= MAX_MODELS)
        return NULL;
    return cl.models[index];
}

/* Client copies sampling inputs and the day-phase stat. The game renderer
 * evaluates those into viewDef.terrainLight / entityLight; this path must
 * not include a game header or compile-guard the clock slot. */
static void V_UpdateEnvironmentLighting(viewDef_t *view, BOOL world) {
    if (!view) return;
    view->terrainLight = (ENVIRONLIGHT){0};
    view->entityLight = (ENVIRONLIGHT){0};
    if (!world) {
        view->terrainLightModel = NULL;
        view->entityLightModel = NULL;
        view->environmentPhase = 0.0f;
        return;
    }
    view->terrainLightModel = V_ConfigLightModel(CS_TERRAIN_LIGHT_MODEL);
    view->entityLightModel = V_ConfigLightModel(CS_ENTITY_LIGHT_MODEL);
    view->skyModel = V_ConfigSkyModel();
    view->environmentPhase =
        (FLOAT)cl.playerstate.stats[UI_PLAYERSTAT_ENV_PHASE] / (FLOAT)USHRT_MAX;
}

VECTOR3 lightAngles = {-40,0,60};

/* A reconnect receives a fresh configstring table; reset only the refresh
 * lifecycle flags so CL_PrepRefresh performs one registration pass. */
void CL_RestartRefresh(void) {
    world_loaded = false;
    begin_sent = false;
    cl.refresh_prepped = false;
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
    QUATERNION qa = Quaternion_fromEuler(&a->viewangles, ROTATE_ZYX);
    QUATERNION qb = Quaternion_fromEuler(&b->viewangles, ROTATE_ZYX);
    QUATERNION quat = Quaternion_slerp(&qa, &qb, cl.viewDef.lerpfrac);
    FLOAT distance = LerpNumber(a->distance, b->distance, cl.viewDef.lerpfrac);
    FLOAT fov = LerpNumber(a->fov, b->fov, cl.viewDef.lerpfrac);
    FLOAT viewport_width = cl.viewDef.viewport.w * windowSize.width;
    FLOAT viewport_height = cl.viewDef.viewport.h * windowSize.height;
    FLOAT aspect = viewport_height > 0.0f
        ? viewport_width / viewport_height
        : (FLOAT)windowSize.width / (FLOAT)windowSize.height;
    FLOAT znear = LerpNumber(a->znear, b->znear, cl.viewDef.lerpfrac);
    FLOAT zfar = LerpNumber(a->zfar, b->zfar, cl.viewDef.lerpfrac);
    
#ifdef WOW
    /* Look-at Z comes from the player entity, not the camera sample. */
    origin.z = LerpNumber(cl.ents[0].prev.origin.z, cl.ents[0].current.origin.z, cl.viewDef.lerpfrac) + WOW_CAMERA_EYE_HEIGHT;
#endif
    Matrix4_perspective(&proj, fov, aspect, znear, zfar);
    Matrix4_fromViewQuat(&origin, &quat, distance, &view);
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
    /* model is a BYTE and MAX_MODELS is 256, so it is always a valid index;
       the old `>= MAX_MODELS` guard was a constant-false comparison. */
    re.origin = Vector3_lerp(&ent->prev.origin, &ent->current.origin, cl.viewDef.lerpfrac);
    re.angle = LerpRotation(ent->prev.angle, ent->current.angle, cl.viewDef.lerpfrac);
#ifdef WOW
    re.rotation = Vector3_lerp(&ent->prev.rotation, &ent->current.rotation, cl.viewDef.lerpfrac);
#endif
    re.scale = LerpNumber(ent->prev.scale, ent->current.scale, cl.viewDef.lerpfrac);
    re.frame = ent->current.frame;
    re.oldframe = ent->prev.frame;
    re.health = ent->current.stats[ENT_HEALTH];
    re.effect_flags = ent->current.effect_flags;
    re.effect_model = cl.models[ent->current.effect];
    re.model = cl.models[ent->current.model];
    re.skin = cl.pics[ent->current.image];
    if (ent->current.name) {
        DWORD i = ent->current.name - 1;
        LPCSTR cs = cl.configstrings[CS_GENERAL + (i >> 4)];
        re.name = cs ? cs + (i & 0xF) * ENT_NAME_SLOT_SIZE : NULL;
    }
    re.team = ent->current.player;
#ifdef WOW
    /* WoW reuses the existing snapshot class ID for the DBC creature display ID. */
    re.display_id = ent->current.class_id;
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
    if (ent->current.flags & EF_MOUNTED) re.flags |= RF_MOUNTED;
    if (ent->current.flags & EF_HAS_QUEST) re.flags |= RF_HAS_QUEST;
    if (ent->current.flags & EF_QUEST_COMPLETE) re.flags |= RF_QUEST_COMPLETE;
    if (ent->current.flags & EF_HOSTILE) re.flags |= RF_HOSTILE;
    if (ent->current.flags & EF_NEUTRAL) re.flags |= RF_NEUTRAL;
    if (ent->current.flags & EF_NOT_SELECTABLE) re.flags |= RF_NOT_SELECTABLE;
    if (ent->current.flags & EF_BUILDING) re.flags |= RF_BUILDING;
    re.radius = ent->current.radius;
    re.number = ent->current.number;
    re.splat = cl.pics[ent->current.splat & 0xffff];
    re.splatsize = ent->current.splat >> 16;
#ifndef USE_SHADOWMAPS
    re.shadow = cl.pics[ent->current.shadow];
    re.shadow_rect = MAKE(RECT,
                          ShadowUnpackRectComponent((BYTE)(ent->current.shadow_rect & 0xff)),
                          ShadowUnpackRectComponent((BYTE)((ent->current.shadow_rect >> 8) & 0xff)),
                          ShadowUnpackRectComponent((BYTE)((ent->current.shadow_rect >> 16) & 0xff)),
                          ShadowUnpackRectComponent((BYTE)((ent->current.shadow_rect >> 24) & 0xff)));
#endif
#ifdef WOW
    /* model2 is a BYTE, so every nonzero value is a valid MAX_MODELS index. */
    if (ent->current.model2 > 0 && (ent->current.renderfx & RF_ATTACH_OVERHEAD))
        re.overhead_model = cl.models[ent->current.model2];
    else if (ent->current.model2 > 0)
        re.attached_model = cl.models[ent->current.model2];
#endif

    view_state.entities[view_state.num_entities++] = re;

    if (ent->current.model2 > 0) {
#ifdef WOW
        if (re.attached_model || re.overhead_model) return;
#endif
        if (view_state.num_entities >= MAX_CLIENT_ENTITIES) {
            return;
        }
        /* model2 is a BYTE and MAX_MODELS is 256, so it is always a valid index. */
        re.model = cl.models[ent->current.model2];
        re.skin = 0;
        re.frame = 0;
        re.oldframe = 0;
        re.scale = 1;
        re.name = NULL;
        re.number = 0;
        re.health = 0;
        re.flags &= ~RF_BUILDING;
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
    if (!cl.cursorEntity->model)  /* 0 = no model registered (BYTE; always < MAX_MODELS) */
        return;

    renderEntity_t ent;
    memset(&ent, 0, sizeof(renderEntity_t));
    
    re.TraceLocation(&cl.viewDef, mouse.origin.x, mouse.origin.y, &ent.origin);

    if (cl.cursorEntity->pathing_width && cl.cursorEntity->pathing_height) {
        DWORD const path_width = cl.cursorEntity->pathing_width;
        DWORD const path_height = cl.cursorEntity->pathing_height;
        ent.origin.x = floorf(ent.origin.x / 64.0f) * 64.0f;
        ent.origin.y = floorf(ent.origin.y / 64.0f) * 64.0f;
        if (((path_width / 2) & 1) != 0) ent.origin.x += 32.0f;
        if (((path_height / 2) & 1) != 0) ent.origin.y += 32.0f;
    } else {
        ent.origin.x = floorf(ent.origin.x / 32.0f) * 32.0f;
        ent.origin.y = floorf(ent.origin.y / 32.0f) * 32.0f;
    }
    ent.origin.z = CM_GetHeightAtPoint(ent.origin.x, ent.origin.y);
    ent.scale = cl.cursorEntity->scale;
    ent.angle = cl.cursorEntity->angle;
    ent.team = cl.cursorEntity->player;
    ent.frame = cl.cursorEntity->frame;
    ent.oldframe = cl.cursorEntity->frame;
    ent.model = cl.models[cl.cursorEntity->model];
    
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
    FOR_LOOP(i, cl.num_active) {
        V_AddClientEntity(&cl.ents[cl.active_entities[i]]);
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

    BOOL register_sounds = !cl.refresh_prepped;
    if (register_sounds) S_BeginRegistration();

#ifdef SC2
    if (world_loaded && cls.state != ca_active) {
        viewCamera_t camera = { 0 };
        gameCamera_t defaults;

        CL_GameDefaultCamera(&defaults);
        camera.origin = defaults.target;
        camera.viewangles = (VECTOR3){ defaults.pitch, 0.0f, defaults.yaw };
        camera.fov = defaults.fov;
        camera.distance = defaults.distance;
        camera.znear = defaults.znear;
        camera.zfar = defaults.zfar;
        cl.viewDef.camerastate[0] = camera;
        cl.viewDef.camerastate[1] = camera;
        cl.playerstate.vieworigin = camera.origin;
        cl.playerstate.distance = camera.distance;
        cl.playerstate.viewangles = camera.viewangles;
        player_set_lens(&cl.playerstate, &defaults);
    }
#endif

    for (DWORD i = 1; i < MAX_MODELS; i++) {
        if (!*cl.configstrings[CS_MODELS + i])
            continue;
        CL_RegisterConfigString(CS_MODELS + i);
    }

    for (DWORD i = 1; i < MAX_IMAGES; i++) {
        if (!*cl.configstrings[CS_IMAGES + i])
            continue;
        CL_RegisterConfigString(CS_IMAGES + i);
    }

    if (register_sounds)
        for (DWORD i = 1; i < MAX_SOUNDS; i++)
            if (*cl.configstrings[CS_SOUNDS + i]) S_RegisterSound(cl.configstrings[CS_SOUNDS + i]);

    for (DWORD i = 1; i < MAX_FONTSTYLES; i++) {
        if (!*cl.configstrings[CS_FONTS + i])
            continue;
        CL_RegisterConfigString(CS_FONTS + i);
    }

    if (world_loaded && !begin_sent) {
        CL_SendBegin();
        begin_sent = true;
    }

    if (world_loaded && !cl.refresh_prepped) {
        S_EndRegistration();
        cl.refresh_prepped = true;
    }
}

void V_RenderView(void) {
    static DWORD lastTime = 0;
    BOOL rebuild;
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
        V_UpdateEnvironmentLighting(&cl.viewDef, false);

        re.RenderFrame(&cl.viewDef);
        lastTime = cl.time;
        return;
    }

    rebuild = V_AdvanceSceneTime(&cl.viewDef, cl.time, &lastTime, Cvar_Integer("paused", 0));
    if (rebuild) {
        cl.viewDef.lerpfrac = (FLOAT)(cl.time - cl.frame.servertime) / FRAMETIME;
        cl.viewDef.lerpfrac = MAX(0.0f, MIN(1.0f, cl.viewDef.lerpfrac));
#if defined(WOW) || defined(SC2)
        cl.viewDef.viewport = (RECT) { 0, 0, 1, 1 };
        cl.viewDef.scissor = cl.viewDef.viewport;
#else
        /* Warcraft III's 3D world occupies the area above the command console.
         * Use that rectangle as the real projection viewport rather than drawing a
         * full-window camera and merely clipping it afterwards. */
        cl.viewDef.viewport = (RECT) { 0, 0.22, 1, 0.76 };
        cl.viewDef.scissor = cl.viewDef.viewport;
#endif
        cl.viewDef.rdflags = cl.playerstate.rdflags;
        cl.viewDef.player = cl.playerstate.number;
        cl.viewDef.hover_entity = cl.hover_entity;
    
#if !defined(WOW) && !defined(SC2)
        {
            float yaw_rad = (float)DEG2RAD(cl.playerstate.viewangles.z);
            VECTOR2 listener_origin = { cl.playerstate.vieworigin.x, cl.playerstate.vieworigin.y };
            VECTOR2 listener_right = { cosf(yaw_rad), sinf(yaw_rad) };
            S_SetListener(&listener_origin, &listener_right);
        }
#endif
        Matrix4_getCameraMatrix(&cl.viewDef.viewProjectionMatrix);
        Matrix4_getLightMatrix(&lightAngles, VIEW_SHADOW_SIZE, &cl.viewDef.lightMatrix);

        V_ClearScene();
        CL_AddEntities();
    }

    V_UpdateEnvironmentLighting(&cl.viewDef, true);
    re.RenderFrame(&cl.viewDef);
    CL_DrawTEnts();
    
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

BOOL V_FindEntity(DWORD number, renderEntity_t *out) {
    if (!number || !out) return false;
    FOR_LOOP(i, view_state.num_entities) {
        if (view_state.entities[i].number != number) continue;
        *out = view_state.entities[i];
        return true;
    }
    return false;
}

void V_AddDecal(renderDecal_t *decal) {
    if (view_state.num_decals >= MAX_RENDER_DECALS) {
        return;
    }
    view_state.decals[view_state.num_decals++] = *decal;
}

void V_Shutdown(void) {
}
