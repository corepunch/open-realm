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
static float sc2_ux[MAX_GALAXY_UNITS], sc2_uy[MAX_GALAXY_UNITS];
static uint8_t sc2_uxy_set[MAX_GALAXY_UNITS];
static void sc2_ev_spatial(jass_t *j, int32_t unit, float ox, float oy, float nx, float ny);
static void sc2_ev_note_xy(int32_t h, float x, float y) {
    if (h <= 0 || h > MAX_GALAXY_UNITS) return;
    sc2_ux[h - 1] = x; sc2_uy[h - 1] = y; sc2_uxy_set[h - 1] = 1;
}

typedef struct { char ability[64]; float x, y; int32_t order_h; bool started; } sc2GUnitOrder_t;
static int32_t sc2_gcargo[MAX_GALAXY_UNITS][MAX_CARGO_PER_UNIT];
static int32_t sc2_gcargo_n[MAX_GALAXY_UNITS];
static int32_t sc2_gtransport[MAX_GALAXY_UNITS]; /* carrying unit handle while loaded; 0 once unloaded */
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
        if (handle) {
            sc2_group_append(j,&sc2_unit_groups[sc2_last_unit_group],handle);
            sc2_ev_note_xy(handle, x, y);
            /* Creation is its own event; EventUnit and EventUnitCreatedUnit are the new unit. */
            sc2_ev_emit(j, (sc2evresp_t){ .type = SC2_EV_CREATED, .unit = handle, .created = handle, .player = player });
        }
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
    if (ent) sc2_galaxy_unit_set_owner(ent,player,color);
    return 0;
}
static uint32_t sc2_unit_life(jass_t *j, bool revive) {
    int32_t h = (int32_t)(uintptr_t)jass_checkhandle(j,1,"unit"); void *ent = sc2_ent_from_handle(j,1);
    sc2UnitState_t *u = sc2_unit_data(j,ent);
    if (!u) return 0;
    float old = SC2_UnitProperty(u, 0); bool was_alive = SC2_UnitAlive(u);
    SC2_UnitSetProperty(u, 0, revive ? u->vitals[0].max_value : 0); sc2_uorder_n[h-1]=0; sc2_unit_changed(j,ent);
    /* Life is a property event, then death or revival is its own event so each callback sees its response.
     * Killing a dead unit or reviving a live one changes nothing and publishes nothing. */
    if (SC2_UnitProperty(u, 0) != old)
        sc2_ev_emit(j, (sc2evresp_t){ .type = SC2_EV_PROP, .unit = h, .player = sc2_ev_owner(h), .ival = 0 });
    if (was_alive != SC2_UnitAlive(u))
        sc2_ev_emit(j, (sc2evresp_t){ .type = revive ? SC2_EV_REVIVE : SC2_EV_DIED, .unit = h, .player = sc2_ev_owner(h) });
    return 0;
}
static uint32_t sc2_UnitKill(jass_t *j) { return sc2_unit_life(j,false); }
static uint32_t sc2_UnitRevive(jass_t *j) { return sc2_unit_life(j,true); }
static uint32_t sc2_UnitRemove(jass_t *j) {
    int32_t h = (int32_t)(uintptr_t)jass_checkhandle(j,1,"unit"); void *ent = sc2_ent_from_handle(j,1);
    if (!ent) return 0;
    sc2_ev_emit(j, (sc2evresp_t){ .type = SC2_EV_REMOVED, .unit = h, .player = sc2_ev_owner(h) });
    if (!sc2_galaxy_unit_remove) { jass_rterror(j,"Galaxy removal callback unavailable"); return 0; }
    sc2_galaxy_unit_remove(ent); sc2_gunits[h-1]=NULL; sc2_uorder_n[h-1]=0;
    for (int i=1;i<sc2_unit_group_n;i++) sc2_group_remove(&sc2_unit_groups[i],h);
    for (uint32_t i=0;i<sc2_gunit_n;i++) for (int32_t k=0;k<sc2_gcargo_n[i];k++) if (sc2_gcargo[i][k]==h) {
        memmove(sc2_gcargo[i]+k,sc2_gcargo[i]+k+1,(--sc2_gcargo_n[i]-k)*sizeof(int32_t)); break;
    }
    sc2_gcargo_n[h-1]=0; sc2_gtransport[h-1]=0;
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
    int32_t h=(int32_t)(uintptr_t)jass_checkhandle(j,1,"unit");
    void *ent=sc2_ent_from_handle(j,1); sc2UnitState_t *u=sc2_unit_data(j,ent);
    int prop=sc2_checked_index(j,2,24); float value=integral ? jass_checkinteger(j,3) : jass_checknumber(j,3);
    float old=u ? SC2_UnitProperty(u,prop) : 0;
    if (u && !SC2_UnitSetProperty(u,prop,value)) { jass_rterror(j,"Unit property is read-only or value is nonfinite"); return 0; }
    if (u) sc2_unit_changed(j,ent);
    if (u && SC2_UnitProperty(u,prop) != old)
        sc2_ev_emit(j,(sc2evresp_t){ .type=SC2_EV_PROP, .unit=h, .player=sc2_ev_owner(h), .ival=prop });
    return 0;
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
    /* Writable indexes from natives.galaxy. Read-only 0-7, 13-15, 22-23, and 28 still abort.
     * Targetable (18) and tooltipable (20) are stored flags; rejecting them aborted map init. */
    if (!((1u << state) & 0x0F3F1F00u)) {
        jass_rterror(j,"Unit state is read-only or its behavior is not implemented"); return 0;
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
    int32_t h = (int32_t)(uintptr_t)jass_checkhandle(j, 1, "unit");
    void *ent = sc2_ent_from_handle(j, 1);
    int32_t pt_h = (int32_t)(uintptr_t)jass_checkhandle(j, 2, "point");
    float ox = 0, oy = 0, nx, ny;
    bool had = h > 0 && h <= MAX_GALAXY_UNITS && sc2_uxy_set[h - 1];
    sc2GPoint_t loc;
    if (had) { ox = sc2_ux[h - 1]; oy = sc2_uy[h - 1]; }
    if (ent && sc2_galaxy_unit_set_position && pt_h > 0 && pt_h < sc2_gpoint_n)
        sc2_galaxy_unit_set_position(ent, sc2_gpoints[pt_h].x, sc2_gpoints[pt_h].y, NAN);
    if (sc2_unit_location_handle(h, &loc)) { nx = loc.x; ny = loc.y; }
    else if (pt_h > 0 && pt_h < sc2_gpoint_n) { nx = sc2_gpoints[pt_h].x; ny = sc2_gpoints[pt_h].y; }
    else return jass_pushnull(j);
    if (had && (nx != ox || ny != oy)) sc2_ev_spatial(j, h, ox, oy, nx, ny);
    sc2_ev_note_xy(h, nx, ny);
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
    bool was_idle = sc2_uorder_n[unit_h - 1] == 0;
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
    queued->order_h = order_h;
    queued->started = false;
#ifdef SC2_DEBUG_CUTSCENE
    fprintf(stderr, "UnitIssueOrder: unit=%ld ability=%s target=(%.1f,%.1f) queue=%ld depth=%ld\n",
            (long)unit_h, ability, tx, ty, (long)queue, (long)*count);
#endif
    if (was_idle)
        sc2_ev_emit(j, (sc2evresp_t){ .type = SC2_EV_IDLE, .unit = unit_h, .player = sc2_ev_owner(unit_h), .ival = 0 });
    sc2evresp_t ev = { .type = SC2_EV_ORDER, .unit = unit_h, .player = sc2_ev_owner(unit_h), .order = order_h,
        .abil = ac_h, .target = ord->target_type == 2 ? ord->unit_h : 0 };
    if (ord->target_type == 1 && pt_h > 0) { ev.x = tx; ev.y = ty; ev.has_point = true; }
    sc2_ev_emit(j, ev);
    return true;
}
static uint32_t sc2_UnitIssueOrder(jass_t *j) {
    int32_t u=(int32_t)(uintptr_t)jass_checkhandle(j,1,"unit"), o=(int32_t)(uintptr_t)jass_checkhandle(j,2,"order");
    return jass_pushboolean(j,sc2_issue_unit_order(j,u,o,jass_checkinteger(j,3)));
}

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
        /* Loaded cargo rides at its transport, so the unload crossing starts there, not at the map origin. */
        sc2GPoint_t at;
        if (sc2_unit_location_handle(t_h, &at)) sc2_ev_note_xy(handle, at.x, at.y);
        sc2_gtransport[handle - 1] = t_h;
        sc2_ev_emit(j, (sc2evresp_t){ .type = SC2_EV_CREATED, .unit = handle, .created = handle, .player = player });
        sc2_ev_emit(j, (sc2evresp_t){ .type = SC2_EV_CARGO, .unit = t_h, .cargo = handle, .player = player, .ival = 1 });
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
static uint32_t sc2_UnitLoadModel(jass_t *j)            { (void)j; return jass_pushnull(j); }
static uint32_t sc2_UnitUnloadModel(jass_t *j)          { (void)j; return jass_pushnull(j); }
#include "galaxy_unitgroup.h"

static bool sc2_unit_handle_live(int32_t h) { return h > 0 && h <= (int32_t)sc2_gunit_n && sc2_gunits[h - 1]; }

/* Selection. Galaxy separates local and synchronous selection; this server has one authoritative
 * set per player, so a script's change is visible to the next query and reaches the client after the frame. */
static void sc2_unit_select(jass_t *j, int32_t h, int player, bool on) {
    if (!sc2_unit_handle_live(h)) return;
    if (!sc2_galaxy_unit_select) { jass_rterror(j,"Galaxy selection callback unavailable"); return; }
    sc2_galaxy_unit_select(sc2_gunits[h-1],player,on);
}
static uint32_t sc2_UnitSelect(jass_t *j) {
    int32_t h=(int32_t)(uintptr_t)jass_checkhandle(j,1,"unit"); int player=sc2_player_index(j,2);
    sc2_unit_select(j,h,player,jass_checkboolean(j,3)); return 0;
}
static uint32_t sc2_UnitGroupSelect(jass_t *j) {
    sc2GGroup_t *g=sc2_unit_group(j); int player=sc2_player_index(j,2); bool on=jass_checkboolean(j,3);
    if (g) for (int i=0;i<g->count;i++) sc2_unit_select(j,g->items[i],player,on);
    return 0;
}
static uint32_t sc2_UnitClearSelection(jass_t *j) {
    int player=sc2_player_index(j,1);
    for (int32_t h=1;h<=(int32_t)sc2_gunit_n;h++) sc2_unit_select(j,h,player,false);
    return 0;
}
static bool sc2_unit_is_selected(int32_t h, int player) {
    return sc2_unit_handle_live(h) && sc2_galaxy_unit_is_selected && sc2_galaxy_unit_is_selected(sc2_gunits[h-1],player);
}
static uint32_t sc2_UnitIsSelected(jass_t *j) {
    int32_t h=(int32_t)(uintptr_t)jass_checkhandle(j,1,"unit");
    return jass_pushboolean(j,sc2_unit_is_selected(h,sc2_player_index(j,2)));
}
static uint32_t sc2_UnitGroupSelected(jass_t *j) {
    int player=sc2_player_index(j,1); int32_t g=sc2_group_new(j,sc2_unit_groups,&sc2_unit_group_n,NULL);
    if (g) for (int32_t h=1;h<=(int32_t)sc2_gunit_n;h++) if (sc2_unit_is_selected(h,player)) sc2_group_append(j,&sc2_unit_groups[g],h);
    return jass_pushlighthandle(j,(handle_t)(uintptr_t)g,"unitgroup");
}

/* Cargo membership. Space values need CAbilTransport data, which the catalog does not load yet. */
static uint32_t sc2_UnitCargo(jass_t *j) {
    int32_t h=(int32_t)(uintptr_t)jass_checkhandle(j,1,"unit"), n=jass_checkinteger(j,2)-1;
    int32_t c=sc2_unit_handle_live(h) && n>=0 && n<sc2_gcargo_n[h-1] ? sc2_gcargo[h-1][n] : 0;
    return jass_pushlighthandle(j,(handle_t)(uintptr_t)(sc2_unit_handle_live(c)?c:0),"unit");
}
static uint32_t sc2_UnitTransport(jass_t *j) {
    int32_t h=(int32_t)(uintptr_t)jass_checkhandle(j,1,"unit"), t=sc2_unit_handle_live(h) ? sc2_gtransport[h-1] : 0;
    return jass_pushlighthandle(j,(handle_t)(uintptr_t)(sc2_unit_handle_live(t)?t:0),"unit");
}
enum { SC2_CARGO_UNIT_COUNT=0, SC2_CARGO_POSITION=6 }; /* natives.galaxy c_unitCargoUnitCount, c_unitCargoPosition */
static uint32_t sc2_UnitCargoValue(jass_t *j) {
    int32_t h=(int32_t)(uintptr_t)jass_checkhandle(j,1,"unit"); int value=sc2_checked_index(j,2,7), result=0;
    if (!sc2_unit_handle_live(h)) return jass_pushinteger(j,0);
    if (value==SC2_CARGO_UNIT_COUNT) result=sc2_gcargo_n[h-1];
    else if (value==SC2_CARGO_POSITION) {
        int32_t t=sc2_gtransport[h-1];
        if (t) for (int32_t i=0;i<sc2_gcargo_n[t-1];i++) if (sc2_gcargo[t-1][i]==h) result=i+1;
    } else jass_rterror(j,"Cargo space values need CAbilTransport data, which is not loaded");
    return jass_pushinteger(j,result);
}

/* Order queue queries return the handles the script issued, so identity comparisons hold. Index 0 is the current order. */
static uint32_t sc2_UnitOrderCount(jass_t *j) {
    int32_t h=(int32_t)(uintptr_t)jass_checkhandle(j,1,"unit");
    return jass_pushinteger(j,sc2_unit_handle_live(h) ? sc2_uorder_n[h-1] : 0);
}
static uint32_t sc2_UnitOrder(jass_t *j) {
    int32_t h=(int32_t)(uintptr_t)jass_checkhandle(j,1,"unit"), n=jass_checkinteger(j,2);
    int32_t o=sc2_unit_handle_live(h) && n>=0 && n<sc2_uorder_n[h-1] ? sc2_uorders[h-1][n].order_h : 0;
    return jass_pushlighthandle(j,(handle_t)(uintptr_t)o,"order");
}
static uint32_t sc2_UnitOrderHasAbil(jass_t *j) {
    int32_t h=(int32_t)(uintptr_t)jass_checkhandle(j,1,"unit"); cstring_t abil=jass_checkstring(j,2);
    if (sc2_unit_handle_live(h) && abil) for (int32_t i=0;i<sc2_uorder_n[h-1];i++)
        if (!strcmp(sc2_uorders[h-1][i].ability,abil)) return jass_pushboolean(j,true);
    return jass_pushboolean(j,false);
}

static uint32_t sc2_UnitSetAIOption(jass_t *j) {
    sc2UnitState_t *u=sc2_unit_data(j,sc2_ent_from_handle(j,1)); int option=sc2_checked_index(j,2,32);
    bool on=jass_checkboolean(j,3);
    if (u) { if (on) u->ai_options |= 1u<<option; else u->ai_options &= ~(1u<<option); }
    return 0;
}
static uint32_t sc2_UnitGetAIOption(jass_t *j) {
    sc2UnitState_t *u=sc2_unit_data(j,sc2_ent_from_handle(j,1)); int option=sc2_checked_index(j,2,32);
    return jass_pushboolean(j,u && (u->ai_options & (1u<<option)));
}
/* Speed goes back to the catalog value captured at spawn (property 20). */
static uint32_t sc2_UnitResetSpeed(jass_t *j) {
    void *ent=sc2_ent_from_handle(j,1); sc2UnitState_t *u=sc2_unit_data(j,ent);
    if (u) { u->speed=u->normal[20]; sc2_unit_changed(j,ent); }
    return 0;
}
static void sc2_unit_team_color(jass_t *j, int index) {
    void *ent=sc2_ent_from_handle(j,1);
    if (!ent) return;
    if (!sc2_galaxy_unit_team_color) { jass_rterror(j,"Galaxy team-color callback unavailable"); return; }
    sc2_galaxy_unit_team_color(ent,index);
}
static uint32_t sc2_UnitSetTeamColorIndex(jass_t *j) { sc2_unit_team_color(j,sc2_player_index(j,2)); return 0; }
static uint32_t sc2_UnitResetTeamColorIndex(jass_t *j) { sc2_unit_team_color(j,-1); return 0; }
/* Liberty natives.galaxy declares no plane constants; later releases use c_planeGround 0 and c_planeAir 1. */
static uint32_t sc2_UnitTestPlane(jass_t *j) {
    void *ent=sc2_ent_from_handle(j,1); int plane=sc2_checked_index(j,2,2);
    bool flying=ent && sc2_galaxy_unit_is_flying && sc2_galaxy_unit_is_flying(ent);
    return jass_pushboolean(j,ent && (plane==1)==flying);
}
static uint32_t sc2_UnitGroupWaitUntilIdle(jass_t *j)    { (void)j; return jass_pushnull(j); }
static uint32_t sc2_UnitInventoryGroup(jass_t *j)        { return jass_pushnullhandle(j, "unitgroup"); }
static uint32_t sc2_UnitTechTreeBehaviorCount(jass_t *j) { return jass_pushinteger(j, 0); }
static uint32_t sc2_UnitTechTreeUnitCount(jass_t *j)     { return jass_pushinteger(j, 0); }
static uint32_t sc2_UnitTechTreeUpgradeCount(jass_t *j)  { return jass_pushinteger(j, 0); }

static uint32_t sc2_UnitGroupIdle(jass_t *j)             { return jass_pushboolean(j, false); }
static uint32_t sc2_UnitGroupAlliance(jass_t *j)         { return jass_pushnullhandle(j, "unitgroup"); }
static uint32_t sc2_UnitGroupFilterAlliance(jass_t *j)   { return jass_pushnullhandle(j, "unitgroup"); }
static uint32_t sc2_UnitGroupFilterPlane(jass_t *j)      { return jass_pushnullhandle(j, "unitgroup"); }
static uint32_t sc2_UnitGroupFilterThreat(jass_t *j)     { return jass_pushnullhandle(j, "unitgroup"); }
static uint32_t sc2_UnitGroupFromId(jass_t *j)           { return jass_pushnullhandle(j, "unitgroup"); }
static uint32_t sc2_UnitGroupTestPlane(jass_t *j)        { return jass_pushboolean(j, false); }
static uint32_t sc2_UnitFilterStr(jass_t *j)             { return jass_pushnullhandle(j, "unitfilter"); }

/* Unit queries must return live registry members; an empty placeholder causes
 * campaign defeat conditions to fire even while Raynor and Marines survive. */
typedef struct { uint64_t required, excluded; } sc2UnitFilter_t;
static sc2UnitFilter_t sc2_filters[4096];
static uint32_t sc2_filter_n=1;
static uint32_t sc2_UnitFilter(jass_t *j) {
    sc2UnitFilter_t filter={
        (uint32_t)jass_checkinteger(j,1) | ((uint64_t)(uint32_t)jass_checkinteger(j,2)<<32),
        (uint32_t)jass_checkinteger(j,3) | ((uint64_t)(uint32_t)jass_checkinteger(j,4)<<32)};
    if (sc2_filter_n==4096) { jass_rterror(j,"Galaxy unit filter table full"); return 0; }
    uint32_t h=sc2_filter_n++; sc2_filters[h]=filter;
    return jass_pushlighthandle(j,(handle_t)(uintptr_t)h,"unitfilter");
}
static sc2UnitFilter_t *sc2_unit_filter(jass_t *j,int arg) {
    uint32_t h=(uint32_t)(uintptr_t)jass_checkhandle(j,arg,"unitfilter");
    if (h>=sc2_filter_n) { jass_rterror(j,"Invalid Galaxy unit filter"); return &sc2_filters[0]; }
    return &sc2_filters[h];
}
static bool sc2_filter_match(jass_t *j,int32_t h,int player,sc2UnitFilter_t const *filter) {
    if (!sc2_unit_handle_live(h)) return false;
    sc2UnitState_t *u=sc2_galaxy_unit_state ? sc2_galaxy_unit_state(sc2_gunits[h-1]) : NULL;
    if (!u) return false;
    /* Visibility/cloak/construction require systems not yet implemented.
     * Reject such queries explicitly instead of falsely satisfying conditions. */
    uint64_t supported=((1ull<<28)-1) | (1ull<<33) | (1ull<<35) | (1ull<<37) | (1ull<<38) | (1ull<<39);
    if ((filter->required|filter->excluded)&~supported) { jass_rterror(j,"Unsupported Galaxy unit filter bits"); return false; }
    uint64_t flags=u->target_flags;
    int owner=sc2_galaxy_unit_owner ? sc2_galaxy_unit_owner(sc2_gunits[h-1]) : -1;
    if (owner==player) flags|=(1ull<<1)|(1ull<<2);
    else if (!owner) flags|=1ull<<3;
    else if (player>=0 && player<32 && owner<32 && (sc2_players[player].alliances[owner]&1)) flags|=1ull<<2;
    else flags|=1ull<<4;
    bool air=sc2_galaxy_unit_is_flying && sc2_galaxy_unit_is_flying(sc2_gunits[h-1]);
    flags&=~((1ull<<5)|(1ull<<6)); flags|=1ull<<(air?5:6);
    if (u->vitals[1].max_value>0) flags|=1ull<<24;
    if (u->vitals[2].max_value>0) flags|=1ull<<25;
    if (!SC2_UnitAlive(u)) flags|=1ull<<33;
    if (u->states&(1u<<SC2_UNIT_HIDDEN)) flags|=1ull<<35;
    if (u->states&(1u<<SC2_UNIT_INVULNERABLE)) flags|=1ull<<37;
    if (u->vitals[1].value>0) flags|=1ull<<38;
    if (u->vitals[2].value>0) flags|=1ull<<39;
    return (flags & filter->required)==filter->required && !(flags & filter->excluded);
}
static uint32_t sc2_UnitFilterMatch(jass_t *j) {
    int32_t h=(int32_t)(uintptr_t)jass_checkhandle(j,1,"unit");
    return jass_pushboolean(j,sc2_filter_match(j,h,jass_checkinteger(j,2),sc2_unit_filter(j,3)));
}
static uint32_t sc2_UnitFilterSetState(jass_t *j) {
    sc2UnitFilter_t *f=sc2_unit_filter(j,1); int bit=sc2_checked_index(j,2,64),state=sc2_checked_index(j,3,3);
    f->required&=~(1ull<<bit); f->excluded&=~(1ull<<bit);
    if (state==1) f->required|=1ull<<bit;
    if (state==2) f->excluded|=1ull<<bit;
    return 0;
}
static uint32_t sc2_query_units(jass_t *j,bool from_group) {
    cstring_t type=jass_checkstring(j,1); int player=jass_checkinteger(j,2);
    int32_t source=(int32_t)(uintptr_t)jass_checkhandle(j,3,from_group?"unitgroup":"region");
    sc2UnitFilter_t *filter=sc2_unit_filter(j,4); int max=jass_checkinteger(j,5);
    int32_t result=sc2_group_new(j,sc2_unit_groups,&sc2_unit_group_n,NULL);
    if (!result) return 0;
    if (from_group && (source<=0 || source>=sc2_unit_group_n)) { jass_rterror(j,"Invalid source unit group"); return 0; }
    if (!from_group && (source<0 || source>=sc2_region_n)) { jass_rterror(j,"Invalid query region"); return 0; }
    uint32_t count=from_group ? sc2_unit_groups[source].count : sc2_gunit_n;
    for (uint32_t i=0;i<count && (max<=0 || sc2_unit_groups[result].count<max);i++) {
        int32_t h=from_group ? sc2_unit_groups[source].items[i] : (int32_t)i+1;
        if (!sc2_unit_handle_live(h)) continue;
        sc2UnitState_t *u=sc2_galaxy_unit_state ? sc2_galaxy_unit_state(sc2_gunits[h-1]) : NULL;
        if (!u || (type && *type && strcmp(type,u->type))) continue;
        if (player!=-1 && (!sc2_galaxy_unit_owner || sc2_galaxy_unit_owner(sc2_gunits[h-1])!=player)) continue;
        sc2GPoint_t point;
        if (!from_group && source && (!sc2_unit_location_handle(h,&point) || !sc2_region_has_point(&sc2_regions[source],point))) continue;
        if (sc2_filter_match(j,h,player,filter)) sc2_group_append(j,&sc2_unit_groups[result],h);
    }
    return jass_pushlighthandle(j,(handle_t)(uintptr_t)result,"unitgroup");
}
static uint32_t sc2_UnitGroup(jass_t *j) { return sc2_query_units(j,false); }
static uint32_t sc2_UnitGroupFilter(jass_t *j) { return sc2_query_units(j,true); }

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
static void sc2_run_unit_orders(jass_t *j) {
    for (int32_t unit_h = 1; unit_h <= (int32_t)sc2_gunit_n; unit_h++) {
        int32_t *count = &sc2_uorder_n[unit_h - 1];
        void *ent = sc2_gunits[unit_h - 1];
        sc2UnitState_t *state=ent && sc2_galaxy_unit_state ? sc2_galaxy_unit_state(ent) : NULL;
        if (!ent) { *count=0; continue; }
        if (state && (!SC2_UnitAlive(state) || (state->states & (1u<<SC2_UNIT_PAUSED)))) continue;
        bool finished = false;
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
                finished = *count == 0;
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
                    if (cargo_h > 0 && cargo_h <= MAX_GALAXY_UNITS) sc2_gtransport[cargo_h - 1] = 0;
                    sc2_ev_emit(j, (sc2evresp_t){ .type = SC2_EV_CARGO, .unit = unit_h, .cargo = cargo_h,
                        .player = sc2_ev_owner(unit_h), .ival = 0 });
                }
#ifdef SC2_DEBUG_CUTSCENE
                fprintf(stderr, "UnitIssueOrder: unloaded %ld cargo units at (%.1f,%.1f)\n",
                        (long)cargo_n, ord->x, ord->y);
#endif
                sc2_gcargo_n[unit_h - 1] = 0;
            }
            sc2_pop_unit_order(unit_h);
            finished = *count == 0;
        }
        if (finished)
            sc2_ev_emit(j, (sc2evresp_t){ .type = SC2_EV_IDLE, .unit = unit_h, .player = sc2_ev_owner(unit_h), .ival = 1 });
    }
}

/* Region and range callbacks see a crossing the way WC3 compares old_origin with origin2.
 * The other endpoint is its current stored position, so one mover is the supported transition. */
static int sc2_ev_cross(float od, float nd, float range) {
    if (od > range && nd <= range) return 1;
    return (od <= range && nd > range) ? 0 : -1;
}
static void sc2_ev_spatial(jass_t *j, int32_t unit, float ox, float oy, float nx, float ny) {
    int32_t player = sc2_ev_owner(unit);
    for (uint32_t i = 0; i < MAX_SC2_EVENTS; i++) {
        sc2evreg_t *r = &sc2_evregs[i];
        bool enter, want;
        if (!r->inuse) continue;
        want = (r->flags & SC2_EF_STATE) != 0;
        if (r->type == SC2_EV_REGION) {
            bool old_in, now_in;
            if (r->unit && r->unit != unit) continue;
            if (r->other <= 0 || r->other >= sc2_region_n) continue;
            old_in = sc2_region_has_point(&sc2_regions[r->other], (sc2GPoint_t){ ox, oy, 0, 0 });
            now_in = sc2_region_has_point(&sc2_regions[r->other], (sc2GPoint_t){ nx, ny, 0, 0 });
            if (old_in == now_in) continue;
            enter = !old_in && now_in;
            if (want != enter) continue;
            sc2_ev_fire_reg(j, r, (sc2evresp_t){ .unit = unit, .player = player, .region = r->other, .ival = enter });
        } else if (r->type == SC2_EV_RANGE_PT) {
            float ax, ay, od, nd;
            if (r->unit && r->unit != unit) continue;
            if (r->other <= 0 || r->other >= sc2_gpoint_n) continue;
            ax = sc2_gpoints[r->other].x; ay = sc2_gpoints[r->other].y;
            od = hypotf(ox - ax, oy - ay); nd = hypotf(nx - ax, ny - ay);
            int cross = sc2_ev_cross(od, nd, r->fval);
            enter = cross == 1;
            if (cross < 0 || want != enter) continue;
            sc2_ev_fire_reg(j, r, (sc2evresp_t){ .unit = unit, .player = player, .ival = enter, .x = ax, .y = ay, .has_point = true });
        } else if (r->type == SC2_EV_RANGE) {
            bool anchor_moved = r->other == unit && r->unit && r->unit != unit;
            int32_t subject = anchor_moved ? r->unit : unit, anchor = anchor_moved ? unit : r->other;
            float od, nd;
            sc2GPoint_t p;
            if (!r->unit && r->other == unit) continue;
            if (r->unit && r->unit != unit && !anchor_moved) continue;
            if (anchor_moved) {
                if (!sc2_uxy_set[subject - 1]) continue;
                od = hypotf(sc2_ux[subject - 1] - ox, sc2_uy[subject - 1] - oy);
                nd = hypotf(sc2_ux[subject - 1] - nx, sc2_uy[subject - 1] - ny);
            } else {
                if (!sc2_unit_location_handle(anchor, &p)) continue;
                od = hypotf(ox - p.x, oy - p.y); nd = hypotf(nx - p.x, ny - p.y);
            }
            int cross = sc2_ev_cross(od, nd, r->fval);
            enter = cross == 1;
            if (cross < 0 || want != enter) continue;
            sc2_ev_fire_reg(j, r, (sc2evresp_t){
                .unit = subject, .player = sc2_ev_owner(subject), .target = anchor, .ival = enter });
        }
    }
}
static void sc2_ev_spatial_tick(jass_t *j) {
    for (int32_t h = 1; h <= (int32_t)sc2_gunit_n; h++) {
        sc2GPoint_t p;
        int32_t carrier = sc2_gtransport[h - 1];
        if (!sc2_gunits[h - 1]) continue;
        /* Loaded cargo follows its transport silently; its own edict position is meaningless until unload. */
        if (carrier) { if (sc2_unit_location_handle(carrier, &p)) sc2_ev_note_xy(h, p.x, p.y); continue; }
        if (!sc2_unit_location_handle(h, &p)) continue;
        if (!sc2_uxy_set[h - 1]) { sc2_ev_note_xy(h, p.x, p.y); continue; }
        if (p.x == sc2_ux[h - 1] && p.y == sc2_uy[h - 1]) continue;
        sc2_ev_spatial(j, h, sc2_ux[h - 1], sc2_uy[h - 1], p.x, p.y);
        sc2_ev_note_xy(h, p.x, p.y);
    }
}
