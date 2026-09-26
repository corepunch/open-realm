/* galaxy_actor.h — actor, bank, conversation, achievement, and timer natives */

#define MAX_GALAXY_ACTORS 1024 // actors/map; bounds script actor records; used for actor identity
#define BZ_SC2_SCOPES (MAX_GALAXY_UNITS + MAX_GALAXY_ACTORS) // scopes/map; one per unit and created actor; used for scope ownership

typedef struct {
    uint32_t id;
    char  model[256];   /* actor type / model link from ActorCreate */
    int32_t scope;
    int32_t  unit_id;      /* owning unit handle (1-based), or 0 for world actors */
    float x, y;
} sc2GActor_t;

static sc2GActor_t sc2_gactors[MAX_GALAXY_ACTORS];
static uint32_t sc2_gactor_n;
static int32_t  sc2_last_actor_handle;

typedef struct { int32_t parent, unit; bool live; } sc2Scope_t;


static sc2Scope_t sc2_scopes[BZ_SC2_SCOPES];
static int32_t sc2_scope_n, sc2_scope_last;
static int32_t sc2_unit_scope[MAX_GALAXY_UNITS];

/* Scope ownership is independent of actor identity: killing an attachment must not kill the unit scope. */
static int32_t sc2_scope_create(jass_t *j, int32_t parent, int32_t unit) {
    if (sc2_scope_n == BZ_SC2_SCOPES) jass_rterror(j, "ActorCreate: scope capacity exhausted");
    sc2_scopes[sc2_scope_n++] = (sc2Scope_t){ .parent = parent, .unit = unit, .live = true };
    sc2_scope_last = sc2_scope_n;
    return sc2_scope_last;
}

static int32_t sc2_actor_h(jass_t *j, int idx) {
    return (int32_t)(uintptr_t)jass_checkhandle(j, idx, "actor");
}

/* NativeLib passes scope first, then actor and three content links; the old reversed ABI asserted on a live scope. */
static uint32_t sc2_ActorCreate(jass_t *j) {
    cstring_t type   = jass_checkstring(j, 2);
    int32_t   scope_h = (int32_t)(uintptr_t)jass_checkhandle(j, 1, "actorscope");
    if (sc2_gactor_n >= MAX_GALAXY_ACTORS)
        jass_rterror(j, "ActorCreate: actor capacity exhausted");
    if (scope_h < 0 || scope_h > sc2_scope_n || (scope_h && !sc2_scopes[scope_h - 1].live))
        jass_rterror(j, "ActorCreate: invalid parent scope");
    int32_t h = (int32_t)(++sc2_gactor_n);
    sc2GActor_t *a = &sc2_gactors[h - 1];
    memset(a, 0, sizeof(*a));
    a->id = (uint32_t)h;
    if (type) strlcpy(a->model, type, sizeof(a->model));
    a->unit_id = scope_h ? sc2_scopes[scope_h - 1].unit : 0;
    a->scope = sc2_scope_create(j, scope_h, a->unit_id);
    sc2_last_actor_handle = h;
    if (sc2_galaxy_on_actor_create)
        sc2_galaxy_on_actor_create((uint32_t)h, a->model, (uint32_t)a->unit_id, a->x, a->y);
    return jass_pushlighthandle(j, (handle_t)(uintptr_t)h, "actor");
}

/* ActorSend: actor, msg — dispatch to renderer; handle "Destroy" locally */
static uint32_t sc2_ActorSend(jass_t *j) {
    int32_t   h   = sc2_actor_h(j, 1);
    cstring_t msg = jass_checkstring(j, 2);
    if (h > 0 && h <= (int32_t)sc2_gactor_n && msg) {
        if (sc2_galaxy_on_actor_send)
            sc2_galaxy_on_actor_send((uint32_t)h, msg);
        if (strcmp(msg, "Destroy") == 0)
            memset(&sc2_gactors[h - 1], 0, sizeof(sc2_gactors[0]));
    }
    return jass_pushnull(j);
}

/* A unit owns one stable root scope, shared by its script-created attachments. */
static uint32_t sc2_ActorScopeFromUnit(jass_t *j) {
    int32_t h = (int32_t)(uintptr_t)jass_checkhandle(j, 1, "unit");
    if (h <= 0 || h > (int32_t)sc2_gunit_n)
        return jass_pushnullhandle(j, "actorscope");
    if (!sc2_unit_scope[h - 1]) sc2_unit_scope[h - 1] = sc2_scope_create(j, 0, h);
    return jass_pushlighthandle(j, (handle_t)(uintptr_t)sc2_unit_scope[h - 1], "actorscope");
}

/* NativeLib retrieves both hosted actors through the engine's last-created reference. */
static uint32_t sc2_ActorFrom(jass_t *j) {
    cstring_t ref = jass_checkstring(j, 1);
    if (ref && !strcmp(ref, "::LastCreated"))
        return jass_pushlighthandle(j, (handle_t)(uintptr_t)(sc2_last_actor_handle ? sc2_gactors[sc2_last_actor_handle - 1].id : 0), "actor");
    jass_rterror(j, "ActorFrom: unsupported actor reference");
    return 0;
}

/* NativeLib uses the last-created scope to remove a transmission icon after its wait completes. */
static uint32_t sc2_ActorScopeFrom(jass_t *j) {
    cstring_t ref = jass_checkstring(j, 1);
    if (ref && !strcmp(ref, "::LastCreated"))
        return jass_pushlighthandle(j, (handle_t)(uintptr_t)sc2_scope_last, "actorscope");
    jass_rterror(j, "ActorScopeFrom: unsupported scope reference");
    return 0;
}

static uint32_t sc2_ActorScopeFromActor(jass_t *j) {
    int32_t h = sc2_actor_h(j, 1);
    int32_t scope = h > 0 && h <= (int32_t)sc2_gactor_n ? sc2_gactors[h - 1].scope : 0;
    return jass_pushlighthandle(j, (handle_t)(uintptr_t)scope, "actorscope");
}

/* Child scopes are allocated after parents, allowing one forward pass to destroy the subtree. */
static uint32_t sc2_ActorScopeKill(jass_t *j) {
    int32_t scope = (int32_t)(uintptr_t)jass_checkhandle(j, 1, "actorscope");
    if (!scope) return 0;
    if (scope < 1 || scope > sc2_scope_n) jass_rterror(j, "ActorScopeKill: invalid scope");
    sc2_scopes[scope - 1].live = false;
    for (int32_t i = scope; i < sc2_scope_n; i++) {
        int32_t parent = sc2_scopes[i].parent;
        if (parent && !sc2_scopes[parent - 1].live) sc2_scopes[i].live = false;
    }
    for (uint32_t i = 0; i < sc2_gactor_n; i++) {
        sc2GActor_t *actor = &sc2_gactors[i];
        if (actor->id && !sc2_scopes[actor->scope - 1].live) {
            if (sc2_galaxy_on_actor_destroy) sc2_galaxy_on_actor_destroy(actor->id);
            memset(actor, 0, sizeof(*actor));
        }
    }
    return 0;
}

static uint32_t sc2_ActorFromScope(jass_t *j)      { (void)j; return jass_pushnullhandle(j, "actor"); }
static uint32_t sc2_ActorRegionCreate(jass_t *j)   { (void)j; return jass_pushnullhandle(j, "actorscope"); }
static uint32_t sc2_ActorRegionSend(jass_t *j)     { (void)j; return jass_pushnull(j); }

static uint32_t sc2_AchievementAward(jass_t *j)              { (void)j; return jass_pushnull(j); }
static uint32_t sc2_AchievementErase(jass_t *j)              { (void)j; return jass_pushnull(j); }
static uint32_t sc2_AchievementPanelSetCategory(jass_t *j)   { (void)j; return jass_pushnull(j); }
static uint32_t sc2_AchievementPanelSetVisible(jass_t *j)    { (void)j; return jass_pushnull(j); }
static uint32_t sc2_AchievementPercentText(jass_t *j)        { (void)j; return jass_pushnull(j); }
static uint32_t sc2_AchievementTermQuantitySet(jass_t *j)    { (void)j; return jass_pushnull(j); }
static uint32_t sc2_AchievementsDisable(jass_t *j)           { (void)j; return jass_pushnull(j); }

static uint32_t sc2_BankExists(jass_t *j)                    { (void)j; return jass_pushboolean(j, false); }
static uint32_t sc2_BankKeyRemove(jass_t *j)                 { (void)j; return jass_pushnull(j); }
static uint32_t sc2_BankLastCreated(jass_t *j)               { (void)j; return jass_pushnullhandle(j, "bank"); }
static uint32_t sc2_BankLoad(jass_t *j)                      { (void)j; return jass_pushnullhandle(j, "bank"); }
static uint32_t sc2_BankSave(jass_t *j)                      { (void)j; return jass_pushnull(j); }
static uint32_t sc2_BankValueSetFromFlag(jass_t *j)          { (void)j; return jass_pushnull(j); }
static uint32_t sc2_BankValueSetFromInt(jass_t *j)           { (void)j; return jass_pushnull(j); }
static uint32_t sc2_BankValueSetFromString(jass_t *j)        { (void)j; return jass_pushnull(j); }
static uint32_t sc2_BankValueSetFromText(jass_t *j)          { (void)j; return jass_pushnull(j); }

static uint32_t sc2_ConversationDataResetNodeState(jass_t *j)   { (void)j; return jass_pushnull(j); }
static uint32_t sc2_ConversationDataResetStateValues(jass_t *j) { (void)j; return jass_pushnull(j); }
static uint32_t sc2_ConversationDataSaveNodeState(jass_t *j)    { (void)j; return jass_pushnull(j); }
static uint32_t sc2_ConversationDataSaveStateValues(jass_t *j)  { (void)j; return jass_pushnull(j); }
static uint32_t sc2_ConversationDataStateFixedValue(jass_t *j)  { (void)j; return jass_pushnull(j); }
static uint32_t sc2_ConversationDataStateGetValue(jass_t *j)    { (void)j; return jass_pushnull(j); }
static uint32_t sc2_ConversationDataStateIndex(jass_t *j)       { (void)j; return jass_pushinteger(j, 0); }
static uint32_t sc2_ConversationDataStateIndexCount(jass_t *j)  { (void)j; return jass_pushinteger(j, 0); }
/* Presentation fields come from the layered ConversationState catalog, retained by the game module. */
static uint32_t sc2_conversation_field(jass_t *j, cstring_t field) {
    cstring_t key = jass_checkstring(j, 1);
    cstring_t value = sc2_galaxy_conversation_field(key, field);
    if (!value) jass_rterror(j, "ConversationDataState: unresolved catalog field");
    return jass_pushstring(j, value);
}
static uint32_t sc2_ConversationDataStateName(jass_t *j) { return sc2_conversation_field(j, "Name"); }
static uint32_t sc2_ConversationDataStateImagePath(jass_t *j) { return sc2_conversation_field(j, "ImagePath"); }
static uint32_t sc2_ConversationDataStateSetValue(jass_t *j)    { (void)j; return jass_pushnull(j); }
static uint32_t sc2_ConversationDataStateText(jass_t *j) {
    char field[128];
    snprintf(field, sizeof(field), "Text:%s", jass_checkstring(j, 2));
    return sc2_conversation_field(j, field);
}

static uint32_t sc2_TimerPause(jass_t *j)                    { (void)j; return jass_pushnull(j); }
