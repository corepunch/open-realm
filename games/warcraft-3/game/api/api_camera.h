#define API_PLAYERSTATE(NAME) \
LPCJASSCONTEXT NAME##Context = jass_getcontext(j); \
LPPLAYER NAME = NAME##Context && NAME##Context->unit ? G_GetPlayerByNumber(NAME##Context->unit->s.player) : currentplayer;

extern LPPLAYER currentplayer;

#define WC3_CAMERA_DEFAULT_NEAR_Z 100.0f /* world units; default near clipping plane */
#define WC3_CAMERA_DEFAULT_FAR_Z 5000.0f /* world units; default far clipping plane */
#define WC3_CAMERA_ASPECT 1.66f /* Warcraft camera horizontal/vertical FOV conversion aspect */

static LPGAMECLIENT G_CurrentCameraClient(LPCSTR func) {
    (void)func;
    if (!currentplayer) {
        return NULL;
    }
    return G_GetPlayerClientByNumber(PLAYER_NUM(currentplayer));
}

static FLOAT G_CameraHorizontalToVerticalFov(FLOAT horizontal) {
    FLOAT const hfov_rad = horizontal * (FLOAT)M_PI / 180.0f;
    return 2.0f * atanf(tanf(hfov_rad / 2.0f) / WC3_CAMERA_ASPECT) * 180.0f / (FLOAT)M_PI;
}

static FLOAT G_CameraVerticalToHorizontalFov(FLOAT vertical) {
    FLOAT const vfov_rad = vertical * (FLOAT)M_PI / 180.0f;
    return 2.0f * atanf(tanf(vfov_rad / 2.0f) * WC3_CAMERA_ASPECT) * 180.0f / (FLOAT)M_PI;
}

static void G_SetCameraPositionForCurrentPlayer(LPCSTR func, FLOAT x, FLOAT y,
                                                 BOOL set_z, FLOAT z_offset,
                                                 FLOAT duration) {
    LPGAMECLIENT gc = G_CurrentCameraClient(func);
    VECTOR2 position = { x, y };

    if (!gc) {
        return;
    }
    if (G_SkipCutscene()) {
        duration = 0;
    }
    position = G_ClampCameraPosition(gc, &position);
    G_ClearCameraTarget(gc, func);
    gc->camera.old_state = gc->camera.state;
    gc->camera.state.position = position;
    if (set_z) {
        gc->camera.state.z_offset = z_offset;
    }
    gc->camera.start_time = gi.GetTime();
    gc->camera.end_time = gc->camera.start_time + duration * 1000;
}

DWORD SetCameraTargetController(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    FLOAT xoffset = jass_checknumber(j, 2);
    FLOAT yoffset = jass_checknumber(j, 3);
    BOOL inheritOrientation = jass_checkboolean(j, 4);
    LPGAMECLIENT gc = G_CurrentCameraClient("SetCameraTargetController");
    if (!gc) {
        return 0;
    }
    gc->camera.target_controller = whichUnit;
    gc->camera.target_offset = (VECTOR2){ xoffset, yoffset };
    gc->camera.target_inherit_orientation = inheritOrientation;
    if (whichUnit) {
        VECTOR2 position = { whichUnit->s.origin2.x + xoffset, whichUnit->s.origin2.y + yoffset };
        gc->camera.old_state = gc->camera.state;
        gc->camera.state.position = G_ClampCameraPosition(gc, &position);
        if (inheritOrientation) {
            gc->camera.old_state.viewangles.z = 90.0f - (FLOAT)RAD2DEG(whichUnit->s.angle);
            gc->camera.state.viewangles.z = 90.0f - (FLOAT)RAD2DEG(whichUnit->s.angle);
        }
        gc->camera.start_time = gi.GetTime();
        gc->camera.end_time = gc->camera.start_time;
    } else {
        gc->camera.target_offset = (VECTOR2){ 0, 0 };
        gc->camera.target_inherit_orientation = false;
    }
    return 0;
}
DWORD SetCameraOrientController(LPJASS j) {
    //LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    //FLOAT xoffset = jass_checknumber(j, 2);
    //FLOAT yoffset = jass_checknumber(j, 3);
    return 0;
}
DWORD SetCameraPosition(LPJASS j) {
    FLOAT x = jass_checknumber(j, 1);
    FLOAT y = jass_checknumber(j, 2);
    G_SetCameraPositionForCurrentPlayer("SetCameraPosition", x, y, false, 0.0f, 0);
    return 0;
}
DWORD SetCameraQuickPosition(LPJASS j) {
    FLOAT x = jass_checknumber(j, 1);
    FLOAT y = jass_checknumber(j, 2);
    LPGAMECLIENT gc = G_CurrentCameraClient("SetCameraQuickPosition");
    if (!gc) {
        return 0;
    }
    /* Warcraft's quick position is the spacebar recall point. It must not
     * mutate the current camera target when the script assigns it. */
    gc->camera.quick_position = MAKE(VECTOR2, x, y);
    gc->camera.quick_position_set = true;
    return 0;
}
DWORD SetCameraBounds(LPJASS j) {
    FLOAT bounds[8];

    FOR_LOOP(i, 8) {
        bounds[i] = jass_checknumber(j, i + 1);
    }
    if (currentplayer) {
        G_SetClientCameraBounds(PLAYER_CLIENT(currentplayer), bounds);
    } else {
        FOR_LOOP(i, game.max_clients) {
            G_SetClientCameraBounds(game.clients + i, bounds);
        }
    }
    return 0;
}
DWORD StopCamera(LPJASS j) {
    return 0;
}
DWORD ResetToGameCamera(LPJASS j) {
    FLOAT duration = jass_checknumber(j, 1);
    LPGAMECLIENT gc = G_CurrentCameraClient("ResetToGameCamera");
    if (!gc) {
        return 0;
    }
    if (G_SkipCutscene()) {
        duration = 0;
    }
    G_ClearCameraTarget(gc, "ResetToGameCamera");
    gc->camera.old_state = gc->camera.state;
    gc->camera.state.viewangles = (VECTOR3) { 326, 0, 0 };
    gc->camera.state.fov = 50;
    gc->camera.state.target_distance = 1650;
    gc->camera.state.z_offset = 0.0f;
    gc->camera.state.near_z = WC3_CAMERA_DEFAULT_NEAR_Z;
    gc->camera.state.far_z = WC3_CAMERA_DEFAULT_FAR_Z;
    gc->camera.start_time = gi.GetTime();
    gc->camera.end_time = gc->camera.start_time + (duration * 1000);
    return 0;
}
DWORD PanCameraTo(LPJASS j) {
    FLOAT x = jass_checknumber(j, 1);
    FLOAT y = jass_checknumber(j, 2);
    G_SetCameraPositionForCurrentPlayer("PanCameraTo", x, y, false, 0.0f, 0);
    return 0;
}
DWORD PanCameraToTimed(LPJASS j) {
    FLOAT x = jass_checknumber(j, 1);
    FLOAT y = jass_checknumber(j, 2);
    FLOAT duration = jass_checknumber(j, 3);
    G_SetCameraPositionForCurrentPlayer("PanCameraToTimed", x, y, false, 0.0f, duration);
    return 0;
}
DWORD PanCameraToWithZ(LPJASS j) {
    FLOAT x = jass_checknumber(j, 1);
    FLOAT y = jass_checknumber(j, 2);
    FLOAT zOffsetDest = jass_checknumber(j, 3);
    G_SetCameraPositionForCurrentPlayer("PanCameraToWithZ", x, y, true, zOffsetDest, 0);
    return 0;
}
DWORD PanCameraToTimedWithZ(LPJASS j) {
    FLOAT x = jass_checknumber(j, 1);
    FLOAT y = jass_checknumber(j, 2);
    FLOAT zOffsetDest = jass_checknumber(j, 3);
    FLOAT duration = jass_checknumber(j, 4);
    G_SetCameraPositionForCurrentPlayer("PanCameraToTimedWithZ", x, y, true, zOffsetDest, duration);
    return 0;
}
DWORD SetCinematicCamera(LPJASS j) {
    //LPCSTR cameraModelFile = jass_checkstring(j, 1);
    return 0;
}
DWORD SetCameraField(LPJASS j) {
    //HANDLE whichField = jass_checkhandle(j, 1, "camerafield");
    //FLOAT value = jass_checknumber(j, 2);
    //FLOAT duration = jass_checknumber(j, 3);
    return 0;
}
DWORD AdjustCameraField(LPJASS j) {
    //HANDLE whichField = jass_checkhandle(j, 1, "camerafield");
    //FLOAT offset = jass_checknumber(j, 2);
    //FLOAT duration = jass_checknumber(j, 3);
    return 0;
}
DWORD CreateCameraSetup(LPJASS j) {
    API_ALLOC(CAMERASETUP, camerasetup);
    camerasetup->near_z = WC3_CAMERA_DEFAULT_NEAR_Z;
    return 1;
}
DWORD CameraSetupSetField(LPJASS j) {
    LPCAMERASETUP whichSetup = jass_checkhandle(j, 1, "camerasetup");
    CAMERAFIELD *whichField = jass_checkhandle(j, 2, "camerafield");
    FLOAT value = jass_checknumber(j, 3);
    switch (*whichField) {
        case CAMERA_FIELD_TARGET_DISTANCE: whichSetup->target_distance = value; break;
        case CAMERA_FIELD_FARZ: whichSetup->far_z = value; break;
        case CAMERA_FIELD_NEARZ: whichSetup->near_z = value; break;
        case CAMERA_FIELD_ANGLE_OF_ATTACK: whichSetup->viewangles.x = -90 - value; break;
        case CAMERA_FIELD_FIELD_OF_VIEW: whichSetup->fov = G_CameraHorizontalToVerticalFov(value); break;
        case CAMERA_FIELD_ROLL: whichSetup->viewangles.y = value; break;
        case CAMERA_FIELD_ROTATION: whichSetup->viewangles.z = 90 - value; break;
        case CAMERA_FIELD_ZOFFSET: whichSetup->z_offset = value; break;
        case CAMERA_FIELD_LOCAL_PITCH:
        case CAMERA_FIELD_LOCAL_YAW:
        case CAMERA_FIELD_LOCAL_ROLL:
            break;
    }
//    FLOAT duration = jass_checknumber(j, 4);
    return 0;
}
DWORD CameraSetupGetField(LPJASS j) {
    LPCAMERASETUP whichSetup = jass_checkhandle(j, 1, "camerasetup");
    DWORD *whichField = jass_checkhandle(j, 2, "camerafield");
    FLOAT value = 0;
    switch (*whichField) {
        case CAMERA_FIELD_TARGET_DISTANCE: value = whichSetup->target_distance; break;
        case CAMERA_FIELD_FARZ: value = whichSetup->far_z; break;
        case CAMERA_FIELD_NEARZ: value = whichSetup->near_z; break;
        case CAMERA_FIELD_ANGLE_OF_ATTACK: value = -90 - whichSetup->viewangles.x; break;
        case CAMERA_FIELD_FIELD_OF_VIEW: value = G_CameraVerticalToHorizontalFov(whichSetup->fov); break;
        case CAMERA_FIELD_ROLL: value = whichSetup->viewangles.y; break;
        case CAMERA_FIELD_ROTATION: value = 90 - whichSetup->viewangles.z; break;
        case CAMERA_FIELD_ZOFFSET: value = whichSetup->z_offset; break;
        case CAMERA_FIELD_LOCAL_PITCH:
        case CAMERA_FIELD_LOCAL_YAW:
        case CAMERA_FIELD_LOCAL_ROLL:
            break;
    }
    return jass_pushnumber(j, value);
}
DWORD CameraSetupSetDestPosition(LPJASS j) {
    LPCAMERASETUP whichSetup = jass_checkhandle(j, 1, "camerasetup");
    FLOAT x = jass_checknumber(j, 2);
    FLOAT y = jass_checknumber(j, 3);
//    FLOAT duration = jass_checknumber(j, 4);
    whichSetup->position.x = x;
    whichSetup->position.y = y;
    return 0;
}
DWORD CameraSetupGetDestPositionLoc(LPJASS j) {
    LPCAMERASETUP whichSetup = jass_checkhandle(j, 1, "camerasetup");
    return jass_pushlighthandle(j, &whichSetup->position, "location");
}
DWORD CameraSetupGetDestPositionX(LPJASS j) {
    LPCAMERASETUP whichSetup = jass_checkhandle(j, 1, "camerasetup");
    return jass_pushnumber(j, whichSetup->position.x);
}
DWORD CameraSetupGetDestPositionY(LPJASS j) {
    LPCAMERASETUP whichSetup = jass_checkhandle(j, 1, "camerasetup");
    return jass_pushnumber(j, whichSetup->position.y);
}
/* CameraSetup Apply variants share one state transition. The plain Apply
 * still has no retained per-field duration, but WithZ must override the setup's
 * authored Z offset exactly like Warsmash's setTargetZOffset path. */
static void G_ApplyCameraSetup(LPCAMERASETUP setup, BOOL apply_position,
                               BOOL override_z, FLOAT z_offset, FLOAT duration_ms) {
    LPGAMECLIENT gc = G_CurrentCameraClient("CameraSetupApply");
    if (!gc || !setup) {
        return;
    }
    if (G_SkipCutscene()) {
        duration_ms = 0;
    }
    G_ClearCameraTarget(gc, "CameraSetupApply");
    gc->camera.old_state = gc->camera.state;
    gc->camera.state = *setup;
    if (!apply_position) {
        gc->camera.state.position = gc->camera.old_state.position;
    }
    if (override_z) {
        gc->camera.state.z_offset = z_offset;
    }
    gc->camera.state.position = G_ClampCameraPosition(gc, &gc->camera.state.position);
    gc->camera.start_time = gi.GetTime();
    gc->camera.end_time = gc->camera.start_time + duration_ms;
}
DWORD CameraSetupApply(LPJASS j) {
    LPCAMERASETUP whichSetup = jass_checkhandle(j, 1, "camerasetup");
    BOOL doPan = jass_checkboolean(j, 2);
    (void)jass_checkboolean(j, 3); /* panTimed: untimed camera rates are not retained yet */
    G_ApplyCameraSetup(whichSetup, doPan, false, 0.0f, 0);
    return 0;
}
DWORD CameraSetupApplyWithZ(LPJASS j) {
    LPCAMERASETUP whichSetup = jass_checkhandle(j, 1, "camerasetup");
    FLOAT zDestOffset = jass_checknumber(j, 2);
    G_ApplyCameraSetup(whichSetup, true, true, zDestOffset, 0);
    return 0;
}
DWORD CameraSetupApplyForceDuration(LPJASS j) {
    LPCAMERASETUP whichSetup = jass_checkhandle(j, 1, "camerasetup");
    BOOL doPan = jass_checkboolean(j, 2);
    FLOAT forceDuration = jass_checknumber(j, 3);
    G_ApplyCameraSetup(whichSetup, doPan, false, 0.0f, forceDuration * 1000);
    return 0;
}
DWORD CameraSetupApplyForceDurationWithZ(LPJASS j) {
    LPCAMERASETUP whichSetup = jass_checkhandle(j, 1, "camerasetup");
    FLOAT zDestOffset = jass_checknumber(j, 2);
    FLOAT forceDuration = jass_checknumber(j, 3);
    G_ApplyCameraSetup(whichSetup, true, true, zDestOffset, forceDuration * 1000);
    return 0;
}
DWORD CameraSetTargetNoise(LPJASS j) {
    //FLOAT mag = jass_checknumber(j, 1);
    //FLOAT velocity = jass_checknumber(j, 2);
    return 0;
}
DWORD CameraSetSourceNoise(LPJASS j) {
    //FLOAT mag = jass_checknumber(j, 1);
    //FLOAT velocity = jass_checknumber(j, 2);
    return 0;
}
DWORD CameraSetSmoothingFactor(LPJASS j) {
    //FLOAT factor = jass_checknumber(j, 1);
    return 0;
}
static BOX2 G_DefaultCameraBounds(void) {
    FLOAT const *bounds = level.mapinfo->cameraBounds.bounds;

    return MAKE(BOX2,
        .min = {
            MIN(MIN(bounds[0], bounds[2]), MIN(bounds[4], bounds[6])),
            MIN(MIN(bounds[1], bounds[3]), MIN(bounds[5], bounds[7])),
        },
        .max = {
            MAX(MAX(bounds[0], bounds[2]), MAX(bounds[4], bounds[6])),
            MAX(MAX(bounds[1], bounds[3]), MAX(bounds[5], bounds[7])),
        });
}

static BOX2 G_PlayableMapBounds(void) {
    mapCameraBounds_t const *camera = &level.mapinfo->cameraBounds;
    BOX2 playable = CM_GetWorldBounds();

    /* W3I complements describe the terrain cells outside the playable map.
     * They are not the values returned by the JASS GetCameraMargin native. */
    playable.min.x += camera->complement.left * TILE_SIZE;
    playable.max.x -= camera->complement.right * TILE_SIZE;
    playable.min.y += camera->complement.bottom * TILE_SIZE;
    playable.max.y -= camera->complement.top * TILE_SIZE;
    return playable;
}

DWORD GetCameraMargin(LPJASS j) {
    LONG whichMargin = jass_checkinteger(j, 1);
    BOX2 const camera = G_DefaultCameraBounds();
    BOX2 const playable = G_PlayableMapBounds();

    switch (whichMargin) {
        case 0: jass_pushnumber(j, camera.min.x - playable.min.x); break;
        case 1: jass_pushnumber(j, playable.max.x - camera.max.x); break;
        case 2: jass_pushnumber(j, playable.max.y - camera.max.y); break;
        case 3: jass_pushnumber(j, camera.min.y - playable.min.y); break;
        default: jass_pushnull(j);
    }
    return 1;
}
DWORD GetCameraBoundMinX(LPJASS j) {
    LPGAMECLIENT gc = currentplayer ? PLAYER_CLIENT(currentplayer) : game.clients;
    return jass_pushnumber(j, gc ? gc->ps.camera_bounds.min.x : 0);
}
DWORD GetCameraBoundMinY(LPJASS j) {
    LPGAMECLIENT gc = currentplayer ? PLAYER_CLIENT(currentplayer) : game.clients;
    return jass_pushnumber(j, gc ? gc->ps.camera_bounds.min.y : 0);
}
DWORD GetCameraBoundMaxX(LPJASS j) {
    LPGAMECLIENT gc = currentplayer ? PLAYER_CLIENT(currentplayer) : game.clients;
    return jass_pushnumber(j, gc ? gc->ps.camera_bounds.max.x : 0);
}
DWORD GetCameraBoundMaxY(LPJASS j) {
    LPGAMECLIENT gc = currentplayer ? PLAYER_CLIENT(currentplayer) : game.clients;
    return jass_pushnumber(j, gc ? gc->ps.camera_bounds.max.y : 0);
}
DWORD GetCameraField(LPJASS j) {
    //HANDLE whichField = jass_checkhandle(j, 1, "camerafield");
    return jass_pushnumber(j, 0);
}
DWORD GetCameraTargetPositionX(LPJASS j) {
    API_PLAYERSTATE(playerstate);
    return jass_pushnumber(j, playerstate ? playerstate->origin.x : 0);
}
DWORD GetCameraTargetPositionY(LPJASS j) {
    API_PLAYERSTATE(playerstate);
    return jass_pushnumber(j, playerstate ? playerstate->origin.y : 0);
}
DWORD GetCameraTargetPositionZ(LPJASS j) {
    return jass_pushnumber(j, 0);
}

DWORD GetCameraTargetPositionLoc(LPJASS j) {
    API_ALLOC(VECTOR2, location);
    API_PLAYERSTATE(playerstate);
    if (playerstate) {
        *location = playerstate->origin;
    }
    return 1;
}
DWORD GetCameraEyePositionX(LPJASS j) {
    return jass_pushnumber(j, 0);
}
DWORD GetCameraEyePositionY(LPJASS j) {
    return jass_pushnumber(j, 0);
}
DWORD GetCameraEyePositionZ(LPJASS j) {
    return jass_pushnumber(j, 0);
}
DWORD GetCameraEyePositionLoc(LPJASS j) {
    return jass_pushnullhandle(j, "location");
}
