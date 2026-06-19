#define API_PLAYERSTATE(NAME) \
LPCJASSCONTEXT NAME##Context = jass_getcontext(j); \
LPPLAYER NAME = NAME##Context && NAME##Context->unit ? G_GetPlayerByNumber(NAME##Context->unit->s.player) : currentplayer;

extern LPPLAYER currentplayer;

static LPGAMECLIENT G_CurrentCameraClient(LPCSTR func) {
    if (!currentplayer) {
        fprintf(stderr,
                "%s skipped: no currentplayer time=%u\n",
                func,
                (unsigned)gi.GetTime());
        return NULL;
    }
    return G_GetPlayerClientByNumber(PLAYER_NUM(currentplayer));
}

static void G_SetCameraPositionForCurrentPlayer(LPCSTR func, FLOAT x, FLOAT y, FLOAT duration) {
    LPGAMECLIENT gc = G_CurrentCameraClient(func);
    if (!gc) {
        return;
    }
    if (G_SkipCutscene()) {
        duration = 0;
    }
    G_ClearCameraTarget(gc, func);
    gc->camera.old_state = gc->camera.state;
    gc->camera.state.position.x = x;
    gc->camera.state.position.y = y;
    gc->camera.start_time = gi.GetTime();
    gc->camera.end_time = gc->camera.start_time + duration * 1000;
    fprintf(stderr,
            "%s: player=%u pos=(%.1f,%.1f) duration=%.3f start=%u end=%u\n",
            func,
            (unsigned)PLAYER_NUM(currentplayer),
            x,
            y,
            duration,
            (unsigned)gc->camera.start_time,
            (unsigned)gc->camera.end_time);
}

DWORD SetCameraTargetController(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    FLOAT xoffset = jass_checknumber(j, 2);
    FLOAT yoffset = jass_checknumber(j, 3);
    (void)jass_checkboolean(j, 4);
    LPGAMECLIENT gc = G_CurrentCameraClient("SetCameraTargetController");
    if (!gc) {
        return 0;
    }
    gc->camera.target_controller = whichUnit;
    gc->camera.target_offset = (VECTOR2){ xoffset, yoffset };
    if (whichUnit) {
        gc->camera.old_state = gc->camera.state;
        gc->camera.state.position.x = whichUnit->s.origin2.x + xoffset;
        gc->camera.state.position.y = whichUnit->s.origin2.y + yoffset;
        gc->camera.start_time = gi.GetTime();
        gc->camera.end_time = gc->camera.start_time;
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
    G_SetCameraPositionForCurrentPlayer("SetCameraPosition", x, y, 0);
    return 0;
}
DWORD SetCameraQuickPosition(LPJASS j) {
    FLOAT x = jass_checknumber(j, 1);
    FLOAT y = jass_checknumber(j, 2);
    G_SetCameraPositionForCurrentPlayer("SetCameraQuickPosition", x, y, 0);
    return 0;
}
DWORD SetCameraBounds(LPJASS j) {
    (void)jass_checknumber(j, 1);
    (void)jass_checknumber(j, 2);
    (void)jass_checknumber(j, 3);
    (void)jass_checknumber(j, 4);
    (void)jass_checknumber(j, 5);
    (void)jass_checknumber(j, 6);
    (void)jass_checknumber(j, 7);
    (void)jass_checknumber(j, 8);
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
    gc->camera.start_time = gi.GetTime();
    gc->camera.end_time = gc->camera.start_time + (duration * 1000);
    return 0;
}
DWORD PanCameraTo(LPJASS j) {
    FLOAT x = jass_checknumber(j, 1);
    FLOAT y = jass_checknumber(j, 2);
    G_SetCameraPositionForCurrentPlayer("PanCameraTo", x, y, 0);
    return 0;
}
DWORD PanCameraToTimed(LPJASS j) {
    FLOAT x = jass_checknumber(j, 1);
    FLOAT y = jass_checknumber(j, 2);
    FLOAT duration = jass_checknumber(j, 3);
    G_SetCameraPositionForCurrentPlayer("PanCameraToTimed", x, y, duration);
    return 0;
}
DWORD PanCameraToWithZ(LPJASS j) {
    FLOAT x = jass_checknumber(j, 1);
    FLOAT y = jass_checknumber(j, 2);
    //FLOAT zOffsetDest = jass_checknumber(j, 3);
    G_SetCameraPositionForCurrentPlayer("PanCameraToWithZ", x, y, 0);
    return 0;
}
DWORD PanCameraToTimedWithZ(LPJASS j) {
    FLOAT x = jass_checknumber(j, 1);
    FLOAT y = jass_checknumber(j, 2);
    //FLOAT zOffsetDest = jass_checknumber(j, 3);
    FLOAT duration = jass_checknumber(j, 4);
    G_SetCameraPositionForCurrentPlayer("PanCameraToTimedWithZ", x, y, duration);
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
    (void)camerasetup;
    return 1;
}
DWORD CameraSetupSetField(LPJASS j) {
    LPCAMERASETUP whichSetup = jass_checkhandle(j, 1, "camerasetup");
    CAMERAFIELD *whichField = jass_checkhandle(j, 2, "camerafield");
    FLOAT value = jass_checknumber(j, 3);
    switch (*whichField) {
        case CAMERA_FIELD_TARGET_DISTANCE: whichSetup->target_distance = value; break;
        case CAMERA_FIELD_FARZ: whichSetup->far_z = value; break;
        case CAMERA_FIELD_ANGLE_OF_ATTACK: whichSetup->viewangles.x = -90 - value; break;
        case CAMERA_FIELD_FIELD_OF_VIEW: whichSetup->fov = value; break;
        case CAMERA_FIELD_ROLL: whichSetup->viewangles.y = value; break;
        case CAMERA_FIELD_ROTATION: whichSetup->viewangles.z = 90 - value; break;
        case CAMERA_FIELD_ZOFFSET: whichSetup->z_offset = value; break;
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
        case CAMERA_FIELD_ANGLE_OF_ATTACK: value = -90 - whichSetup->viewangles.x; break;
        case CAMERA_FIELD_FIELD_OF_VIEW: value = whichSetup->fov; break;
        case CAMERA_FIELD_ROLL: value = whichSetup->viewangles.y; break;
        case CAMERA_FIELD_ROTATION: value = 90 - whichSetup->viewangles.z; break;
        case CAMERA_FIELD_ZOFFSET: value = whichSetup->z_offset; break;
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
/* Ghidra: CameraSetupApply (FUN_0040b0d0), ...WithZ, ...ForceDuration
 * (FUN_0040b190) and ...ForceDurationWithZ all funnel into one worker
 * (FUN_00336020) that snaps the player camera to the setup, optionally panning
 * over a duration.  ForceDuration supplies an explicit pan time; the plain
 * Apply uses the setup's own (per-field) duration.  Our CAMERASETUP does not
 * carry that intrinsic duration, so the non-force variants apply instantly
 * (correct final framing; only the in-between pan timing is dropped — and the
 * campaign path uses ForceDuration via CameraSetupApplyForPlayer anyway).  The
 * WithZ variants ignore the extra Z offset, matching PanCameraToWithZ here. */
static void G_ApplyCameraSetup(LPCAMERASETUP setup, FLOAT duration_ms) {
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
    gc->camera.start_time = gi.GetTime();
    gc->camera.end_time = gc->camera.start_time + duration_ms;
}
DWORD CameraSetupApply(LPJASS j) {
    LPCAMERASETUP whichSetup = jass_checkhandle(j, 1, "camerasetup");
    //BOOL doPan = jass_checkboolean(j, 2);
    //BOOL panTimed = jass_checkboolean(j, 3);
    G_ApplyCameraSetup(whichSetup, 0);
    return 0;
}
DWORD CameraSetupApplyWithZ(LPJASS j) {
    LPCAMERASETUP whichSetup = jass_checkhandle(j, 1, "camerasetup");
    //FLOAT zDestOffset = jass_checknumber(j, 2);
    G_ApplyCameraSetup(whichSetup, 0);
    return 0;
}
DWORD CameraSetupApplyForceDuration(LPJASS j) {
    LPCAMERASETUP whichSetup = jass_checkhandle(j, 1, "camerasetup");
    BOOL doPan = jass_checkboolean(j, 2);
    FLOAT forceDuration = jass_checknumber(j, 3);
    G_ApplyCameraSetup(whichSetup, doPan ? forceDuration * 1000 : 0);
    return 0;
}
DWORD CameraSetupApplyForceDurationWithZ(LPJASS j) {
    LPCAMERASETUP whichSetup = jass_checkhandle(j, 1, "camerasetup");
    //FLOAT zDestOffset = jass_checknumber(j, 2);
    FLOAT forceDuration = jass_checknumber(j, 3);
    G_ApplyCameraSetup(whichSetup, forceDuration * 1000);
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
DWORD GetCameraMargin(LPJASS j) {
    LONG whichMargin = jass_checkinteger(j, 1);
    mapCameraBounds_t const *bounds = &level.mapinfo->cameraBounds;
    switch (whichMargin) {
        case 0: jass_pushnumber(j, bounds->margin.left * TILE_SIZE); break;
        case 1: jass_pushnumber(j, bounds->margin.right * TILE_SIZE); break;
        case 2: jass_pushnumber(j, bounds->margin.top * TILE_SIZE); break;
        case 3: jass_pushnumber(j, bounds->margin.bottom * TILE_SIZE); break;
        default: jass_pushnull(j);
    }
    return 1;
}
DWORD GetCameraBoundMinX(LPJASS j) {
    return jass_pushnumber(j, 0);
}
DWORD GetCameraBoundMinY(LPJASS j) {
    return jass_pushnumber(j, 0);
}
DWORD GetCameraBoundMaxX(LPJASS j) {
    return jass_pushnumber(j, 0);
}
DWORD GetCameraBoundMaxY(LPJASS j) {
    return jass_pushnumber(j, 0);
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
