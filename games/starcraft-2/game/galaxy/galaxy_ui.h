/* galaxy_ui.h — UI, objective, ping, and help-panel natives */
/* Objectives are server-owned script state; IDs remain stable until map shutdown. */
#define BZ_SC2_OBJECTIVES 256 // objectives/map; bounds script-owned mission records; used as objective capacity

typedef struct { string_t name, desc; int32_t state; bool primary, visible; } SC2OBJECTIVE;
typedef SC2OBJECTIVE *LPSC2OBJECTIVE;
typedef const SC2OBJECTIVE *LPCSC2OBJECTIVE;
static SC2OBJECTIVE sc2_objs[BZ_SC2_OBJECTIVES];
static int32_t sc2_obj_n, sc2_obj_last;

static void sc2_objectives_reset(void) {
    for (int32_t i = 0; i < sc2_obj_n; i++) { free(sc2_objs[i].name); free(sc2_objs[i].desc); }
    memset(sc2_objs, 0, sizeof(sc2_objs));
    sc2_obj_n = sc2_obj_last = 0;
}

/* Invalid IDs have the authored Unknown state; mutators diagnose stale references. */
static LPSC2OBJECTIVE sc2_objective(LPJASS j) {
    int32_t id = jass_checkinteger(j, 1);
    return id > 0 && id <= sc2_obj_n && sc2_objs[id - 1].name ? &sc2_objs[id - 1] : NULL;
}

/* Legacy ObjectiveCreate implicitly shows the objective; Create3 adds explicit visibility before primary. */
static uint32_t sc2_objective_create(LPJASS j, bool legacy) {
    cstring_t name = jass_checkstring(j, 1), desc = jass_checkstring(j, 2);
    if (sc2_obj_n == BZ_SC2_OBJECTIVES) jass_rterror(j, "ObjectiveCreate: objective capacity exhausted");
    SC2OBJECTIVE obj = { .state = jass_checkinteger(j, 3), .visible = legacy || jass_checkboolean(j, 4),
        .primary = jass_checkboolean(j, legacy ? 4 : 5) };
    obj.name = strdup(name ? name : ""); obj.desc = strdup(desc ? desc : "");
    sc2_objs[sc2_obj_n++] = obj;
    sc2_obj_last = sc2_obj_n;
#ifdef SC2_DEBUG_CUTSCENE
    fprintf(stderr, "SC2 objective: id=%ld state=%ld primary=%d name=%s\n", (long)sc2_obj_last, (long)sc2_objs[sc2_obj_n - 1].state, sc2_objs[sc2_obj_n - 1].primary, name);
#endif
    return jass_pushinteger(j, sc2_obj_last);
}

static uint32_t sc2_ObjectiveCreate(LPJASS j) { return sc2_objective_create(j, true); }
static uint32_t sc2_ObjectiveCreate3(LPJASS j) { return sc2_objective_create(j, false); }
static uint32_t sc2_ObjectiveLastCreated(LPJASS j) { return jass_pushinteger(j, sc2_obj_last); }
static uint32_t sc2_ObjectiveGetState(LPJASS j) { LPSC2OBJECTIVE obj = sc2_objective(j); return jass_pushinteger(j, obj ? obj->state : -1); }
static uint32_t sc2_ObjectiveGetPrimary(LPJASS j) { LPSC2OBJECTIVE obj = sc2_objective(j); return jass_pushboolean(j, obj && obj->primary); }
static uint32_t sc2_ObjectiveGetName(LPJASS j) { LPSC2OBJECTIVE obj = sc2_objective(j); return jass_pushstring(j, obj ? obj->name : ""); }
static uint32_t sc2_ObjectiveGetDescription(LPJASS j) { LPSC2OBJECTIVE obj = sc2_objective(j); return jass_pushstring(j, obj ? obj->desc : ""); }

/* Copy text before the native call releases its argument stack. */
static uint32_t sc2_ObjectiveSetName(LPJASS j) {
    LPSC2OBJECTIVE obj = sc2_objective(j);
    cstring_t name = jass_checkstring(j, 2);
    if (!obj) jass_rterror(j, "ObjectiveSetName: invalid objective");
    free(obj->name); obj->name = strdup(name ? name : "");
    return 0;
}

static uint32_t sc2_ObjectiveSetState(LPJASS j) {
    LPSC2OBJECTIVE obj = sc2_objective(j);
    if (!obj) jass_rterror(j, "ObjectiveSetState: invalid objective");
    obj->state = jass_checkinteger(j, 2);
    return 0;
}

static uint32_t sc2_ObjectiveDestroy(LPJASS j) {
    LPSC2OBJECTIVE obj = sc2_objective(j);
    if (!obj) jass_rterror(j, "ObjectiveDestroy: invalid objective");
    free(obj->name); free(obj->desc); memset(obj, 0, sizeof(*obj));
    return 0;
}

static uint32_t sc2_PingLastCreated(LPJASS j)    { return jass_pushinteger(j, 0); }
static uint32_t sc2_PingDestroy(LPJASS j)        { (void)j; return jass_pushnull(j); }
static uint32_t sc2_PingSetScale(LPJASS j)       { (void)j; return jass_pushnull(j); }
static uint32_t sc2_PingSetTooltip(LPJASS j)     { (void)j; return jass_pushnull(j); }
static uint32_t sc2_MinimapPing(LPJASS j)        { (void)j; return jass_pushnull(j); }
static uint32_t sc2_UISetMode(LPJASS j)          { (void)j; return jass_pushnull(j); }
static uint32_t sc2_UIAlertPoint(LPJASS j)       { (void)j; return jass_pushnull(j); }
static uint32_t sc2_UIAlertUnit(LPJASS j)        { (void)j; return jass_pushnull(j); }
static uint32_t sc2_UISetFrameVisible(LPJASS j)  { (void)j; return jass_pushnull(j); }
static uint32_t sc2_HelpPanelAddTip(LPJASS j)    { (void)j; return jass_pushnull(j); }
static uint32_t sc2_HelpPanelDisplayPage(LPJASS j){ (void)j; return jass_pushnull(j); }
static uint32_t sc2_HelpPanelEnableTechTreeButton(LPJASS j){ (void)j; return jass_pushnull(j); }
static uint32_t sc2_DialogControlSetPropertyAsText(LPJASS j){ (void)j; return jass_pushnull(j); }
static uint32_t sc2_DialogControlSetVisible(LPJASS j)       { (void)j; return jass_pushnull(j); }
static uint32_t sc2_HelpPanelAddTutorial(LPJASS j)          { (void)j; return jass_pushnull(j); }
static uint32_t sc2_HelpPanelShowTechTreeRace(LPJASS j)     { (void)j; return jass_pushnull(j); }
static uint32_t sc2_PingCreate(LPJASS j)                    { (void)j; return jass_pushnull(j); }
static uint32_t sc2_UIClearMessages(LPJASS j)               { (void)j; return jass_pushnull(j); }
static uint32_t sc2_UIFlyerHelperClearOverride(LPJASS j)    { (void)j; return jass_pushnull(j); }
static uint32_t sc2_UIFlyerHelperOverride(LPJASS j)         { (void)j; return jass_pushnull(j); }
static uint32_t sc2_UIFrameVisible(LPJASS j)                { (void)j; return jass_pushboolean(j, false); }
static uint32_t sc2_UISetCursorVisible(LPJASS j)            { (void)j; return jass_pushnull(j); }
static uint32_t sc2_UISetGameMenuItemVisible(LPJASS j)      { (void)j; return jass_pushnull(j); }
static uint32_t sc2_UISetRestartLoadingScreen(LPJASS j)     { (void)j; return jass_pushnull(j); }
