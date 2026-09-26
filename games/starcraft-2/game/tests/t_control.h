/* Included by g_sc2.c so tests drive the production command handlers and movement state. */
#include "shared/test.h"
#include "renderer/r_game.h"

static int32_t sc2_test_wire[80];
static uint32_t sc2_test_count, sc2_test_time;
static void sc2_test_write(pfWriteType_t type, void const *value) {
    if (type == PF_BYTE || type == PF_LONG) sc2_test_wire[sc2_test_count++] = *(int32_t const *)value;
}
static void sc2_test_unicast(edict_t *ent) { (void)ent; }
static void sc2_test_link(edict_t *ent) { (void)ent; }
static uint32_t sc2_test_clock(void) { return sc2_test_time; }

/* Compare the renderer's native model front with a completed authoritative move step. */
static void sc2_test_model_follows_step(edict_t const *ent, vector2_t previous) {
    entityState_t state = ent->s;
    model_t model = { .modeltype = ID_43DM };
    renderEntity_t render = { .model = &model, .origin = state.origin, .angle = state.angle, .scale = 1 };
    matrix4_t matrix;
    R_GetEntityMatrix(&render, &matrix);
    vector3_t front = Matrix4_multiply_vector3(&matrix, &MAKE(vector3_t, 0, -1, 0));
    vector2_t forward = { front.x - state.origin.x, front.y - state.origin.y };
    vector2_t step = Vector2_sub(&ent->s.origin2, &previous);
    Vector2_normalize(&step); Vector2_normalize(&forward);
    T_ASSERT(forward.x * step.x + forward.y * step.y > 0.9999f);
}

/* Test invalid requests after a valid selection as well as owner filtering and replacement. */
TEST(sc2_control, selection_orders_and_clear) {
    struct game_import saved = gi;
    animation_t anims[] = { { .name = "Stand", .interval = {0, 1000} }, { .name = "Walk", .interval = {1000, 2000} } };
    g_cmodel_t model = g_models[1];
    g_models[1].animations = anims; g_models[1].num_animations = 2;
    gi.Write = sc2_test_write; gi.unicast = sc2_test_unicast;
    gi.LinkEntity = sc2_test_link; gi.GetTime = sc2_test_clock;
    memset(sc2_edicts, 0, sizeof(sc2_edicts));
    memset(sc2_move, 0, sizeof(sc2_move));
    globals.num_edicts = 4;
    sc2_edicts[0].client = &sc2_clients[0]; sc2_clients[0].ps.number = 1;
    for (uint32_t i = 1; i < 4; i++) {
        sc2_edicts[i] = (edict_t){ .inuse = true, .s = { .number = i, .player = i == 3 ? 2 : 1, .model = 1 } };
        sc2_move[i].mobile = true;
    }
    sc2_test_count = 0;
    SC2_ClientCommand(sc2_edicts, 5, (cstring_t[]){"select", "1", "1", "3", "99999"});
    T_EQ(sc2_edicts[1].selected, 2); T_EQ(sc2_edicts[3].selected, 0);
    T_EQ(sc2_test_wire[0], svc_set_selection); T_EQ(sc2_test_wire[1], 1); T_EQ(sc2_test_wire[2], 1);
    entityState_t state = sc2_edicts[1].s;
    SC2_CustomizeEntity(1, &sc2_edicts[1], &state); T_ASSERT(!(state.flags & EF_NOT_SELECTABLE));
    SC2_CustomizeEntity(2, &sc2_edicts[1], &state); T_ASSERT(state.flags & EF_NOT_SELECTABLE);
    sc2_test_count = 0;
    SC2_ClientCommand(sc2_edicts, 3, (cstring_t[]){"smartpoint", "2.5", "1.5"});
    T_ASSERT(sc2_move[1].moving); T_ASSERT(!sc2_move[3].moving);
    T_FEQ(sc2_move[1].target.x, 2.5f, 0.001f);
    SC2_RunUnit(&sc2_edicts[1]);
    T_ASSERT(sc2_edicts[1].s.origin2.x > 0); T_EQ(sc2_edicts[1].s.ability, 1);
    FOR_LOOP(i, 20) SC2_RunUnit(&sc2_edicts[1]);
    T_ASSERT(!sc2_move[1].moving); T_EQ(sc2_edicts[1].s.ability, 0);
    T_FEQ(sc2_edicts[1].s.origin2.x, 2.5f, 0.001f);
    sc2_test_count = 0;
    SC2_ClientCommand(sc2_edicts, 2, (cstring_t[]){"select", "2"});
    T_EQ(sc2_edicts[1].selected, 0); T_EQ(sc2_edicts[2].selected, 2);
    sc2_test_count = 0;
    SC2_ClientCommand(sc2_edicts, 2, (cstring_t[]){"select", "0"});
    T_EQ(sc2_edicts[2].selected, 0); T_EQ(sc2_test_wire[1], 0);
    sc2_test_count = 0;
    SC2_ClientCommand(sc2_edicts, 3, (cstring_t[]){"smartpoint", "10", "10"});
    T_ASSERT(!sc2_move[2].moving);
    g_models[1] = model;
    gi = saved;
}

/* SC2 orders must use WC3's obstacle detour and retain exact reachable click coordinates. */
TEST(sc2_control, shared_router_detours_and_arrives) {
    struct game_import saved = gi;
    sc2Map_t *map = SC2_MapCurrent();
    sc2MapInfo_t info = map->MapInfo;
    float cell = map->cell_size;
    vector2_t origin = map->origin;
    uint8_t cells[32 * 32] = { 0 };
    animation_t anims[] = { { .name = "Stand", .interval = {0, 1000} }, { .name = "Walk", .interval = {1000, 2000} } };
    g_cmodel_t model = g_models[1];
    g_models[1].animations = anims; g_models[1].num_animations = 2;
    gi.LinkEntity = sc2_test_link; gi.GetTime = sc2_test_clock;
    memset(sc2_edicts, 0, sizeof(sc2_edicts)); memset(sc2_move, 0, sizeof(sc2_move));
    globals.num_edicts = 2;
    map->MapInfo.width = map->MapInfo.height = 32; map->cell_size = 1; map->origin = (vector2_t){0};
    FOR_LOOP(y, 20) cells[y * 32 + 16] = 2;
    CM_SetupPathMap(32, 32, cells);
    edict_t *ent = &sc2_edicts[1];
    *ent = (edict_t){ .inuse = true, .svflags = SVF_MONSTER, .collision = 0.375f,
        .s = { .number = 1, .model = 1, .origin = {8.25f, 10.25f, 0} } };
    sc2_move[1].mobile = true;
    vector2_t target = {24.375f, 10.625f};
    SC2_OrderMove(ent, &target);
    T_FEQ(sc2_move[1].target.x, target.x, 0.00001f);
    T_ASSERT(!CM_LineIsWalkableForRadius(&ent->s.origin2, &target, ent->collision));
    SC2_RunUnit(ent);
    T_ASSERT(sc2_move[1].path.valid); /* WC3's immediate A* handles a pending field. */
    bool detour = false;
    for (int i = 0; i < 300 && sc2_move[1].moving; i++) {
        vector2_t prev = ent->s.origin2;
        CM_ProcessPathJobs(BZ_PATH_WORK_BUDGET);
        SC2_RunUnit(ent);
        T_ASSERT(CM_LineIsWalkableForRadius(&prev, &ent->s.origin2, ent->collision));
        if (Vector2_distance(&prev, &ent->s.origin2) > 0.001f)
            sc2_test_model_follows_step(ent, prev);
        if (ent->s.origin2.y > 20) detour = true;
    }
    T_ASSERT(detour); T_ASSERT(!sc2_move[1].moving);
    T_FEQ(ent->s.origin2.x, target.x, 0.00001f); T_FEQ(ent->s.origin2.y, target.y, 0.00001f);
    CM_SetupPathMap(0, 0, NULL);
    map->MapInfo = info; map->cell_size = cell; map->origin = origin;
    g_models[1] = model; gi = saved;
}

/* Galaxy flight uses the same snapshots as ground units; interpolation needs every fractional server sample. */
TEST(sc2_control, cutscene_flight_preserves_positions) {
    struct game_import saved = gi;
    animation_t anims[] = { { .name = "Stand", .interval = {0, 1000} }, { .name = "Walk", .interval = {1000, 2000} } };
    g_cmodel_t model = g_models[1];
    g_models[1].animations = anims; g_models[1].num_animations = 2;
    gi.LinkEntity = sc2_test_link; gi.GetTime = sc2_test_clock;
    memset(sc2_edicts, 0, sizeof(sc2_edicts)); memset(sc2_move, 0, sizeof(sc2_move));
    globals.num_edicts = 2;
    edict_t *ent = &sc2_edicts[1];
    *ent = (edict_t){ .inuse = true, .s = { .number = 1, .model = 1, .radius = 0.375f } };
    sc2_move[1].mobile = sc2_move[1].flying = true; sc2_move[1].height = 4.375f;
    SC2_GalaxyUnitSetPosition(ent, 8.25f, 10.125f, 0);
    SC2_GalaxyUnitMove(ent, 12.375f, 13.625f);
    FOR_LOOP(i, 4) {
        SC2_RunUnit(ent);
        T_ASSERT(SC2_GalaxyUnitIsMoving(ent));
        T_ASSERT(fabsf(ent->s.origin.x - floorf(ent->s.origin.x)) > 0.001f);
        T_FEQ(ent->s.origin.z, 4.375f, 0.00001f);
    }
    g_models[1] = model; gi = saved;
}

TEST(sc2_control, cardinal_move_orders_face_displacement) {
    struct game_import saved = gi;
    animation_t anims[] = { { .name = "Stand", .interval = {0, 1000} }, { .name = "Walk", .interval = {1000, 2000} } };
    g_cmodel_t model = g_models[1];
    g_models[1].animations = anims; g_models[1].num_animations = 2;
    gi.Write = sc2_test_write; gi.unicast = sc2_test_unicast;
    gi.LinkEntity = sc2_test_link; gi.GetTime = sc2_test_clock;
    FOR_LOOP(i, 4) {
        memset(sc2_edicts, 0, sizeof(sc2_edicts)); memset(sc2_move, 0, sizeof(sc2_move));
        globals.num_edicts = 2; sc2_edicts[0].client = &sc2_clients[0]; sc2_clients[0].ps.number = 1;
        edict_t *ent = &sc2_edicts[1];
        *ent = (edict_t){ .inuse = true, .s = { .number = 1, .model = 1, .scale = 1, .player = 1, .origin = {8, 8, 0} } };
        sc2_move[1].mobile = true;
        sc2_test_count = 0;
        SC2_ClientCommand(sc2_edicts, 2, (cstring_t[]){"select", "1"});
        cstring_t points[][2] = { {"20", "8"}, {"8", "20"}, {"0", "8"}, {"8", "0"} };
        SC2_ClientCommand(sc2_edicts, 3, (cstring_t[]){"smartpoint", points[i][0], points[i][1]});
        vector2_t previous = ent->s.origin2;
        SC2_RunUnit(ent);
        T_ASSERT(sc2_move[1].moving);
        sc2_test_model_follows_step(ent, previous);
    }
    g_models[1] = model; gi = saved;
}

/* Same lifecycle as WC3: kill stops movement and selection; revival restores both. */
TEST(sc2_control, galaxy_vitals_lifecycle_and_pause) {
    struct game_import saved=gi;
    animation_t anims[]={ {.name="Stand",.interval={0,1000}}, {.name="Walk",.interval={1000,2000}}, {.name="Death",.interval={2000,3000}} };
    g_cmodel_t model=g_models[1]; g_models[1].animations=anims; g_models[1].num_animations=3;
    gi.LinkEntity=sc2_test_link; gi.UnlinkEntity=sc2_test_link; gi.GetTime=sc2_test_clock;
    memset(sc2_edicts,0,sizeof(sc2_edicts)); memset(sc2_move,0,sizeof(sc2_move)); memset(sc2_units,0,sizeof(sc2_units));
    globals.num_edicts=2; globals.max_clients=1;
    edict_t *ent=&sc2_edicts[1]; *ent=(edict_t){.inuse=true,.s={.number=1,.player=2,.model=1}};
    sc2MapObject_t object={.id=72,.name="Marine",.radius=0.375f};
    object.unit_properties[0]=73; object.unit_properties[2]=117; object.unit_properties[3]=1.5f;
    object.unit_properties[4]=17; object.unit_properties[6]=31; object.unit_properties[20]=3.75f;
    SC2_UnitInit(ent,&object); sc2_move[1].mobile=true; sc2_move[1].flying=true;
    T_ASSERT(SC2_IsSelectable(ent,2)); T_ASSERT(SC2_GalaxyUnitIsAlive(ent));
    T_ASSERT(SC2_UnitFromId(72)==ent);
    SC2_OrderMove(ent,&(vector2_t){10,0}); SC2_RunUnit(ent);
    T_ASSERT(ent->s.origin.x>0); T_FEQ(sc2_move[1].speed,3.75f,0.001f);
    float x=ent->s.origin.x, hp=sc2_units[1].vitals[0].value;
    sc2_units[1].states |= 1u<<SC2_UNIT_PAUSED; SC2_UnitChanged(ent);
    SC2_RunUnit(ent); SC2_UnitTick(ent); T_FEQ(ent->s.origin.x,x,0.001f); T_FEQ(sc2_units[1].vitals[0].value,hp,0.001f);
    sc2_units[1].states &= ~(1u<<SC2_UNIT_PAUSED); SC2_UnitChanged(ent);
    SC2_RunUnit(ent); SC2_UnitTick(ent); T_ASSERT(ent->s.origin.x>x); T_ASSERT(sc2_units[1].vitals[0].value>hp);
    ent->selected=1u<<2; SC2_UnitSetProperty(&sc2_units[1],0,0); SC2_UnitChanged(ent);
    T_ASSERT(!SC2_GalaxyUnitIsAlive(ent)); T_ASSERT(!SC2_IsSelectable(ent,2)); T_ASSERT(!sc2_move[1].moving);
    T_EQ(ent->selected,0); T_ASSERT(ent->svflags & SVF_DEADMONSTER); T_EQ(ent->s.frame,2000);
    T_ASSERT(!sc2_collision_filter(ent));
    SC2_UnitSetProperty(&sc2_units[1],0,117); SC2_UnitChanged(ent);
    T_ASSERT(SC2_GalaxyUnitIsAlive(ent)); T_ASSERT(SC2_IsSelectable(ent,2)); T_ASSERT(!(ent->svflags & SVF_DEADMONSTER));
    sc2_units[1].states |= 1u<<SC2_UNIT_HIDDEN; SC2_UnitChanged(ent);
    T_ASSERT(ent->s.renderfx & RF_HIDDEN); T_ASSERT(!SC2_IsSelectable(ent,2));
    sc2_units[1].states &= ~(1u<<SC2_UNIT_HIDDEN); SC2_UnitChanged(ent); T_ASSERT(!(ent->s.renderfx & RF_HIDDEN));
    SC2_UnitSetOwner(ent,7,false); T_EQ(ent->s.player,7);
    T_EQ((ent->s.effect_flags & EFX_TEAM_COLOR_MASK)>>EFX_TEAM_COLOR_SHIFT,3);
    SC2_UnitSetOwner(ent,2,true); T_EQ(ent->s.effect_flags & EFX_TEAM_COLOR_MASK,0);
    SC2_UnitRemove(ent); T_ASSERT(!ent->inuse); T_NULL(SC2_UnitFromId(72)); T_NULL(SC2_UnitState(ent));
    memset(sc2_units,0,sizeof(sc2_units)); g_models[1]=model; gi=saved;
}

static handle_t sc2_test_alloc(long size) { return calloc(1,(size_t)size); }
TEST(sc2_control, galaxy_resources_publish_without_camera_change) {
    struct game_import saved=gi; bool started=sc2_level.scriptsStarted;
    gi.GetTime=sc2_test_clock; sc2_level.scriptsStarted=false; globals.num_edicts=1; globals.max_clients=1;
    memset(sc2_move,0,sizeof(sc2_move)); memset(sc2_units,0,sizeof(sc2_units));
    memset(sc2_edicts,0,sizeof(sc2_edicts)); memset(sc2_players,0,sizeof(sc2_players));
    sc2_clients[0].ps.number=1;
    jass_sethost(&(jassHost_t){.MemAlloc=sc2_test_alloc,.MemFree=free,.galaxy_natives=galaxy_get_natives()});
    jass_t *vm=jass_newstate();
    char script[]="native void PlayerModifyPropertyInt(int p,int prop,int op,int value);"
        "native void PlayerModifyPropertyFixed(int p,int prop,int op,fixed value);"
        "void main() { PlayerModifyPropertyInt(1,0,0,137); PlayerModifyPropertyInt(1,1,0,29);"
        "PlayerModifyPropertyFixed(1,4,0,3.5); PlayerModifyPropertyInt(1,5,0,19); }";
    T_ASSERT(jass_dobuffer_ex(vm,script,JASS_MODE_GALAXY)); jass_callbyname(vm,"main",false);
    T_ASSERT(!jass_rterror_pending(vm)); SC2_RunFrame();
    T_EQ(sc2_clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD],137);
    T_EQ(sc2_clients[0].ps.stats[PLAYERSTATE_RESOURCE_LUMBER],29);
    T_EQ(sc2_clients[0].ps.stats[PLAYERSTATE_RESOURCE_FOOD_USED],3);
    T_EQ(sc2_clients[0].ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP],19);
    T_FEQ(sc2_players[1].properties[4],3.5f,0.001f);
    jass_close(vm); gi=saved; sc2_level.scriptsStarted=started;
}
