#include "games/starcraft-2/common/sc2_coords.h"

/* galaxy_unit.h — unit, unitgroup, and unittype natives */

#define MAX_GALAXY_UNITS   256
#define MAX_CARGO_PER_UNIT 16
/* Bit flag ORed into a unitgroup handle to mark it as a cargo-group reference.
 * The lower bits encode the transport unit handle (max 256, well below 0x40000000). */
#define CARGO_GROUP_FLAG   ((int32_t)0x40000000)
#define MAX_UNIT_ORDERS    8   /* enough for generated cutscene queues */
#define SC2_CARGO_DROP_SPACING 1.1f  /* map units; separates the two-row cinematic unload formation */

void *sc2_gunits[MAX_GALAXY_UNITS];
uint32_t sc2_gunit_n;
int32_t  sc2_last_unit_handle;

typedef struct { char ability[64]; float x, y; bool started; } sc2GUnitOrder_t;
static int32_t sc2_gcargo[MAX_GALAXY_UNITS][MAX_CARGO_PER_UNIT];
static int32_t sc2_gcargo_n[MAX_GALAXY_UNITS];
static int32_t sc2_last_cargo_handle;
static sc2GUnitOrder_t sc2_uorders[MAX_GALAXY_UNITS][MAX_UNIT_ORDERS];
static int32_t sc2_uorder_n[MAX_GALAXY_UNITS];

/* UnitCreate: resolve unit model from catalog, spawn at point position. */
static uint32_t sc2_UnitCreate(jass_t * j) {
    int32_t   count  = jass_checkinteger(j, 1);
    cstring_t type   = jass_checkstring(j, 2);
    int32_t   player = jass_checkinteger(j, 4);
    int32_t   pt_h   = (int32_t)(uintptr_t)jass_checkhandle(j, 5, "point");
    float  angle  = SC2_FacingRadians(jass_checknumber(j, 6));
    float  x = 0.0f, y = 0.0f;
    if (pt_h > 0 && pt_h < sc2_gpoint_n) { x = sc2_gpoints[pt_h].x; y = sc2_gpoints[pt_h].y; }
    int32_t handle = 0;
    for (int32_t i = 0; i < count && sc2_gunit_n < MAX_GALAXY_UNITS; i++) {
        void *ent = sc2_galaxy_on_unit_create ?
            sc2_galaxy_on_unit_create(type ? type : "", (int)player, x, y, angle) : NULL;
        if (!ent)
            fprintf(stderr, "sc2_UnitCreate: on_unit_create returned NULL for type '%s' (%ld/%ld) — unit will be invisible\n",
                    type ? type : "(null)", (long)(i + 1), (long)count);
        handle = (int32_t)(++sc2_gunit_n);
        sc2_gunits[handle - 1] = ent;
        sc2_last_unit_handle = handle;
    }
    return handle ? jass_pushlighthandle(j, (handle_t)(uintptr_t)handle, "unit")
                  : jass_pushnullhandle(j, "unit");
}

static uint32_t sc2_UnitLastCreated(jass_t * j) {
    return sc2_last_unit_handle ?
        jass_pushlighthandle(j, (handle_t)(uintptr_t)sc2_last_unit_handle, "unit") :
        jass_pushnullhandle(j, "unit");
}
static uint32_t sc2_UnitLastCreatedGroup(jass_t * j)     { return jass_pushnullhandle(j, "unitgroup"); }
static void *sc2_ent_from_handle(jass_t * j, int idx) {
    int32_t h = (int32_t)(uintptr_t)jass_checkhandle(j, idx, "unit");
    return (h > 0 && h <= (int32_t)sc2_gunit_n) ? sc2_gunits[h - 1] : NULL;
}

static uint32_t sc2_UnitSetPosition(jass_t * j) {
    void *ent = sc2_ent_from_handle(j, 1);
    int32_t pt_h = (int32_t)(uintptr_t)jass_checkhandle(j, 2, "point");
    if (ent && sc2_galaxy_unit_set_position && pt_h > 0 && pt_h < sc2_gpoint_n)
        sc2_galaxy_unit_set_position(ent, sc2_gpoints[pt_h].x, sc2_gpoints[pt_h].y, 0.0f);
    return jass_pushnull(j);
}

static uint32_t sc2_UnitSetFacing(jass_t * j) {
    void *ent = sc2_ent_from_handle(j, 1);
    float ang = jass_checknumber(j, 2);
    if (ent && sc2_galaxy_unit_set_position) {
        /* Re-use set_position with NaN for x/y to indicate facing-only update.
         * g_sc2.c checks for this sentinel and only updates the angle. */
        sc2_galaxy_unit_set_position(ent, 0.0f/0.0f, 0.0f/0.0f, SC2_FacingRadians(ang));
    }
    return jass_pushnull(j);
}

static uint32_t sc2_UnitSetOwner(jass_t * j)             { (void)j; return jass_pushnull(j); }
static uint32_t sc2_UnitGetOwner(jass_t * j) {
    void *ent = sc2_ent_from_handle(j, 1);
    return jass_pushinteger(j, ent && sc2_galaxy_unit_owner ? sc2_galaxy_unit_owner(ent) : 0);
}
static uint32_t sc2_UnitSetHeight(jass_t * j)            { (void)j; return jass_pushnull(j); }
static uint32_t sc2_UnitSetScale(jass_t * j)             { (void)j; return jass_pushnull(j); }
static uint32_t sc2_UnitSetState(jass_t * j)             { (void)j; return jass_pushnull(j); }
static uint32_t sc2_UnitSetCursor(jass_t * j)            { (void)j; return jass_pushnull(j); }
static uint32_t sc2_UnitTestState(jass_t * j)            { return jass_pushboolean(j, false); }
static uint32_t sc2_UnitIsAlive(jass_t * j) {
    void *ent = sc2_ent_from_handle(j, 1);
    return jass_pushboolean(j, ent && (!sc2_galaxy_unit_is_alive || sc2_galaxy_unit_is_alive(ent)));
}
static uint32_t sc2_UnitIsValid(jass_t * j) {
    void *ent = sc2_ent_from_handle(j, 1);
    return jass_pushboolean(j, ent != NULL);
}
static uint32_t sc2_UnitKill(jass_t * j)                 { (void)j; return jass_pushnull(j); }
static uint32_t sc2_UnitRemove(jass_t * j)               { (void)j; return jass_pushnull(j); }
static uint32_t sc2_UnitRevive(jass_t * j)               { (void)j; return jass_pushnull(j); }
static uint32_t sc2_UnitWaitUntilIdle(jass_t * j)        { (void)j; return jass_pushnull(j); }
static uint32_t sc2_UnitIssueOrder(jass_t * j) {
    int32_t unit_h  = (int32_t)(uintptr_t)jass_checkhandle(j, 1, "unit");
    int32_t order_h = (int32_t)(uintptr_t)jass_checkhandle(j, 2, "order");
    int32_t queue   = jass_checkinteger(j, 3);
    if (order_h <= 0 || order_h >= sc2_gorder_n)
        return jass_pushboolean(j, false);
    sc2GOrder_t *ord = &sc2_gorders[order_h];
    int32_t  ac_h = ord->abilcmd_h;
    int32_t  pt_h = ord->pt_h;
    float tx   = (pt_h > 0 && pt_h < sc2_gpoint_n) ? sc2_gpoints[pt_h].x : 0.0f;
    float ty   = (pt_h > 0 && pt_h < sc2_gpoint_n) ? sc2_gpoints[pt_h].y : 0.0f;
    const char *ability = (ac_h > 0 && ac_h < sc2_gabilcmd_n)
                          ? sc2_gabilcmds[ac_h].ability : "move";
    if (unit_h <= 0 || unit_h > (int32_t)sc2_gunit_n || (queue != 0 && queue != 1))
        return jass_pushboolean(j, false);
    if (queue == 0) sc2_uorder_n[unit_h - 1] = 0;
    int32_t *count = &sc2_uorder_n[unit_h - 1];
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
static uint32_t sc2_UnitPauseAll(jass_t * j)             { (void)j; return jass_pushnull(j); }
static uint32_t sc2_UnitGetFacing(jass_t * j)            { return jass_pushnumber(j, 0.0f); }
static uint32_t sc2_UnitGetHeight(jass_t * j)            { return jass_pushnumber(j, 0.0f); }
static uint32_t sc2_UnitGetPosition(jass_t * j) {
    return jass_pushnullhandle(j, "point");  /* TODO: extract ent position */
}
static uint32_t sc2_UnitGetType(jass_t * j)   { (void)jass_checkhandle(j, 1, "unit"); return jass_pushstring(j, ""); }
static uint32_t sc2_UnitFromId(jass_t * j)    { return jass_pushnullhandle(j, "unit"); }
static uint32_t sc2_UnitBehaviorAdd(jass_t * j)          { (void)j; return jass_pushnull(j); }
static uint32_t sc2_UnitBehaviorRemove(jass_t * j)       { (void)j; return jass_pushnull(j); }
static uint32_t sc2_UnitCargoCreate(jass_t * j) {
    int32_t   t_h  = (int32_t)(uintptr_t)jass_checkhandle(j, 1, "unit");
    cstring_t type = jass_checkstring(j, 2);
    int32_t   cnt  = jass_checkinteger(j, 3);
    void *transport = t_h > 0 && t_h <= (int32_t)sc2_gunit_n ? sc2_gunits[t_h - 1] : NULL;
    if (!transport || !sc2_galaxy_unit_owner) {
        fprintf(stderr, "UnitCargoCreate: transport %ld or owner callback unavailable\n", (long)t_h);
        return jass_pushnullhandle(j, "unit");
    }
    /* Cargo inherits its transport's owner; neutral cargo could never receive the user's orders. */
    int player = sc2_galaxy_unit_owner(transport);
    if (cnt < 1) cnt = 1;
    int32_t handle  = 0;
    for (int32_t i = 0; i < cnt && sc2_gunit_n < MAX_GALAXY_UNITS; i++) {
        void *ent = sc2_galaxy_on_unit_create ?
            sc2_galaxy_on_unit_create(type ? type : "", player, 0.0f, 0.0f, 0.0f) : NULL;
        if (!ent)
            fprintf(stderr, "sc2_UnitCargoCreate: on_unit_create returned NULL for type '%s' (%ld/%ld) — cargo unit will be invisible\n",
                    type ? type : "(null)", (long)(i + 1), (long)cnt);
        handle = (int32_t)(++sc2_gunit_n);
        sc2_gunits[handle - 1] = ent;
        sc2_last_cargo_handle  = handle;
        sc2_last_unit_handle   = handle;
        if (t_h > 0 && t_h <= MAX_GALAXY_UNITS) {
            int32_t ci = sc2_gcargo_n[t_h - 1];
            if (ci < MAX_CARGO_PER_UNIT)
                sc2_gcargo[t_h - 1][sc2_gcargo_n[t_h - 1]++] = handle;
        }
    }
    fprintf(stderr, "UnitCargoCreate: transport=%ld type=%s count=%ld\n",
            (long)t_h, type ? type : "(null)", (long)cnt);
    return handle ? jass_pushlighthandle(j, (handle_t)(uintptr_t)handle, "unit")
                  : jass_pushnullhandle(j, "unit");
}
static uint32_t sc2_UnitCargoLastCreated(jass_t * j) {
    return sc2_last_cargo_handle ?
        jass_pushlighthandle(j, (handle_t)(uintptr_t)sc2_last_cargo_handle, "unit") :
        jass_pushnullhandle(j, "unit");
}
static uint32_t sc2_UnitCargoGroup(jass_t * j) {
    int32_t h = (int32_t)(uintptr_t)jass_checkhandle(j, 1, "unit");
    if (h > 0 && h <= (int32_t)sc2_gunit_n)
        return jass_pushlighthandle(j,
            (handle_t)(uintptr_t)(CARGO_GROUP_FLAG | h), "unitgroup");
    return jass_pushnullhandle(j, "unitgroup");
}
static uint32_t sc2_UnitCargoLastCreatedGroup(jass_t * j){ return jass_pushnullhandle(j, "unitgroup"); }
static uint32_t sc2_UnitClearSelection(jass_t * j)       { (void)j; return jass_pushnull(j); }
/* UnitRef wraps a unit handle into a unitref (same pointer, different type name). */
static uint32_t sc2_UnitRefFromUnit(jass_t * j) {
    handle_t h = jass_checkhandle(j, 1, "unit");
    return h ? jass_pushlighthandle(j, h, "unitref") : jass_pushnullhandle(j, "unitref");
}
static uint32_t sc2_UnitRefFromVariable(jass_t * j)  { return jass_pushnullhandle(j, "unitref"); }
static uint32_t sc2_UnitRefToUnit(jass_t * j) {
    handle_t h = jass_checkhandle(j, 1, "unitref");
    return h ? jass_pushlighthandle(j, h, "unit") : jass_pushnullhandle(j, "unit");
}
static uint32_t sc2_UnitSetInfoText(jass_t * j)          { (void)j; return jass_pushnull(j); }
static uint32_t sc2_UnitClearInfoText(jass_t * j)        { (void)j; return jass_pushnull(j); }
static uint32_t sc2_UnitForceStatusBar(jass_t * j)       { (void)j; return jass_pushnull(j); }
static uint32_t sc2_UnitGetAttachmentPoint(jass_t * j)   { return jass_pushinteger(j, 0); }
static uint32_t sc2_UnitSetTeamColorIndex(jass_t * j)    { (void)j; return jass_pushnull(j); }
static uint32_t sc2_UnitLoadModel(jass_t * j)            { (void)j; return jass_pushnull(j); }
static uint32_t sc2_UnitUnloadModel(jass_t * j)          { (void)j; return jass_pushnull(j); }
static uint32_t sc2_UnitSetPropertyFixed(jass_t * j)     { (void)j; return jass_pushnull(j); }
static uint32_t sc2_UnitGetPropertyFixed(jass_t * j)     { return jass_pushnumber(j, 0.0f); }
static uint32_t sc2_EventUnit(jass_t * j)       { return jass_pushnullhandle(j, "unit"); }
static uint32_t sc2_EventUnitCargo(jass_t * j)  { return jass_pushnullhandle(j, "unit"); }
static uint32_t sc2_EventUnitTarget(jass_t * j) { return jass_pushnullhandle(j, "unit"); }

/* UnitGroup */
static uint32_t sc2_UnitGroupEmpty(jass_t * j)            { return jass_pushnullhandle(j, "unitgroup"); }
static uint32_t sc2_UnitGroupAdd(jass_t * j)              { return jass_checkhandle(j, 1, "unitgroup") ? jass_pushlighthandle(j, jass_checkhandle(j, 1, "unitgroup"), "unitgroup") : jass_pushnullhandle(j, "unitgroup"); }
static uint32_t sc2_UnitGroupCount(jass_t * j) {
    int32_t h    = (int32_t)(uintptr_t)jass_checkhandle(j, 1, "unitgroup");
    int32_t mode = jass_checkinteger(j, 2); (void)mode;
    if (h & CARGO_GROUP_FLAG) {
        int32_t t = h & ~CARGO_GROUP_FLAG;
        if (t > 0 && t <= (int32_t)sc2_gunit_n)
            return jass_pushinteger(j, sc2_gcargo_n[t - 1]);
    }
    return jass_pushinteger(j, 0);
}
static uint32_t sc2_UnitGroupHasUnit(jass_t * j)          { return jass_pushboolean(j, false); }
static uint32_t sc2_UnitGroupWaitUntilIdle(jass_t * j)    { (void)j; return jass_pushnull(j); }
static uint32_t sc2_UnitInventoryGroup(jass_t * j)        { return jass_pushnullhandle(j, "unitgroup"); }
static uint32_t sc2_UnitTechTreeBehaviorCount(jass_t * j) { return jass_pushinteger(j, 0); }
static uint32_t sc2_UnitTechTreeUnitCount(jass_t * j)     { return jass_pushinteger(j, 0); }
static uint32_t sc2_UnitTechTreeUpgradeCount(jass_t * j)  { return jass_pushinteger(j, 0); }
/* UnitGroupUnit(group, index): 1-based index; supports cargo groups. */
static uint32_t sc2_UnitGroupUnit(jass_t * j) {
    int32_t h   = (int32_t)(uintptr_t)jass_checkhandle(j, 1, "unitgroup");
    int32_t idx = jass_checkinteger(j, 2) - 1;  /* 1-based → 0-based */
    if (h & CARGO_GROUP_FLAG) {
        int32_t t = h & ~CARGO_GROUP_FLAG;
        if (t > 0 && t <= (int32_t)sc2_gunit_n && idx >= 0 && idx < sc2_gcargo_n[t - 1]) {
            int32_t cargo_h = sc2_gcargo[t - 1][idx];
            return jass_pushlighthandle(j, (handle_t)(uintptr_t)cargo_h, "unit");
        }
    }
    return jass_pushnullhandle(j, "unit");
}
static uint32_t sc2_UnitGroupIssueOrder(jass_t * j)       { (void)j; return jass_pushnull(j); }
static uint32_t sc2_UnitGroupIdle(jass_t * j)             { return jass_pushboolean(j, false); }
static uint32_t sc2_UnitGroupFilter(jass_t * j)           { return jass_pushinteger(j, 0); }
static uint32_t sc2_UnitGroupClear(jass_t * j)            { return jass_pushnullhandle(j, "unitgroup"); }
static uint32_t sc2_UnitGroupCopy(jass_t * j)             { return jass_pushnullhandle(j, "unitgroup"); }
static uint32_t sc2_UnitGroupRemove(jass_t * j)           { (void)j; return jass_pushnull(j); }
static uint32_t sc2_UnitGroupAlliance(jass_t * j)         { return jass_pushnullhandle(j, "unitgroup"); }
static uint32_t sc2_UnitGroupFilterAlliance(jass_t * j)   { return jass_pushnullhandle(j, "unitgroup"); }
static uint32_t sc2_UnitGroupFilterPlayer(jass_t * j)     { return jass_pushnullhandle(j, "unitgroup"); }
static uint32_t sc2_UnitGroupFilterPlane(jass_t * j)      { return jass_pushnullhandle(j, "unitgroup"); }
static uint32_t sc2_UnitGroupFilterRegion(jass_t * j)     { return jass_pushnullhandle(j, "unitgroup"); }
static uint32_t sc2_UnitGroupFilterThreat(jass_t * j)     { return jass_pushnullhandle(j, "unitgroup"); }
static uint32_t sc2_UnitGroupFromId(jass_t * j)           { return jass_pushnullhandle(j, "unitgroup"); }

/* UnitGroupLoop: tracks a single active group iteration (single-coroutine). */
typedef struct { int32_t group_h; int32_t cur; bool active; } SC2GroupLoop;
static SC2GroupLoop sc2_group_loop;

static int32_t sc2_group_size(int32_t h) {
    if (h & CARGO_GROUP_FLAG) {
        int32_t t = h & ~CARGO_GROUP_FLAG;
        if (t > 0 && t <= (int32_t)sc2_gunit_n) return sc2_gcargo_n[t - 1];
    }
    return 0;
}
static int32_t sc2_group_nth(int32_t h, int32_t idx) {
    if (h & CARGO_GROUP_FLAG) {
        int32_t t = h & ~CARGO_GROUP_FLAG;
        if (t > 0 && t <= (int32_t)sc2_gunit_n && idx >= 0 && idx < sc2_gcargo_n[t - 1])
            return sc2_gcargo[t - 1][idx];
    }
    return 0;
}

static uint32_t sc2_UnitGroupLoopBegin(jass_t * j) {
    int32_t h = (int32_t)(uintptr_t)jass_checkhandle(j, 1, "unitgroup");
    sc2_group_loop = (SC2GroupLoop){ h, 0, true };
    return jass_pushnull(j);
}
static uint32_t sc2_UnitGroupLoopDone(jass_t * j) {
    (void)j;
    if (!sc2_group_loop.active) return jass_pushboolean(j, true);
    return jass_pushboolean(j, sc2_group_loop.cur >= sc2_group_size(sc2_group_loop.group_h));
}
static uint32_t sc2_UnitGroupLoopCurrent(jass_t * j) {
    (void)j;
    if (!sc2_group_loop.active) return jass_pushnullhandle(j, "unit");
    int32_t h = sc2_group_nth(sc2_group_loop.group_h, sc2_group_loop.cur);
    return h ? jass_pushlighthandle(j, (handle_t)(uintptr_t)h, "unit")
             : jass_pushnullhandle(j, "unit");
}
static uint32_t sc2_UnitGroupLoopStep(jass_t * j) {
    (void)j;
    if (sc2_group_loop.active) sc2_group_loop.cur++;
    return jass_pushnull(j);
}
static uint32_t sc2_UnitGroupLoopEnd(jass_t * j) {
    (void)j;
    sc2_group_loop.active = false;
    return jass_pushnull(j);
}
static uint32_t sc2_UnitGroupNearestUnit(jass_t * j)      { return jass_pushnullhandle(j, "unit"); }
static uint32_t sc2_UnitGroupRandomUnit(jass_t * j)       { return jass_pushnullhandle(j, "unit"); }
static uint32_t sc2_UnitGroupTestPlane(jass_t * j)        { return jass_pushboolean(j, false); }
static uint32_t sc2_UnitFilter(jass_t * j)                { return jass_pushnullhandle(j, "unitfilter"); }
static uint32_t sc2_UnitFilterMatch(jass_t * j)           { return jass_pushboolean(j, false); }
static uint32_t sc2_UnitFilterSetState(jass_t * j)        { (void)j; return jass_pushnull(j); }
static uint32_t sc2_UnitFilterStr(jass_t * j)             { return jass_pushnullhandle(j, "unitfilter"); }
static uint32_t sc2_UnitGroup(jass_t * j)                 { return jass_pushnullhandle(j, "unitgroup"); }

/* UnitType */
static uint32_t sc2_UnitTypeFromString(jass_t * j)          { return jass_pushstring(j, ""); }
static uint32_t sc2_UnitTypeGetCost(jass_t * j)             { return jass_pushinteger(j, 0); }
static uint32_t sc2_UnitTypeGetName(jass_t * j)             { return jass_pushstring(j, ""); }
static uint32_t sc2_UnitTypeGetProperty(jass_t * j)         { return jass_pushinteger(j, 0); }
static uint32_t sc2_UnitTypeIsAffectedByUpgrade(jass_t * j) { return jass_pushboolean(j, false); }
static uint32_t sc2_UnitTypeTestAttribute(jass_t * j)       { return jass_pushboolean(j, false); }
static uint32_t sc2_UnitTypeTestFlag(jass_t * j)            { return jass_pushboolean(j, false); }
static uint32_t sc2_UnitTypeAnimationLoad(jass_t * j)       { (void)j; return jass_pushnull(j); }
static uint32_t sc2_UnitTypeAnimationUnload(jass_t * j)     { (void)j; return jass_pushnull(j); }

static void sc2_pop_unit_order(int32_t unit_h) {
    int32_t *count = &sc2_uorder_n[unit_h - 1];
    if (--*count > 0)
        memmove(&sc2_uorders[unit_h - 1][0], &sc2_uorders[unit_h - 1][1], sizeof(sc2GUnitOrder_t) * *count);
}

/* Galaxy append orders begin only after the active movement reports completion. */
static void sc2_run_unit_orders(void) {
    for (int32_t unit_h = 1; unit_h <= (int32_t)sc2_gunit_n; unit_h++) {
        int32_t *count = &sc2_uorder_n[unit_h - 1];
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
                int32_t cargo_n = sc2_gcargo_n[unit_h - 1];
                for (int32_t i = 0; i < cargo_n; i++) {
                    int32_t cargo_h = sc2_gcargo[unit_h - 1][i];
                    void *cargo = (cargo_h > 0 && cargo_h <= (int32_t)sc2_gunit_n) ? sc2_gunits[cargo_h - 1] : NULL;
                    float x = ord->x + ((float)(i % 3) - 1.0f) * SC2_CARGO_DROP_SPACING;
                    float y = ord->y + (0.75f + (float)(i / 3) * SC2_CARGO_DROP_SPACING);
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
