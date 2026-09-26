/* These scenarios use the retail native declarations and the production binding table. */
static void gal_foundation_run(cstring_t body) {
    gal_state_t s = gal_new();
    uint32_t size;
    char *decl = gal_read_file("games/starcraft-2/tests/resources-src/TriggerLibs/natives.galaxy", &size);
    T_NOT_NULL(decl);
    size_t length = strlen(body) + size + 128;
    char *source = malloc(length);
    snprintf(source, length, "%s\nnative void TestFail(string message);\n%s", decl, body);
    jass_sethost(&MAKE(jassHost_t, .MemAlloc = gal_alloc, .MemFree = gal_free,
        .ReadFile = gal_read_file, .natives = gal_assert_natives, .galaxy_natives = galaxy_get_natives()));
    int ok = gal_run(&s, source);
    if (!ok) fprintf(stderr, "Galaxy foundation scenario: %s\n", s.errmsg);
    T_ASSERT(ok);
    galaxy_tick(s.j);
    free(source); free(decl); gal_destroy(&s); galaxy_reset();
}


/* Minimal entity backing; vitals use the same state and mutation functions as g_unit.h. */
typedef struct { sc2UnitState_t state; float x,y,z,facing; int owner; bool removed; } gal_entity_t;
static gal_entity_t gal_entities[16];
static int gal_entity_n, gal_changes, gal_foundation_moves;
static void gal_foundation_move(void *e,float x,float y) { gal_entity_t *unit=e; unit->x=x; unit->y=y; gal_foundation_moves++; }
static void *gal_foundation_create(cstring_t type,int player,float x,float y,float facing) {
    gal_entity_t *e=&gal_entities[gal_entity_n++]; *e=(gal_entity_t){.x=x,.y=y,.facing=facing*180/M_PI,.owner=player};
    snprintf(e->state.type,sizeof(e->state.type),"%s",type);
    e->state.vitals[0]=(sc2Vital_t){73,117,1.5f}; e->state.vitals[1]=(sc2Vital_t){17,31,0};
    e->state.states=1u<<SC2_UNIT_SELECTABLE;
    for (int p=0;p<24;p++) e->state.normal[p]=SC2_UnitProperty(&e->state,p);
    return e;
}
static sc2UnitState_t *gal_foundation_state(void *e) { return &((gal_entity_t *)e)->state; }
static bool gal_foundation_alive(void *e) { return !((gal_entity_t *)e)->removed && SC2_UnitAlive(gal_foundation_state(e)); }
static bool gal_foundation_location(void *ptr,float *x,float *y,float *z,float *facing) {
    gal_entity_t *e=ptr; *x=e->x; *y=e->y; *z=e->z; *facing=e->facing; return !e->removed;
}
static void gal_foundation_changed(void *e) { (void)e; gal_changes++; }
static void gal_foundation_remove(void *e) { ((gal_entity_t *)e)->removed=true; }
static int gal_foundation_owner(void *e) { return ((gal_entity_t *)e)->owner; }
static void gal_foundation_set_owner(void *e,int owner,bool color) { (void)color; ((gal_entity_t *)e)->owner=owner; }
static void *gal_foundation_from_id(uint32_t id) { return id==72 && gal_entity_n ? &gal_entities[0] : NULL; }

TEST(galaxy, foundation_units_groups_lifecycle) {
    gal_entity_n=gal_changes=gal_foundation_moves=0;
    sc2_galaxy_unit_move=gal_foundation_move;
    sc2_galaxy_on_unit_create=gal_foundation_create; sc2_galaxy_unit_state=gal_foundation_state;
    sc2_galaxy_unit_is_alive=gal_foundation_alive; sc2_galaxy_unit_location=gal_foundation_location;
    sc2_galaxy_unit_changed=gal_foundation_changed; sc2_galaxy_unit_remove=gal_foundation_remove;
    sc2_galaxy_unit_owner=gal_foundation_owner; sc2_galaxy_unit_set_owner=gal_foundation_set_owner;
    sc2_galaxy_unit_from_id=gal_foundation_from_id;
    gal_foundation_run(
        "void main() { region attached; order targetOrder; unit u=UnitCreate(1,\"Marine\",0,2,Point(3.0,4.0),90.0); unit v; unitgroup g=UnitLastCreatedGroup(); unitgroup copy; int n=0;"
        "if (UnitGetPropertyFixed(u,0,true)!=73.0 || UnitGetType(u)!=\"Marine\" || UnitFromId(72)!=u) { TestFail(\"unit identity and authored life\"); }"
        "UnitSetPropertyFixed(u,0,UnitGetPropertyFixed(u,0,true)+9.5);"
        "if (UnitGetPropertyFixed(u,0,true)!=82.5 || UnitGetPropertyFixed(u,0,false)!=73.0) { TestFail(\"add health\"); }"
        "UnitSetPropertyFixed(u,1,50.0); if (UnitGetPropertyFixed(u,0,true)!=58.5) { TestFail(\"life percentage\"); }"
        "UnitSetPropertyInt(u,6,21); if (UnitGetPropertyInt(u,6,true)!=21) { TestFail(\"energy cap\"); }"
        "UnitSetCustomValue(u,63,6.75); if (UnitGetCustomValue(u,63)!=6.75) { TestFail(\"custom value\"); }"
        "attached=RegionCircle(Point(0.0,0.0),2.0); RegionAttachToUnit(attached,u,Point(1.0,0.0));"
        "if (RegionGetAttachUnit(attached)!=u || !RegionContainsPoint(attached,Point(4.0,4.0))) { TestFail(\"attached region\"); }"
        "targetOrder=OrderTargetingUnit(AbilityCommand(\"move\",0),u);"
        "if (OrderGetTargetUnit(targetOrder)!=u || !PointsInRange(OrderGetTargetPosition(targetOrder),Point(3.0,4.0),0.0)) { TestFail(\"unit order position\"); }"
        "OrderSetTargetUnit(targetOrder,u); if (OrderGetTargetType(targetOrder)!=2) { TestFail(\"unit order target\"); }"
        "UnitSetOwner(u,7,true); if (UnitGetOwner(u)!=7) { TestFail(\"owner\"); }"
        "UnitSetState(u,9,true); if (!UnitTestState(u,9)) { TestFail(\"pause\"); } UnitPauseAll(false);"
        "if (UnitTestState(u,9) || UnitGetFacing(u)!=90.0 || UnitGetHeight(u)!=0.0 || !PointsInRange(UnitGetPosition(u),Point(3.0,4.0),0.0)) { TestFail(\"unit transform/resume\"); }"
        "v=UnitCreate(1,\"Marine\",0,2,Point(12.0,5.0),0.0); UnitGroupAdd(g,v); UnitGroupAdd(g,v); copy=UnitGroupCopy(g);"
        "if (UnitGroupCount(g,0)!=2 || UnitGroupCount(UnitGroupFilterPlayer(g,7,0),0)!=1) { TestFail(\"unit group membership\"); }"
        "if (UnitGroupCount(UnitGroupFilterRegion(g,RegionCircle(Point(3.0,4.0),2.0),0),0)!=1 || UnitGroupNearestUnit(g,Point(3.0,4.0))!=u) { TestFail(\"spatial group\"); }"
        "if (!UnitGroupIssueOrder(g,OrderTargetingPoint(AbilityCommand(\"move\",0),Point(20.0,20.0)),0)) { TestFail(\"group order\"); }"
        "UnitKill(u); if (UnitIsAlive(u) || !UnitTestState(u,22) || UnitGroupCount(g,2)!=1 || UnitGroupRandomUnit(g,2)!=u) { TestFail(\"death\"); }"
        "UnitRevive(u); if (!UnitIsAlive(u) || UnitGetPropertyFixed(u,0,true)!=117.0) { TestFail(\"revival\"); }"
        "UnitGroupLoopBegin(g); while (!UnitGroupLoopDone()) { if (UnitGroupLoopCurrent()==null) { TestFail(\"loop unit\"); } n=n+1; UnitGroupLoopStep(); } UnitGroupLoopEnd();"
        "if (n!=2 || UnitGroupUnit(g,1)!=u || !UnitGroupHasUnit(g,v)) { TestFail(\"unit iteration\"); }"
        "UnitGroupRemove(g,v); if (UnitGroupCount(copy,0)!=2 || UnitGroupCount(g,0)!=1) { TestFail(\"independent copy\"); }"
        "UnitRemove(u); if (UnitIsValid(u) || UnitGroupHasUnit(copy,u)) { TestFail(\"removal\"); }"
        "UnitGroupClear(copy); if (UnitGroupCount(copy,0)!=0 || UnitGroupCount(UnitGroupEmpty(),0)!=0) { TestFail(\"empty group\"); } }");
    T_EQ(gal_foundation_moves,1); T_FEQ(gal_entities[1].x,20,0.001f);
    sc2_galaxy_unit_move=NULL;
    T_ASSERT(gal_changes>=7); T_ASSERT(gal_entities[0].removed);
    sc2_galaxy_on_unit_create=NULL; sc2_galaxy_unit_state=NULL; sc2_galaxy_unit_is_alive=NULL;
    sc2_galaxy_unit_location=NULL; sc2_galaxy_unit_changed=NULL; sc2_galaxy_unit_remove=NULL;
    sc2_galaxy_unit_owner=NULL; sc2_galaxy_unit_set_owner=NULL; sc2_galaxy_unit_from_id=NULL;
}

TEST(galaxy, foundation_player_groups) {
    sc2_players[7].active=true; sc2_players[7].type=2;
    gal_foundation_run(
        "void main() { playergroup a = PlayerGroupEmpty(); playergroup b; int sum = 0;"
        "if (PlayerType(7)!=2 || PlayerGroupCount(PlayerGroupActive())!=1 || !PlayerGroupHasPlayer(PlayerGroupActive(),7)) { TestFail(\"active players\"); }"
        "PlayerGroupAdd(a, 2); PlayerGroupAdd(a, 7); PlayerGroupAdd(a, 2); b = PlayerGroupCopy(a);"
        "PlayerGroupRemove(a, 2); if (PlayerGroupCount(a) != 1 || !PlayerGroupHasPlayer(b, 2)) { TestFail(\"membership/copy\"); }"
        "PlayerGroupLoopBegin(b); while (!PlayerGroupLoopDone()) { sum = sum + PlayerGroupLoopCurrent();"
        "PlayerGroupLoopBegin(a); if (PlayerGroupLoopCurrent() != 7) { TestFail(\"nested loop\"); } PlayerGroupLoopEnd();"
        "PlayerGroupLoopStep(); } PlayerGroupLoopEnd(); if (sum != 9) { TestFail(\"outer loop\"); }"
        "PlayerGroupClear(b); if (PlayerGroupCount(b) != 0 || PlayerGroupPlayer(a, 1) != 7) { TestFail(\"clear/index\"); }"
        "if (PlayerGroupCount(PlayerGroupAll()) != 32 || PlayerGroupCount(PlayerGroupSingle(31)) != 1) { TestFail(\"player range\"); }"
        "PlayerSetAlliance(2, 0, 7, true); if (!PlayerGetAlliance(2, 0, 7) || PlayerGetAlliance(7, 0, 2)) { TestFail(\"directional alliance\"); }"
        "if (!PlayerGroupHasPlayer(PlayerGroupAlliance(0, 2), 7)) { TestFail(\"alliance group\"); }"
        "}");
}

TEST(galaxy, foundation_player_properties) {
    gal_foundation_run(
        "void main() { PlayerModifyPropertyInt(2, 0, 0, 137); PlayerModifyPropertyInt(2, 0, 1, 13);"
        "PlayerModifyPropertyFixed(2, 4, 0, 3.5); PlayerModifyPropertyFixed(2, 4, 2, 0.25);"
        "if (PlayerGetPropertyInt(2, 0) != 150 || PlayerGetPropertyInt(1, 0) != 0 || PlayerGetPropertyFixed(2, 4) != 3.25) { TestFail(\"player properties\"); }"
        "PlayerSetState(2, 3, true); if (!PlayerGetState(2, 3)) { TestFail(\"state set\"); }"
        "PlayerSetState(2, 3, false); if (PlayerGetState(2, 3)) { TestFail(\"state clear\"); }"
        "PlayerSetDifficulty(2, 4); if (PlayerDifficulty(2) != 4) { TestFail(\"difficulty\"); } }");
}

TEST(galaxy, foundation_points_regions_orders) {
    gal_foundation_run(
        "void main() { point p = Point(2.0, 3.0); point q = Point(0.0, 0.0); region r; region hole; order o;"
        "PointSetFacing(p, 123.0); PointSetHeight(p, 4.5); PointSet(q, p);"
        "if (PointGetFacing(q) != 123.0 || PointGetHeight(q) != 4.5 || !PointsInRange(p,q,0.0)) { TestFail(\"point values\"); }"
        "if (!PointsInRange(PointReflect(Point(0.0,1.0),Point(1.0,2.0),0.0),Point(0.0,3.0),0.001)) { TestFail(\"reflection normal\"); }"
        "r = RegionRect(0.0, 0.0, 10.0, 10.0); RegionAddCircle(r, false, Point(5.0,5.0), 2.0);"
        "if (!RegionContainsPoint(r, p) || RegionContainsPoint(r, Point(5.0,5.0))) { TestFail(\"region hole\"); }"
        "RegionSetOffset(r, Point(20.0,30.0)); if (!RegionContainsPoint(r,Point(22.0,33.0))) { TestFail(\"offset\"); }"
        "if (!PointsInRange(RegionGetOffset(r),Point(20.0,30.0),0.0) || !PointsInRange(RegionGetCenter(r),Point(25.0,35.0),0.0)) { TestFail(\"region offset center\"); }"
        "if (!PointsInRange(RegionGetBoundsMin(r),Point(20.0,30.0),0.0) || !PointsInRange(RegionGetBoundsMax(r),Point(30.0,40.0),0.0)) { TestFail(\"region bounds\"); }"
        "hole = RegionEmpty(); RegionAddRegion(hole,r); RegionSetCenter(hole,Point(0.0,0.0));"
        "if (!RegionContainsPoint(hole,Point(-3.0,-2.0)) || RegionContainsPoint(hole,Point(0.0,0.0))) { TestFail(\"copy translated shape\"); }"
        "RegionAddRect(r,true,8.0,8.0,12.0,12.0); if (!RegionContainsPoint(r,Point(31.0,41.0))) { TestFail(\"add rectangle\"); }"
        "o = Order(AbilityCommand(\"move\",0)); if (OrderGetTargetType(o) != 0) { TestFail(\"no target\"); }"
        "OrderSetAbilityCommand(o,AbilityCommand(\"move\",2)); if (OrderGetAbilityCommand(o)==null) { TestFail(\"ability command\"); }"
        "OrderSetPlayer(o,7); OrderSetTargetPoint(o,p); OrderSetFlag(o,31,true);"
        "if (OrderGetPlayer(o) != 7 || OrderGetTargetType(o) != 1 || !OrderGetFlag(o,31) || !PointsInRange(OrderGetTargetPoint(o),p,0.0)) { TestFail(\"order state\"); }"
        "OrderSetFlag(o,31,false); if (OrderGetFlag(o,31)) { TestFail(\"order flag inverse\"); } }");
}


TEST(galaxy, foundation_coroutine_loop_isolation) {
    gal_state_t s=gal_new(); galaxy_reset();
    jass_sethost(&MAKE(jassHost_t,.MemAlloc=gal_alloc,.MemFree=gal_free,.ReadFile=gal_read_file,
        .natives=gal_assert_natives,.galaxy_natives=galaxy_get_natives()));
    T_ASSERT(gal_parse(&s,
        "native void TestFail(string s); native void Wait(fixed seconds,int clock);"
        "native playergroup PlayerGroupSingle(int p); native void PlayerGroupLoopBegin(playergroup g);"
        "native int PlayerGroupLoopCurrent(); native void PlayerGroupLoopEnd(); int done=0;"
        "void first() { PlayerGroupLoopBegin(PlayerGroupSingle(2)); Wait(0.0,0);"
        "if (PlayerGroupLoopCurrent()!=2) { TestFail(\"first loop overwritten\"); } PlayerGroupLoopEnd(); done=done+1; }"
        "void second() { PlayerGroupLoopBegin(PlayerGroupSingle(7)); Wait(0.0,0);"
        "if (PlayerGroupLoopCurrent()!=7) { TestFail(\"second loop overwritten\"); } PlayerGroupLoopEnd(); done=done+1; }"
        "void verify() { if (done!=2) { TestFail(\"coroutines unfinished\"); } }"));
    jass_callbyname(s.j,"first",true); jass_callbyname(s.j,"second",true);
    jass_runevents(s.j); jass_runevents(s.j);
    T_ASSERT(!jass_rterror_pending(s.j));
    jass_callbyname(s.j,"verify",false); T_ASSERT(!jass_rterror_pending(s.j));
    gal_destroy(&s); galaxy_reset();
}

TEST(galaxy, foundation_reset_and_invalid_indices) {
    gal_state_t s=gal_new(); galaxy_reset();
    jass_sethost(&MAKE(jassHost_t,.MemAlloc=gal_alloc,.MemFree=gal_free,.ReadFile=gal_read_file,
        .natives=gal_assert_natives,.galaxy_natives=galaxy_get_natives()));
    T_ASSERT(gal_parse(&s,
        "native void TestFail(string s); native void PlayerSetState(int p,int state,bool value);"
        "native bool PlayerGetState(int p,int state); native playergroup PlayerGroupEmpty();"
        "native void PlayerGroupAdd(playergroup g,int p); native int PlayerGroupCount(playergroup g);"
        "playergroup g; void init() { g=PlayerGroupEmpty(); PlayerGroupAdd(g,31); PlayerSetState(31,8,true); }"
        "void badPlayer() { PlayerSetState(32,0,true); } void badState() { PlayerSetState(31,32,true); }"
        "void verify() { if (!PlayerGetState(31,8) || PlayerGroupCount(g)!=1) { TestFail(\"invalid write damaged state\"); } }"
        "void fresh() { if (PlayerGetState(31,8) || PlayerGroupCount(g)!=0) { TestFail(\"state survived reset\"); } }"));
    jass_callbyname(s.j,"init",false);
    jass_callbyname(s.j,"badPlayer",false); T_ASSERT(jass_rterror_pending(s.j)); jass_rterror_clear(s.j);
    jass_callbyname(s.j,"badState",false); T_ASSERT(jass_rterror_pending(s.j)); jass_rterror_clear(s.j);
    jass_callbyname(s.j,"verify",false); T_ASSERT(!jass_rterror_pending(s.j));
    galaxy_reset(); jass_callbyname(s.j,"fresh",false); T_ASSERT(!jass_rterror_pending(s.j));
    gal_destroy(&s); galaxy_reset();
}

