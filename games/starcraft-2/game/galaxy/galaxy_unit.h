#include "games/starcraft-2/common/sc2_coords.h"

/* galaxy_unit.h — unit, unitgroup, and unittype natives */

#define MAX_GALAXY_UNITS   4096
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

static int32_t sc2_last_unit_group;
static int32_t sc2_unit_register(jass_t *j, void *ent) {
    if (!ent) return 0;
    for (uint32_t i=0; i<sc2_gunit_n; i++) if (sc2_gunits[i] == ent) return i+1;
    if (sc2_gunit_n == MAX_GALAXY_UNITS) { jass_rterror(j,"Galaxy unit handle table full"); return 0; }
    sc2_gunits[sc2_gunit_n++] = ent; return sc2_gunit_n;
}
static bool sc2_unit_location_handle(int32_t h, sc2GPoint_t *p) {
    if (h <= 0 || h > sc2_gunit_n || !sc2_gunits[h-1] || !sc2_galaxy_unit_location) return false;
    return sc2_galaxy_unit_location(sc2_gunits[h-1],&p->x,&p->y,&p->height,&p->facing);
}

/* UnitCreate: resolve unit model from catalog, spawn at point position. */
static uint32_t sc2_UnitCreate(jass_t *j) {
    int32_t   count  = jass_checkinteger(j, 1);
    cstring_t type   = jass_checkstring(j, 2);
    int32_t   player = jass_checkinteger(j, 4);
    int32_t   pt_h   = (int32_t)(uintptr_t)jass_checkhandle(j, 5, "point");
    float  angle  = SC2_FacingRadians(jass_checknumber(j, 6));
    float  x = 0.0f, y = 0.0f;
    if (pt_h > 0 && pt_h < sc2_gpoint_n) { x = sc2_gpoints[pt_h].x; y = sc2_gpoints[pt_h].y; }
    int32_t handle = 0;
    sc2_last_unit_group = sc2_group_new(j,sc2_unit_groups,&sc2_unit_group_n,NULL);
    for (int32_t i = 0; i < count && sc2_gunit_n < MAX_GALAXY_UNITS; i++) {
        void *ent = sc2_galaxy_on_unit_create ?
            sc2_galaxy_on_unit_create(type ? type : "", (int)player, x, y, angle) : NULL;
        if (!ent)
            fprintf(stderr, "sc2_UnitCreate: on_unit_create returned NULL for type '%s' (%ld/%ld) — unit will be invisible\n",
                    type ? type : "(null)", (long)(i + 1), (long)count);
        handle = sc2_unit_register(j,ent);
        if (handle) sc2_group_append(j,&sc2_unit_groups[sc2_last_unit_group],handle);
        sc2_last_unit_handle = handle;
    }
    return handle ? jass_pushlighthandle(j, (handle_t)(uintptr_t)handle, "unit")
                  : jass_pushnullhandle(j, "unit");
}

static uint32_t sc2_UnitLastCreated(jass_t *j) {
    return sc2_last_unit_handle ?
        jass_pushlighthandle(j, (handle_t)(uintptr_t)sc2_last_unit_handle, "unit") :
        jass_pushnullhandle(j, "unit");
}
static uint32_t sc2_UnitLastCreatedGroup(jass_t *j) { return jass_pushlighthandle(j,(handle_t)(uintptr_t)sc2_last_unit_group,"unitgroup"); }
static void *sc2_ent_from_handle(jass_t *j, int idx) {
    int32_t h = (int32_t)(uintptr_t)jass_checkhandle(j, idx, "unit");
    return (h > 0 && h <= (int32_t)sc2_gunit_n) ? sc2_gunits[h - 1] : NULL;
}

static sc2UnitState_t *sc2_unit_data(jass_t *j, void *ent) {
    sc2UnitState_t *u = ent && sc2_galaxy_unit_state ? sc2_galaxy_unit_state(ent) : NULL;
    if (ent && !u) jass_rterror(j,"Galaxy unit state callback unavailable");
    return u;
}
static void sc2_unit_changed(jass_t *j, void *ent) {
    if (ent && sc2_galaxy_unit_changed) sc2_galaxy_unit_changed(ent);
    else if (ent) jass_rterror(j,"Galaxy unit lifecycle callback unavailable");
}
static uint32_t sc2_UnitSetOwner(jass_t *j) {
    void *ent = sc2_ent_from_handle(j,1); int player = sc2_player_index(j,2); bool color = jass_checkboolean(j,3);
    if (ent && !sc2_galaxy_unit_set_owner) jass_rterror(j,"Galaxy owner callback unavailable");
    if (ent) sc2_galaxy_unit_set_owner(ent,player,color); return 0;
}
static uint32_t sc2_unit_life(jass_t *j, bool revive) {
    int32_t h = (int32_t)(uintptr_t)jass_checkhandle(j,1,"unit"); void *ent = sc2_ent_from_handle(j,1);
    sc2UnitState_t *u = sc2_unit_data(j,ent);
    if (u) { SC2_UnitSetProperty(u,0,revive ? u->vitals[0].max_value : 0); sc2_uorder_n[h-1]=0; sc2_unit_changed(j,ent); }
    return 0;
}
static uint32_t sc2_UnitKill(jass_t *j) { return sc2_unit_life(j,false); }
static uint32_t sc2_UnitRevive(jass_t *j) { return sc2_unit_life(j,true); }
static uint32_t sc2_UnitRemove(jass_t *j) {
    int32_t h = (int32_t)(uintptr_t)jass_checkhandle(j,1,"unit"); void *ent = sc2_ent_from_handle(j,1);
    if (!ent) return 0;
    if (!sc2_galaxy_unit_remove) { jass_rterror(j,"Galaxy removal callback unavailable"); return 0; }
    sc2_galaxy_unit_remove(ent); sc2_gunits[h-1]=NULL; sc2_uorder_n[h-1]=0;
    for (int i=1;i<sc2_unit_group_n;i++) sc2_group_remove(&sc2_unit_groups[i],h);
    for (uint32_t i=0;i<sc2_gunit_n;i++) for (int32_t k=0;k<sc2_gcargo_n[i];k++) if (sc2_gcargo[i][k]==h) {
        memmove(sc2_gcargo[i]+k,sc2_gcargo[i]+k+1,(--sc2_gcargo_n[i]-k)*sizeof(int32_t)); break;
    }
    sc2_gcargo_n[h-1]=0;
    return 0;
}
static uint32_t sc2_unit_location_result(jass_t *j, int field) {
    int32_t h = (int32_t)(uintptr_t)jass_checkhandle(j,1,"unit"); sc2GPoint_t p={0};
    if (!sc2_unit_location_handle(h,&p)) {
        if (sc2_ent_from_handle(j,1)) jass_rterror(j,"Galaxy unit location unavailable");
        return field == 0 ? jass_pushnullhandle(j,"point") : jass_pushnumber(j,0);
    }
    return field == 0 ? sc2_point_result(j,p) : jass_pushnumber(j,field == 1 ? p.height : p.facing);
}
static uint32_t sc2_UnitGetPosition(jass_t *j) { return sc2_unit_location_result(j,0); }
static uint32_t sc2_UnitGetHeight(jass_t *j) { return sc2_unit_location_result(j,1); }
static uint32_t sc2_UnitGetFacing(jass_t *j) { return sc2_unit_location_result(j,2); }
static uint32_t sc2_UnitGetType(jass_t *j) {
    sc2UnitState_t *u=sc2_unit_data(j,sc2_ent_from_handle(j,1)); return jass_pushstring(j,u ? u->type : "");
}
static uint32_t sc2_UnitFromId(jass_t *j) {
    uint32_t id=jass_checkinteger(j,1);
    if (!sc2_galaxy_unit_from_id) { jass_rterror(j,"Galaxy map-unit lookup unavailable"); return 0; }
    int32_t h=sc2_unit_register(j,sc2_galaxy_unit_from_id(id));
    return jass_pushlighthandle(j,(handle_t)(uintptr_t)h,"unit");
}
static uint32_t sc2_unit_set_property(jass_t *j, bool integral) {
    void *ent=sc2_ent_from_handle(j,1); sc2UnitState_t *u=sc2_unit_data(j,ent);
    int prop=sc2_checked_index(j,2,24); float value=integral ? jass_checkinteger(j,3) : jass_checknumber(j,3);
    if (u && !SC2_UnitSetProperty(u,prop,value)) { jass_rterror(j,"Unit property is read-only or value is nonfinite"); return 0; }
    if (u) sc2_unit_changed(j,ent); return 0;
}
static uint32_t sc2_UnitSetPropertyFixed(jass_t *j) { return sc2_unit_set_property(j,false); }
static uint32_t sc2_UnitSetPropertyInt(jass_t *j) { return sc2_unit_set_property(j,true); }
static uint32_t sc2_unit_get_property(jass_t *j, bool integral) {
    sc2UnitState_t *u=sc2_unit_data(j,sc2_ent_from_handle(j,1)); int prop=sc2_checked_index(j,2,24);
    bool current=jass_checkboolean(j,3); float value=u ? (current ? SC2_UnitProperty(u,prop) : u->normal[prop]) : 0;
    return integral ? jass_pushinteger(j,(int32_t)value) : jass_pushnumber(j,value);
}
static uint32_t sc2_UnitGetPropertyFixed(jass_t *j) { return sc2_unit_get_property(j,false); }
static uint32_t sc2_UnitGetPropertyInt(jass_t *j) { return sc2_unit_get_property(j,true); }
static uint32_t sc2_UnitSetCustomValue(jass_t *j) {
    sc2UnitState_t *u=sc2_unit_data(j,sc2_ent_from_handle(j,1)); int index=sc2_checked_index(j,2,64);
    float value=jass_checknumber(j,3); if (u) u->custom[index]=value; return 0;
}
static uint32_t sc2_UnitGetCustomValue(jass_t *j) {
    sc2UnitState_t *u=sc2_unit_data(j,sc2_ent_from_handle(j,1)); int index=sc2_checked_index(j,2,64);
    return jass_pushnumber(j,u ? u->custom[index] : 0);
}
static uint32_t sc2_UnitSetState(jass_t *j) {
    void *ent=sc2_ent_from_handle(j,1); sc2UnitState_t *u=sc2_unit_data(j,ent);
    int state=sc2_checked_index(j,2,29); bool value=jass_checkboolean(j,3);
    switch (state) {
    case 8: case 9: case 10: case 17: case 24: case 25: break;
    default: jass_rterror(j,"Unit state is read-only or its behavior is not implemented"); return 0;
    }
    if (u) { if (value) u->states |= 1u<<state; else u->states &= ~(1u<<state); sc2_unit_changed(j,ent); }
    return 0;
}
static uint32_t sc2_UnitTestState(jass_t *j) {
    void *ent=sc2_ent_from_handle(j,1); sc2UnitState_t *u=sc2_unit_data(j,ent); int state=sc2_checked_index(j,2,29);
    bool value=u && (u->states & (1u<<state));
    if (state==22) value=u && !SC2_UnitAlive(u);
    if (state==15) value=ent && sc2_galaxy_unit_is_moving && !sc2_galaxy_unit_is_moving(ent);
    return jass_pushboolean(j,value);
}
static uint32_t sc2_UnitPauseAll(jass_t *j) {
    bool pause=jass_checkboolean(j,1);
    for (uint32_t i=0;i<sc2_gunit_n;i++) if (sc2_gunits[i]) {
        sc2UnitState_t *u=sc2_unit_data(j,sc2_gunits[i]);
        if (u) { if (pause) u->states |= 1u<<SC2_UNIT_PAUSED; else u->states &= ~(1u<<SC2_UNIT_PAUSED); sc2_unit_changed(j,sc2_gunits[i]); }
    }
    return 0;
}

static uint32_t sc2_UnitSetPosition(jass_t *j) {
    void *ent = sc2_ent_from_handle(j, 1);
    int32_t pt_h = (int32_t)(uintptr_t)jass_checkhandle(j, 2, "point");
    if (ent && sc2_galaxy_unit_set_position && pt_h > 0 && pt_h < sc2_gpoint_n)
        sc2_galaxy_unit_set_position(ent, sc2_gpoints[pt_h].x, sc2_gpoints[pt_h].y, NAN);
    return jass_pushnull(j);
}

static uint32_t sc2_UnitSetFacing(jass_t *j) {
    void *ent = sc2_ent_from_handle(j, 1);
    float ang = jass_checknumber(j, 2);
    if (ent && sc2_galaxy_unit_set_position) {
        /* Re-use set_position with NaN for x/y to indicate facing-only update.
         * g_sc2.c checks for this sentinel and only updates the angle. */
        sc2_galaxy_unit_set_position(ent, 0.0f/0.0f, 0.0f/0.0f, SC2_FacingRadians(ang));
    }
    return jass_pushnull(j);
}

static uint32_t sc2_UnitGetOwner(jass_t *j) {
    void *ent = sc2_ent_from_handle(j, 1);
    return jass_pushinteger(j, ent && sc2_galaxy_unit_owner ? sc2_galaxy_unit_owner(ent) : 0);
}
static uint32_t sc2_UnitSetHeight(jass_t *j)            { (void)j; return jass_pushnull(j); }
static uint32_t sc2_UnitSetScale(jass_t *j)             { (void)j; return jass_pushnull(j); }
static uint32_t sc2_UnitSetCursor(jass_t *j)            { (void)j; return jass_pushnull(j); }
static uint32_t sc2_UnitIsAlive(jass_t *j) {
    void *ent = sc2_ent_from_handle(j, 1);
    return jass_pushboolean(j, ent && (!sc2_galaxy_unit_is_alive || sc2_galaxy_unit_is_alive(ent)));
}
static uint32_t sc2_UnitIsValid(jass_t *j) {
    void *ent = sc2_ent_from_handle(j, 1);
    return jass_pushboolean(j, ent != NULL);
}
static uint32_t sc2_UnitWaitUntilIdle(jass_t *j)        { (void)j; return jass_pushnull(j); }
static bool sc2_issue_unit_order(jass_t *j,int32_t unit_h,int32_t order_h,int32_t queue) {
    if (order_h <= 0 || order_h >= sc2_gorder_n)
        return false;
    sc2GOrder_t *ord = &sc2_gorders[order_h];
    int32_t  ac_h = ord->abilcmd_h;
    int32_t  pt_h = ord->pt_h;
    float tx   = (pt_h > 0 && pt_h < sc2_gpoint_n) ? sc2_gpoints[pt_h].x : 0.0f;
    float ty   = (pt_h > 0 && pt_h < sc2_gpoint_n) ? sc2_gpoints[pt_h].y : 0.0f;
    char const *ability = (ac_h > 0 && ac_h < sc2_gabilcmd_n)
                          ? sc2_gabilcmds[ac_h].ability : "move";
    if (unit_h <= 0 || unit_h > (int32_t)sc2_gunit_n || (queue != 0 && queue != 1))
        return false;
    void *ent=sc2_gunits[unit_h-1];
    if (!ent || (sc2_galaxy_unit_is_alive && !sc2_galaxy_unit_is_alive(ent))) return false;
    if (ord->target_type==2) {
        sc2GPoint_t p; if (!sc2_unit_location_handle(ord->unit_h,&p)) return false;
        tx=p.x; ty=p.y;
    }
    if (ord->flags) { fprintf(stderr,"SC2 UnitIssueOrder: order flags 0x%x are not supported by movement\n",ord->flags); return false; }
    if (strcmp(ability,"move") && strcmp(ability,"SpecOpsDropshipTransport")) {
        fprintf(stderr,"SC2 UnitIssueOrder: ability '%s' is not implemented\n",ability); return false;
    }
    if (ord->target_type==0) { fprintf(stderr,"SC2 UnitIssueOrder: movement needs a target\n"); return false; }
    if (queue == 0) sc2_uorder_n[unit_h - 1] = 0;
    int32_t *count = &sc2_uorder_n[unit_h - 1];
    if (*count >= MAX_UNIT_ORDERS) {
        fprintf(stderr, "UnitIssueOrder: queue full for unit %ld\n", (long)unit_h);
        return false;
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
    return true;
}
static uint32_t sc2_UnitIssueOrder(jass_t *j) {
    int32_t u=(int32_t)(uintptr_t)jass_checkhandle(j,1,"unit"), o=(int32_t)(uintptr_t)jass_checkhandle(j,2,"order");
    return jass_pushboolean(j,sc2_issue_unit_order(j,u,o,jass_checkinteger(j,3)));
}

static uint32_t sc2_UnitBehaviorAdd(jass_t *j)          { (void)j; return jass_pushnull(j); }
static uint32_t sc2_UnitBehaviorRemove(jass_t *j)       { (void)j; return jass_pushnull(j); }
static uint32_t sc2_UnitCargoCreate(jass_t *j) {
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
static uint32_t sc2_UnitCargoLastCreated(jass_t *j) {
    return sc2_last_cargo_handle ?
        jass_pushlighthandle(j, (handle_t)(uintptr_t)sc2_last_cargo_handle, "unit") :
        jass_pushnullhandle(j, "unit");
}
static uint32_t sc2_UnitCargoGroup(jass_t *j) {
    int32_t h = (int32_t)(uintptr_t)jass_checkhandle(j, 1, "unit");
    if (h > 0 && h <= (int32_t)sc2_gunit_n)
        return jass_pushlighthandle(j,
            (handle_t)(uintptr_t)(CARGO_GROUP_FLAG | h), "unitgroup");
    return jass_pushnullhandle(j, "unitgroup");
}
static uint32_t sc2_UnitCargoLastCreatedGroup(jass_t *j){ return jass_pushnullhandle(j, "unitgroup"); }
static uint32_t sc2_UnitClearSelection(jass_t *j)       { (void)j; return jass_pushnull(j); }
/* UnitRef wraps a unit handle into a unitref (same pointer, different type name). */
static uint32_t sc2_UnitRefFromUnit(jass_t *j) {
    handle_t h = jass_checkhandle(j, 1, "unit");
    return h ? jass_pushlighthandle(j, h, "unitref") : jass_pushnullhandle(j, "unitref");
}
static uint32_t sc2_UnitRefFromVariable(jass_t *j)  { return jass_pushnullhandle(j, "unitref"); }
static uint32_t sc2_UnitRefToUnit(jass_t *j) {
    handle_t h = jass_checkhandle(j, 1, "unitref");
    return h ? jass_pushlighthandle(j, h, "unit") : jass_pushnullhandle(j, "unit");
}
static uint32_t sc2_UnitSetInfoText(jass_t *j)          { (void)j; return jass_pushnull(j); }
static uint32_t sc2_UnitClearInfoText(jass_t *j)        { (void)j; return jass_pushnull(j); }
static uint32_t sc2_UnitForceStatusBar(jass_t *j)       { (void)j; return jass_pushnull(j); }
static uint32_t sc2_UnitGetAttachmentPoint(jass_t *j)   { return jass_pushinteger(j, 0); }
static uint32_t sc2_UnitSetTeamColorIndex(jass_t *j)    { (void)j; return jass_pushnull(j); }
static uint32_t sc2_UnitLoadModel(jass_t *j)            { (void)j; return jass_pushnull(j); }
static uint32_t sc2_UnitUnloadModel(jass_t *j)          { (void)j; return jass_pushnull(j); }
static uint32_t sc2_EventUnit(jass_t *j)       { return jass_pushnullhandle(j, "unit"); }
static uint32_t sc2_EventUnitCargo(jass_t *j)  { return jass_pushnullhandle(j, "unit"); }
static uint32_t sc2_EventUnitTarget(jass_t *j) { return jass_pushnullhandle(j, "unit"); }

#include "galaxy_unitgroup.h"
static uint32_t sc2_UnitGroupWaitUntilIdle(jass_t *j)    { (void)j; return jass_pushnull(j); }
static uint32_t sc2_UnitInventoryGroup(jass_t *j)        { return jass_pushnullhandle(j, "unitgroup"); }
static uint32_t sc2_UnitTechTreeBehaviorCount(jass_t *j) { return jass_pushinteger(j, 0); }
static uint32_t sc2_UnitTechTreeUnitCount(jass_t *j)     { return jass_pushinteger(j, 0); }
static uint32_t sc2_UnitTechTreeUpgradeCount(jass_t *j)  { return jass_pushinteger(j, 0); }

static uint32_t sc2_UnitGroupIdle(jass_t *j)             { return jass_pushboolean(j, false); }
static uint32_t sc2_UnitGroupFilter(jass_t *j)           { return jass_pushinteger(j, 0); }
static uint32_t sc2_UnitGroupAlliance(jass_t *j)         { return jass_pushnullhandle(j, "unitgroup"); }
static uint32_t sc2_UnitGroupFilterAlliance(jass_t *j)   { return jass_pushnullhandle(j, "unitgroup"); }
static uint32_t sc2_UnitGroupFilterPlane(jass_t *j)      { return jass_pushnullhandle(j, "unitgroup"); }
static uint32_t sc2_UnitGroupFilterThreat(jass_t *j)     { return jass_pushnullhandle(j, "unitgroup"); }
static uint32_t sc2_UnitGroupFromId(jass_t *j)           { return jass_pushnullhandle(j, "unitgroup"); }
static uint32_t sc2_UnitGroupTestPlane(jass_t *j)        { return jass_pushboolean(j, false); }
static uint32_t sc2_UnitFilter(jass_t *j)                { return jass_pushnullhandle(j, "unitfilter"); }
static uint32_t sc2_UnitFilterMatch(jass_t *j)           { return jass_pushboolean(j, false); }
static uint32_t sc2_UnitFilterSetState(jass_t *j)        { (void)j; return jass_pushnull(j); }
static uint32_t sc2_UnitFilterStr(jass_t *j)             { return jass_pushnullhandle(j, "unitfilter"); }
static uint32_t sc2_UnitGroup(jass_t *j)                 { return jass_pushnullhandle(j, "unitgroup"); }

/* UnitType */
static uint32_t sc2_UnitTypeFromString(jass_t *j)          { return jass_pushstring(j, ""); }
static uint32_t sc2_UnitTypeGetCost(jass_t *j)             { return jass_pushinteger(j, 0); }
static uint32_t sc2_UnitTypeGetName(jass_t *j)             { return jass_pushstring(j, ""); }
static uint32_t sc2_UnitTypeGetProperty(jass_t *j)         { return jass_pushinteger(j, 0); }
static uint32_t sc2_UnitTypeIsAffectedByUpgrade(jass_t *j) { return jass_pushboolean(j, false); }
static uint32_t sc2_UnitTypeTestAttribute(jass_t *j)       { return jass_pushboolean(j, false); }
static uint32_t sc2_UnitTypeTestFlag(jass_t *j)            { return jass_pushboolean(j, false); }
static uint32_t sc2_UnitTypeAnimationLoad(jass_t *j)       { (void)j; return jass_pushnull(j); }
static uint32_t sc2_UnitTypeAnimationUnload(jass_t *j)     { (void)j; return jass_pushnull(j); }

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
        sc2UnitState_t *state=ent && sc2_galaxy_unit_state ? sc2_galaxy_unit_state(ent) : NULL;
        if (!ent) { *count=0; continue; }
        if (state && (!SC2_UnitAlive(state) || (state->states & (1u<<SC2_UNIT_PAUSED)))) continue;
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
