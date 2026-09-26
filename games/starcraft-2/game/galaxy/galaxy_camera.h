/* galaxy_camera.h — camera natives */

#define MAX_GALAXY_CAMS   64
typedef struct { float tx, ty, tz, pitch, yaw, dist, fov, height; } sc2GCam_t;
static sc2GCam_t sc2_gcams[MAX_GALAXY_CAMS];
static int32_t sc2_gcam_n = 1;    /* 1-based; 0 = null handle */
static sc2GCam_t sc2_gcam_current;  /* last applied camera state, for Save/Pan */

/* CameraInfoFromId: look up map camera by ID, store in local table, return handle. */
static uint32_t sc2_CameraInfoFromId(LPJASS j) {
    uint32_t map_id = (uint32_t)jass_checkinteger(j, 1);
    float tx = 0, ty = 0, tz = 0, pitch = 56.0f, yaw = 180.0f, dist = 34.0f, fov = 28.0f, height = 0;
    if (sc2_galaxy_get_camera_by_id &&
        sc2_gcam_n < MAX_GALAXY_CAMS &&
        sc2_galaxy_get_camera_by_id(map_id, &tx, &ty, &tz, &pitch, &yaw, &dist, &fov, &height)) {
        int32_t h = sc2_gcam_n++;
        sc2_gcams[h] = (sc2GCam_t){ tx, ty, tz, pitch, yaw, dist, fov, height };
        return jass_pushlighthandle(j, (handle_t)(uintptr_t)h, "camerainfo");
    }
    return jass_pushnullhandle(j, "camerainfo");
}

static uint32_t sc2_CameraInfoDefault(LPJASS j) { return jass_pushnullhandle(j, "camerainfo"); }

static void sc2_camera_fire(sc2GCam_t *c, float dur) {
    sc2_gcam_current = *c;
    if (sc2_galaxy_on_camera)
        sc2_galaxy_on_camera(c->tx, c->ty, c->yaw, c->pitch, c->dist, c->fov, c->height, dur);
}

/* CameraApplyInfo: apply stored camera to client (duration = 0 → instant snap). */
static uint32_t sc2_CameraApplyInfo(LPJASS j) {
    int32_t h = (int32_t)(uintptr_t)jass_checkhandle(j, 2, "camerainfo");
    float dur = jass_checknumber(j, 3);
#ifdef SC2_DEBUG_CUTSCENE
    fprintf(stderr, "CameraApplyInfo: handle=%ld count=%ld duration=%.2f callback=%d\n",
            (long)h, (long)sc2_gcam_n, dur, sc2_galaxy_on_camera != NULL);
#endif
    if (h > 0 && h < sc2_gcam_n)
        sc2_camera_fire(&sc2_gcams[h], dur);
    return jass_pushnull(j);
}

/* CameraPan: move camera target to a point at given speed (seconds). */
static uint32_t sc2_CameraPan(LPJASS j) {
    int32_t  pt_h = (int32_t)(uintptr_t)jass_checkhandle(j, 1, "point");
    float dur  = jass_checknumber(j, 2);   /* panSpeed: duration in seconds */
    if (pt_h > 0 && pt_h < (int32_t)sc2_gpoint_n) {
        sc2GCam_t c = sc2_gcam_current;
        c.tx = sc2_gpoints[pt_h].x;
        c.ty = sc2_gpoints[pt_h].y;
        sc2_camera_fire(&c, dur);
    }
    return jass_pushnull(j);
}

/* CameraSave: capture current camera state into a new slot, return handle. */
static uint32_t sc2_CameraSave(LPJASS j) {
    (void)j;
    if (sc2_gcam_n >= MAX_GALAXY_CAMS)
        return jass_pushnullhandle(j, "camerainfo");
    int32_t h = sc2_gcam_n++;
    sc2_gcams[h] = sc2_gcam_current;
    return jass_pushlighthandle(j, (handle_t)(uintptr_t)h, "camerainfo");
}

/* CameraRestore: apply saved camera with a fade-in duration. */
static uint32_t sc2_CameraRestore(LPJASS j) {
    int32_t  h   = (int32_t)(uintptr_t)jass_checkhandle(j, 1, "camerainfo");
    float dur = jass_checknumber(j, 2);   /* durationIn */
    if (h > 0 && h < sc2_gcam_n)
        sc2_camera_fire(&sc2_gcams[h], dur);
    return jass_pushnull(j);
}

static uint32_t sc2_CameraLockInput(LPJASS j)   { (void)j; return jass_pushnull(j); }
static uint32_t sc2_CameraShakeStart(LPJASS j)  { (void)j; return jass_pushnull(j); }
static uint32_t sc2_CameraGetTarget(LPJASS j)   { return jass_pushinteger(j, 0); }
