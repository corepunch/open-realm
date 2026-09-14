#include <stdlib.h> // atoi()

#include "client.h"
#include "sound/s_local.h"
#include "tr_public.h"

static struct {
    renderEntity_t entities[MAX_CLIENT_ENTITIES];
    renderDecal_t decals[MAX_RENDER_DECALS];
    renderSplatRect_t splat_rects[MAX_RENDER_SPLAT_RECTS];
    int num_entities;
    int num_decals;
    int num_splat_rects;
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

/* CS_SCENE_FOG is a generic server-authored distance-fog contract. Games may
 * leave the slot empty. Positive styles share the renderer's linear start/end
 * path until a producer proves and exposes additional equations. */
static void V_UpdateSceneFog(viewDef_t *view, BOOL world) {
    static BOOL invalid_logged;
    int style = 0;
    FLOAT start = 0.0f, end = 0.0f, density = 0.0f;
    FLOAT red = 0.0f, green = 0.0f, blue = 0.0f;

    if (!view) return;
    view->fogEnable = false;
    view->fogStart = view->fogEnd = 0.0f;
    view->fogColor = (VECTOR3){0};
    if (!world || !*cl.configstrings[CS_SCENE_FOG]) {
        invalid_logged = false;
        return;
    }
    if (sscanf(cl.configstrings[CS_SCENE_FOG], "%d %f %f %f %f %f %f",
               &style, &start, &end, &density, &red, &green, &blue) != 7) {
        if (!invalid_logged) fprintf(stderr, "CL: invalid scene fog configstring\n");
        invalid_logged = true;
        return;
    }
    invalid_logged = false;
    (void)density;
    if (style <= 0) return;

    view->fogEnable = true;
    view->fogStart = start;
    view->fogEnd = end;
    view->fogColor = (VECTOR3){ red, green, blue };
}

/* Client copies sampling inputs and the day-phase stat. The game renderer
 * evaluates those into viewDef.terrainLight / entityLight; this path must
 * not include a game header or compile-guard the clock slot. */
static void V_UpdateEnvironmentLighting(viewDef_t *view, BOOL world) {
    if (!view) return;
    view->terrainLight = (ENVIRONLIGHT){0};
    view->entityLight = (ENVIRONLIGHT){0};
    V_UpdateSceneFog(view, world);
    if (!world) {
        view->terrainLightModel = NULL;
        view->entityLightModel = NULL;
        view->skyModel = NULL;
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

static void CL_LoadingStage(FLOAT progress) {
    /* CL_PrepRefresh may still be entered after the first active frame.
     * Progress is loading-screen presentation state, so ignore later passes. */
    if (cl.playerstate.client_ui_state != CLIENT_UI_LOADING) return;
    CL_SetLoadingProgress(progress);
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
    VECTOR3 const target = cl.viewDef.target;
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
    MATRIX4 proj, view, inverse;
    size2_t windowSize = re.GetWindowSize();
    viewCamera_t *a = cl.viewDef.camerastate+1;
    viewCamera_t *b = cl.viewDef.camerastate+0;
    VECTOR3 origin = Vector3_lerp(&a->origin, &b->origin, cl.viewDef.lerpfrac);
    if (re.CameraUsesTerrainHeight()) {
        FLOAT az = a->origin.z - re.GetHeightAtPoint(a->origin.x, a->origin.y);
        FLOAT bz = b->origin.z - re.GetHeightAtPoint(b->origin.x, b->origin.y);
        /* Only the authored offsets interpolate; the terrain base follows the current rendered XY. */
        origin.z = re.GetCameraHeightAtPoint(origin.x, origin.y) + LerpNumber(az, bz, cl.viewDef.lerpfrac);
    }
    cl.viewDef.target = origin;
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
    
    Matrix4_perspective(&proj, fov, aspect, znear, zfar);
    Matrix4_fromViewQuat(&origin, &quat, distance, &view);
    Matrix4_inverse(&view, &inverse);
    cl.viewDef.camerastate[0].eye = (VECTOR3){ inverse.v[12], inverse.v[13], inverse.v[14] };
    /* Some game cameras orbit a target and require a world-up basis to keep low shots upright. */
    if (distance > 0.0f && CL_GameCameraUsesWorldUp()) {
        VECTOR3 direction = Vector3_sub(&origin, &cl.viewDef.camerastate[0].eye);
        Matrix4_lookAt(&view, &cl.viewDef.camerastate[0].eye, &direction, &(VECTOR3){0, 0, 1});
    }
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
    {
        DWORD const encoded_color =
            (ent->current.effect_flags & EFX_TEAM_COLOR_MASK) >> EFX_TEAM_COLOR_SHIFT;
        re.team = encoded_color ? encoded_color - 1 : ent->current.player;
    }
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
    if (ent->current.flags & EF_GROUND_CONFORM) re.flags |= RF_GROUND_CONFORM;
    if (ent->current.flags & EF_GROUND_SURFACE) re.flags |= RF_GROUND_SURFACE;
    re.radius = ent->current.radius;
    re.ground_offset = ent->current.ground_offset;
    re.tint_valid = ent->tint_valid;
    re.tint = ent->tint_valid ? ent->tint : COLOR32_WHITE;
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

    CL_ApplyIndicator(&re);
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
        re.indicator = (COLOR32){ 0 };
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
    view_state.num_splat_rects = 0;
    cl.viewDef.num_entities = 0;
    cl.viewDef.num_decals = 0;
    cl.viewDef.num_splat_rects = 0;
}

static BOOL CL_CircleOverlapsSplatRect(LPCENTITYSTATE state, renderSplatRect_t const *rect) {
    FLOAT const x = MAX(rect->mins.x, MIN(rect->maxs.x, state->origin.x));
    FLOAT const y = MAX(rect->mins.y, MIN(rect->maxs.y, state->origin.y));
    FLOAT const dx = x - state->origin.x;
    FLOAT const dy = y - state->origin.y;
    return dx * dx + dy * dy < state->collision * state->collision;
}

static void CL_AddBuildingPlacementGrid(LPCVECTOR3 origin) {
    DWORD const width = cl.cursorEntity->pathing_width;
    DWORD const height = cl.cursorEntity->pathing_height;
    DWORD const preview = cl.cursorEntity->pathing_preview;
    BYTE const prevented = EntityPathingPreviewPrevented(preview);
    BYTE const required = EntityPathingPreviewRequired(preview);
    USHORT const ignore_entity = EntityPathingPreviewIgnore(preview);
    FLOAT const cell_size = 32.0f;
    FLOAT const half_width = width * cell_size * 0.5f;
    FLOAT const half_height = height * cell_size * 0.5f;
    DWORD const first_rect = view_state.num_splat_rects;
    DWORD const remaining = MAX_RENDER_SPLAT_RECTS - first_rect;

    /* Zero preview flags deliberately suppress build-on-target structures until
     * the client receives enough parent-target data to colour them truthfully. */
    if (!width || !height || (!prevented && !required) ||
        height > remaining || width > remaining / height) {
        return;
    }

    FOR_LOOP(x, width) {
        FOR_LOOP(y, height) {
            renderSplatRect_t rect;
            VECTOR2 sample;
            BYTE pathing = 0;
            BOOL blocked;

            rect.mins.x = origin->x - half_width + x * cell_size;
            rect.mins.y = origin->y - half_height + y * cell_size;
            rect.maxs.x = rect.mins.x + cell_size;
            rect.maxs.y = rect.mins.y + cell_size;
            sample = (VECTOR2){
                (rect.mins.x + rect.maxs.x) * 0.5f,
                (rect.mins.y + rect.maxs.y) * 0.5f,
            };
            blocked = !CM_GetPathingFlagsAt(&sample, &pathing) ||
                      (pathing & prevented) != 0 ||
                      (pathing & required) != required;
            rect.color = blocked
                ? (COLOR32){ 255, 0, 0, 166 }
                : (COLOR32){ 0, 255, 0, 166 };
            view_state.splat_rects[view_state.num_splat_rects++] = rect;
        }
    }

    /* Mark only the cells touched by each live collision circle. This mirrors
     * the server's circle-vs-footprint rule without doing entities*cells work
     * for every preview frame on low-end clients. */
    FOR_LOOP(i, cl.num_active) {
        DWORD const number = cl.active_entities[i];
        entityState_t const *state;
        LONG x0, y0, x1, y1;

        if (!number || number >= MAX_CLIENT_ENTITIES ||
            number == ignore_entity) {
            continue;
        }
        state = &cl.ents[number].current;
        if (state->collision <= 0.0f || (state->flags & EF_NOT_SELECTABLE)) {
            continue;
        }
        x0 = (LONG)floorf((state->origin.x - state->collision - (origin->x - half_width)) / cell_size);
        y0 = (LONG)floorf((state->origin.y - state->collision - (origin->y - half_height)) / cell_size);
        x1 = (LONG)floorf((state->origin.x + state->collision - (origin->x - half_width)) / cell_size);
        y1 = (LONG)floorf((state->origin.y + state->collision - (origin->y - half_height)) / cell_size);
        x0 = MAX(0, x0); y0 = MAX(0, y0);
        x1 = MIN((LONG)width - 1, x1); y1 = MIN((LONG)height - 1, y1);
        if (x0 > x1 || y0 > y1) continue;

        for (LONG x = x0; x <= x1; x++) {
            for (LONG y = y0; y <= y1; y++) {
                renderSplatRect_t *rect = &view_state.splat_rects[first_rect + (DWORD)x * height + (DWORD)y];
                if (CL_CircleOverlapsSplatRect(state, rect)) {
                    rect->color = (COLOR32){ 255, 0, 0, 166 };
                }
            }
        }
    }
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
    
    if (!re.TraceLocation(&cl.viewDef, mouse.origin.x, mouse.origin.y, &ent.origin)) {
        return;
    }

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
    ent.tint = MAKE(COLOR32, 255, 255, 255, 255);

    CL_AddBuildingPlacementGrid(&ent.origin);
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
    cl.viewDef.num_splat_rects = view_state.num_splat_rects;
    cl.viewDef.splat_rects = view_state.splat_rects;
}

void CL_PrepRefresh(void) {
    if (!cl.precache_ready || !cl.layout[LAYER_LOADING]) return;
    if (!*cl.configstrings[CS_WORLD]) {
        world_loaded = false;
        begin_sent = false;
        return;
    }

    CL_LoadingStage(0.10f);

    if (!world_loaded) {
        if (!CM_IsMapLoaded(cl.configstrings[CS_WORLD])) {
            CM_LoadMap(cl.configstrings[CS_WORLD], CL_LoadingFrame);
        }
        re.RegisterMap(cl.configstrings[CS_WORLD]);
        world_loaded = true;
    }
    CL_LoadingStage(0.40f);

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
    CL_RegisterConfigString(CS_ORDER_MARKER);
    CL_LoadingStage(0.60f);

    for (DWORD i = 1; i < MAX_IMAGES; i++) {
        if (!*cl.configstrings[CS_IMAGES + i])
            continue;
        CL_RegisterConfigString(CS_IMAGES + i);
    }
    CL_LoadingStage(0.75f);

    if (register_sounds)
        for (DWORD i = 1; i < MAX_SOUNDS; i++)
            if (*cl.configstrings[CS_SOUNDS + i]) S_RegisterSound(cl.configstrings[CS_SOUNDS + i]);
    CL_LoadingStage(0.87f);

    for (DWORD i = 1; i < MAX_FONTSTYLES; i++) {
        if (!*cl.configstrings[CS_FONTS + i])
            continue;
        CL_RegisterConfigString(CS_FONTS + i);
    }
    CL_LoadingStage(0.94f);

    if (world_loaded && !begin_sent) {
        CL_SendBegin();
        begin_sent = true;
    }
    CL_LoadingStage(0.98f);

    if (world_loaded && !cl.refresh_prepped) {
        S_EndRegistration();
        cl.refresh_prepped = true;
    }
    if (cl.refresh_prepped) CL_LoadingStage(1.0f);
}

void V_RenderView(void) {
    static DWORD lastTime = 0;
    BOOL rebuild;
    cl.viewDef.weather_effects = cl.weather_effects;
    cl.viewDef.num_weather_effects = cl.num_weather_effects;
    cl.viewDef.fow_width = cl.fow.width;
    cl.viewDef.fow_height = cl.fow.height;
    cl.viewDef.fow_data = cl.fow.texture;
    cl.viewDef.fow_generation = cl.fow.generation;
    if (!world_loaded || cls.state != ca_active) {
        VECTOR3 target = { 0, 0, 90 };
        DWORD const elapsed = lastTime && cl.time >= lastTime ? cl.time - lastTime : 0;

        cl.viewDef.target = target;
        cl.viewDef.viewport = (RECT) { 0, 0, 1, 1 };
        cl.viewDef.scissor = (RECT) { 0, 0, 1, 1 };
        cl.viewDef.time = cl.time;
        cl.viewDef.deltaTime = elapsed;
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

#ifdef BZ_TESTS
#include "shared/test.h"

static size2_t v_test_window(void) { return (size2_t){ 1024, 768 }; }
static BOOL v_test_terrain(void) { return true; }
static BOOL v_test_absolute(void) { return false; }
static FLOAT v_test_exact(FLOAT x, FLOAT y) { (void)y; return x; }
static FLOAT v_test_blurred(FLOAT x, FLOAT y) { (void)x; (void)y; return 50.0f; }

/* Recover camera Z from the actual projection with a zero-angle, zero-distance test camera. */
static FLOAT v_test_camera_z(void) {
    MATRIX4 inv;
    Matrix4_getCameraMatrix(&cl.viewDef.viewProjectionMatrix);
    Matrix4_inverse(&cl.viewDef.viewProjectionMatrix, &inv);
    return Matrix4_multiply_vector3(&inv, &(VECTOR3){ 0, 0, -1 }).z + 1.0f;
}

/* Sky and particles consume the same interpolated eye used to build the final view. */
TEST(client_camera, rendered_eye_tracks_orbit_distance) {
    viewDef_t saved = cl.viewDef;
    refExport_t api = re;
    BOOL loaded = world_loaded;
    re.GetWindowSize = v_test_window; re.CameraUsesTerrainHeight = v_test_absolute;
    world_loaded = true;
    cl.viewDef = (viewDef_t){ .viewport = { 0, 0, 1, 1 } };
    cl.viewDef.camerastate[1] = (viewCamera_t){
        .origin = { 100, 200, 300 }, .viewangles = { -35, 0, 25 },
        .distance = 1650, .fov = 60, .znear = 1, .zfar = 5000
    };
    cl.viewDef.camerastate[0] = cl.viewDef.camerastate[1];
    cl.viewDef.lerpfrac = 0.5f;

    Matrix4_getCameraMatrix(&cl.viewDef.viewProjectionMatrix);

    T_FEQ(Vector3_distance(&cl.viewDef.target, &cl.viewDef.camerastate[0].eye), 1650.0f, 0.01f);
    cl.viewDef = saved; re = api; world_loaded = loaded;
}

/* No-world/UI views must not retain the previous map's sky handle. */
TEST(client_environment, no_world_clears_sky_model) {
    model_t sentinel = { 0 };
    viewDef_t view = { .skyModel = &sentinel };
    V_UpdateEnvironmentLighting(&view, false);
    T_NULL(view.skyModel);
}

/* Terrain follows current XY, but authored height offsets still interpolate at render frequency. */
TEST(client_camera, terrain_offsets_interpolate) {
    viewDef_t saved = cl.viewDef;
    refExport_t api = re;
    BOOL loaded = world_loaded;
    re.GetWindowSize = v_test_window; re.CameraUsesTerrainHeight = v_test_terrain;
    re.GetHeightAtPoint = v_test_exact; re.GetCameraHeightAtPoint = v_test_blurred;
    world_loaded = true;
    cl.viewDef = (viewDef_t){ .viewport = { 0, 0, 1, 1 } };
    cl.viewDef.camerastate[1] = (viewCamera_t){ .origin = { 0, 0, 20 }, .fov = 60, .znear = 1, .zfar = 1000 };
    cl.viewDef.camerastate[0] = cl.viewDef.camerastate[1];
    cl.viewDef.camerastate[0].origin = (VECTOR3){ 100, 0, 140 };
    FOR_LOOP(i, 3) {
        cl.viewDef.lerpfrac = i * 0.5f;
        T_FEQ(v_test_camera_z(), 70.0f + i * 10.0f, 0.001f);
        T_FEQ(cl.viewDef.target.z, 70.0f + i * 10.0f, 0.001f);
    }
    re.CameraUsesTerrainHeight = v_test_absolute;
    cl.viewDef.lerpfrac = 0.25f;
    T_FEQ(v_test_camera_z(), 50.0f, 0.001f);
    world_loaded = false;
    Matrix4_getCameraMatrix(&cl.viewDef.viewProjectionMatrix);
    MATRIX4 identity;
    Matrix4_identity(&identity);
    T_EQ(memcmp(&cl.viewDef.viewProjectionMatrix, &identity, sizeof(identity)), 0);
    cl.viewDef = saved; re = api; world_loaded = loaded;
}
#endif
