/* galaxy_host.c — SC2 Galaxy scripting host.
 * Provides all native functions referenced by TRaynor01 campaign scripts.
 * Unimplemented functions return zero/null via galaxy_stub().
 */

#include "galaxy_host.h"
#include "games/warcraft-3/jass/jass_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

/* -------------------------------------------------------------------------
 * ReadFile fallback for Galaxy script includes.
 * Tries VFS first, then <script_dir>/<filename>, then <filename> directly.
 * ------------------------------------------------------------------------- */
static char sc2_gdir[512] = "data/TRaynor01-galaxy";
static HANDLE (*sc2_orig_readfile)(LPCSTR, DWORD *);
static HANDLE (*sc2_memalloc)(long);
static void   (*sc2_memfree)(HANDLE);

void galaxy_set_script_dir(LPCSTR dir) {
    if (dir) snprintf(sc2_gdir, sizeof(sc2_gdir), "%s", dir);
}

static HANDLE sc2_galaxy_readfile(LPCSTR filename, DWORD *size) {
    char path[1024];
    FILE *f;
    long sz;
    LPSTR buf;

    if (sc2_orig_readfile) {
        HANDLE h = sc2_orig_readfile(filename, size);
        if (h) return h;
    }
    snprintf(path, sizeof(path), "%s/%s", sc2_gdir, filename);
    f = fopen(path, "rb");
    if (!f) f = fopen(filename, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return NULL; }
    buf = sc2_memalloc ? (LPSTR)sc2_memalloc((long)sz + 1) : (LPSTR)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (got != (size_t)sz) {
        if (sc2_memfree) sc2_memfree(buf); else free(buf);
        return NULL;
    }
    buf[sz] = '\0';
    *size = (DWORD)sz;
    return buf;
}

/* -------------------------------------------------------------------------
 * Trigger table — integer handles mapped to Galaxy function names.
 * Unit, point, camera handle tables — reset per map load via galaxy_reset().
 * ------------------------------------------------------------------------- */
#define MAX_SC2_TRIGGERS  512
#define MAX_GALAXY_UNITS  256
#define MAX_GALAXY_POINTS 4096
#define MAX_GALAXY_CAMS   64

typedef struct { LONG id; LPCSTR func; BOOL mapinit; BOOL wrapper_conds[2]; } sc2trig_t;
static sc2trig_t sc2_trigs[MAX_SC2_TRIGGERS];
static DWORD sc2_trig_n;
static LONG  sc2_trig_next_id = 1;

void *sc2_gunits[MAX_GALAXY_UNITS];
DWORD sc2_gunit_n;
LONG  sc2_last_unit_handle;

typedef struct { FLOAT x, y; } sc2GPoint_t;
static sc2GPoint_t sc2_gpoints[MAX_GALAXY_POINTS];
static LONG sc2_gpoint_n = 1;  /* 1-based; 0 = null handle */

typedef struct { FLOAT tx, ty, tz, pitch, yaw, dist, fov, height; } sc2GCam_t;
static sc2GCam_t sc2_gcams[MAX_GALAXY_CAMS];
static LONG sc2_gcam_n = 1;    /* 1-based; 0 = null handle */

/* Cargo tracking — per-unit list of cargo unit handles. */
#define MAX_CARGO_PER_UNIT 16
/* Bit flag ORed into a unitgroup handle to mark it as a cargo-group reference.
 * The lower bits encode the transport unit handle (max 256, well below 0x40000000). */
#define CARGO_GROUP_FLAG   ((LONG)0x40000000)
static LONG sc2_gcargo[MAX_GALAXY_UNITS][MAX_CARGO_PER_UNIT];
static LONG sc2_gcargo_n[MAX_GALAXY_UNITS];
static LONG sc2_last_cargo_handle;

/* AbilityCommand handle table — stores ability name + command index. */
#define MAX_GALAXY_ABILCMDS 1024
typedef struct { char ability[64]; LONG cmd_idx; } sc2GAbilCmd_t;
static sc2GAbilCmd_t sc2_gabilcmds[MAX_GALAXY_ABILCMDS];
static LONG sc2_gabilcmd_n = 1; /* 1-based; 0 = null */

/* Order handle table — stores (abilcmd_h, target_pt_h) per issued order. */
#define MAX_GALAXY_ORDERS 1024
typedef struct { LONG abilcmd_h; LONG pt_h; } sc2GOrder_t;
static sc2GOrder_t sc2_gorders[MAX_GALAXY_ORDERS];
static LONG sc2_gorder_n = 1;  /* 1-based; 0 = null */

#define MAX_UNIT_ORDERS 8 // orders; enough for generated cutscene queues; bounds per-unit storage
#define SC2_CARGO_DROP_SPACING 1.1f // map units; separates the two-row cinematic unload formation
typedef struct { char ability[64]; FLOAT x, y; BOOL started; } sc2GUnitOrder_t;
static sc2GUnitOrder_t sc2_uorders[MAX_GALAXY_UNITS][MAX_UNIT_ORDERS];
static LONG sc2_uorder_n[MAX_GALAXY_UNITS];

#define MAX_GALAXY_SOUNDS 256 // links; bounds SoundLink handles retained by live Galaxy scripts
typedef struct { char id[96]; LONG asset; } sc2GSound_t;
static sc2GSound_t sc2_gsounds[MAX_GALAXY_SOUNDS];
static LONG sc2_gsound_n = 1;

/* Defined in jdo.c — not part of the public JASS API; owned by this subsystem. */
void galaxy_loaded_reset(void);

void galaxy_reset(void) {
    for (DWORD i = 0; i < sc2_trig_n; i++) {
        free((void *)sc2_trigs[i].func);  /* free strdup'd names */
        sc2_trigs[i].func = NULL;
    }
    memset(sc2_trigs, 0, sizeof(sc2_trigs));
    sc2_trig_n = 0;
    sc2_trig_next_id = 1;
    sc2_gunit_n = 0;
    sc2_last_unit_handle = 0;
    sc2_gpoint_n = 1;
    sc2_gcam_n = 1;
    memset(sc2_gcargo,   0, sizeof(sc2_gcargo));
    memset(sc2_gcargo_n, 0, sizeof(sc2_gcargo_n));
    sc2_last_cargo_handle = 0;
    memset(sc2_gabilcmds, 0, sizeof(sc2_gabilcmds));
    sc2_gabilcmd_n = 1;
    memset(sc2_gorders, 0, sizeof(sc2_gorders));
    sc2_gorder_n = 1;
    memset(sc2_uorders, 0, sizeof(sc2_uorders));
    memset(sc2_uorder_n, 0, sizeof(sc2_uorder_n));
    memset(sc2_gsounds, 0, sizeof(sc2_gsounds));
    sc2_gsound_n = 1;
    galaxy_loaded_reset();
}

/* Fire a named Galaxy trigger function with the Galaxy convention (testConds=false, runActions=true).
 * Galaxy trigger functions have signature `bool f(bool testConds, bool runActions)`.
 * We compile a thin wrapper so jass_startcoroutinebyname (no args) correctly enters with args. */
static void sc2_fire_trigger_func(LPJASS j, LPCSTR funcname, BOOL testConds, BOOL as_coroutine) {
    LPJASS root = jass_getroot(j);
    char name[128];
    /* Encode testConds in the wrapper name so each variant is compiled at most once. */
    snprintf(name, sizeof(name), "__trig_%llx_%d",
             (unsigned long long)(uintptr_t)funcname, testConds ? 1 : 0);

    /* Find the trigger entry to check/set the compiled flag. */
    sc2trig_t *trig = NULL;
    for (DWORD i = 0; i < sc2_trig_n; i++) {
        if (sc2_trigs[i].func == funcname) { trig = &sc2_trigs[i]; break; }
    }
    BOOL already = trig && trig->wrapper_conds[testConds ? 1 : 0];
    if (!already) {
        char code[512];
        snprintf(code, sizeof(code), "void %s() { %s(%s, true); }",
                 name, funcname, testConds ? "true" : "false");
        char *buf = strdup(code);
        /* Earlier unsupported calls are logged where they occur; they must not be attributed to this wrapper parse. */
        jass_rterror_clear(root);
        BOOL ok = jass_dobuffer_ex(root, buf, JASS_MODE_GALAXY);
        free(buf);
        if (!ok || jass_rterror_pending(root)) {
            fprintf(stderr, "sc2_fire_trigger_func: wrapper compile error for %s: %s\n",
                    funcname, jass_rterror_message(root));
            jass_rterror_clear(root);
            return;
        }
        if (trig) trig->wrapper_conds[testConds ? 1 : 0] = true;
    }
#ifdef SC2_DEBUG_CUTSCENE
    fprintf(stderr, "sc2_fire_trigger_func: calling %s (coroutine=%d)\n", funcname, as_coroutine);
#endif
    if (as_coroutine) {
        jass_startcoroutinebyname(root, name);
    } else {
        if (!jass_callcoroutinebyname(root, name))
            jass_callbyname(root, name, false);
        if (jass_rterror_pending(root)) {
            fprintf(stderr, "galaxy trigger %s: runtime error: %s\n",
                    funcname, jass_rterror_message(root));
            jass_rterror_clear(root);
        }
    }
}

void galaxy_fire_mapinit(LPJASS j) {
    fprintf(stderr, "galaxy_fire_mapinit: %u triggers registered, firing MapInit\n", sc2_trig_n);
    for (DWORD i = 0; i < sc2_trig_n; i++) {
        if (sc2_trigs[i].mapinit && sc2_trigs[i].func) {
            fprintf(stderr, "  firing MapInit trigger: %s\n", sc2_trigs[i].func);
            sc2_fire_trigger_func(j, sc2_trigs[i].func, false, true);
        }
    }
    jass_runevents(j);
}

/* -------------------------------------------------------------------------
 * Callbacks into g_sc2.c — set during SC2_InitGalaxyHost()
 * ------------------------------------------------------------------------- */
void (*sc2_galaxy_on_camera)(float target_x, float target_y,
                             float yaw, float pitch,
                             float dist, float fov, float height_offset, float duration);
void (*sc2_galaxy_on_cinematic)(BOOL enable, float duration);
void (*sc2_galaxy_on_fade)(float alpha, float duration);
float (*sc2_galaxy_sound_length)(LPCSTR sound_id, int asset);
void (*sc2_galaxy_on_sound)(LPCSTR sound_id, int asset);
void *(*sc2_galaxy_on_unit_create)(LPCSTR model, int player,
                                   float x, float y, float angle);

BOOL (*sc2_galaxy_get_camera_by_id)(DWORD map_id,
    float *tx, float *ty, float *tz,
    float *pitch, float *yaw, float *dist, float *fov, float *height_offset);
BOOL (*sc2_galaxy_get_point_by_id)(DWORD map_id, float *x, float *y);
const char *(*sc2_galaxy_get_unit_model)(LPCSTR unit_type);
void (*sc2_galaxy_unit_set_position)(void *ent, float x, float y, float facing);
void (*sc2_galaxy_unit_move)(void *ent, float x, float y);
BOOL (*sc2_galaxy_unit_is_moving)(void *ent);
BOOL (*sc2_galaxy_unit_is_alive)(void *ent);

static void sc2_pop_unit_order(LONG unit_h) {
    LONG *count = &sc2_uorder_n[unit_h - 1];
    if (--*count > 0)
        memmove(&sc2_uorders[unit_h - 1][0], &sc2_uorders[unit_h - 1][1], sizeof(sc2GUnitOrder_t) * *count);
}

/* Galaxy append orders begin only after the active movement reports completion. */
static void sc2_run_unit_orders(void) {
    for (LONG unit_h = 1; unit_h <= (LONG)sc2_gunit_n; unit_h++) {
        LONG *count = &sc2_uorder_n[unit_h - 1];
        void *ent = sc2_gunits[unit_h - 1];
        while (*count > 0) {
            sc2GUnitOrder_t *ord = &sc2_uorders[unit_h - 1][0];
            if (!strcmp(ord->ability, "move")) {
                if (!ord->started) {
                    if (ent && sc2_galaxy_unit_move) sc2_galaxy_unit_move(ent, ord->x, ord->y);
                    ord->started = true;
                    break;
                }
                if (ent && sc2_galaxy_unit_is_moving && sc2_galaxy_unit_is_moving(ent)) break;
                sc2_pop_unit_order(unit_h);
                continue;
            }
            if (!strcmp(ord->ability, "SpecOpsDropshipTransport")) {
                LONG cargo_n = sc2_gcargo_n[unit_h - 1];
                for (LONG i = 0; i < cargo_n; i++) {
                    LONG cargo_h = sc2_gcargo[unit_h - 1][i];
                    void *cargo = (cargo_h > 0 && cargo_h <= (LONG)sc2_gunit_n) ? sc2_gunits[cargo_h - 1] : NULL;
                    FLOAT x = ord->x + ((FLOAT)(i % 3) - 1.0f) * SC2_CARGO_DROP_SPACING;
                    FLOAT y = ord->y + (0.75f + (FLOAT)(i / 3) * SC2_CARGO_DROP_SPACING);
                    if (cargo && sc2_galaxy_unit_set_position)
                        sc2_galaxy_unit_set_position(cargo, x, y, 0.0f);
                }
#ifdef SC2_DEBUG_CUTSCENE
                fprintf(stderr, "UnitIssueOrder: unloaded %ld cargo units at (%.1f,%.1f)\n",
                        (long)cargo_n, ord->x, ord->y);
#endif
                sc2_gcargo_n[unit_h - 1] = 0;
            }
            sc2_pop_unit_order(unit_h);
        }
    }
}

/* -------------------------------------------------------------------------
 * VM lifecycle wrappers — g_sc2.c calls these instead of jass_* directly.
 * ------------------------------------------------------------------------- */
LPJASS galaxy_open(HANDLE (*readfile)(LPCSTR, DWORD *),
                   DWORD  (*gettime)(void),
                   HANDLE (*memalloc)(long),
                   void   (*memfree)(HANDLE)) {
    char path[512];
    sc2_orig_readfile = readfile;
    sc2_memalloc      = memalloc;
    sc2_memfree       = memfree;
    jass_sethost(&(JASSHOST){
        .MemAlloc       = memalloc,
        .MemFree        = memfree,
        .GetTime        = gettime,
        .ReadFile       = sc2_galaxy_readfile,
        .galaxy_natives = galaxy_get_natives(),
    });
    LPJASS vm = jass_newstate();
    /* Load MapScript.galaxy only — it includes NativeLib/LibertyLib/CampaignLib
     * via its own `include` directives, each parsed exactly once.  Pre-loading
     * them separately causes each to be re-parsed 4–7 times via nested includes,
     * making initialization O(n^2) in the JASS VM's linked-list variable lookup. */
    snprintf(path, sizeof(path), "%s/MapScript.galaxy", sc2_gdir);
    if (!jass_dofile(vm, path))
        fprintf(stderr, "galaxy_open: failed to load MapScript.galaxy from %s\n", sc2_gdir);
    if (jass_rterror_pending(vm)) {
        fprintf(stderr, "galaxy_open: error: %s\n", jass_rterror_message(vm));
        jass_rterror_clear(vm);
    }
    return vm;
}

void galaxy_close(LPJASS vm) {
    if (vm) jass_close(vm);
    galaxy_reset();
}

void galaxy_start(LPJASS vm) {
    /* Skip InitLibs() — heavy array-init loops are O(n^2) in JASS VM (50+ s).
     * InitGlobals() sets map-script globals; InitTriggers() registers the
     * MapInit event handler for the intro cutscene. */
    /* InitGlobals has ~5600 lines of global inits with O(n) variable lookup per
     * assignment — too slow without a hash table.  The cutscene only needs
     * triggers registered by InitTriggers; skip InitGlobals for now. */
    fprintf(stderr, "galaxy_start: calling InitTriggers\n");
    jass_callbyname(vm, "InitTriggers", false);
    if (jass_rterror_pending(vm)) {
        fprintf(stderr, "galaxy_start: InitTriggers error: %s\n", jass_rterror_message(vm));
        jass_rterror_clear(vm);
    }
    fprintf(stderr, "galaxy_start: done, %u triggers registered\n", sc2_trig_n);
}

void galaxy_tick(LPJASS vm) {
    sc2_run_unit_orders();
    jass_runevents(vm);
    if (jass_rterror_pending(vm)) {
        fprintf(stderr, "galaxy: runtime error: %s\n", jass_rterror_message(vm));
        jass_rterror_clear(vm);
    }
}

/* -------------------------------------------------------------------------
 * Generic no-op stub
 * ------------------------------------------------------------------------- */
static DWORD galaxy_stub(LPJASS j) { (void)j; return jass_pushnull(j); }

/* -------------------------------------------------------------------------
 * Trigger natives
 * ------------------------------------------------------------------------- */
/* Triggers are opaque handles; extract the integer ID via pointer cast. */
static LONG sc2_trigger_id(LPJASS j, int index) {
    HANDLE h = jass_checkhandle(j, index, "trigger");
    return h ? (LONG)(uintptr_t)h : 0;
}

static DWORD sc2_TriggerCreate(LPJASS j) {
    LPCSTR name = jass_checkstring(j, 1);
    LONG id = 0;
    if (name && sc2_trig_n < MAX_SC2_TRIGGERS) {
        id = sc2_trig_next_id++;
        /* strdup: the JASS VM may GC the string after this call returns. */
        sc2_trigs[sc2_trig_n++] = (sc2trig_t){ id, strdup(name), false };
    }
    return id ? jass_pushlighthandle(j, (HANDLE)(uintptr_t)id, "trigger")
              : jass_pushnullhandle(j, "trigger");
}

static DWORD sc2_TriggerEnable(LPJASS j)      { (void)j; return jass_pushnull(j); }
static DWORD sc2_TriggerStop(LPJASS j)        { (void)j; return jass_pushnull(j); }
static DWORD sc2_TriggerGetCurrent(LPJASS j)  { return jass_pushnullhandle(j, "trigger"); }
static DWORD sc2_TriggerGetExecCount(LPJASS j){ return jass_pushinteger(j, 0); }
static DWORD sc2_TriggerIsEnabled(LPJASS j)   { return jass_pushboolean(j, true); }

static DWORD sc2_TriggerExecute(LPJASS j) {
    LONG   id        = sc2_trigger_id(j, 1);
    BOOL   testConds = jass_checkboolean(j, 2);
    BOOL   waitDone  = jass_checkboolean(j, 3);
    for (DWORD i = 0; i < sc2_trig_n; i++) {
        if (sc2_trigs[i].id == id && sc2_trigs[i].func) {
#ifdef SC2_DEBUG_CUTSCENE
            fprintf(stderr, "TriggerExecute: %s testConds=%d waitDone=%d\n",
                    sc2_trigs[i].func, testConds, waitDone);
#endif
            sc2_fire_trigger_func(j, sc2_trigs[i].func, testConds, !waitDone);
            break;
        }
    }
    return jass_pushnull(j);
}

static DWORD sc2_TriggerAddEventMapInit(LPJASS j) {
    LONG id = sc2_trigger_id(j, 1);
    for (DWORD i = 0; i < sc2_trig_n; i++) {
        if (sc2_trigs[i].id == id) { sc2_trigs[i].mapinit = true; break; }
    }
    return jass_pushnull(j);
}

static DWORD sc2_TriggerSkippableBegin(LPJASS j) { (void)j; return jass_pushnull(j); }
static DWORD sc2_TriggerSkippableEnd(LPJASS j)   { (void)j; return jass_pushnull(j); }
static DWORD sc2_TriggerQueueEnter(LPJASS j)     { (void)j; return jass_pushnull(j); }
static DWORD sc2_TriggerQueueExit(LPJASS j)      { (void)j; return jass_pushnull(j); }
static DWORD sc2_TriggerQueueIsEmpty(LPJASS j)   { return jass_pushboolean(j, true); }
static DWORD sc2_TriggerQueuePause(LPJASS j)     { (void)j; return jass_pushnull(j); }
static DWORD sc2_TriggerQueueClear(LPJASS j)     { (void)j; return jass_pushnull(j); }

static DWORD sc2_Wait(LPJASS j) {
    FLOAT secs = jass_checknumber(j, 1);
    jass_sleep(j, (DWORD)(secs * 1000.0f));
    return 0;
}

/* Event registration stubs */
static DWORD sc2_TriggerAddEventPlayerAIWave(LPJASS j)            { (void)j; return jass_pushnull(j); }
static DWORD sc2_TriggerAddEventPlayerAllianceChange(LPJASS j)    { (void)j; return jass_pushnull(j); }
static DWORD sc2_TriggerAddEventPlayerLeft(LPJASS j)              { (void)j; return jass_pushnull(j); }
static DWORD sc2_TriggerAddEventPlayerPropChange(LPJASS j)        { (void)j; return jass_pushnull(j); }
static DWORD sc2_TriggerAddEventTimeElapsed(LPJASS j)             { (void)j; return jass_pushnull(j); }
static DWORD sc2_TriggerAddEventTimePeriodic(LPJASS j)            { (void)j; return jass_pushnull(j); }
static DWORD sc2_TriggerAddEventTimer(LPJASS j)                   { (void)j; return jass_pushnull(j); }
static DWORD sc2_TriggerAddEventUnitAttacked(LPJASS j)            { (void)j; return jass_pushnull(j); }
static DWORD sc2_TriggerAddEventUnitCargo(LPJASS j)               { (void)j; return jass_pushnull(j); }
static DWORD sc2_TriggerAddEventUnitDamaged(LPJASS j)             { (void)j; return jass_pushnull(j); }
static DWORD sc2_TriggerAddEventUnitDied(LPJASS j)                { (void)j; return jass_pushnull(j); }
static DWORD sc2_TriggerAddEventUnitOrder(LPJASS j)               { (void)j; return jass_pushnull(j); }
static DWORD sc2_TriggerAddEventUnitRange(LPJASS j)               { (void)j; return jass_pushnull(j); }
static DWORD sc2_TriggerAddEventUnitRangePoint(LPJASS j)          { (void)j; return jass_pushnull(j); }
static DWORD sc2_TriggerAddEventUnitRegion(LPJASS j)              { (void)j; return jass_pushnull(j); }

/* -------------------------------------------------------------------------
 * Camera
 * ------------------------------------------------------------------------- */

/* CameraInfoFromId: look up map camera by ID, store in local table, return handle. */
static DWORD sc2_CameraInfoFromId(LPJASS j) {
    DWORD map_id = (DWORD)jass_checkinteger(j, 1);
    FLOAT tx = 0, ty = 0, tz = 0, pitch = 56.0f, yaw = 180.0f, dist = 34.0f, fov = 28.0f, height = 0;
    if (sc2_galaxy_get_camera_by_id &&
        sc2_gcam_n < MAX_GALAXY_CAMS &&
        sc2_galaxy_get_camera_by_id(map_id, &tx, &ty, &tz, &pitch, &yaw, &dist, &fov, &height)) {
        LONG h = sc2_gcam_n++;
        sc2_gcams[h] = (sc2GCam_t){ tx, ty, tz, pitch, yaw, dist, fov, height };
        return jass_pushlighthandle(j, (HANDLE)(uintptr_t)h, "camerainfo");
    }
    return jass_pushnullhandle(j, "camerainfo");
}

static DWORD sc2_CameraInfoDefault(LPJASS j) { return jass_pushnullhandle(j, "camerainfo"); }

/* CameraApplyInfo: apply stored camera to client (duration = 0 → instant snap). */
static DWORD sc2_CameraApplyInfo(LPJASS j) {
    LONG h = (LONG)(uintptr_t)jass_checkhandle(j, 2, "camerainfo");
    FLOAT dur = jass_checknumber(j, 3);
#ifdef SC2_DEBUG_CUTSCENE
    fprintf(stderr, "CameraApplyInfo: handle=%ld count=%ld duration=%.2f callback=%d\n",
            (long)h, (long)sc2_gcam_n, dur, sc2_galaxy_on_camera != NULL);
#endif
    if (h > 0 && h < sc2_gcam_n && sc2_galaxy_on_camera) {
        sc2GCam_t *c = &sc2_gcams[h];
        sc2_galaxy_on_camera(c->tx, c->ty, c->yaw, c->pitch, c->dist, c->fov, c->height, dur);
    }
    return jass_pushnull(j);
}

static DWORD sc2_CameraPan(LPJASS j)         { (void)j; return jass_pushnull(j); }
static DWORD sc2_CameraSave(LPJASS j)        { (void)j; return jass_pushnull(j); }
static DWORD sc2_CameraRestore(LPJASS j)     { (void)j; return jass_pushnull(j); }
static DWORD sc2_CameraLockInput(LPJASS j)   { (void)j; return jass_pushnull(j); }
static DWORD sc2_CameraShakeStart(LPJASS j)  { (void)j; return jass_pushnull(j); }
static DWORD sc2_CameraGetTarget(LPJASS j)   { return jass_pushinteger(j, 0); }

/* -------------------------------------------------------------------------
 * Cinematic
 * ------------------------------------------------------------------------- */
static DWORD sc2_CinematicMode(LPJASS j) {
    BOOL  enable = jass_checkboolean(j, 2);
    FLOAT dur    = jass_checknumber(j, 3);
#ifdef SC2_DEBUG_CUTSCENE
    fprintf(stderr, "CinematicMode: enable=%d dur=%.1f\n", enable, dur);
#endif
    if (sc2_galaxy_on_cinematic) sc2_galaxy_on_cinematic(enable, dur);
    return jass_pushnull(j);
}

static DWORD sc2_CinematicFade(LPJASS j) {
    BOOL  fadein = jass_checkboolean(j, 1);
    FLOAT dur    = jass_checknumber(j, 2);
#ifdef SC2_DEBUG_CUTSCENE
    fprintf(stderr, "CinematicFade: fadein=%d dur=%.1f\n", fadein, dur);
#endif
    if (sc2_galaxy_on_fade) sc2_galaxy_on_fade(fadein ? 0.0f : 1.0f, dur);
    return jass_pushnull(j);
}

static DWORD sc2_CinematicOverlay(LPJASS j) { (void)j; return jass_pushnull(j); }

/* -------------------------------------------------------------------------
 * Units
 * UnitCreate: resolve unit model from catalog, spawn at point position.
 * ------------------------------------------------------------------------- */
static DWORD sc2_UnitCreate(LPJASS j) {
    LONG   count  = jass_checkinteger(j, 1);
    LPCSTR type   = jass_checkstring(j, 2);
    LONG   player = jass_checkinteger(j, 4);
    LONG   pt_h   = (LONG)(uintptr_t)jass_checkhandle(j, 5, "point");
    FLOAT  angle  = jass_checknumber(j, 6);
    FLOAT  x = 0.0f, y = 0.0f;
    if (pt_h > 0 && pt_h < sc2_gpoint_n) { x = sc2_gpoints[pt_h].x; y = sc2_gpoints[pt_h].y; }
    LONG handle = 0;
    for (LONG i = 0; i < count && sc2_gunit_n < MAX_GALAXY_UNITS; i++) {
        void *ent = sc2_galaxy_on_unit_create ?
            sc2_galaxy_on_unit_create(type ? type : "", (int)player, x, y, angle) : NULL;
        if (!ent)
            fprintf(stderr, "sc2_UnitCreate: on_unit_create returned NULL for type '%s' (%ld/%ld) — unit will be invisible\n",
                    type ? type : "(null)", (long)(i + 1), (long)count);
        handle = (LONG)(++sc2_gunit_n);
        sc2_gunits[handle - 1] = ent;
        sc2_last_unit_handle = handle;
    }
    return handle ? jass_pushlighthandle(j, (HANDLE)(uintptr_t)handle, "unit")
                  : jass_pushnullhandle(j, "unit");
}

static DWORD sc2_UnitLastCreated(LPJASS j) {
    return sc2_last_unit_handle ?
        jass_pushlighthandle(j, (HANDLE)(uintptr_t)sc2_last_unit_handle, "unit") :
        jass_pushnullhandle(j, "unit");
}
static DWORD sc2_UnitLastCreatedGroup(LPJASS j)     { return jass_pushnullhandle(j, "unitgroup"); }
static void *sc2_ent_from_handle(LPJASS j, int idx) {
    LONG h = (LONG)(uintptr_t)jass_checkhandle(j, idx, "unit");
    return (h > 0 && h <= (LONG)sc2_gunit_n) ? sc2_gunits[h - 1] : NULL;
}

static DWORD sc2_UnitSetPosition(LPJASS j) {
    void *ent = sc2_ent_from_handle(j, 1);
    LONG pt_h = (LONG)(uintptr_t)jass_checkhandle(j, 2, "point");
    if (ent && sc2_galaxy_unit_set_position && pt_h > 0 && pt_h < sc2_gpoint_n)
        sc2_galaxy_unit_set_position(ent, sc2_gpoints[pt_h].x, sc2_gpoints[pt_h].y, 0.0f);
    return jass_pushnull(j);
}

static DWORD sc2_UnitSetFacing(LPJASS j) {
    void *ent = sc2_ent_from_handle(j, 1);
    FLOAT ang = jass_checknumber(j, 2);
    if (ent && sc2_galaxy_unit_set_position) {
        /* Re-use set_position with NaN for x/y to indicate facing-only update.
         * g_sc2.c checks for this sentinel and only updates the angle. */
        sc2_galaxy_unit_set_position(ent, 0.0f/0.0f, 0.0f/0.0f, ang * 3.14159265f / 180.0f);
    }
    return jass_pushnull(j);
}

static DWORD sc2_UnitSetOwner(LPJASS j)             { (void)j; return jass_pushnull(j); }
static DWORD sc2_UnitGetOwner(LPJASS j) {
    (void)jass_checkhandle(j, 1, "unit");  /* consume arg */
    return jass_pushinteger(j, 0);
}
static DWORD sc2_UnitSetHeight(LPJASS j)            { (void)j; return jass_pushnull(j); }
static DWORD sc2_UnitSetScale(LPJASS j)             { (void)j; return jass_pushnull(j); }
static DWORD sc2_UnitSetState(LPJASS j)             { (void)j; return jass_pushnull(j); }
static DWORD sc2_UnitSetCursor(LPJASS j)            { (void)j; return jass_pushnull(j); }
static DWORD sc2_UnitTestState(LPJASS j)            { return jass_pushboolean(j, false); }
static DWORD sc2_UnitIsAlive(LPJASS j) {
    void *ent = sc2_ent_from_handle(j, 1);
    return jass_pushboolean(j, ent && (!sc2_galaxy_unit_is_alive || sc2_galaxy_unit_is_alive(ent)));
}
static DWORD sc2_UnitIsValid(LPJASS j) {
    void *ent = sc2_ent_from_handle(j, 1);
    return jass_pushboolean(j, ent != NULL);
}
static DWORD sc2_UnitKill(LPJASS j)                 { (void)j; return jass_pushnull(j); }
static DWORD sc2_UnitRemove(LPJASS j)               { (void)j; return jass_pushnull(j); }
static DWORD sc2_UnitRevive(LPJASS j)               { (void)j; return jass_pushnull(j); }
static DWORD sc2_UnitWaitUntilIdle(LPJASS j)        { (void)j; return jass_pushnull(j); }
static DWORD sc2_UnitIssueOrder(LPJASS j) {
    LONG unit_h  = (LONG)(uintptr_t)jass_checkhandle(j, 1, "unit");
    LONG order_h = (LONG)(uintptr_t)jass_checkhandle(j, 2, "order");
    LONG queue   = jass_checkinteger(j, 3);
    if (order_h <= 0 || order_h >= sc2_gorder_n)
        return jass_pushboolean(j, false);
    sc2GOrder_t *ord = &sc2_gorders[order_h];
    LONG  ac_h = ord->abilcmd_h;
    LONG  pt_h = ord->pt_h;
    FLOAT tx   = (pt_h > 0 && pt_h < sc2_gpoint_n) ? sc2_gpoints[pt_h].x : 0.0f;
    FLOAT ty   = (pt_h > 0 && pt_h < sc2_gpoint_n) ? sc2_gpoints[pt_h].y : 0.0f;
    const char *ability = (ac_h > 0 && ac_h < sc2_gabilcmd_n)
                          ? sc2_gabilcmds[ac_h].ability : "move";
    if (unit_h <= 0 || unit_h > (LONG)sc2_gunit_n || (queue != 0 && queue != 1))
        return jass_pushboolean(j, false);
    if (queue == 0) sc2_uorder_n[unit_h - 1] = 0;
    LONG *count = &sc2_uorder_n[unit_h - 1];
    if (*count >= MAX_UNIT_ORDERS) {
        fprintf(stderr, "UnitIssueOrder: queue full for unit %ld\n", (long)unit_h);
        return jass_pushboolean(j, false);
    }
    sc2GUnitOrder_t *queued = &sc2_uorders[unit_h - 1][(*count)++];
    snprintf(queued->ability, sizeof(queued->ability), "%s", ability);
    queued->x = tx;
    queued->y = ty;
    queued->started = false;
#ifdef SC2_DEBUG_CUTSCENE
    fprintf(stderr, "UnitIssueOrder: unit=%ld ability=%s target=(%.1f,%.1f) queue=%ld depth=%ld\n",
            (long)unit_h, ability, tx, ty, (long)queue, (long)*count);
#endif
    return jass_pushboolean(j, true);
}
static DWORD sc2_UnitPauseAll(LPJASS j)             { (void)j; return jass_pushnull(j); }
static DWORD sc2_UnitGetFacing(LPJASS j)            { return jass_pushnumber(j, 0.0f); }
static DWORD sc2_UnitGetHeight(LPJASS j)            { return jass_pushnumber(j, 0.0f); }
static DWORD sc2_UnitGetPosition(LPJASS j) {
    return jass_pushnullhandle(j, "point");  /* TODO: extract ent position */
}
static DWORD sc2_UnitGetType(LPJASS j)   { (void)jass_checkhandle(j, 1, "unit"); return jass_pushstring(j, ""); }
static DWORD sc2_UnitFromId(LPJASS j)    { return jass_pushnullhandle(j, "unit"); }
static DWORD sc2_UnitBehaviorAdd(LPJASS j)          { (void)j; return jass_pushnull(j); }
static DWORD sc2_UnitBehaviorRemove(LPJASS j)       { (void)j; return jass_pushnull(j); }
static DWORD sc2_UnitCargoCreate(LPJASS j) {
    LONG   t_h  = (LONG)(uintptr_t)jass_checkhandle(j, 1, "unit");
    LPCSTR type = jass_checkstring(j, 2);
    LONG   cnt  = jass_checkinteger(j, 3);
    if (cnt < 1) cnt = 1;
    LONG handle  = 0;
    for (LONG i = 0; i < cnt && sc2_gunit_n < MAX_GALAXY_UNITS; i++) {
        void *ent = sc2_galaxy_on_unit_create ?
            sc2_galaxy_on_unit_create(type ? type : "", 0, 0.0f, 0.0f, 0.0f) : NULL;
        if (!ent)
            fprintf(stderr, "sc2_UnitCargoCreate: on_unit_create returned NULL for type '%s' (%ld/%ld) — cargo unit will be invisible\n",
                    type ? type : "(null)", (long)(i + 1), (long)cnt);
        handle = (LONG)(++sc2_gunit_n);
        sc2_gunits[handle - 1] = ent;
        sc2_last_cargo_handle  = handle;
        sc2_last_unit_handle   = handle;
        if (t_h > 0 && t_h <= MAX_GALAXY_UNITS) {
            LONG ci = sc2_gcargo_n[t_h - 1];
            if (ci < MAX_CARGO_PER_UNIT)
                sc2_gcargo[t_h - 1][sc2_gcargo_n[t_h - 1]++] = handle;
        }
    }
    fprintf(stderr, "UnitCargoCreate: transport=%ld type=%s count=%ld\n",
            (long)t_h, type ? type : "(null)", (long)cnt);
    return handle ? jass_pushlighthandle(j, (HANDLE)(uintptr_t)handle, "unit")
                  : jass_pushnullhandle(j, "unit");
}
static DWORD sc2_UnitCargoLastCreated(LPJASS j) {
    return sc2_last_cargo_handle ?
        jass_pushlighthandle(j, (HANDLE)(uintptr_t)sc2_last_cargo_handle, "unit") :
        jass_pushnullhandle(j, "unit");
}
static DWORD sc2_UnitCargoGroup(LPJASS j) {
    LONG h = (LONG)(uintptr_t)jass_checkhandle(j, 1, "unit");
    if (h > 0 && h <= (LONG)sc2_gunit_n)
        return jass_pushlighthandle(j,
            (HANDLE)(uintptr_t)(CARGO_GROUP_FLAG | h), "unitgroup");
    return jass_pushnullhandle(j, "unitgroup");
}
static DWORD sc2_UnitCargoLastCreatedGroup(LPJASS j){ return jass_pushnullhandle(j, "unitgroup"); }
static DWORD sc2_UnitClearSelection(LPJASS j)       { (void)j; return jass_pushnull(j); }
/* UnitRef wraps a unit handle into a unitref (same pointer, different type name). */
static DWORD sc2_UnitRefFromUnit(LPJASS j) {
    HANDLE h = jass_checkhandle(j, 1, "unit");
    return h ? jass_pushlighthandle(j, h, "unitref") : jass_pushnullhandle(j, "unitref");
}
static DWORD sc2_UnitRefFromVariable(LPJASS j)  { return jass_pushnullhandle(j, "unitref"); }
static DWORD sc2_UnitRefToUnit(LPJASS j) {
    HANDLE h = jass_checkhandle(j, 1, "unitref");
    return h ? jass_pushlighthandle(j, h, "unit") : jass_pushnullhandle(j, "unit");
}
static DWORD sc2_UnitSetInfoText(LPJASS j)          { (void)j; return jass_pushnull(j); }
static DWORD sc2_UnitClearInfoText(LPJASS j)        { (void)j; return jass_pushnull(j); }
static DWORD sc2_UnitForceStatusBar(LPJASS j)       { (void)j; return jass_pushnull(j); }
static DWORD sc2_UnitGetAttachmentPoint(LPJASS j)   { return jass_pushinteger(j, 0); }
static DWORD sc2_UnitSetTeamColorIndex(LPJASS j)    { (void)j; return jass_pushnull(j); }
static DWORD sc2_UnitLoadModel(LPJASS j)            { (void)j; return jass_pushnull(j); }
static DWORD sc2_UnitUnloadModel(LPJASS j)          { (void)j; return jass_pushnull(j); }
static DWORD sc2_UnitSetPropertyFixed(LPJASS j)     { (void)j; return jass_pushnull(j); }
static DWORD sc2_UnitGetPropertyFixed(LPJASS j)     { return jass_pushnumber(j, 0.0f); }
static DWORD sc2_EventUnit(LPJASS j)       { return jass_pushnullhandle(j, "unit"); }
static DWORD sc2_EventUnitCargo(LPJASS j)  { return jass_pushnullhandle(j, "unit"); }
static DWORD sc2_EventUnitTarget(LPJASS j) { return jass_pushnullhandle(j, "unit"); }

/* UnitGroup */
static DWORD sc2_UnitGroupEmpty(LPJASS j)            { return jass_pushnullhandle(j, "unitgroup"); }
static DWORD sc2_UnitGroupAdd(LPJASS j)              { return jass_checkhandle(j, 1, "unitgroup") ? jass_pushlighthandle(j, jass_checkhandle(j, 1, "unitgroup"), "unitgroup") : jass_pushnullhandle(j, "unitgroup"); }
static DWORD sc2_UnitGroupCount(LPJASS j) {
    LONG h    = (LONG)(uintptr_t)jass_checkhandle(j, 1, "unitgroup");
    LONG mode = jass_checkinteger(j, 2); (void)mode;
    if (h & CARGO_GROUP_FLAG) {
        LONG t = h & ~CARGO_GROUP_FLAG;
        if (t > 0 && t <= (LONG)sc2_gunit_n)
            return jass_pushinteger(j, sc2_gcargo_n[t - 1]);
    }
    return jass_pushinteger(j, 0);
}
static DWORD sc2_UnitGroupHasUnit(LPJASS j)          { return jass_pushboolean(j, false); }
static DWORD sc2_UnitGroupWaitUntilIdle(LPJASS j)    { (void)j; return jass_pushnull(j); }
static DWORD sc2_UnitInventoryGroup(LPJASS j)        { return jass_pushnullhandle(j, "unitgroup"); }
static DWORD sc2_UnitTechTreeBehaviorCount(LPJASS j) { return jass_pushinteger(j, 0); }
static DWORD sc2_UnitTechTreeUnitCount(LPJASS j)     { return jass_pushinteger(j, 0); }
static DWORD sc2_UnitTechTreeUpgradeCount(LPJASS j)  { return jass_pushinteger(j, 0); }
static DWORD sc2_UnitGroupUnit(LPJASS j)             { return jass_pushinteger(j, 0); }
static DWORD sc2_UnitGroupIssueOrder(LPJASS j)       { (void)j; return jass_pushnull(j); }
static DWORD sc2_UnitGroupIdle(LPJASS j)             { return jass_pushboolean(j, false); }
static DWORD sc2_UnitGroupFilter(LPJASS j)           { return jass_pushinteger(j, 0); }
static DWORD sc2_UnitGroupClear(LPJASS j)            { return jass_pushnullhandle(j, "unitgroup"); }
static DWORD sc2_UnitGroupCopy(LPJASS j)             { return jass_pushnullhandle(j, "unitgroup"); }
static DWORD sc2_UnitGroupRemove(LPJASS j)           { (void)j; return jass_pushnull(j); }
static DWORD sc2_UnitGroupAlliance(LPJASS j)         { return jass_pushnullhandle(j, "unitgroup"); }
static DWORD sc2_UnitGroupFilterAlliance(LPJASS j)   { return jass_pushnullhandle(j, "unitgroup"); }
static DWORD sc2_UnitGroupFilterPlayer(LPJASS j)     { return jass_pushnullhandle(j, "unitgroup"); }
static DWORD sc2_UnitGroupFilterPlane(LPJASS j)      { return jass_pushnullhandle(j, "unitgroup"); }
static DWORD sc2_UnitGroupFilterRegion(LPJASS j)     { return jass_pushnullhandle(j, "unitgroup"); }
static DWORD sc2_UnitGroupFilterThreat(LPJASS j)     { return jass_pushnullhandle(j, "unitgroup"); }
static DWORD sc2_UnitGroupFromId(LPJASS j)           { return jass_pushnullhandle(j, "unitgroup"); }
static DWORD sc2_UnitGroupLoopBegin(LPJASS j)        { (void)j; return jass_pushnull(j); }
static DWORD sc2_UnitGroupLoopCurrent(LPJASS j)      { return jass_pushnullhandle(j, "unit"); }
static DWORD sc2_UnitGroupLoopDone(LPJASS j)         { return jass_pushboolean(j, true); }
static DWORD sc2_UnitGroupLoopEnd(LPJASS j)          { (void)j; return jass_pushnull(j); }
static DWORD sc2_UnitGroupLoopStep(LPJASS j)         { (void)j; return jass_pushnull(j); }
static DWORD sc2_UnitGroupNearestUnit(LPJASS j)      { return jass_pushnullhandle(j, "unit"); }
static DWORD sc2_UnitGroupRandomUnit(LPJASS j)       { return jass_pushnullhandle(j, "unit"); }
static DWORD sc2_UnitGroupTestPlane(LPJASS j)        { return jass_pushboolean(j, false); }
static DWORD sc2_UnitFilter(LPJASS j)                { return jass_pushnullhandle(j, "unitfilter"); }
static DWORD sc2_UnitFilterMatch(LPJASS j)           { return jass_pushboolean(j, false); }
static DWORD sc2_UnitFilterSetState(LPJASS j)        { (void)j; return jass_pushnull(j); }
static DWORD sc2_UnitFilterStr(LPJASS j)             { return jass_pushnullhandle(j, "unitfilter"); }
static DWORD sc2_UnitGroup(LPJASS j)                 { return jass_pushnullhandle(j, "unitgroup"); }

/* UnitType */
static DWORD sc2_UnitTypeFromString(LPJASS j)          { return jass_pushstring(j, ""); }
static DWORD sc2_UnitTypeGetCost(LPJASS j)             { return jass_pushinteger(j, 0); }
static DWORD sc2_UnitTypeGetName(LPJASS j)             { return jass_pushstring(j, ""); }
static DWORD sc2_UnitTypeGetProperty(LPJASS j)         { return jass_pushinteger(j, 0); }
static DWORD sc2_UnitTypeIsAffectedByUpgrade(LPJASS j) { return jass_pushboolean(j, false); }
static DWORD sc2_UnitTypeTestAttribute(LPJASS j)       { return jass_pushboolean(j, false); }
static DWORD sc2_UnitTypeTestFlag(LPJASS j)            { return jass_pushboolean(j, false); }
static DWORD sc2_UnitTypeAnimationLoad(LPJASS j)       { (void)j; return jass_pushnull(j); }
static DWORD sc2_UnitTypeAnimationUnload(LPJASS j)     { (void)j; return jass_pushnull(j); }

/* -------------------------------------------------------------------------
 * Point, Region, Visibility
 * ------------------------------------------------------------------------- */
/* PointFromId: look up map point object by ID, return typed handle. */
static DWORD sc2_PointFromId(LPJASS j) {
    DWORD map_id = (DWORD)jass_checkinteger(j, 1);
    FLOAT x = 0.0f, y = 0.0f;
    if (sc2_galaxy_get_point_by_id && sc2_gpoint_n < MAX_GALAXY_POINTS &&
        sc2_galaxy_get_point_by_id(map_id, &x, &y)) {
        LONG h = sc2_gpoint_n++;
        sc2_gpoints[h] = (sc2GPoint_t){ x, y };
        return jass_pushlighthandle(j, (HANDLE)(uintptr_t)h, "point");
    }
    if (sc2_gpoint_n >= MAX_GALAXY_POINTS)
        fprintf(stderr, "PointFromId: table full (%d entries) — id=%u lost\n",
                MAX_GALAXY_POINTS, map_id);
    return jass_pushnullhandle(j, "point");
}

/* Point(x, y): create a point from explicit coordinates. */
static DWORD sc2_Point(LPJASS j) {
    FLOAT x = jass_checknumber(j, 1), y = jass_checknumber(j, 2);
    if (sc2_gpoint_n < MAX_GALAXY_POINTS) {
        LONG h = sc2_gpoint_n++;
        sc2_gpoints[h] = (sc2GPoint_t){ x, y };
        return jass_pushlighthandle(j, (HANDLE)(uintptr_t)h, "point");
    }
    return jass_pushnullhandle(j, "point");
}

static FLOAT sc2_point_x(LPJASS j, int idx) {
    LONG h = (LONG)(uintptr_t)jass_checkhandle(j, idx, "point");
    return (h > 0 && h < sc2_gpoint_n) ? sc2_gpoints[h].x : 0.0f;
}
static FLOAT sc2_point_y(LPJASS j, int idx) {
    LONG h = (LONG)(uintptr_t)jass_checkhandle(j, idx, "point");
    return (h > 0 && h < sc2_gpoint_n) ? sc2_gpoints[h].y : 0.0f;
}

static DWORD sc2_PointGetX(LPJASS j) { return jass_pushnumber(j, sc2_point_x(j, 1)); }
static DWORD sc2_PointGetY(LPJASS j) { return jass_pushnumber(j, sc2_point_y(j, 1)); }
static DWORD sc2_PointGetHeight(LPJASS j)        { return jass_pushnumber(j, 0.0f); }
static DWORD sc2_PointGetFacing(LPJASS j)        { return jass_pushnumber(j, 0.0f); }

/* PointWithOffset: returns a new point offset from the base. */
static DWORD sc2_PointWithOffset(LPJASS j) {
    FLOAT bx = sc2_point_x(j, 1), by = sc2_point_y(j, 1);
    FLOAT dx = jass_checknumber(j, 2), dy = jass_checknumber(j, 3);
    if (sc2_gpoint_n < MAX_GALAXY_POINTS) {
        LONG h = sc2_gpoint_n++;
        sc2_gpoints[h] = (sc2GPoint_t){ bx + dx, by + dy };
        return jass_pushlighthandle(j, (HANDLE)(uintptr_t)h, "point");
    }
    return jass_pushnullhandle(j, "point");
}

/* PointWithOffsetPolar: returns a new point offset by (dist, angle_deg). */
static DWORD sc2_PointWithOffsetPolar(LPJASS j) {
    FLOAT bx = sc2_point_x(j, 1), by = sc2_point_y(j, 1);
    FLOAT dist = jass_checknumber(j, 2);
    FLOAT ang  = jass_checknumber(j, 3) * 3.14159265f / 180.0f; /* degrees → radians */
    if (sc2_gpoint_n < MAX_GALAXY_POINTS) {
        LONG h = sc2_gpoint_n++;
        sc2_gpoints[h] = (sc2GPoint_t){ bx + dist * cosf(ang), by + dist * sinf(ang) };
        return jass_pushlighthandle(j, (HANDLE)(uintptr_t)h, "point");
    }
    return jass_pushnullhandle(j, "point");
}

static DWORD sc2_PointReflect(LPJASS j)          { return jass_pushnullhandle(j, "point"); }
static DWORD sc2_PointPathingCliffLevel(LPJASS j){ return jass_pushinteger(j, 0); }

static DWORD sc2_AngleBetweenPoints(LPJASS j) {
    FLOAT ax = sc2_point_x(j, 1), ay = sc2_point_y(j, 1);
    FLOAT bx = sc2_point_x(j, 2), by = sc2_point_y(j, 2);
    return jass_pushnumber(j, atan2f(by - ay, bx - ax) * 180.0f / 3.14159265f);
}

static DWORD sc2_DistanceBetweenPoints(LPJASS j) {
    FLOAT ax = sc2_point_x(j, 1), ay = sc2_point_y(j, 1);
    FLOAT bx = sc2_point_x(j, 2), by = sc2_point_y(j, 2);
    FLOAT dx = bx - ax, dy = by - ay;
    return jass_pushnumber(j, sqrtf(dx*dx + dy*dy));
}

static DWORD sc2_RegionEmpty(LPJASS j)           { return jass_pushnullhandle(j, "region"); }
static DWORD sc2_RegionEntireMap(LPJASS j)       { return jass_pushnullhandle(j, "region"); }
static DWORD sc2_RegionPlayableMap(LPJASS j)     { return jass_pushnullhandle(j, "region"); }
static DWORD sc2_RegionPlayableMapSet(LPJASS j)  { (void)j; return jass_pushnull(j); }
static DWORD sc2_RegionRect(LPJASS j)            { return jass_pushnullhandle(j, "region"); }
static DWORD sc2_RegionCircle(LPJASS j)          { return jass_pushnullhandle(j, "region"); }
static DWORD sc2_RegionAddCircle(LPJASS j)       { (void)j; return jass_pushnull(j); }
static DWORD sc2_RegionAddRect(LPJASS j)         { (void)j; return jass_pushnull(j); }
static DWORD sc2_RegionAddRegion(LPJASS j)       { (void)j; return jass_pushnull(j); }
static DWORD sc2_RegionAttachToUnit(LPJASS j)    { (void)j; return jass_pushnull(j); }
static DWORD sc2_RegionContainsPoint(LPJASS j)   { return jass_pushboolean(j, false); }
static DWORD sc2_RegionFromId(LPJASS j)          { return jass_pushnullhandle(j, "region"); }
static DWORD sc2_RegionGetAttachUnit(LPJASS j)   { return jass_pushnullhandle(j, "unit"); }
static DWORD sc2_RegionGetBoundsMax(LPJASS j)    { return jass_pushnullhandle(j, "point"); }
static DWORD sc2_RegionGetBoundsMin(LPJASS j)    { return jass_pushnullhandle(j, "point"); }
static DWORD sc2_RegionGetCenter(LPJASS j)       { return jass_pushnullhandle(j, "point"); }
static DWORD sc2_RegionGetOffset(LPJASS j)       { return jass_pushnullhandle(j, "point"); }
static DWORD sc2_RegionRandomPoint(LPJASS j)     { return jass_pushnullhandle(j, "point"); }
static DWORD sc2_RegionSetCenter(LPJASS j)       { (void)j; return jass_pushnull(j); }

static DWORD sc2_VisRevealArea(LPJASS j)         { (void)j; return jass_pushnull(j); }
static DWORD sc2_VisExploreArea(LPJASS j)        { (void)j; return jass_pushnull(j); }
static DWORD sc2_VisRevealerCreate(LPJASS j)     { return jass_pushnullhandle(j, "revealer"); }
static DWORD sc2_VisRevealerLastCreated(LPJASS j){ return jass_pushnullhandle(j, "revealer"); }
static DWORD sc2_VisRevealerDestroy(LPJASS j)    { (void)j; return jass_pushnull(j); }

/* -------------------------------------------------------------------------
 * Player / PlayerGroup
 * ------------------------------------------------------------------------- */
static DWORD sc2_PlayerGroupAll(LPJASS j)            { return jass_pushnullhandle(j, "playergroup"); }
static DWORD sc2_PlayerGroupActive(LPJASS j)         { return jass_pushnullhandle(j, "playergroup"); }
static DWORD sc2_PlayerGroupAdd(LPJASS j)            { (void)j; return jass_pushnull(j); }
static DWORD sc2_PlayerGroupClear(LPJASS j)          { return jass_pushnullhandle(j, "playergroup"); }
static DWORD sc2_PlayerGroupCopy(LPJASS j)           { return jass_pushnullhandle(j, "playergroup"); }
/* PlayerGroupCount / PlayerGroupPlayer: singleplayer stubs — exactly 1 human player. */
static DWORD sc2_PlayerGroupCount(LPJASS j) {
    (void)jass_checkhandle(j, 1, "playergroup");
    return jass_pushinteger(j, 1);
}
static DWORD sc2_PlayerGroupEmpty(LPJASS j)          { return jass_pushnullhandle(j, "playergroup"); }
static DWORD sc2_PlayerGroupHasPlayer(LPJASS j)      { return jass_pushboolean(j, false); }
static DWORD sc2_PlayerGroupPlayer(LPJASS j) {
    (void)jass_checkhandle(j, 1, "playergroup");
    (void)jass_checkinteger(j, 2);
    return jass_pushinteger(j, 1);
}
static DWORD sc2_PlayerGroupLoopBegin(LPJASS j)      { (void)j; return jass_pushnull(j); }
static DWORD sc2_PlayerGroupLoopDone(LPJASS j)       { return jass_pushboolean(j, true); }
static DWORD sc2_PlayerGroupLoopEnd(LPJASS j)        { (void)j; return jass_pushnull(j); }
static DWORD sc2_PlayerGroupLoopStep(LPJASS j)       { (void)j; return jass_pushnull(j); }
static DWORD sc2_PlayerGroupRemove(LPJASS j)         { (void)j; return jass_pushnull(j); }
static DWORD sc2_PlayerGroupSingle(LPJASS j)         { return jass_pushnullhandle(j, "playergroup"); }
static DWORD sc2_PlayerGroupAlliance(LPJASS j)       { return jass_pushnullhandle(j, "playergroup"); }
static DWORD sc2_PlayerGetState(LPJASS j)            { return jass_pushinteger(j, 0); }
static DWORD sc2_PlayerSetState(LPJASS j)            { (void)j; return jass_pushnull(j); }
static DWORD sc2_PlayerGetAlliance(LPJASS j)         { return jass_pushboolean(j, false); }
static DWORD sc2_PlayerSetAlliance(LPJASS j)         { (void)j; return jass_pushnull(j); }
static DWORD sc2_PlayerDifficulty(LPJASS j)          { return jass_pushinteger(j, 0); }
static DWORD sc2_PlayerModifyPropertyInt(LPJASS j)   { (void)j; return jass_pushnull(j); }
static DWORD sc2_PlayerAddChargeRegen(LPJASS j)      { (void)j; return jass_pushnull(j); }
static DWORD sc2_PlayerAddChargeUsed(LPJASS j)       { (void)j; return jass_pushnull(j); }
static DWORD sc2_PlayerAddCooldown(LPJASS j)         { (void)j; return jass_pushnull(j); }
static DWORD sc2_PlayerGetChargeRegen(LPJASS j)      { return jass_pushnumber(j, 0.0f); }
static DWORD sc2_PlayerGetChargeUsed(LPJASS j)       { return jass_pushnumber(j, 0.0f); }
static DWORD sc2_PlayerGetCooldown(LPJASS j)         { return jass_pushnumber(j, 0.0f); }
static DWORD sc2_PlayerScoreValueEnable(LPJASS j)    { (void)j; return jass_pushnull(j); }
static DWORD sc2_PlayerScoreValueEnableAll(LPJASS j) { (void)j; return jass_pushnull(j); }
static DWORD sc2_PlayerScoreValueGetAsFixed(LPJASS j){ return jass_pushnumber(j, 0.0f); }
static DWORD sc2_PlayerScoreValueGetAsInt(LPJASS j)  { return jass_pushinteger(j, 0); }
static DWORD sc2_PlayerScoreValueSetFromFixed(LPJASS j){ (void)j; return jass_pushnull(j); }
static DWORD sc2_PlayerScoreValueSetFromInt(LPJASS j){ (void)j; return jass_pushnull(j); }
static DWORD sc2_PlayerPauseAllCharges(LPJASS j)     { (void)j; return jass_pushnull(j); }
static DWORD sc2_PlayerPauseAllCooldowns(LPJASS j)   { (void)j; return jass_pushnull(j); }
static DWORD sc2_PlayerBeaconAlert(LPJASS j)         { (void)j; return jass_pushnull(j); }
static DWORD sc2_PlayerBeaconClearTarget(LPJASS j)   { (void)j; return jass_pushnull(j); }
static DWORD sc2_PlayerBeaconGetTargetPoint(LPJASS j){ return jass_pushinteger(j, 0); }
static DWORD sc2_PlayerBeaconGetTargetUnit(LPJASS j) { return jass_pushinteger(j, 0); }
static DWORD sc2_PlayerBeaconIsAutoCast(LPJASS j)    { return jass_pushboolean(j, false); }
static DWORD sc2_PlayerBeaconIsFromUser(LPJASS j)    { return jass_pushboolean(j, false); }
static DWORD sc2_PlayerBeaconIsSet(LPJASS j)         { return jass_pushboolean(j, false); }
static DWORD sc2_PlayerBeaconSetAutoCast(LPJASS j)   { (void)j; return jass_pushnull(j); }
static DWORD sc2_PlayerBeaconSetTargetPoint(LPJASS j){ (void)j; return jass_pushnull(j); }
static DWORD sc2_PlayerBeaconSetTargetUnit(LPJASS j) { (void)j; return jass_pushnull(j); }
static DWORD sc2_PlayerCreateEffectPoint(LPJASS j)   { (void)j; return jass_pushnull(j); }
static DWORD sc2_PlayerCreateEffectUnit(LPJASS j)    { (void)j; return jass_pushnull(j); }
static DWORD sc2_PlayerValidateEffectPoint(LPJASS j) { return jass_pushboolean(j, false); }
static DWORD sc2_PlayerValidateEffectUnit(LPJASS j)  { return jass_pushboolean(j, false); }

/* -------------------------------------------------------------------------
 * Sound
 * ------------------------------------------------------------------------- */
static DWORD sc2_SoundLink(LPJASS j) {
    LPCSTR id = jass_checkstring(j, 1);
    LONG asset = jass_checkinteger(j, 2), h;
    if (!id || !*id || sc2_gsound_n >= MAX_GALAXY_SOUNDS)
        return jass_pushnullhandle(j, "soundlink");
    h = sc2_gsound_n++;
    snprintf(sc2_gsounds[h].id, sizeof(sc2_gsounds[h].id), "%s", id);
    sc2_gsounds[h].asset = asset;
    return jass_pushlighthandle(j, (HANDLE)(uintptr_t)h, "soundlink");
}
static DWORD sc2_SoundLinkAsset(LPJASS j)     { return jass_pushnullhandle(j, "soundlink"); }
static DWORD sc2_SoundLinkId(LPJASS j)        { return jass_pushnullhandle(j, "soundlink"); }
static DWORD sc2_SoundPlay(LPJASS j)          { return jass_pushnullhandle(j, "sound"); }
static DWORD sc2_SoundPlayAtPoint(LPJASS j)   { return jass_pushnullhandle(j, "sound"); }
static DWORD sc2_SoundPlayOnUnit(LPJASS j)    { return jass_pushnullhandle(j, "sound"); }
static DWORD sc2_SoundPlayScene(LPJASS j)     { return jass_pushnullhandle(j, "sound"); }
static DWORD sc2_SoundPlaySceneFile(LPJASS j) { return jass_pushnullhandle(j, "sound"); }
static DWORD sc2_SoundStop(LPJASS j)          { (void)j; return jass_pushnull(j); }
static DWORD sc2_SoundWait(LPJASS j) {
    FLOAT secs = jass_checknumber(j, 2);
    if (secs > 0.0f) jass_sleep(j, (DWORD)(secs * 1000.0f));
    return jass_pushnull(j);
}
static FLOAT sc2_sound_length(LPJASS j, int arg) {
    LONG h = (LONG)(uintptr_t)jass_checkhandle(j, arg, "soundlink");
    return h > 0 && h < sc2_gsound_n && sc2_galaxy_sound_length
        ? sc2_galaxy_sound_length(sc2_gsounds[h].id, sc2_gsounds[h].asset) : 0.0f;
}
static DWORD sc2_SoundLengthSync(LPJASS j)    { return jass_pushnumber(j, sc2_sound_length(j, 1)); }
static DWORD sc2_SoundtrackPlay(LPJASS j)     { (void)j; return jass_pushnull(j); }
static DWORD sc2_SoundtrackPause(LPJASS j)    { (void)j; return jass_pushnull(j); }
static DWORD sc2_SoundtrackDefault(LPJASS j)  { (void)j; return jass_pushnull(j); }

/* -------------------------------------------------------------------------
 * Math
 * ------------------------------------------------------------------------- */
static DWORD sc2_AbsF(LPJASS j)  { FLOAT x = jass_checknumber(j, 1); return jass_pushnumber(j, x < 0 ? -x : x); }
static DWORD sc2_MaxF(LPJASS j)  { FLOAT a = jass_checknumber(j,1), b = jass_checknumber(j,2); return jass_pushnumber(j, a > b ? a : b); }
static DWORD sc2_MinF(LPJASS j)  { FLOAT a = jass_checknumber(j,1), b = jass_checknumber(j,2); return jass_pushnumber(j, a < b ? a : b); }
static DWORD sc2_ModF(LPJASS j)  { FLOAT a = jass_checknumber(j,1), b = jass_checknumber(j,2); return jass_pushnumber(j, b ? fmodf(a, b) : 0.0f); }
static DWORD sc2_Pow(LPJASS j)   { return jass_pushnumber(j, powf(jass_checknumber(j,1), jass_checknumber(j,2))); }
static DWORD sc2_SquareRoot(LPJASS j) { FLOAT x = jass_checknumber(j,1); return jass_pushnumber(j, x > 0.0f ? sqrtf(x) : 0.0f); }
static DWORD sc2_Sin(LPJASS j)   { return jass_pushnumber(j, sinf(jass_checknumber(j,1))); }
static DWORD sc2_Cos(LPJASS j)   { return jass_pushnumber(j, cosf(jass_checknumber(j,1))); }
static DWORD sc2_Tan(LPJASS j)   { return jass_pushnumber(j, tanf(jass_checknumber(j,1))); }
static DWORD sc2_ASin(LPJASS j)  { return jass_pushnumber(j, asinf(jass_checknumber(j,1))); }
static DWORD sc2_ACos(LPJASS j)  { return jass_pushnumber(j, acosf(jass_checknumber(j,1))); }
static DWORD sc2_ATan(LPJASS j)  { return jass_pushnumber(j, atanf(jass_checknumber(j,1))); }
static DWORD sc2_ATan2(LPJASS j) { return jass_pushnumber(j, atan2f(jass_checknumber(j,1), jass_checknumber(j,2))); }
static DWORD sc2_RandomFixed(LPJASS j) { (void)j; return jass_pushnumber(j, 0.0f); }
static DWORD sc2_RandomInt(LPJASS j)   { return jass_pushinteger(j, jass_checkinteger(j, 1)); }

/* -------------------------------------------------------------------------
 * String / Text / Color / Game / AI / Order / Catalog / Objectives / etc.
 * ------------------------------------------------------------------------- */
static DWORD sc2_StringExternal(LPJASS j)     { LPCSTR s = jass_checkstring(j,1); return jass_pushstring(j, s ? s : ""); }
/* Galaxy uses a null result to terminate whitespace-delimited word iteration. */
static DWORD sc2_StringWord(LPJASS j) {
    LPCSTR str = jass_checkstring(j, 1), word;
    LONG index = jass_checkinteger(j, 2);
    if (!str || index < 1) return jass_pushnull(j);
    while (*str) {
        while (*str && isspace((unsigned char)*str)) str++;
        if (!*str) break;
        word = str;
        while (*str && !isspace((unsigned char)*str)) str++;
        if (!--index) return jass_pushstringlen(j, word, (DWORD)(str - word));
    }
    return jass_pushnull(j);
}
static DWORD sc2_IntToText(LPJASS j)          { return jass_pushinteger(j, 0); }
static DWORD sc2_Color(LPJASS j)              { (void)j; return jass_pushinteger(j, 0); }
static DWORD sc2_ColorWithAlpha(LPJASS j)     { (void)j; return jass_pushinteger(j, 0); }
static DWORD sc2_GameTimeOfDayPause(LPJASS j) { (void)j; return jass_pushnull(j); }
static DWORD sc2_GameTimeOfDaySet(LPJASS j)   { (void)j; return jass_pushnull(j); }
static DWORD sc2_GameSetBackground(LPJASS j)  { (void)j; return jass_pushnull(j); }
static DWORD sc2_GameSetLighting(LPJASS j)    { (void)j; return jass_pushnull(j); }
static DWORD sc2_DifficultyEnabled(LPJASS j)  { return jass_pushboolean(j, false); }
static DWORD sc2_DifficultyName(LPJASS j)     { return jass_pushstring(j, ""); }
static DWORD sc2_DifficultyNameCampaign(LPJASS j) { return jass_pushstring(j, ""); }
static DWORD sc2_AITimePause(LPJASS j)        { (void)j; return jass_pushnull(j); }
static DWORD sc2_AbilityClass(LPJASS j)       { return jass_pushinteger(j, 0); }
static DWORD sc2_AbilityCommand(LPJASS j) {
    LPCSTR name = jass_checkstring(j, 1);
    LONG   cmd  = jass_checkinteger(j, 2);
    if (sc2_gabilcmd_n < MAX_GALAXY_ABILCMDS) {
        LONG h = sc2_gabilcmd_n++;
        snprintf(sc2_gabilcmds[h].ability, sizeof(sc2_gabilcmds[h].ability),
                 "%s", name ? name : "");
        sc2_gabilcmds[h].cmd_idx = cmd;
        return jass_pushinteger(j, h);
    }
    fprintf(stderr, "AbilityCommand: table full (%d entries) — '%s' lost\n",
            MAX_GALAXY_ABILCMDS, name ? name : "");
    return jass_pushinteger(j, 0);
}
static DWORD sc2_AbilityCommandGetAbility(LPJASS j){ return jass_pushstring(j, ""); }
static DWORD sc2_AbilityCommandGetCommand(LPJASS j){ return jass_pushinteger(j, 0); }
static DWORD sc2_AbilityCommandGetAction(LPJASS j) { return jass_pushinteger(j, 0); }
static DWORD sc2_Order(LPJASS j)              { return jass_pushnullhandle(j, "order"); }
static DWORD sc2_OrderTargetingPoint(LPJASS j) {
    LONG abilcmd_h = jass_checkinteger(j, 1);
    LONG pt_h      = (LONG)(uintptr_t)jass_checkhandle(j, 2, "point");
    if (sc2_gorder_n < MAX_GALAXY_ORDERS) {
        LONG h = sc2_gorder_n++;
        sc2_gorders[h] = (sc2GOrder_t){ abilcmd_h, pt_h };
        return jass_pushlighthandle(j, (HANDLE)(uintptr_t)h, "order");
    }
    fprintf(stderr, "OrderTargetingPoint: table full (%d entries)\n", MAX_GALAXY_ORDERS);
    return jass_pushnullhandle(j, "order");
}
static DWORD sc2_OrderTargetingUnit(LPJASS j) { return jass_pushnullhandle(j, "order"); }
static DWORD sc2_OrderSetPlayer(LPJASS j)     { (void)j; return jass_pushnull(j); }
static DWORD sc2_UnitOrderIsValid(LPJASS j)   { return jass_pushboolean(j, false); }
static DWORD sc2_CatalogEntryClass(LPJASS j)  { return jass_pushinteger(j, 0); }
static DWORD sc2_CatalogEntryCount(LPJASS j)  { return jass_pushinteger(j, 0); }
static DWORD sc2_CatalogEntryGet(LPJASS j)    { return jass_pushstring(j, ""); }
static DWORD sc2_CatalogEntryIsValid(LPJASS j){ return jass_pushboolean(j, false); }
static DWORD sc2_CatalogEntryParent(LPJASS j) { return jass_pushstring(j, ""); }
static DWORD sc2_CatalogEntryScope(LPJASS j)  { return jass_pushstring(j, ""); }
static DWORD sc2_CatalogFieldCount(LPJASS j)  { return jass_pushinteger(j, 0); }
static DWORD sc2_CatalogFieldGet(LPJASS j)    { return jass_pushstring(j, ""); }
static DWORD sc2_CatalogFieldIsArray(LPJASS j){ return jass_pushboolean(j, false); }
static DWORD sc2_CatalogFieldIsScope(LPJASS j){ return jass_pushboolean(j, false); }
static DWORD sc2_CatalogFieldType(LPJASS j)   { return jass_pushstring(j, ""); }
static DWORD sc2_CatalogFieldValueCount(LPJASS j){ return jass_pushinteger(j, 0); }
static DWORD sc2_CatalogFieldValueGet(LPJASS j){ return jass_pushstring(j, ""); }
static DWORD sc2_CatalogFieldValueSet(LPJASS j){ return jass_pushboolean(j, false); }
static DWORD sc2_ObjectiveCreate3(LPJASS j)   { return jass_pushinteger(j, 0); }
static DWORD sc2_ObjectiveLastCreated(LPJASS j){ return jass_pushinteger(j, 0); }
static DWORD sc2_ObjectiveSetName(LPJASS j)   { (void)j; return jass_pushnull(j); }
static DWORD sc2_ObjectiveSetState(LPJASS j)  { (void)j; return jass_pushnull(j); }
static DWORD sc2_ObjectiveGetState(LPJASS j)  { return jass_pushinteger(j, 0); }
static DWORD sc2_PingLastCreated(LPJASS j)    { return jass_pushinteger(j, 0); }
static DWORD sc2_PingDestroy(LPJASS j)        { (void)j; return jass_pushnull(j); }
static DWORD sc2_PingSetScale(LPJASS j)       { (void)j; return jass_pushnull(j); }
static DWORD sc2_PingSetTooltip(LPJASS j)     { (void)j; return jass_pushnull(j); }
static DWORD sc2_MinimapPing(LPJASS j)        { (void)j; return jass_pushnull(j); }
static DWORD sc2_UISetMode(LPJASS j)          { (void)j; return jass_pushnull(j); }
static DWORD sc2_UIAlertPoint(LPJASS j)       { (void)j; return jass_pushnull(j); }
static DWORD sc2_UIAlertUnit(LPJASS j)        { (void)j; return jass_pushnull(j); }
static DWORD sc2_UISetFrameVisible(LPJASS j)  { (void)j; return jass_pushnull(j); }
static DWORD sc2_HelpPanelAddTip(LPJASS j)    { (void)j; return jass_pushnull(j); }
static DWORD sc2_HelpPanelDisplayPage(LPJASS j){ (void)j; return jass_pushnull(j); }
static DWORD sc2_HelpPanelEnableTechTreeButton(LPJASS j){ (void)j; return jass_pushnull(j); }
static DWORD sc2_TransmissionSource(LPJASS j)        { return jass_pushnullhandle(j, "transmissionsource"); }
static DWORD sc2_TransmissionSourceFromModel(LPJASS j){ return jass_pushnullhandle(j, "transmissionsource"); }
static DWORD sc2_TransmissionSourceFromUnit(LPJASS j) { return jass_pushnullhandle(j, "transmissionsource"); }
/* TransmissionSend: send a transmission to players; sleep if waitUntilDone and duration > 0. */
static DWORD sc2_TransmissionSend(LPJASS j) {
    /* args: playergroup, source, camerainfo, string anim, soundlink, text speaker,
     *       text msg, fixed duration, int durationType, bool waitUntilDone */
    FLOAT sound_dur  = sc2_sound_length(j, 5);
    FLOAT dur        = jass_checknumber(j, 8);
    LONG  dur_type   = jass_checkinteger(j, 9);
    BOOL  wait_done  = jass_checkboolean(j, 10);
    LONG  sound_h    = (LONG)(uintptr_t)jass_checkhandle(j, 5, "soundlink");
    /* Native SC2 derives transmission time from the linked asset before applying the requested modifier. */
    if (dur_type == 0) dur = sound_dur;
    else if (dur_type == 1) dur += sound_dur;
    else if (dur_type == 2) dur = MAX(sound_dur - dur, 0.0f);
    if (sound_h > 0 && sound_h < sc2_gsound_n && sc2_galaxy_on_sound)
        sc2_galaxy_on_sound(sc2_gsounds[sound_h].id, sc2_gsounds[sound_h].asset);
    fprintf(stderr, "TransmissionSend: sound=%s asset=%ld duration=%.2f wait=%d\n",
            sound_h > 0 && sound_h < sc2_gsound_n ? sc2_gsounds[sound_h].id : "(null)",
            sound_h > 0 && sound_h < sc2_gsound_n ? (long)sc2_gsounds[sound_h].asset : -1L, dur, wait_done);
    if (wait_done && dur > 0.0f)
        jass_sleep(j, (DWORD)(dur * 1000.0f));
    return jass_pushnullhandle(j, "sound");
}
static DWORD sc2_TransmissionLastSent(LPJASS j)      { return jass_pushinteger(j, 0); }
static DWORD sc2_TransmissionClear(LPJASS j)         { (void)j; return jass_pushnull(j); }
static DWORD sc2_TransmissionClearAll(LPJASS j)      { (void)j; return jass_pushnull(j); }
static DWORD sc2_TransmissionWait(LPJASS j) {
    FLOAT secs = jass_checknumber(j, 2);
    if (secs > 0.0f) jass_sleep(j, (DWORD)(secs * 1000.0f));
    return jass_pushnull(j);
}
static DWORD sc2_TechTreeUpgradeAddLevel(LPJASS j)          { (void)j; return jass_pushnull(j); }
static DWORD sc2_VictoryPanelAddAchievement(LPJASS j)       { (void)j; return jass_pushnull(j); }
static DWORD sc2_VictoryPanelAddCustomStatisticLine(LPJASS j){ (void)j; return jass_pushnull(j); }
static DWORD sc2_VictoryPanelAddTrackedStatistic(LPJASS j)  { (void)j; return jass_pushnull(j); }
static DWORD sc2_IntLoopBegin(LPJASS j)  { (void)j; return jass_pushnull(j); }
static DWORD sc2_IntLoopDone(LPJASS j)   { return jass_pushboolean(j, true); }
static DWORD sc2_IntLoopEnd(LPJASS j)    { (void)j; return jass_pushnull(j); }
static DWORD sc2_IntLoopStep(LPJASS j)   { (void)j; return jass_pushnull(j); }
static DWORD sc2_PreloadAsset(LPJASS j)  { (void)j; return jass_pushnull(j); }
static DWORD sc2_PreloadImage(LPJASS j)  { (void)j; return jass_pushnull(j); }
static DWORD sc2_PreloadModel(LPJASS j)  { (void)j; return jass_pushnull(j); }
static DWORD sc2_PreloadMovie(LPJASS j)  { (void)j; return jass_pushnull(j); }
static DWORD sc2_PreloadObject(LPJASS j) { (void)j; return jass_pushnull(j); }
static DWORD sc2_PreloadScene(LPJASS j)  { (void)j; return jass_pushnull(j); }
static DWORD sc2_PreloadScript(LPJASS j) { (void)j; return jass_pushnull(j); }
static DWORD sc2_PreloadSound(LPJASS j)  { (void)j; return jass_pushnull(j); }

/* -------------------------------------------------------------------------
 * Native table
 * ------------------------------------------------------------------------- */
static JASSMODULE sc2_galaxy_natives[] = {
    { "ACos",                                sc2_ACos },
    { "ASin",                                sc2_ASin },
    { "ATan",                                sc2_ATan },
    { "ATan2",                               sc2_ATan2 },
    { "AbilityClass",                        sc2_AbilityClass },
    { "AbilityCommand",                      sc2_AbilityCommand },
    { "AbilityCommandGetAbility",            sc2_AbilityCommandGetAbility },
    { "AbilityCommandGetCommand",            sc2_AbilityCommandGetCommand },
    { "AbilityCommandGetAction",             sc2_AbilityCommandGetAction },
    { "AbsF",                                sc2_AbsF },
    { "AchievementAward",                    galaxy_stub },
    { "AchievementErase",                    galaxy_stub },
    { "AchievementPanelSetCategory",         galaxy_stub },
    { "AchievementPanelSetVisible",          galaxy_stub },
    { "AchievementPercentText",              galaxy_stub },
    { "AchievementTermQuantitySet",          galaxy_stub },
    { "AchievementsDisable",                 galaxy_stub },
    { "AITimePause",                         sc2_AITimePause },
    { "AngleBetweenPoints",                  sc2_AngleBetweenPoints },
    { "CameraApplyInfo",                     sc2_CameraApplyInfo },
    { "CameraGetTarget",                     sc2_CameraGetTarget },
    { "CameraInfoDefault",                   sc2_CameraInfoDefault },
    { "CameraInfoFromId",                    sc2_CameraInfoFromId },
    { "CameraLockInput",                     sc2_CameraLockInput },
    { "CameraPan",                           sc2_CameraPan },
    { "CameraRestore",                       sc2_CameraRestore },
    { "CameraSave",                          sc2_CameraSave },
    { "CameraShakeStart",                    sc2_CameraShakeStart },
    { "CampaignMode",                        galaxy_stub }, /* void CampaignMode(playergroup, bool); TRaynor01 Init calls it */
    { "CatalogEntryClass",                   sc2_CatalogEntryClass },
    { "CatalogEntryCount",                   sc2_CatalogEntryCount },
    { "CatalogEntryGet",                     sc2_CatalogEntryGet },
    { "CatalogEntryIsValid",                 sc2_CatalogEntryIsValid },
    { "CatalogEntryParent",                  sc2_CatalogEntryParent },
    { "CatalogEntryScope",                   sc2_CatalogEntryScope },
    { "CatalogFieldCount",                   sc2_CatalogFieldCount },
    { "CatalogFieldGet",                     sc2_CatalogFieldGet },
    { "CatalogFieldIsArray",                 sc2_CatalogFieldIsArray },
    { "CatalogFieldIsScope",                 sc2_CatalogFieldIsScope },
    { "CatalogFieldType",                    sc2_CatalogFieldType },
    { "CatalogFieldValueCount",              sc2_CatalogFieldValueCount },
    { "CatalogFieldValueGet",                sc2_CatalogFieldValueGet },
    { "CatalogFieldValueSet",                sc2_CatalogFieldValueSet },
    { "CinematicDataRun",                    galaxy_stub },
    { "CinematicDataStop",                   galaxy_stub },
    { "CinematicFade",                       sc2_CinematicFade },
    { "CinematicMode",                       sc2_CinematicMode },
    { "CinematicOverlay",                    sc2_CinematicOverlay },
    { "Color",                               sc2_Color },
    { "ColorWithAlpha",                      sc2_ColorWithAlpha },
    { "Cos",                                 sc2_Cos },
    { "DifficultyEnabled",                   sc2_DifficultyEnabled },
    { "DifficultyName",                      sc2_DifficultyName },
    { "DifficultyNameCampaign",              sc2_DifficultyNameCampaign },
    { "DistanceBetweenPoints",               sc2_DistanceBetweenPoints },
    { "EventUnit",                           sc2_EventUnit },
    { "EventUnitCargo",                      sc2_EventUnitCargo },
    { "EventUnitTarget",                     sc2_EventUnitTarget },
    { "GameSetBackground",                   sc2_GameSetBackground },
    { "GameSetLighting",                     sc2_GameSetLighting },
    { "GameTimeOfDayPause",                  sc2_GameTimeOfDayPause },
    { "GameTimeOfDaySet",                    sc2_GameTimeOfDaySet },
    { "HelpPanelAddTip",                     sc2_HelpPanelAddTip },
    { "HelpPanelDisplayPage",                sc2_HelpPanelDisplayPage },
    { "HelpPanelEnableTechTreeButton",       sc2_HelpPanelEnableTechTreeButton },
    { "IntLoopBegin",                        sc2_IntLoopBegin },
    { "IntLoopDone",                         sc2_IntLoopDone },
    { "IntLoopEnd",                          sc2_IntLoopEnd },
    { "IntLoopStep",                         sc2_IntLoopStep },
    { "IntToText",                           sc2_IntToText },
    { "MaxF",                                sc2_MaxF },
    { "MinF",                                sc2_MinF },
    { "MinimapPing",                         sc2_MinimapPing },
    { "ModF",                                sc2_ModF },
    { "ObjectiveCreate3",                    sc2_ObjectiveCreate3 },
    { "ObjectiveGetState",                   sc2_ObjectiveGetState },
    { "ObjectiveLastCreated",                sc2_ObjectiveLastCreated },
    { "ObjectiveSetName",                    sc2_ObjectiveSetName },
    { "ObjectiveSetState",                   sc2_ObjectiveSetState },
    { "Order",                               sc2_Order },
    { "OrderSetPlayer",                      sc2_OrderSetPlayer },
    { "OrderTargetingPoint",                 sc2_OrderTargetingPoint },
    { "OrderTargetingUnit",                  sc2_OrderTargetingUnit },
    { "UnitOrderIsValid",                    sc2_UnitOrderIsValid },
    { "PingDestroy",                         sc2_PingDestroy },
    { "PingLastCreated",                     sc2_PingLastCreated },
    { "PingSetScale",                        sc2_PingSetScale },
    { "PingSetTooltip",                      sc2_PingSetTooltip },
    { "PlayerAddChargeRegen",                sc2_PlayerAddChargeRegen },
    { "PlayerAddChargeUsed",                 sc2_PlayerAddChargeUsed },
    { "PlayerAddCooldown",                   sc2_PlayerAddCooldown },
    { "PlayerBeaconAlert",                   sc2_PlayerBeaconAlert },
    { "PlayerBeaconClearTarget",             sc2_PlayerBeaconClearTarget },
    { "PlayerBeaconGetTargetPoint",          sc2_PlayerBeaconGetTargetPoint },
    { "PlayerBeaconGetTargetUnit",           sc2_PlayerBeaconGetTargetUnit },
    { "PlayerBeaconIsAutoCast",              sc2_PlayerBeaconIsAutoCast },
    { "PlayerBeaconIsFromUser",              sc2_PlayerBeaconIsFromUser },
    { "PlayerBeaconIsSet",                   sc2_PlayerBeaconIsSet },
    { "PlayerBeaconSetAutoCast",             sc2_PlayerBeaconSetAutoCast },
    { "PlayerBeaconSetTargetPoint",          sc2_PlayerBeaconSetTargetPoint },
    { "PlayerBeaconSetTargetUnit",           sc2_PlayerBeaconSetTargetUnit },
    { "PlayerCreateEffectPoint",             sc2_PlayerCreateEffectPoint },
    { "PlayerCreateEffectUnit",              sc2_PlayerCreateEffectUnit },
    { "PlayerDifficulty",                    sc2_PlayerDifficulty },
    { "PlayerGetAlliance",                   sc2_PlayerGetAlliance },
    { "PlayerGetChargeRegen",                sc2_PlayerGetChargeRegen },
    { "PlayerGetChargeUsed",                 sc2_PlayerGetChargeUsed },
    { "PlayerGetCooldown",                   sc2_PlayerGetCooldown },
    { "PlayerGetState",                      sc2_PlayerGetState },
    { "PlayerGroupActive",                   sc2_PlayerGroupActive },
    { "PlayerGroupAdd",                      sc2_PlayerGroupAdd },
    { "PlayerGroupAll",                      sc2_PlayerGroupAll },
    { "PlayerGroupAlliance",                 sc2_PlayerGroupAlliance },
    { "PlayerGroupClear",                    sc2_PlayerGroupClear },
    { "PlayerGroupCopy",                     sc2_PlayerGroupCopy },
    { "PlayerGroupCount",                    sc2_PlayerGroupCount },
    { "PlayerGroupEmpty",                    sc2_PlayerGroupEmpty },
    { "PlayerGroupHasPlayer",                sc2_PlayerGroupHasPlayer },
    { "PlayerGroupLoopBegin",                sc2_PlayerGroupLoopBegin },
    { "PlayerGroupLoopDone",                 sc2_PlayerGroupLoopDone },
    { "PlayerGroupLoopEnd",                  sc2_PlayerGroupLoopEnd },
    { "PlayerGroupLoopStep",                 sc2_PlayerGroupLoopStep },
    { "PlayerGroupPlayer",                   sc2_PlayerGroupPlayer },
    { "PlayerGroupRemove",                   sc2_PlayerGroupRemove },
    { "PlayerGroupSingle",                   sc2_PlayerGroupSingle },
    { "PlayerModifyPropertyInt",             sc2_PlayerModifyPropertyInt },
    { "PlayerPauseAllCharges",               sc2_PlayerPauseAllCharges },
    { "PlayerPauseAllCooldowns",             sc2_PlayerPauseAllCooldowns },
    { "PlayerScoreValueEnable",              sc2_PlayerScoreValueEnable },
    { "PlayerScoreValueEnableAll",           sc2_PlayerScoreValueEnableAll },
    { "PlayerScoreValueGetAsFixed",          sc2_PlayerScoreValueGetAsFixed },
    { "PlayerScoreValueGetAsInt",            sc2_PlayerScoreValueGetAsInt },
    { "PlayerScoreValueSetFromFixed",        sc2_PlayerScoreValueSetFromFixed },
    { "PlayerScoreValueSetFromInt",          sc2_PlayerScoreValueSetFromInt },
    { "PlayerSetAlliance",                   sc2_PlayerSetAlliance },
    { "PlayerSetState",                      sc2_PlayerSetState },
    { "PlayerValidateEffectPoint",           sc2_PlayerValidateEffectPoint },
    { "PlayerValidateEffectUnit",            sc2_PlayerValidateEffectUnit },
    { "Point",                               sc2_Point },
    { "PointFromId",                         sc2_PointFromId },
    { "PointGetFacing",                      sc2_PointGetFacing },
    { "PointGetHeight",                      sc2_PointGetHeight },
    { "PointGetX",                           sc2_PointGetX },
    { "PointGetY",                           sc2_PointGetY },
    { "PointPathingCliffLevel",              sc2_PointPathingCliffLevel },
    { "PointReflect",                        sc2_PointReflect },
    { "PointWithOffset",                     sc2_PointWithOffset },
    { "PointWithOffsetPolar",                sc2_PointWithOffsetPolar },
    { "Pow",                                 sc2_Pow },
    { "PreloadAsset",                        sc2_PreloadAsset },
    { "PreloadImage",                        sc2_PreloadImage },
    { "PreloadModel",                        sc2_PreloadModel },
    { "PreloadMovie",                        sc2_PreloadMovie },
    { "PreloadObject",                       sc2_PreloadObject },
    { "PreloadScene",                        sc2_PreloadScene },
    { "PreloadScript",                       sc2_PreloadScript },
    { "PreloadSound",                        sc2_PreloadSound },
    { "RandomFixed",                         sc2_RandomFixed },
    { "RandomInt",                           sc2_RandomInt },
    { "RegionAddCircle",                     sc2_RegionAddCircle },
    { "RegionAddRect",                       sc2_RegionAddRect },
    { "RegionAddRegion",                     sc2_RegionAddRegion },
    { "RegionAttachToUnit",                  sc2_RegionAttachToUnit },
    { "RegionCircle",                        sc2_RegionCircle },
    { "RegionContainsPoint",                 sc2_RegionContainsPoint },
    { "RegionEmpty",                         sc2_RegionEmpty },
    { "RegionEntireMap",                     sc2_RegionEntireMap },
    { "RegionFromId",                        sc2_RegionFromId },
    { "RegionGetAttachUnit",                 sc2_RegionGetAttachUnit },
    { "RegionGetBoundsMax",                  sc2_RegionGetBoundsMax },
    { "RegionGetBoundsMin",                  sc2_RegionGetBoundsMin },
    { "RegionGetCenter",                     sc2_RegionGetCenter },
    { "RegionGetOffset",                     sc2_RegionGetOffset },
    { "RegionPlayableMap",                   sc2_RegionPlayableMap },
    { "RegionPlayableMapSet",                sc2_RegionPlayableMapSet },
    { "RegionRandomPoint",                   sc2_RegionRandomPoint },
    { "RegionRect",                          sc2_RegionRect },
    { "RegionSetCenter",                     sc2_RegionSetCenter },
    { "Sin",                                 sc2_Sin },
    { "SoundLink",                           sc2_SoundLink },
    { "SoundLinkAsset",                      sc2_SoundLinkAsset },
    { "SoundLinkId",                         sc2_SoundLinkId },
    { "SoundLengthSync",                     sc2_SoundLengthSync },
    { "SoundPlay",                           sc2_SoundPlay },
    { "SoundPlayAtPoint",                    sc2_SoundPlayAtPoint },
    { "SoundPlayOnUnit",                     sc2_SoundPlayOnUnit },
    { "SoundPlayScene",                      sc2_SoundPlayScene },
    { "SoundPlaySceneFile",                  sc2_SoundPlaySceneFile },
    { "SoundStop",                           sc2_SoundStop },
    { "SoundWait",                           sc2_SoundWait },
    { "SoundtrackDefault",                   sc2_SoundtrackDefault },
    { "SoundtrackPause",                     sc2_SoundtrackPause },
    { "SoundtrackPlay",                      sc2_SoundtrackPlay },
    { "SquareRoot",                          sc2_SquareRoot },
    { "StringExternal",                      sc2_StringExternal },
    { "StringWord",                          sc2_StringWord },
    { "Tan",                                 sc2_Tan },
    { "TechTreeUpgradeAddLevel",             sc2_TechTreeUpgradeAddLevel },
    { "TransmissionClear",                   sc2_TransmissionClear },
    { "TransmissionClearAll",                sc2_TransmissionClearAll },
    { "TransmissionLastSent",                sc2_TransmissionLastSent },
    { "TransmissionSend",                    sc2_TransmissionSend },
    { "TransmissionSource",                  sc2_TransmissionSource },
    { "TransmissionSourceFromModel",         sc2_TransmissionSourceFromModel },
    { "TransmissionSourceFromUnit",          sc2_TransmissionSourceFromUnit },
    { "TransmissionWait",                    sc2_TransmissionWait },
    { "TriggerAddEventMapInit",              sc2_TriggerAddEventMapInit },
    { "TriggerAddEventPlayerAIWave",         sc2_TriggerAddEventPlayerAIWave },
    { "TriggerAddEventPlayerAllianceChange", sc2_TriggerAddEventPlayerAllianceChange },
    { "TriggerAddEventPlayerLeft",           sc2_TriggerAddEventPlayerLeft },
    { "TriggerAddEventPlayerPropChange",     sc2_TriggerAddEventPlayerPropChange },
    { "TriggerAddEventTimeElapsed",          sc2_TriggerAddEventTimeElapsed },
    { "TriggerAddEventTimePeriodic",         sc2_TriggerAddEventTimePeriodic },
    { "TriggerAddEventTimer",                sc2_TriggerAddEventTimer },
    { "TriggerAddEventUnitAttacked",         sc2_TriggerAddEventUnitAttacked },
    { "TriggerAddEventUnitCargo",            sc2_TriggerAddEventUnitCargo },
    { "TriggerAddEventUnitDamaged",          sc2_TriggerAddEventUnitDamaged },
    { "TriggerAddEventUnitDied",             sc2_TriggerAddEventUnitDied },
    { "TriggerAddEventUnitOrder",            sc2_TriggerAddEventUnitOrder },
    { "TriggerAddEventUnitRange",            sc2_TriggerAddEventUnitRange },
    { "TriggerAddEventUnitRangePoint",       sc2_TriggerAddEventUnitRangePoint },
    { "TriggerAddEventUnitRegion",           sc2_TriggerAddEventUnitRegion },
    { "TriggerCreate",                       sc2_TriggerCreate },
    { "TriggerEnable",                       sc2_TriggerEnable },
    { "TriggerExecute",                      sc2_TriggerExecute },
    { "TriggerGetCurrent",                   sc2_TriggerGetCurrent },
    { "TriggerGetExecCount",                 sc2_TriggerGetExecCount },
    { "TriggerIsEnabled",                    sc2_TriggerIsEnabled },
    { "TriggerQueueClear",                   sc2_TriggerQueueClear },
    { "TriggerQueueEnter",                   sc2_TriggerQueueEnter },
    { "TriggerQueueExit",                    sc2_TriggerQueueExit },
    { "TriggerQueueIsEmpty",                 sc2_TriggerQueueIsEmpty },
    { "TriggerQueuePause",                   sc2_TriggerQueuePause },
    { "TriggerSkippableBegin",               sc2_TriggerSkippableBegin },
    { "TriggerSkippableEnd",                 sc2_TriggerSkippableEnd },
    { "TriggerStop",                         sc2_TriggerStop },
    { "UIAlertPoint",                        sc2_UIAlertPoint },
    { "UIAlertUnit",                         sc2_UIAlertUnit },
    { "UISetFrameVisible",                   sc2_UISetFrameVisible },
    { "UISetMode",                           sc2_UISetMode },
    { "UnitBehaviorAdd",                     sc2_UnitBehaviorAdd },
    { "UnitBehaviorRemove",                  sc2_UnitBehaviorRemove },
    { "UnitCargoCreate",                     sc2_UnitCargoCreate },
    { "UnitCargoGroup",                      sc2_UnitCargoGroup },
    { "UnitCargoLastCreated",                sc2_UnitCargoLastCreated },
    { "UnitCargoLastCreatedGroup",           sc2_UnitCargoLastCreatedGroup },
    { "UnitClearInfoText",                   sc2_UnitClearInfoText },
    { "UnitClearSelection",                  sc2_UnitClearSelection },
    { "UnitCreate",                          sc2_UnitCreate },
    { "UnitFilter",                          sc2_UnitFilter },
    { "UnitFilterMatch",                     sc2_UnitFilterMatch },
    { "UnitFilterSetState",                  sc2_UnitFilterSetState },
    { "UnitFilterStr",                       sc2_UnitFilterStr },
    { "UnitForceStatusBar",                  sc2_UnitForceStatusBar },
    { "UnitFromId",                          sc2_UnitFromId },
    { "UnitGetAttachmentPoint",              sc2_UnitGetAttachmentPoint },
    { "UnitGetFacing",                       sc2_UnitGetFacing },
    { "UnitGetHeight",                       sc2_UnitGetHeight },
    { "UnitGetOwner",                        sc2_UnitGetOwner },
    { "UnitGetPosition",                     sc2_UnitGetPosition },
    { "UnitGetPropertyFixed",                sc2_UnitGetPropertyFixed },
    { "UnitGetType",                         sc2_UnitGetType },
    { "UnitGroup",                           sc2_UnitGroup },
    { "UnitGroupAdd",                        sc2_UnitGroupAdd },
    { "UnitGroupAlliance",                   sc2_UnitGroupAlliance },
    { "UnitGroupClear",                      sc2_UnitGroupClear },
    { "UnitGroupCopy",                       sc2_UnitGroupCopy },
    { "UnitGroupCount",                      sc2_UnitGroupCount },
    { "UnitGroupEmpty",                      sc2_UnitGroupEmpty },
    { "UnitGroupFilter",                     sc2_UnitGroupFilter },
    { "UnitGroupFilterAlliance",             sc2_UnitGroupFilterAlliance },
    { "UnitGroupFilterPlane",                sc2_UnitGroupFilterPlane },
    { "UnitGroupFilterPlayer",               sc2_UnitGroupFilterPlayer },
    { "UnitGroupFilterRegion",               sc2_UnitGroupFilterRegion },
    { "UnitGroupFilterThreat",               sc2_UnitGroupFilterThreat },
    { "UnitGroupFromId",                     sc2_UnitGroupFromId },
    { "UnitGroupHasUnit",                    sc2_UnitGroupHasUnit },
    { "UnitGroupIdle",                       sc2_UnitGroupIdle },
    { "UnitGroupIssueOrder",                 sc2_UnitGroupIssueOrder },
    { "UnitGroupLoopBegin",                  sc2_UnitGroupLoopBegin },
    { "UnitGroupLoopCurrent",                sc2_UnitGroupLoopCurrent },
    { "UnitGroupLoopDone",                   sc2_UnitGroupLoopDone },
    { "UnitGroupLoopEnd",                    sc2_UnitGroupLoopEnd },
    { "UnitGroupLoopStep",                   sc2_UnitGroupLoopStep },
    { "UnitGroupNearestUnit",                sc2_UnitGroupNearestUnit },
    { "UnitGroupRandomUnit",                 sc2_UnitGroupRandomUnit },
    { "UnitGroupRemove",                     sc2_UnitGroupRemove },
    { "UnitGroupTestPlane",                  sc2_UnitGroupTestPlane },
    { "UnitGroupUnit",                       sc2_UnitGroupUnit },
    { "UnitGroupWaitUntilIdle",              sc2_UnitGroupWaitUntilIdle },
    { "UnitInventoryGroup",                  sc2_UnitInventoryGroup },
    { "UnitIsAlive",                         sc2_UnitIsAlive },
    { "UnitIsValid",                         sc2_UnitIsValid },
    { "UnitIssueOrder",                      sc2_UnitIssueOrder },
    { "UnitKill",                            sc2_UnitKill },
    { "UnitLastCreated",                     sc2_UnitLastCreated },
    { "UnitLastCreatedGroup",                sc2_UnitLastCreatedGroup },
    { "UnitLoadModel",                       sc2_UnitLoadModel },
    { "UnitPauseAll",                        sc2_UnitPauseAll },
    { "UnitRefFromUnit",                     sc2_UnitRefFromUnit },
    { "UnitRefFromVariable",                 sc2_UnitRefFromVariable },
    { "UnitRefToUnit",                       sc2_UnitRefToUnit },
    { "UnitRemove",                          sc2_UnitRemove },
    { "UnitRevive",                          sc2_UnitRevive },
    { "UnitSetCursor",                       sc2_UnitSetCursor },
    { "UnitSetFacing",                       sc2_UnitSetFacing },
    { "UnitSetHeight",                       sc2_UnitSetHeight },
    { "UnitSetInfoText",                     sc2_UnitSetInfoText },
    { "UnitSetOwner",                        sc2_UnitSetOwner },
    { "UnitSetPosition",                     sc2_UnitSetPosition },
    { "UnitSetPropertyFixed",                sc2_UnitSetPropertyFixed },
    { "UnitSetScale",                        sc2_UnitSetScale },
    { "UnitSetState",                        sc2_UnitSetState },
    { "UnitSetTeamColorIndex",               sc2_UnitSetTeamColorIndex },
    { "UnitTechTreeBehaviorCount",           sc2_UnitTechTreeBehaviorCount },
    { "UnitTechTreeUnitCount",               sc2_UnitTechTreeUnitCount },
    { "UnitTechTreeUpgradeCount",            sc2_UnitTechTreeUpgradeCount },
    { "UnitTestState",                       sc2_UnitTestState },
    { "UnitTypeAnimationLoad",               sc2_UnitTypeAnimationLoad },
    { "UnitTypeAnimationUnload",             sc2_UnitTypeAnimationUnload },
    { "UnitTypeFromString",                  sc2_UnitTypeFromString },
    { "UnitTypeGetCost",                     sc2_UnitTypeGetCost },
    { "UnitTypeGetName",                     sc2_UnitTypeGetName },
    { "UnitTypeGetProperty",                 sc2_UnitTypeGetProperty },
    { "UnitTypeIsAffectedByUpgrade",         sc2_UnitTypeIsAffectedByUpgrade },
    { "UnitTypeTestAttribute",               sc2_UnitTypeTestAttribute },
    { "UnitTypeTestFlag",                    sc2_UnitTypeTestFlag },
    { "UnitUnloadModel",                     sc2_UnitUnloadModel },
    { "UnitWaitUntilIdle",                   sc2_UnitWaitUntilIdle },
    { "VictoryPanelAddAchievement",          sc2_VictoryPanelAddAchievement },
    { "VictoryPanelAddCustomStatisticLine",  sc2_VictoryPanelAddCustomStatisticLine },
    { "VictoryPanelAddTrackedStatistic",     sc2_VictoryPanelAddTrackedStatistic },
    { "VisExploreArea",                      sc2_VisExploreArea },
    { "VisRevealArea",                       sc2_VisRevealArea },
    { "VisRevealerCreate",                   sc2_VisRevealerCreate },
    { "VisRevealerDestroy",                  sc2_VisRevealerDestroy },
    { "VisRevealerLastCreated",              sc2_VisRevealerLastCreated },
    { "Wait",                                sc2_Wait },
    { NULL, NULL },
};

LPCJASSMODULE galaxy_get_natives(void) { return sc2_galaxy_natives; }
