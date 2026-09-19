#define API_PLAYERSTATE(NAME) \
LPCJASSCONTEXT NAME##Context = jass_getcontext(j); \
LPPLAYER NAME = NAME##Context && NAME##Context->unit ? G_GetPlayerByNumber(NAME##Context->unit->s.player) : currentplayer;

extern LPPLAYER currentplayer;

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

static FLOAT G_CameraDegreesToRadians(FLOAT value) { return value * (FLOAT)M_PI / 180.0f; }

static CAMERASETUP G_CameraStateAtTime(LPGAMECLIENT gc, DWORD now) {
    CAMERASETUP current;
    DWORD duration;
    FLOAT k;

    if (!gc) {
        return (CAMERASETUP){ 0 };
    }
    duration = gc->camera.end_time - gc->camera.start_time;
    if (!duration || now >= gc->camera.end_time) {
        return gc->camera.state;
    }
    if (now <= gc->camera.start_time) {
        return gc->camera.old_state;
    }

    k = (now - gc->camera.start_time) / (FLOAT)duration;
    current.position = Vector2_lerp(&gc->camera.old_state.position, &gc->camera.state.position, k);
    current.viewangles = (VECTOR3){
        CL_GameLerpDegrees(gc->camera.old_state.viewangles.x, gc->camera.state.viewangles.x, k),
        CL_GameLerpDegrees(gc->camera.old_state.viewangles.y, gc->camera.state.viewangles.y, k),
        CL_GameLerpDegrees(gc->camera.old_state.viewangles.z, gc->camera.state.viewangles.z, k),
    };
    current.target_distance = LerpNumber(gc->camera.old_state.target_distance, gc->camera.state.target_distance, k);
    current.fov = LerpNumber(gc->camera.old_state.fov, gc->camera.state.fov, k);
    current.z_offset = LerpNumber(gc->camera.old_state.z_offset, gc->camera.state.z_offset, k);
    current.near_z = LerpNumber(gc->camera.old_state.near_z, gc->camera.state.near_z, k);
    current.far_z = LerpNumber(gc->camera.old_state.far_z, gc->camera.state.far_z, k);
    return current;
}

/* Reconstruct the rendered orbit eye from the same target, orientation, and distance sent to the client. */
static VECTOR3 G_CameraEyePositionFromState(LPCVECTOR3 target, LPCVECTOR3 angles, FLOAT distance) {
    QUATERNION quat;
    MATRIX4 view, inverse;
    VECTOR3 eye, origin = Vector3_unm(target);

    quat = Quaternion_fromEuler(angles, ROTATE_ZYX);
    Matrix4_identity(&view);
    Matrix4_translate(&view, &(VECTOR3){ 0, 0, -distance });
    Matrix4_rotateQuat(&view, &quat);
    Matrix4_translate(&view, &origin);
    Matrix4_inverse(&view, &inverse);
    eye = (VECTOR3){ inverse.v[12], inverse.v[13], inverse.v[14] };
    return eye;
}
static VECTOR3 G_CameraEyePosition(LPCPLAYER playerstate) {
    return G_CameraEyePositionFromState(&playerstate->vieworigin, &playerstate->viewangles, playerstate->distance);
}

/* Mirror low authored AoA values above the target while preserving their world-facing orbit. */
static FLOAT G_CameraAuthoredToPitch(FLOAT value) { return value < 90 ? 90 + value : -90 - value; }
static FLOAT G_CameraPitchToAuthored(FLOAT value) {
    return value >= 90 ? value - 90 : value >= 0 ? 90 - value : -90 - value;
}
static FLOAT G_CameraAuthoredToYaw(FLOAT value, FLOAT pitch) { return pitch < 0 ? 450 - value : 270 - value; }
static FLOAT G_CameraYawToAuthored(FLOAT value, FLOAT pitch) { return pitch < 0 ? 450 - value : 270 - value; }
/* Convert the client Euler yaw back to Warcraft's authored rotation field. */
static FLOAT G_CameraRotation(LPCPLAYER p) { return G_CameraYawToAuthored(p->viewangles.z, p->viewangles.x); }

/* Camera setters use Warcraft's authored units. GetCameraField separately
 * preserves the retail runtime-getter contract for angular/FOV radians. */
static FLOAT G_GetCameraStateField(LPCCAMERASETUP camera, CAMERAFIELD field) {
    if (!camera) {
        return 0.0f;
    }
    switch (field) {
        case CAMERA_FIELD_TARGET_DISTANCE: return camera->target_distance;
        case CAMERA_FIELD_FARZ: return camera->far_z;
        case CAMERA_FIELD_NEARZ: return camera->near_z;
        case CAMERA_FIELD_ANGLE_OF_ATTACK: return G_CameraPitchToAuthored(camera->viewangles.x);
        case CAMERA_FIELD_FIELD_OF_VIEW: return G_CameraVerticalToHorizontalFov(camera->fov);
        case CAMERA_FIELD_ROLL: return camera->viewangles.y;
        case CAMERA_FIELD_ROTATION: return G_CameraYawToAuthored(camera->viewangles.z, camera->viewangles.x);
        case CAMERA_FIELD_ZOFFSET: return camera->z_offset;
        case CAMERA_FIELD_LOCAL_PITCH:
        case CAMERA_FIELD_LOCAL_YAW:
        case CAMERA_FIELD_LOCAL_ROLL:
            return 0.0f;
    }
    return 0.0f;
}

static BOOL G_SetCameraStateField(LPCAMERASETUP camera, CAMERAFIELD field, FLOAT value) {
    if (!camera) {
        return false;
    }
    switch (field) {
        case CAMERA_FIELD_TARGET_DISTANCE: camera->target_distance = value; break;
        case CAMERA_FIELD_FARZ: camera->far_z = value; break;
        case CAMERA_FIELD_NEARZ: camera->near_z = value; break;
        case CAMERA_FIELD_ANGLE_OF_ATTACK: {
            FLOAT rotation = G_CameraYawToAuthored(camera->viewangles.z, camera->viewangles.x);
            camera->viewangles.x = G_CameraAuthoredToPitch(value);
            camera->viewangles.z = G_CameraAuthoredToYaw(rotation, camera->viewangles.x);
            break;
        }
        case CAMERA_FIELD_FIELD_OF_VIEW: camera->fov = G_CameraHorizontalToVerticalFov(value); break;
        case CAMERA_FIELD_ROLL: camera->viewangles.y = value; break;
        case CAMERA_FIELD_ROTATION: camera->viewangles.z = G_CameraAuthoredToYaw(value, camera->viewangles.x); break;
        case CAMERA_FIELD_ZOFFSET: camera->z_offset = value; break;
        case CAMERA_FIELD_LOCAL_PITCH:
        case CAMERA_FIELD_LOCAL_YAW:
        case CAMERA_FIELD_LOCAL_ROLL:
            return false;
    }
    return true;
}

static void G_SetCameraFieldForCurrentPlayer(LPCSTR func, CAMERAFIELD field, FLOAT value, FLOAT duration) {
    LPGAMECLIENT gc = G_CurrentCameraClient(func);
    CAMERASETUP current, target;
    DWORD now;

    if (!gc) {
        return;
    }
    if (G_SkipCutscene()) {
        duration = 0.0f;
    }
    now = G_Time();
    current = G_CameraStateAtTime(gc, now);
    target = current;
    if (!G_SetCameraStateField(&target, field, value)) {
        return;
    }
    gc->camera.old_state = current;
    gc->camera.state = target;
    gc->camera.start_time = now;
    gc->camera.end_time = now + (DWORD)(MAX(0.0f, duration) * 1000.0f);
}

/* Recover the authored target offset from the terrain-composed runtime target height. */
static FLOAT G_CameraZOffset(LPCPLAYER p) {
    LPGAMECLIENT gc = G_CurrentCameraClient("G_CameraZOffset");
    return gc ? p->vieworigin.z - gc->camera.target_height
              : p->vieworigin.z - CM_GetHeightAtPoint(p->vieworigin.x, p->vieworigin.y) - CM_GetCameraHeightOffset();
}

#ifdef WC3_DEBUG_CAMERA_TRACE
/* Emit opt-in camera samples for retail/OpenRealm comparisons without changing map JASS. */
void G_CameraTraceSnapshotForClient(LPGAMECLIENT gc, LPCSTR label) {
    LPCPLAYER p;
    LPCAMERASETUP s;
    LPCVECTOR3 ang;
    FLOAT dist, fov, roll, zoff, farz;
    FLOAT terrain, sample_height, realized_base, composed_z;
    FLOAT k = 0.0f;
    VECTOR3 eye, realized_target;
    static DWORD sample;
    LPCSTR enabled = gi.CvarString("camera_trace", "0");

    if (!enabled || !*enabled || !strcmp(enabled, "0")) return;
    if (!gc) return;
    p = &gc->ps;
    s = &gc->camera.state;
    terrain = CM_GetHeightAtPoint(p->vieworigin.x, p->vieworigin.y);
    sample_height = terrain;
    if (gc->camera.end_time > G_Time() && G_Time() != gc->camera.start_time) {
        k = (G_Time() - gc->camera.start_time) /
            (FLOAT)(gc->camera.end_time - gc->camera.start_time);
        /* Retail keeps the realized eye on the old setup while logical fields expose the new setup. */
        realized_target = p->vieworigin;
        eye = G_CameraEyePositionFromState(&realized_target, &gc->camera.old_state.viewangles,
                                           gc->camera.old_state.target_distance);
        ang = &p->viewangles;
        dist = p->distance;
        fov = p->fov;
        roll = p->viewangles.y;
        /* Report the logical interpolated offset; deriving it from vieworigin
         * uses the stale target height while a transition is in progress. */
        zoff = LerpNumber(gc->camera.old_state.z_offset, gc->camera.state.z_offset, k);
        farz = p->zfar;
    } else {
        if (gc->camera.end_time > gc->camera.start_time && G_Time() < gc->camera.end_time)
            s = &gc->camera.old_state;
        ang = &s->viewangles;
        dist = s->target_distance;
        fov = s->fov;
        roll = s->viewangles.y;
        zoff = s->z_offset;
        farz = s->far_z;
        realized_target = p->vieworigin;
        eye = G_CameraEyePositionFromState(&realized_target, &p->viewangles, p->distance);
    }
    realized_base = p->vieworigin.z - zoff;
    composed_z = gc->camera.target_height + zoff;
    /* Retail exposes newly applied setup fields immediately, while its eye and
     * target getters continue to report the realized camera until the next
     * client update. Keep both halves of that contract in the trace. */
    fprintf(stderr,
            "CAMTRACE n=%u t=%.3f label=%s tx=%.3f ty=%.3f tz=%.3f ex=%.3f ey=%.3f ez=%.3f dist=%.3f aoa=%.3f rot=%.3f fov=%.3f roll=%.3f zoff=%.3f farz=%.3f terrain=%.3f sampleheight=%.3f targetbase=%.3f realizedbase=%.3f composedz=%.3f setupx=%.3f setupy=%.3f k=%.3f\n",
            (unsigned)++sample, G_Time() / 1000.0f, label ? label : "camera-event",
            p->vieworigin.x, p->vieworigin.y, p->vieworigin.z, eye.x, eye.y, eye.z,
            dist, G_CameraDegreesToRadians(G_CameraPitchToAuthored(ang->x)),
            G_CameraDegreesToRadians(G_CameraYawToAuthored(ang->z, ang->x)),
            G_CameraDegreesToRadians(G_CameraVerticalToHorizontalFov(fov)),
            G_CameraDegreesToRadians(roll), zoff, farz, terrain, sample_height, gc->camera.target_height,
            realized_base, composed_z, gc->camera.state.position.x, gc->camera.state.position.y, k);
}

void G_CameraTraceSnapshot(LPCSTR label) {
    G_CameraTraceSnapshotForClient(G_CurrentCameraClient("G_CameraTraceSnapshot"), label);
}
#endif

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
    gc->camera.target_height = gc->ps.vieworigin.z;
    gc->camera.state.position = position;
    if (set_z) {
        gc->camera.state.z_offset = z_offset;
    }
    gc->camera.start_time = G_Time();
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
        gc->camera.start_time = G_Time();
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
    G_SetCameraBounds(bounds);
    return 0;
}
DWORD StopCamera(LPJASS j) {
    LPGAMECLIENT gc = G_CurrentCameraClient("StopCamera");
    DWORD now;

    (void)j;
    if (!gc) {
        return 0;
    }
    now = G_Time();
    gc->camera.state = G_CameraStateAtTime(gc, now);
    gc->camera.old_state = gc->camera.state;
    gc->camera.start_time = gc->camera.end_time = now;
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
    {
        gameCamera_t cam;
        CL_GameDefaultCamera(&cam);
        gc->camera.state.viewangles = (VECTOR3){ cam.pitch, 0, cam.yaw };
        gc->camera.state.fov = cam.fov;
        gc->camera.state.target_distance = cam.distance;
        gc->camera.state.z_offset = 0.0f;
        gc->camera.state.near_z = cam.znear;
        gc->camera.state.far_z = cam.zfar;
    }
    gc->camera.start_time = G_Time();
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
    CAMERAFIELD *whichField = jass_checkhandle(j, 1, "camerafield");
    FLOAT value = jass_checknumber(j, 2);
    FLOAT duration = jass_checknumber(j, 3);

    if (whichField) {
        G_SetCameraFieldForCurrentPlayer("SetCameraField", *whichField, value, duration);
    }
    return 0;
}
DWORD AdjustCameraField(LPJASS j) {
    CAMERAFIELD *whichField = jass_checkhandle(j, 1, "camerafield");
    FLOAT offset = jass_checknumber(j, 2);
    FLOAT duration = jass_checknumber(j, 3);
    LPGAMECLIENT gc = G_CurrentCameraClient("AdjustCameraField");

    if (gc && whichField) {
        CAMERASETUP current = G_CameraStateAtTime(gc, G_Time());
        G_SetCameraFieldForCurrentPlayer("AdjustCameraField", *whichField,
                                         G_GetCameraStateField(&current, *whichField) + offset, duration);
    }
    return 0;
}
DWORD CreateCameraSetup(LPJASS j) {
    API_ALLOC(CAMERASETUP, camerasetup);
    {
        gameCamera_t cam;
        CL_GameDefaultCamera(&cam);
        camerasetup->viewangles = (VECTOR3){ cam.pitch, 0, cam.yaw };
        camerasetup->fov = cam.fov;
        camerasetup->target_distance = cam.distance;
        camerasetup->near_z = cam.znear;
        camerasetup->far_z = cam.zfar;
    }
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
        case CAMERA_FIELD_ANGLE_OF_ATTACK: {
            FLOAT rotation = G_CameraYawToAuthored(whichSetup->viewangles.z, whichSetup->viewangles.x);
            whichSetup->viewangles.x = G_CameraAuthoredToPitch(value);
            whichSetup->viewangles.z = G_CameraAuthoredToYaw(rotation, whichSetup->viewangles.x);
            break;
        }
        case CAMERA_FIELD_FIELD_OF_VIEW: whichSetup->fov = G_CameraHorizontalToVerticalFov(value); break;
        case CAMERA_FIELD_ROLL: whichSetup->viewangles.y = value; break;
        case CAMERA_FIELD_ROTATION: whichSetup->viewangles.z = G_CameraAuthoredToYaw(value, whichSetup->viewangles.x); break;
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
        case CAMERA_FIELD_ANGLE_OF_ATTACK: value = G_CameraPitchToAuthored(whichSetup->viewangles.x); break;
        case CAMERA_FIELD_FIELD_OF_VIEW: value = G_CameraVerticalToHorizontalFov(whichSetup->fov); break;
        case CAMERA_FIELD_ROLL: value = whichSetup->viewangles.y; break;
        case CAMERA_FIELD_ROTATION: value = G_CameraYawToAuthored(whichSetup->viewangles.z, whichSetup->viewangles.x); break;
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
    if (apply_position && (setup->position.x != gc->camera.old_state.position.x ||
                           setup->position.y != gc->camera.old_state.position.y)) {
        gc->camera.target_height = CM_GetHeightAtPoint(setup->position.x, setup->position.y);
    }
    gc->camera.state = *setup;
    if (!apply_position) {
        gc->camera.state.position = gc->camera.old_state.position;
    }
    /* Retail applies the setup's authored Z offset for ordinary CameraSetupApply*;
     * only the WithZ variants replace it with their explicit argument. */
    if (override_z) {
        gc->camera.state.z_offset = z_offset;
    }
    gc->camera.state.position = G_ClampCameraPosition(gc, &gc->camera.state.position);
    gc->camera.start_time = G_Time();
    gc->camera.end_time = gc->camera.start_time + duration_ms;
}
DWORD CameraSetupApply(LPJASS j) {
    LPCAMERASETUP whichSetup = jass_checkhandle(j, 1, "camerasetup");
    BOOL doPan = jass_checkboolean(j, 2);
    (void)jass_checkboolean(j, 3); /* panTimed: untimed camera rates are not retained yet */
    G_ApplyCameraSetup(whichSetup, doPan, false, 0.0f, 0);
#ifdef WC3_DEBUG_CAMERA_TRACE
    G_CameraTraceSnapshot("CameraSetupApply");
#endif
    return 0;
}
DWORD CameraSetupApplyWithZ(LPJASS j) {
    LPCAMERASETUP whichSetup = jass_checkhandle(j, 1, "camerasetup");
    FLOAT zDestOffset = jass_checknumber(j, 2);
    G_ApplyCameraSetup(whichSetup, true, true, zDestOffset, 0);
#ifdef WC3_DEBUG_CAMERA_TRACE
    G_CameraTraceSnapshot("CameraSetupApplyWithZ");
#endif
    return 0;
}
DWORD CameraSetupApplyForceDuration(LPJASS j) {
    LPCAMERASETUP whichSetup = jass_checkhandle(j, 1, "camerasetup");
    BOOL doPan = jass_checkboolean(j, 2);
    FLOAT forceDuration = jass_checknumber(j, 3);
    G_ApplyCameraSetup(whichSetup, doPan, false, 0.0f, forceDuration * 1000);
#ifdef WC3_DEBUG_CAMERA_TRACE
    G_CameraTraceSnapshot("CameraSetupApplyForceDuration");
#endif
    return 0;
}
DWORD CameraSetupApplyForceDurationWithZ(LPJASS j) {
    LPCAMERASETUP whichSetup = jass_checkhandle(j, 1, "camerasetup");
    FLOAT zDestOffset = jass_checknumber(j, 2);
    FLOAT forceDuration = jass_checknumber(j, 3);
    G_ApplyCameraSetup(whichSetup, true, true, zDestOffset, forceDuration * 1000);
#ifdef WC3_DEBUG_CAMERA_TRACE
    G_CameraTraceSnapshot("CameraSetupApplyForceDurationWithZ");
#endif
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
    return jass_pushnumber(j, level.camera_bounds.min.x);
}
DWORD GetCameraBoundMinY(LPJASS j) {
    return jass_pushnumber(j, level.camera_bounds.min.y);
}
DWORD GetCameraBoundMaxX(LPJASS j) {
    return jass_pushnumber(j, level.camera_bounds.max.x);
}
DWORD GetCameraBoundMaxY(LPJASS j) {
    return jass_pushnumber(j, level.camera_bounds.max.y);
}
DWORD GetCameraField(LPJASS j) {
    HANDLE whichField = jass_checkhandle(j, 1, "camerafield");
    API_PLAYERSTATE(playerstate);
    FLOAT value = 0;
    if (playerstate && whichField) switch (*(CAMERAFIELD *)whichField) {
        case CAMERA_FIELD_TARGET_DISTANCE: value = playerstate->distance; break;
        case CAMERA_FIELD_FARZ: value = playerstate->zfar; break;
        case CAMERA_FIELD_NEARZ: value = playerstate->znear; break;
        /* Warcraft's field getters expose angles and FOV in radians; runtime state stores degrees. */
        case CAMERA_FIELD_ANGLE_OF_ATTACK:
            value = G_CameraDegreesToRadians(G_CameraPitchToAuthored(playerstate->viewangles.x)); break;
        case CAMERA_FIELD_FIELD_OF_VIEW:
            value = G_CameraDegreesToRadians(G_CameraVerticalToHorizontalFov(playerstate->fov)); break;
        case CAMERA_FIELD_ROLL: value = G_CameraDegreesToRadians(playerstate->viewangles.y); break;
        case CAMERA_FIELD_ROTATION: value = G_CameraDegreesToRadians(G_CameraRotation(playerstate)); break;
        case CAMERA_FIELD_ZOFFSET: value = G_CameraZOffset(playerstate); break;
        case CAMERA_FIELD_LOCAL_PITCH:
        case CAMERA_FIELD_LOCAL_YAW:
        case CAMERA_FIELD_LOCAL_ROLL:
            break;
    }
    return jass_pushnumber(j, value);
}
DWORD GetCameraTargetPositionX(LPJASS j) {
    API_PLAYERSTATE(playerstate);
    return jass_pushnumber(j, playerstate ? playerstate->vieworigin.x : 0);
}
DWORD GetCameraTargetPositionY(LPJASS j) {
    API_PLAYERSTATE(playerstate);
    return jass_pushnumber(j, playerstate ? playerstate->vieworigin.y : 0);
}
DWORD GetCameraTargetPositionZ(LPJASS j) {
    API_PLAYERSTATE(playerstate);
    return jass_pushnumber(j, playerstate ? playerstate->vieworigin.z : 0);
}

DWORD GetCameraTargetPositionLoc(LPJASS j) {
    API_ALLOC(VECTOR2, location);
    API_PLAYERSTATE(playerstate);
    if (playerstate) {
        *location = (VECTOR2){ playerstate->vieworigin.x, playerstate->vieworigin.y };
    }
    return 1;
}
DWORD GetCameraEyePositionX(LPJASS j) {
    API_PLAYERSTATE(playerstate);
    VECTOR3 eye = playerstate ? G_CameraEyePosition(playerstate) : (VECTOR3){ 0 };
    return jass_pushnumber(j, eye.x);
}
DWORD GetCameraEyePositionY(LPJASS j) {
    API_PLAYERSTATE(playerstate);
    VECTOR3 eye = playerstate ? G_CameraEyePosition(playerstate) : (VECTOR3){ 0 };
    return jass_pushnumber(j, eye.y);
}
DWORD GetCameraEyePositionZ(LPJASS j) {
    API_PLAYERSTATE(playerstate);
    VECTOR3 eye = playerstate ? G_CameraEyePosition(playerstate) : (VECTOR3){ 0 };
    return jass_pushnumber(j, eye.z);
}
DWORD GetCameraEyePositionLoc(LPJASS j) {
    API_ALLOC(VECTOR2, location);
    API_PLAYERSTATE(playerstate);
    if (playerstate) {
        VECTOR3 eye = G_CameraEyePosition(playerstate);
        *location = (VECTOR2){ eye.x, eye.y };
    }
    return 1;
}
