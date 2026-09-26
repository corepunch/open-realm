/* galaxy_ui.h — UI, objective, ping, and help-panel natives */
/* Objectives are server-owned script state; IDs remain stable until map shutdown. */
#define BZ_SC2_OBJECTIVES 256 // objectives/map; bounds script-owned mission records; used as objective capacity

typedef struct { string_t name, desc; int32_t state; bool primary, visible; } sc2Objective_t;


static sc2Objective_t sc2_objs[BZ_SC2_OBJECTIVES];
static int32_t sc2_obj_n, sc2_obj_last;

static void sc2_objectives_reset(void) {
    for (int32_t i = 0; i < sc2_obj_n; i++) { free(sc2_objs[i].name); free(sc2_objs[i].desc); }
    memset(sc2_objs, 0, sizeof(sc2_objs));
    sc2_obj_n = sc2_obj_last = 0;
}

/* Invalid IDs have the authored Unknown state; mutators diagnose stale references. */
static sc2Objective_t * sc2_objective(jass_t * j) {
    int32_t id = jass_checkinteger(j, 1);
    return id > 0 && id <= sc2_obj_n && sc2_objs[id - 1].name ? &sc2_objs[id - 1] : NULL;
}

/* Legacy ObjectiveCreate implicitly shows the objective; Create3 adds explicit visibility before primary. */
static uint32_t sc2_objective_create(jass_t * j, bool legacy) {
    cstring_t name = jass_checkstring(j, 1), desc = jass_checkstring(j, 2);
    if (sc2_obj_n == BZ_SC2_OBJECTIVES) jass_rterror(j, "ObjectiveCreate: objective capacity exhausted");
    sc2Objective_t obj = { .state = jass_checkinteger(j, 3), .visible = legacy || jass_checkboolean(j, 4),
        .primary = jass_checkboolean(j, legacy ? 4 : 5) };
    obj.name = strdup(name ? name : ""); obj.desc = strdup(desc ? desc : "");
    sc2_objs[sc2_obj_n++] = obj;
    sc2_obj_last = sc2_obj_n;
#ifdef SC2_DEBUG_CUTSCENE
    fprintf(stderr, "SC2 objective: id=%ld state=%ld primary=%d name=%s\n", (long)sc2_obj_last, (long)sc2_objs[sc2_obj_n - 1].state, sc2_objs[sc2_obj_n - 1].primary, name);
#endif
    return jass_pushinteger(j, sc2_obj_last);
}

static uint32_t sc2_ObjectiveCreate(jass_t * j) { return sc2_objective_create(j, true); }
static uint32_t sc2_ObjectiveCreate3(jass_t * j) { return sc2_objective_create(j, false); }
static uint32_t sc2_ObjectiveLastCreated(jass_t * j) { return jass_pushinteger(j, sc2_obj_last); }
static uint32_t sc2_ObjectiveGetState(jass_t * j) { sc2Objective_t * obj = sc2_objective(j); return jass_pushinteger(j, obj ? obj->state : -1); }
static uint32_t sc2_ObjectiveGetPrimary(jass_t * j) { sc2Objective_t * obj = sc2_objective(j); return jass_pushboolean(j, obj && obj->primary); }
static uint32_t sc2_ObjectiveGetName(jass_t * j) { sc2Objective_t * obj = sc2_objective(j); return jass_pushstring(j, obj ? obj->name : ""); }
static uint32_t sc2_ObjectiveGetDescription(jass_t * j) { sc2Objective_t * obj = sc2_objective(j); return jass_pushstring(j, obj ? obj->desc : ""); }

/* Copy text before the native call releases its argument stack. */
static uint32_t sc2_ObjectiveSetName(jass_t * j) {
    sc2Objective_t * obj = sc2_objective(j);
    cstring_t name = jass_checkstring(j, 2);
    if (!obj) jass_rterror(j, "ObjectiveSetName: invalid objective");
    free(obj->name); obj->name = strdup(name ? name : "");
    return 0;
}

static uint32_t sc2_ObjectiveSetState(jass_t * j) {
    sc2Objective_t * obj = sc2_objective(j);
    if (!obj) jass_rterror(j, "ObjectiveSetState: invalid objective");
    obj->state = jass_checkinteger(j, 2);
    return 0;
}

static uint32_t sc2_ObjectiveDestroy(jass_t * j) {
    sc2Objective_t * obj = sc2_objective(j);
    if (!obj) jass_rterror(j, "ObjectiveDestroy: invalid objective");
    free(obj->name); free(obj->desc); memset(obj, 0, sizeof(*obj));
    return 0;
}

static uint32_t sc2_PingLastCreated(jass_t * j)    { return jass_pushinteger(j, 0); }
static uint32_t sc2_PingDestroy(jass_t * j)        { (void)j; return jass_pushnull(j); }
static uint32_t sc2_PingSetScale(jass_t * j)       { (void)j; return jass_pushnull(j); }
static uint32_t sc2_PingSetTooltip(jass_t * j)     { (void)j; return jass_pushnull(j); }
static uint32_t sc2_MinimapPing(jass_t * j)        { (void)j; return jass_pushnull(j); }
static uint32_t sc2_UISetMode(jass_t * j)          { (void)j; return jass_pushnull(j); }
static uint32_t sc2_UIAlertPoint(jass_t * j)       { (void)j; return jass_pushnull(j); }
static uint32_t sc2_UIAlertUnit(jass_t * j)        { (void)j; return jass_pushnull(j); }
static uint32_t sc2_UISetFrameVisible(jass_t * j)  { (void)j; return jass_pushnull(j); }
static uint32_t sc2_HelpPanelAddTip(jass_t * j)    { (void)j; return jass_pushnull(j); }
static uint32_t sc2_HelpPanelDisplayPage(jass_t * j){ (void)j; return jass_pushnull(j); }
static uint32_t sc2_HelpPanelEnableTechTreeButton(jass_t * j){ (void)j; return jass_pushnull(j); }
static uint32_t sc2_DialogControlSetPropertyAsText(jass_t * j){ (void)j; return jass_pushnull(j); }
static uint32_t sc2_DialogControlSetVisible(jass_t * j)       { (void)j; return jass_pushnull(j); }
static uint32_t sc2_HelpPanelAddTutorial(jass_t * j)          { (void)j; return jass_pushnull(j); }
static uint32_t sc2_HelpPanelShowTechTreeRace(jass_t * j)     { (void)j; return jass_pushnull(j); }
static uint32_t sc2_PingCreate(jass_t * j)                    { (void)j; return jass_pushnull(j); }
static uint32_t sc2_UIClearMessages(jass_t * j)               { (void)j; return jass_pushnull(j); }
static uint32_t sc2_UIFlyerHelperClearOverride(jass_t * j)    { (void)j; return jass_pushnull(j); }
static uint32_t sc2_UIFlyerHelperOverride(jass_t * j)         { (void)j; return jass_pushnull(j); }
static uint32_t sc2_UIFrameVisible(jass_t * j)                { (void)j; return jass_pushboolean(j, false); }
static uint32_t sc2_UISetCursorVisible(jass_t * j)            { (void)j; return jass_pushnull(j); }
static uint32_t sc2_UISetGameMenuItemVisible(jass_t * j)      { (void)j; return jass_pushnull(j); }
static uint32_t sc2_UISetRestartLoadingScreen(jass_t * j)     { (void)j; return jass_pushnull(j); }
