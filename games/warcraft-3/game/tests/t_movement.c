#ifdef BZ_TESTS
/*
 * test_movement.c — Unit movement and pathfinding tests.
 *
 * Tests cover the complete move-order pipeline:
 *
 *  order_move / ai_walk integration
 *    - order_move wires up goalentity and switches to "walk" animation
 *    - unit advances toward goal each frame  (via currentmove->think)
 *    - unit transitions to "stand" once it reaches the goal
 *    - unit_movedistance matches speed × 10 / FRAMETIME
 *
 *  Waypoint helpers
 *    - Waypoint_add places a waypoint at the requested 2-D location
 *
 *  Goal-distance helper
 *    - M_DistanceToGoal returns the 2-D Euclidean distance to goalentity
 *
 * All tests use the test harness mock gi; no actual map or MPQ is needed.
 * Units are given collision = 0 for these movement tests so they don't
 * interact with each other; collision behaviour is covered in
 * test_collision.c.
 */

#include <math.h>
#include "test.h"
#include "../g_local.h"

/* Helpers defined in t_utils.c */
LPEDICT alloc_test_unit(DWORD class_id, FLOAT x, FLOAT y);
void reset_entities(void);
void setup_test_world(void);
void CM_SetupTestPathmap(DWORD width, DWORD height, BYTE const *cells);
void CM_SetupTestWorldBounds(LPCBOX2 bounds);
void CM_ProcessPathJobs(DWORD work_budget);
extern void ai_train_build(LPEDICT ent);



/* NAVI_THRESHOLD is the distance below which ai_walk uses direct
 * vector math rather than the heatmap flow field.  It is defined in
 * g_ai.c; the test helpers that place waypoints reference it. */

/* -----------------------------------------------------------------------
 * Helpers
 * --------------------------------------------------------------------- */

/* Create a unit at (x, y) with the lifecycle callbacks and zero collision
 * (movement tests don't want unintended push-apart).  Resets entity pool
 * so each test starts from a clean slate. */
static LPEDICT make_moving_unit(FLOAT x, FLOAT y) {
    reset_entities();
    setup_test_world();
    LPEDICT ent = alloc_test_unit(MAKEFOURCC('h','p','e','a'), x, y);
    ent->movetype  = MOVETYPE_STEP;
    ent->stand     = unit_stand;
    ent->birth     = unit_birth;
    ent->die       = unit_die;
    ent->collision = 0.0f;
    ent->health.value     = 250.0f;
    ent->health.max_value = 250.0f;
    unit_stand(ent);
    return ent;
}

/* Harvest damage now uses the authoritative destructable lifecycle, so test
 * trees must carry the initialization normally supplied by SP_SpawnDestructable. */
static LPEDICT make_harvest_tree(FLOAT x, FLOAT y, FLOAT life) {
    LPEDICT tree = alloc_test_unit(MAKEFOURCC('L','T','l','t'), x, y);
    SP_monster_tree(tree);
    tree->destructable.initialized = true;
    tree->destructable.item_table = (DWORD)-1;
    tree->targtype = TARG_TREE;
    tree->health.value = tree->health.max_value = life;
    return tree;
}

static UnitAbilities_t const harvest_abilities = { .abilList = "Ahar" };
static UnitAbilities_t const return_gold_lumber_abilities = { .abilList = "Argl" };
static UnitAbilities_t const return_lumber_abilities = { .abilList = "Arlm" };

static void make_live_dropoff(LPEDICT building, UnitAbilities_t const *abilities) {
    building->data.UnitAbilities = abilities;
    building->health.value = building->health.max_value = 1000.0f;
}

/* Command-integration tests exercise server selection/target-mode state, not
 * svc_layout transport. Keep their HUD refreshes inside the game-test boundary. */
static void movement_noop_write(pfWriteType_t type, void const *value) { (void)type; (void)value; }
static void movement_noop_unicast(LPEDICT ent) { (void)ent; }

slkTestData_t *parse_slk_string(const char *slk_text);
void free_slk_rows(slkTestData_t *rows);


extern FLOAT HARVEST_GOLD_CAPACITY;
extern FLOAT HARVEST_TREE_DAMAGE;
extern FLOAT HARVEST_LUMBER_CAPACITY;
extern FLOAT HARVEST_RANGE;
extern FLOAT HARVEST_COOLDOWN;
extern FLOAT HARVEST_SEARCH_RANGE;
extern void harvest_cooldown(LPEDICT);
BOOL harvest_menu_selecttarget(LPEDICT clent, LPEDICT target);

TEST(wc3_movement, harvest_command_button_toggles_to_return_resources_ui) {
    LPEDICT worker = make_moving_unit(0.0f, 0.0f);
    gameCommandButton_t button;

    worker->data.UnitAbilities = &harvest_abilities;

    T_ASSERT(G_BuildCommandButton(worker, "Ahar", false, 0, &button));
    T_STREQ(button.command, "Ahar");
    T_STREQ(button.art, "TestUI\\Textures\\gather.blp");
    T_STREQ(button.tooltip, "Gather");
    T_STREQ(button.ubertip, "Gather resources from a Gold Mine or tree.");
    T_EQ(button.hotkey, 'G');
    T_EQ(button.x, 0);
    T_EQ(button.y, 1);

    S_SetCarriedResource(worker, RETURN_RESOURCE_LUMBER, 1);
    T_ASSERT(G_BuildCommandButton(worker, "Ahar", false, 0, &button));
    T_STREQ(button.command, "Ahar");
    T_STREQ(button.art, "TestUI\\Textures\\return-resources.blp");
    T_STREQ(button.tooltip, "Return Resources");
    T_STREQ(button.ubertip, "Return carried resources to a compatible drop-off.");
    T_EQ(button.hotkey, 'R');
    T_EQ(button.x, 3);
    T_EQ(button.y, 2);

    S_SetCarriedResource(worker, RETURN_RESOURCE_GOLD, 7);
    T_ASSERT(G_BuildCommandButton(worker, "Ahar", false, 0, &button));
    T_STREQ(button.tooltip, "Return Resources");

    S_SetCarriedResource(worker, RETURN_RESOURCE_GOLD, 0);
    T_ASSERT(G_BuildCommandButton(worker, "Ahar", false, 0, &button));
    T_STREQ(button.tooltip, "Gather");
}

TEST(wc3_movement, runtime_added_call_to_arms_exposes_on_and_off_buttons) {
    LPEDICT worker = make_moving_unit(0.0f, 0.0f);
    gameCommandButton_t buttons[16];
    BYTE count;
    BOOL found_on = false, found_off = false;

    T_ASSERT(G_ActorAddSkill(worker, MAKEFOURCC('A','m','i','c')));
    count = G_GetCommandButtons(worker, buttons, (BYTE)(sizeof(buttons) / sizeof(buttons[0])));
    FOR_LOOP(i, count) {
        if (!strcmp(buttons[i].command, "Amic")) {
            found_on = true;
            T_STREQ(buttons[i].tooltip, "Call to Arms");
            T_EQ(buttons[i].x, 1);
            T_EQ(buttons[i].y, 1);
        } else if (!strcmp(buttons[i].command, "Amic:off")) {
            found_off = true;
            T_STREQ(buttons[i].tooltip, "Back to Work");
            T_EQ(buttons[i].x, 2);
            T_EQ(buttons[i].y, 1);
        }
    }
    T_ASSERT(found_on);
    T_ASSERT(found_off);

    T_ASSERT(G_ActorRemoveSkill(worker, MAKEFOURCC('A','m','i','c')));
    count = G_GetCommandButtons(worker, buttons, (BYTE)(sizeof(buttons) / sizeof(buttons[0])));
    FOR_LOOP(i, count) {
        T_ASSERT(strcmp(buttons[i].command, "Amic") != 0);
        T_ASSERT(strcmp(buttons[i].command, "Amic:off") != 0);
    }
}

TEST(wc3_movement, missing_melee_amic_recovers_only_first_tier_one_hall) {
    static UnitAbilities_t const townhall_abilities = {
        .id = MAKEFOURCC('h','t','o','w'),
        .abilList = "",
    };
    LPEDICT first = make_moving_unit(0.0f, 0.0f);
    LPEDICT second = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 512.0f, 0.0f);

    first->class_id = first->s.class_id = MAKEFOURCC('h','t','o','w');
    first->data.UnitAbilities = &townhall_abilities;
    first->svflags |= SVF_MONSTER;
    first->s.player = 0;
    first->spawn_time = 100;

    second->data.UnitAbilities = &townhall_abilities;
    second->svflags |= SVF_MONSTER;
    second->s.player = 0;
    second->spawn_time = 200;

    T_ASSERT(S_MilitiaEnsureHallAbility(first));
    T_ASSERT(G_ActorHasSkill(first, "Amic"));
    T_ASSERT(!S_MilitiaEnsureHallAbility(second));
    T_ASSERT(!G_ActorHasSkill(second, "Amic"));
}

TEST(wc3_movement, carried_resource_toggle_invalidates_selected_command_card) {
    LPGAMECLIENT client = &game.clients[0];
    LPEDICT worker = make_moving_unit(0.0f, 0.0f);

    worker->s.player = client->ps.number;
    G_SelectEntity(client, worker);
    client->commands_dirty = false;

    S_SetCarriedResource(worker, RETURN_RESOURCE_LUMBER, 1);
    T_ASSERT(client->commands_dirty);

    client->commands_dirty = false;
    S_SetCarriedResource(worker, RETURN_RESOURCE_LUMBER, 2);
    T_ASSERT(!client->commands_dirty);

    S_SetCarriedResource(worker, RETURN_RESOURCE_GOLD, 7);
    T_ASSERT(!client->commands_dirty);

    S_SetCarriedResource(worker, RETURN_RESOURCE_GOLD, 0);
    T_ASSERT(client->commands_dirty);
}

static const char slk_goldmine_test_data[] =
    "ID;PWXL;N;E\n"
    "C;Y1;X1;K\"alias\"\n"
    "C;Y1;X2;K\"code\"\n"
    "C;Y1;X3;K\"Data11\"\n"
    "C;Y1;X4;K\"Data12\"\n"
    "C;Y1;X5;K\"Data13\"\n"
    "C;Y2;X1;K\"Agld\"\n"
    "C;Y2;X2;K\"Agld\"\n"
    "C;Y2;X3;K12500\n"
    "C;Y2;X4;K1\n"
    "C;Y2;X5;K1\n"
    "C;Y3;X1;K\"A001\"\n"
    "C;Y3;X2;K\"Agld\"\n"
    "C;Y3;X3;K100\n"
    "C;Y3;X4;K0.01\n"
    "C;Y3;X5;K1\n"
    "C;Y4;X1;K\"A002\"\n"
    "C;Y4;X2;K\"Agld\"\n"
    "C;Y4;X3;K200\n"
    "C;Y4;X4;K2\n"
    "C;Y4;X5;K2\n"
    "E\n";

static UnitAbilities_t const test_goldmine_stock = { .abilList = "Agld" };
static UnitAbilities_t const test_goldmine_cap1 = { .abilList = "A001" };
static UnitAbilities_t const test_goldmine_cap2 = { .abilList = "A002" };

static slkTestData_t *install_goldmine_test_data(slkTestData_t **rows_out) {
    slkTestData_t *rows = parse_slk_string(slk_goldmine_test_data);
    *rows_out = rows;
    return G_SetSLKRows("AbilityData", rows);
}

static void setup_test_goldmine(LPEDICT mine, UnitAbilities_t const *abilities, DWORD resources) {
    mine->data.UnitAbilities = abilities;
    mine->resources = resources;
    mine->health.value = mine->health.max_value = 1000.0f;
}

static pathTex_t *movement_make_goldmine_pathtex(void) {
    enum { W = 16, H = 16 };
    pathTex_t *tex = gi.MemAlloc(sizeof(*tex) + W * H * sizeof(COLOR32));
    T_ASSERT(tex != NULL);
    tex->width = W;
    tex->height = H;
    FOR_LOOP(i, W * H)
        tex->map[i] = (COLOR32){ 0, 0, 0, 255 };
    for (int y = 4; y < 12; y++) {
        for (int x = 4; x < 12; x++)
            tex->map[x + y * W].b = 255;
    }
    return tex;
}

static LPEDICT add_gold_worker(FLOAT x, FLOAT y) {
    LPEDICT worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), x, y);
    worker->movetype = MOVETYPE_STEP;
    worker->stand = unit_stand;
    worker->die = unit_die;
    worker->collision = 16.0f;
    worker->health.value = worker->health.max_value = 250.0f;
    worker->unitinfo.MoveSpeed = 100.0f;
    unit_stand(worker);
    return worker;
}

static BOOL tree_died;
static DWORD tree_pained;
static void test_tree_die(LPEDICT tree, LPEDICT attacker) { (void)tree; (void)attacker; tree_died = true; }
static void test_tree_pain(LPEDICT tree) { (void)tree; tree_pained++; }

typedef struct {
    GAMEMSG msg[32];
    DWORD count;
} MSGTRACE;

static void trace_message(LPCGAMEMSG msg, void *ctx) {
    MSGTRACE *trace = ctx;
    if (trace->count < sizeof(trace->msg) / sizeof(trace->msg[0]))
        trace->msg[trace->count++] = *msg;
}

/* Worker resource movement mirrors CBehaviorHarvest's
 * disableCollision=true contract for unit targets.  A live Peasant directly
 * in the mine lane must therefore not deflect or stop the approaching miner;
 * static pathing remains enabled separately. */
TEST(wc3_movement, worker_resource_gold_approach_ignores_live_units) {
    enum { CELLS = 64 };
    BYTE pathmap[CELLS * CELLS] = {0};
    LPEDICT worker = make_moving_unit(0.0f, 0.0f);
    LPEDICT blocker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 35.0f, 0.0f);
    LPEDICT mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 400.0f, 0.0f);
    VECTOR2 const origin = worker->s.origin2;
    slkTestData_t *rows, *old_abilities;

    worker->collision = 16.0f;
    worker->unitinfo.MoveSpeed = 190.0f;
    worker->s.angle = 0.0f;
    blocker->collision = 16.0f;
    blocker->s.model = 1;
    blocker->movetype = MOVETYPE_NONE;
    mine->collision = 128.0f;
    mine->s.model = 1;
    mine->movetype = MOVETYPE_NONE;
    setup_test_goldmine(mine, &test_goldmine_cap1, 100);
    gi.LinkEntity(worker);
    gi.LinkEntity(blocker);
    gi.LinkEntity(mine);
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(BOX2,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    old_abilities = install_goldmine_test_data(&rows);
    harvest_gold_start(worker, mine);
    worker->currentmove->think(worker);

    T_ASSERT(worker->s.origin2.x > origin.x);
    T_ASSERT(Vector2_distance(&worker->s.origin2, &blocker->s.origin2) <
             worker->collision + blocker->collision);
    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

/* Resource movement must route static geometry with the worker's real collision radius.
 * A Farm-sized obstacle across the direct mine lane reproduces the failure
 * where the old point-sized field chose cells a Peasant could not physically
 * traverse.  The bounded per-mover accelerator should immediately own a
 * collision-sized detour while the shared field is rebuilt. */
TEST(wc3_movement, worker_resource_static_detour_uses_worker_radius) {
    enum { CELLS = 64 };
    BYTE pathmap[CELLS * CELLS] = {0};
    LPEDICT worker = make_moving_unit(-320.0f, 0.0f);
    LPEDICT mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 320.0f, 0.0f);
    slkTestData_t *rows, *old_abilities;

    worker->collision = 16.0f;
    worker->unitinfo.MoveSpeed = 190.0f;
    worker->s.angle = 0.0f;
    mine->collision = 128.0f;
    mine->s.model = 1;
    mine->movetype = MOVETYPE_NONE;
    setup_test_goldmine(mine, &test_goldmine_cap1, 100);
    gi.LinkEntity(worker);
    gi.LinkEntity(mine);
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(BOX2,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    old_abilities = install_goldmine_test_data(&rows);
    harvest_gold_start(worker, mine);

    /* Begin on an open lane, then rebuild the static map with a 4x4 block
     * centred ahead of the already-moving worker, matching construction start. */
    worker->currentmove->think(worker);
    T_ASSERT(worker->s.origin2.x > -320.0f);
    for (int y = 30; y <= 33; y++)
        for (int x = 30; x <= 33; x++)
            pathmap[x + y * CELLS] = 0x02;
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(BOX2,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));
    worker->currentmove->think(worker);

    T_ASSERT(worker->movement.path_valid);
    T_FEQ(worker->movement.path_radius, worker->collision, 0.001f);
    T_ASSERT(fabsf(worker->movement.path_waypoint.y) >= CM_PathCellWorldSize());

    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

/* Returning gold should target the nearest legal edge of a blocked drop-off,
 * not the arbitrary pathable cell chosen around its centre.  Keep a Farm-sized
 * obstacle in the lane so this also proves the mover-owned detour is aimed at
 * that near-side edge rather than at the Town Hall centre/far side. */
TEST(wc3_movement, worker_resource_gold_return_targets_near_side_edge) {
    enum { CELLS = 64 };
    BYTE pathmap[CELLS * CELLS] = {0};
    LPEDICT worker = make_moving_unit(-320.0f, 0.0f);
    LPEDICT mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), -500.0f, 0.0f);
    LPEDICT hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 320.0f, 0.0f);
    pathTex_t *hall_pathtex = movement_make_goldmine_pathtex();

    worker->collision = 16.0f;
    worker->unitinfo.MoveSpeed = 190.0f;
    worker->harvested_gold = 10;
    worker->s.renderfx |= RF_HAS_GOLD;
    worker->secondarygoal = mine;
    hall->collision = 64.0f;
    hall->s.model = 1;
    hall->s.player = worker->s.player;
    hall->pathtex = hall_pathtex;
    make_live_dropoff(hall, &return_gold_lumber_abilities);
    gi.LinkEntity(worker);
    gi.LinkEntity(hall);

    /* Hall authored footprint: centre cell is x=42/y=32 in these bounds and
     * movement_make_goldmine_pathtex() blocks local cells 4..11. */
    for (int y = 28; y < 36; y++)
        for (int x = 38; x < 46; x++)
            pathmap[x + y * CELLS] = 0x02;
    /* Farm-sized obstacle between the mine side and the Hall's left edge. */
    for (int y = 30; y <= 33; y++)
        for (int x = 30; x <= 33; x++)
            pathmap[x + y * CELLS] = 0x02;
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(BOX2,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    T_ASSERT(harvest_gold_return_to(worker, hall));
    worker->currentmove->think(worker);

    T_ASSERT(worker->movement.path_valid);
    T_FEQ(worker->movement.path_radius, worker->collision, 0.001f);
    T_ASSERT(worker->movement.path_target.x < hall->s.origin2.x);
    T_ASSERT(worker->movement.path_target.x > worker->s.origin2.x);

    hall->pathtex = NULL;
    gi.MemFree(hall_pathtex);
}

/* Lumber Return Resources uses the same collision contract but a separate
 * behavior.  A Lumber Mill to the right must likewise keep the route endpoint
 * on its left/near edge, even when the worker has to detour around new static
 * construction on the way there. */
TEST(wc3_movement, worker_resource_lumber_return_targets_near_side_edge) {
    enum { CELLS = 64 };
    BYTE pathmap[CELLS * CELLS] = {0};
    LPEDICT worker = make_moving_unit(-320.0f, 0.0f);
    LPEDICT mill = alloc_test_unit(MAKEFOURCC('h','l','u','m'), 320.0f, 0.0f);
    pathTex_t *mill_pathtex = movement_make_goldmine_pathtex();

    worker->collision = 16.0f;
    worker->unitinfo.MoveSpeed = 190.0f;
    S_SetCarriedResource(worker, RETURN_RESOURCE_LUMBER, 10);
    mill->collision = 64.0f;
    mill->s.model = 1;
    mill->s.player = worker->s.player;
    mill->pathtex = mill_pathtex;
    make_live_dropoff(mill, &return_lumber_abilities);
    gi.LinkEntity(worker);
    gi.LinkEntity(mill);

    for (int y = 28; y < 36; y++)
        for (int x = 38; x < 46; x++)
            pathmap[x + y * CELLS] = 0x02;
    for (int y = 30; y <= 33; y++)
        for (int x = 30; x <= 33; x++)
            pathmap[x + y * CELLS] = 0x02;
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(BOX2,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    T_ASSERT(harvest_lumber_return_to(worker, mill));
    worker->currentmove->think(worker);

    T_ASSERT(worker->movement.path_valid);
    T_FEQ(worker->movement.path_radius, worker->collision, 0.001f);
    T_ASSERT(worker->movement.path_target.x < mill->s.origin2.x);
    T_ASSERT(worker->movement.path_target.x > worker->s.origin2.x);

    mill->pathtex = NULL;
    gi.MemFree(mill_pathtex);
}

/* Collision-sized static routing is cell-centred and therefore can stop just
 * outside the continuous footprint+step deposit test.  Once resource routing reaches
 * the innermost legal near-side endpoint, Return Resources must accept that
 * route end instead of repeatedly steering back across it. */
TEST(wc3_movement, worker_resource_gold_deposits_at_near_side_route_endpoint) {
    enum { CELLS = 64 };
    BYTE pathmap[CELLS * CELLS] = {0};
    LPEDICT worker = make_moving_unit(-320.0f, 0.0f);
    LPEDICT hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 320.0f, 0.0f);
    pathTex_t *hall_pathtex = movement_make_goldmine_pathtex();
    DWORD const old_gold = game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD];
    VECTOR2 approach;
    FLOAT route_band;

    worker->collision = 16.0f;
    worker->unitinfo.MoveSpeed = 190.0f;
    worker->harvested_gold = 10;
    worker->s.renderfx |= RF_HAS_GOLD;
    hall->collision = 64.0f;
    hall->s.model = 1;
    hall->s.player = worker->s.player;
    hall->pathtex = hall_pathtex;
    make_live_dropoff(hall, &return_gold_lumber_abilities);
    gi.LinkEntity(worker);
    gi.LinkEntity(hall);

    for (int y = 28; y < 36; y++)
        for (int x = 38; x < 46; x++)
            pathmap[x + y * CELLS] = 0x02;
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(BOX2,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    route_band = worker->collision + CM_PathCellWorldSize() * 1.41421356237f;
    T_ASSERT(CM_FindInnerApproachPointToFootprintForRadius(
        hall, &worker->s.origin2, route_band, worker->collision, &approach));
    T_ASSERT(CM_DistanceToPathingFootprint(hall, &approach) >
             worker->collision + unit_movedistance(worker));
    worker->s.origin2 = approach;
    gi.LinkEntity(worker);

    T_ASSERT(harvest_gold_return_to(worker, hall));
    worker->currentmove->think(worker);

    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD], old_gold + 10);
    T_EQ(worker->harvested_gold, 0);
    T_ASSERT(!(worker->s.renderfx & RF_HAS_GOLD));

    hall->pathtex = NULL;
    gi.MemFree(hall_pathtex);
}

TEST(wc3_movement, worker_resource_lumber_deposits_at_near_side_route_endpoint) {
    enum { CELLS = 64 };
    BYTE pathmap[CELLS * CELLS] = {0};
    LPEDICT worker = make_moving_unit(-320.0f, 0.0f);
    LPEDICT mill = alloc_test_unit(MAKEFOURCC('h','l','u','m'), 320.0f, 0.0f);
    pathTex_t *mill_pathtex = movement_make_goldmine_pathtex();
    DWORD const old_lumber = game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_LUMBER];
    VECTOR2 approach;
    FLOAT route_band;

    worker->collision = 16.0f;
    worker->unitinfo.MoveSpeed = 190.0f;
    S_SetCarriedResource(worker, RETURN_RESOURCE_LUMBER, 10);
    mill->collision = 64.0f;
    mill->s.model = 1;
    mill->s.player = worker->s.player;
    mill->pathtex = mill_pathtex;
    make_live_dropoff(mill, &return_lumber_abilities);
    gi.LinkEntity(worker);
    gi.LinkEntity(mill);

    for (int y = 28; y < 36; y++)
        for (int x = 38; x < 46; x++)
            pathmap[x + y * CELLS] = 0x02;
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(BOX2,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    route_band = worker->collision + CM_PathCellWorldSize() * 1.41421356237f;
    T_ASSERT(CM_FindInnerApproachPointToFootprintForRadius(
        mill, &worker->s.origin2, route_band, worker->collision, &approach));
    T_ASSERT(CM_DistanceToPathingFootprint(mill, &approach) >
             worker->collision + unit_movedistance(worker));
    worker->s.origin2 = approach;
    gi.LinkEntity(worker);

    T_ASSERT(harvest_lumber_return_to(worker, mill));
    worker->currentmove->think(worker);

    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_LUMBER], old_lumber + 10);
    T_EQ(worker->harvested_lumber, 0);
    T_ASSERT(!(worker->s.renderfx & RF_HAS_LUMBER));

    mill->pathtex = NULL;
    gi.MemFree(mill_pathtex);
}

/* Destructables are the opposite branch in Warsmash: Harvest resets the same
 * generic mover with collision enabled.  A tree approach may route/slide around
 * another unit, but must never commit a step through its collision circle. */
TEST(wc3_movement, worker_resource_tree_approach_keeps_live_unit_collision) {
    enum { CELLS = 64 };
    BYTE pathmap[CELLS * CELLS] = {0};
    FLOAT const saved_range = HARVEST_RANGE;
    LPEDICT worker = make_moving_unit(0.0f, 0.0f);
    LPEDICT blocker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 35.0f, 0.0f);
    LPEDICT tree = make_harvest_tree(400.0f, 0.0f, 100.0f);

    worker->collision = 16.0f;
    worker->unitinfo.MoveSpeed = 190.0f;
    worker->s.angle = 0.0f;
    blocker->collision = 16.0f;
    blocker->s.model = 1;
    blocker->movetype = MOVETYPE_NONE;
    gi.LinkEntity(worker);
    gi.LinkEntity(blocker);
    gi.LinkEntity(tree);
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(BOX2,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    HARVEST_RANGE = 64.0f;
    harvest_start(worker, tree);
    worker->currentmove->think(worker);

    T_ASSERT(Vector2_distance(&worker->s.origin2, &blocker->s.origin2) >=
             worker->collision + blocker->collision);
    HARVEST_RANGE = saved_range;
}

/* CBehaviorReturnResources always disables live-unit collision in Warsmash,
 * independent of whether the carried resource is gold or lumber. */
TEST(wc3_movement, worker_resource_lumber_return_ignores_live_units) {
    enum { CELLS = 64 };
    BYTE pathmap[CELLS * CELLS] = {0};
    LPEDICT worker = make_moving_unit(0.0f, 0.0f);
    LPEDICT blocker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 35.0f, 0.0f);
    LPEDICT hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 400.0f, 0.0f);

    worker->collision = 16.0f;
    worker->unitinfo.MoveSpeed = 190.0f;
    worker->s.angle = 0.0f;
    S_SetCarriedResource(worker, RETURN_RESOURCE_LUMBER, 10);
    blocker->collision = 16.0f;
    blocker->s.model = 1;
    blocker->movetype = MOVETYPE_NONE;
    hall->collision = 64.0f;
    hall->s.model = 1;
    hall->s.player = worker->s.player;
    make_live_dropoff(hall, &return_lumber_abilities);
    gi.LinkEntity(worker);
    gi.LinkEntity(blocker);
    gi.LinkEntity(hall);
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(BOX2,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    T_ASSERT(harvest_lumber_return_to(worker, hall));
    worker->currentmove->think(worker);

    T_ASSERT(Vector2_distance(&worker->s.origin2, &blocker->s.origin2) <
             worker->collision + blocker->collision);
}

/* Gold workers enter at the mine boundary; the mine's collision footprint must
 * not strand them just outside the older fixed interaction radius. */
TEST(wc3_movement, gold_worker_enters_large_mine_footprint) {
    LPEDICT worker = make_moving_unit(0.0f, 0.0f);
    LPEDICT mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 400.0f, 0.0f);
    worker->collision = 16.0f;
    worker->unitinfo.MoveSpeed = 100.0f;
    mine->collision = 128.0f; /* 8 blocked cells across in ROC 16x16Goldmine.tga. */
    mine->s.model = 1;
    mine->movetype = MOVETYPE_NONE;
    setup_test_goldmine(mine, &test_goldmine_cap1, 100);
    gi.LinkEntity(worker);
    gi.LinkEntity(mine);
    slkTestData_t *rows, *old_abilities = install_goldmine_test_data(&rows);
    harvest_gold_start(worker, mine);

    FOR_LOOP(i, 40) {
        worker->currentmove->think(worker);
        if (worker->s.renderfx & RF_HIDDEN) break;
    }

    T_ASSERT(worker->s.renderfx & RF_HIDDEN);
    T_EQ(mine->peonsinside, 1);
    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}


/* A worker at the last legal cell beside an authored mine footprint must be
 * admitted when one movement step reaches the footprint.  Keep this fixture at
 * the interaction boundary so it tests mine-entry semantics independently of
 * global route-cache/build-budget state left by earlier pathfinding tests. */
TEST(wc3_movement, gold_worker_enters_mine_with_blocked_pathing_footprint) {
    enum { CELLS = 64 };
    BYTE pathmap[CELLS * CELLS] = {0};
    LPEDICT worker = make_moving_unit(158.0f, 0.0f);
    LPEDICT mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 320.0f, 0.0f);
    pathTex_t *mine_pathtex = movement_make_goldmine_pathtex();

    worker->collision = 16.0f;
    worker->unitinfo.MoveSpeed = 190.0f;
    mine->collision = 128.0f;
    mine->s.model = 1;
    mine->movetype = MOVETYPE_NONE;
    mine->pathtex = mine_pathtex;
    setup_test_goldmine(mine, &test_goldmine_cap1, 100);
    gi.LinkEntity(worker);
    gi.LinkEntity(mine);

    /* Mirror the mine path texture's central 8x8 no-walk cells into the
     * static test map.  The entity carries the same authored pathtex so
     * interaction distance and movement pathing describe one footprint. */
    for (int y = 28; y < 36; y++) {
        for (int x = 38; x < 46; x++)
            pathmap[x + y * CELLS] = 0x02;
    }
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(BOX2,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    T_ASSERT(CM_PointIsPathableForRadius(&worker->s.origin2, worker->collision));
    T_ASSERT(!CM_PointIsPathableForRadius(&mine->s.origin2, 0.0f));
    T_ASSERT(CM_DistanceToPathingFootprint(mine, &worker->s.origin2) <=
             worker->collision + unit_movedistance(worker));

    slkTestData_t *rows, *old_abilities = install_goldmine_test_data(&rows);
    harvest_gold_start(worker, mine);
    worker->currentmove->think(worker);

    T_ASSERT(worker->s.renderfx & RF_HIDDEN);
    T_EQ(mine->peonsinside, 1);
    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
    gi.MemFree(mine_pathtex);
}

/* Resource-building legs ignore live units, so the old Human02 crowd-settle
 * shortcut is no longer part of mine entry. Static pathing remains authoritative:
 * a worker that cannot get its real collision radius within the authored mine
 * interaction boundary must keep the Harvest order alive rather than entering
 * through a blocked edge. */
TEST(wc3_movement, gold_worker_static_blocked_edge_does_not_fake_mine_entry) {
    enum { CELLS = 64 };
    BYTE pathmap[CELLS * CELLS] = {0};
    LPEDICT worker = make_moving_unit(151.0f, 0.0f);
    LPEDICT mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 320.0f, 0.0f);
    pathTex_t *mine_pathtex = movement_make_goldmine_pathtex();
    FLOAT footprint;

    worker->collision = 16.0f;
    worker->unitinfo.MoveSpeed = 190.0f;
    mine->collision = 128.0f;
    mine->s.model = 1;
    mine->movetype = MOVETYPE_NONE;
    mine->pathtex = mine_pathtex;
    setup_test_goldmine(mine, &test_goldmine_cap1, 100);
    gi.LinkEntity(worker);
    gi.LinkEntity(mine);

    for (int y = 28; y < 36; y++) {
        for (int x = 38; x < 46; x++)
            pathmap[x + y * CELLS] = 0x02;
    }
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(BOX2,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    footprint = CM_DistanceToPathingFootprint(mine, &worker->s.origin2);
    T_ASSERT(footprint > worker->collision + unit_movedistance(worker));

    slkTestData_t *rows, *old_abilities = install_goldmine_test_data(&rows);
    harvest_gold_start(worker, mine);

    FOR_LOOP(i, 20) {
        worker->currentmove->think(worker);
        CM_ProcessPathJobs(65536);
        if (worker->s.renderfx & RF_HIDDEN)
            break;
    }

    T_ASSERT(!(worker->s.renderfx & RF_HIDDEN));
    T_EQ(mine->peonsinside, 0);
    T_ASSERT(worker->goalentity == mine);
    T_STREQ(worker->currentmove->animation, "walk");
    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
    gi.MemFree(mine_pathtex);
}

/* The mine pathing footprint is square/texture-authored, while mine->collision
 * is only a scalar approximation.  At a footprint corner the worker can be one
 * legal movement step from the no-walk cells while its centre distance is still
 * greater than worker+mine collision+step.  Mine entry must use the authored
 * footprint so routing cannot strand a diagonally approaching worker. */
TEST(wc3_movement, gold_worker_enters_at_pathing_footprint_corner) {
    enum { CELLS = 64 };
    BYTE pathmap[CELLS * CELLS] = {0};
    LPEDICT worker = make_moving_unit(170.0f, 170.0f);
    LPEDICT mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 320.0f, 320.0f);
    pathTex_t *mine_pathtex = movement_make_goldmine_pathtex();

    worker->collision = 16.0f;
    worker->unitinfo.MoveSpeed = 190.0f;
    mine->collision = 128.0f;
    mine->s.model = 1;
    mine->movetype = MOVETYPE_NONE;
    mine->pathtex = mine_pathtex;
    setup_test_goldmine(mine, &test_goldmine_cap1, 100);
    gi.LinkEntity(worker);
    gi.LinkEntity(mine);

    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(BOX2,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    /* Centre-circle entry is deliberately still false at this corner.
     * Check the fixture geometry directly: harvest_gold_start() has not yet
     * assigned worker->goalentity, so M_DistanceToGoal() is not valid here. */
    T_ASSERT(Vector2_distance(&worker->s.origin2, &mine->s.origin2) >
             worker->collision + mine->collision + unit_movedistance(worker));
    T_ASSERT(CM_DistanceToPathingFootprint(mine, &worker->s.origin2) <=
             worker->collision + unit_movedistance(worker));

    slkTestData_t *rows, *old_abilities = install_goldmine_test_data(&rows);
    harvest_gold_start(worker, mine);
    worker->currentmove->think(worker);

    T_ASSERT(worker->s.renderfx & RF_HIDDEN);
    T_EQ(mine->peonsinside, 1);
    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
    gi.MemFree(mine_pathtex);
}

/* A final chop equal to the remaining life must run the tree's death callback,
 * which owns its fall animation and pathing removal. */
TEST(wc3_movement, lumber_final_chop_fells_tree) {
    LPEDICT worker = make_moving_unit(0.0f, 0.0f);
    LPEDICT tree = make_harvest_tree(20.0f, 0.0f, 10.0f);
    worker->attack1.damagePoint = 0.01f;
    MSGTRACE trace = {0};
    T_ASSERT(G_SubscribeMessage(trace_message, &trace));
    HARVEST_RANGE = 64.0f;
    HARVEST_TREE_DAMAGE = 10.0f;
    HARVEST_LUMBER_CAPACITY = 10.0f;
    harvest_start(worker, tree);

    worker->currentmove->think(worker);
    worker->wait = 0.01f;
    worker->currentmove->think(worker);
    G_UnsubscribeMessage(trace_message, &trace);

    T_FEQ(tree->health.value, 0.0f, 0.01f);
    T_FEQ(worker->harvested_lumber, 10.0f, 0.01f);
    T_ASSERT(tree->svflags & SVF_DEADMONSTER);
    T_STREQ(tree->currentmove->animation, "death");
    T_EQ(trace.count, 4);
    T_EQ(trace.msg[0].type, GAME_MSG_HARVEST_MOVE_LUMBER);
    T_EQ(trace.msg[1].type, GAME_MSG_HARVEST_START_CHOP);
    T_EQ(trace.msg[2].type, GAME_MSG_HARVEST_CHOP);
    T_EQ(trace.msg[3].type, GAME_MSG_HARVEST_TREE_FELLED);
    FOR_LOOP(i, trace.count) {
        T_EQ(trace.msg[i].actor, worker->s.number);
        T_EQ(trace.msg[i].target, tree->s.number);
    }
}

/* Non-lethal chops damage but do not fell a living tree. */
TEST(wc3_movement, lumber_nonlethal_chop_keeps_tree_standing) {
    LPEDICT worker = make_moving_unit(0.0f, 0.0f);
    LPEDICT tree = make_harvest_tree(20.0f, 0.0f, 11.0f);
    tree->pain = test_tree_pain;
    tree->die = test_tree_die;
    tree_died = false;
    tree_pained = 0;
    HARVEST_RANGE = 64.0f;
    HARVEST_TREE_DAMAGE = 10.0f;
    HARVEST_LUMBER_CAPACITY = 10.0f;
    harvest_start(worker, tree);

    worker->currentmove->think(worker);
    worker->wait = 0.01f;
    worker->currentmove->think(worker);

    T_ASSERT(!tree_died);
    T_EQ(tree_pained, 1);
    T_FEQ(tree->health.value, 1.0f, 0.01f);
}

/* A resumable route miss has not chosen a heading yet.  Harvest used to call
 * unit_moveindirection anyway, which committed a step along the worker's stale
 * facing while flow_generation=0/direct=false.  Hold the order and position
 * until CM_ProcessPathJobs completes the requested field. */
TEST(wc3_movement, lumber_pending_flow_does_not_move_on_stale_heading) {
    enum { CELLS = 64 };
    BYTE pathmap[CELLS * CELLS] = {0};
    LPEDICT worker = make_moving_unit(-320.0f, 0.0f);
    LPEDICT tree = make_harvest_tree(320.0f, 0.0f, 500.0f);
    VECTOR2 const origin = worker->s.origin2;

    worker->collision = 16.0f;
    worker->unitinfo.MoveSpeed = 190.0f;
    worker->s.angle = 1.5707963f; /* stale north-facing movement is legal */
    tree->collision = 0.0f;

    /* A full-height wall blocks the direct approach so the first Harvest tick
     * must request a resumable collision-sized field.  Do not process the job:
     * this test covers the pending state itself, not route completion. */
    for (int y = 0; y < CELLS; y++)
        pathmap[32 + y * CELLS] = 0x02;
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(BOX2,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    HARVEST_RANGE = 64.0f;
    HARVEST_SEARCH_RANGE = 1000.0f;
    harvest_start(worker, tree);
    worker->currentmove->think(worker);

    T_EQ(worker->movement.flow_generation, 0);
    T_ASSERT(!worker->movement.flow_direct);
    T_FEQ(worker->s.origin2.x, origin.x, 0.01f);
    T_FEQ(worker->s.origin2.y, origin.y, 0.01f);
    T_ASSERT(worker->goalentity == tree);
}

/* Same-tree workers should progress toward the same chop target without
 * overtaking or being forced onto an artificial lateral lane. */
TEST(wc3_movement, lumber_same_tree_workers_preserve_direct_order) {
    enum { CELLS = 64 };
    BYTE pathmap[CELLS * CELLS] = {0};
    LPEDICT first = make_moving_unit(-400.0f, 0.0f);
    LPEDICT second = add_gold_worker(-365.0f, 0.0f);
    LPEDICT tree = make_harvest_tree(0.0f, 0.0f, 500.0f);
    VECTOR2 const first_origin = first->s.origin2;
    VECTOR2 const second_origin = second->s.origin2;

    first->collision = second->collision = 16.0f;
    first->unitinfo.MoveSpeed = second->unitinfo.MoveSpeed = 190.0f;
    tree->collision = 0.0f;
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(BOX2,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    HARVEST_RANGE = 116.0f;
    HARVEST_SEARCH_RANGE = 1000.0f;
    harvest_start(first, tree);
    harvest_start(second, tree);
    first->currentmove->think(first);
    second->currentmove->think(second);
    T_ASSERT(first->goalentity == tree);
    T_ASSERT(second->goalentity == tree);
    T_ASSERT(first->movement.flow_direct);
    T_ASSERT(second->movement.flow_direct);
    T_ASSERT(first->s.origin2.x > first_origin.x);
    T_ASSERT(first->s.origin2.x < second->s.origin2.x);
    T_ASSERT(fabsf(first->s.origin2.y - first_origin.y) < 2.0f);
    T_ASSERT(second->s.origin2.x > second_origin.x);
    T_ASSERT(fabsf(second->s.origin2.y - second_origin.y) < 2.0f);
}

/* A nearby static detour uses the bounded per-mover accelerator immediately;
 * it must not wait for the destination field to cover the whole pathmap. */
TEST(wc3_movement, nearby_move_starts_on_accelerated_waypoint) {
    enum { CELLS = 64 };
    BYTE pathmap[CELLS * CELLS] = {0};
    LPEDICT unit = make_moving_unit(320.0f, 0.0f);
    VECTOR2 const origin = unit->s.origin2;
    VECTOR2 dest = {-320.0f, 0.0f};

    FOR_LOOP(y, CELLS)
        pathmap[32 + y * CELLS] = 0x02;
    for (int y = 39; y <= 41; y++)
        pathmap[32 + y * CELLS] = 0;
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(BOX2,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    unit->collision = 16.0f;
    unit->unitinfo.MoveSpeed = 190.0f;
    unit->s.angle = 0.0f;
    order_move(unit, Waypoint_add(&dest));
    unit->currentmove->think(unit);

    T_EQ(unit->movement.flow_generation, 0);
    T_ASSERT(!unit->movement.flow_direct);
    T_ASSERT(unit->movement.path_valid);
    T_ASSERT(CM_LineIsWalkableForRadius(&origin, &unit->movement.path_waypoint, unit->collision));
    T_ASSERT(Vector2_distance(&unit->s.origin2, &origin) > 0.001f);
    T_STREQ(unit->currentmove->animation, "walk");
}

/* Retail WC3 does not leave a worker orbiting an unreachable tree buried in a
 * forest.  The clicked tree remains authoritative while a route exists; once
 * the collision-sized flow field reaches its closest legal approach point and
 * that point is still outside chop range, Harvest selects a reachable edge
 * tree and begins chopping it. */
TEST(wc3_movement, lumber_unreachable_clicked_tree_retargets_reachable_edge_tree) {
    enum { CELLS = 64 };
    BYTE pathmap[CELLS * CELLS] = {0};
    LPEDICT worker = make_moving_unit(0.0f, -320.0f);
    LPEDICT edge = make_harvest_tree(0.0f, -96.0f, 500.0f);
    LPEDICT interior = make_harvest_tree(0.0f, 0.0f, 500.0f);

    worker->collision = 16.0f;
    worker->unitinfo.MoveSpeed = 190.0f;
    worker->attack1.damagePoint = 0.01f;
    edge->collision = interior->collision = 0.0f;

    /* Seven blocked rows/columns model a dense forest around the clicked
     * interior tree.  With a 16u worker radius the closest legal route goal is
     * outside the forest, still >64u from the interior target but within 64u of
     * the southern edge tree. */
    for (int y = 29; y <= 35; y++) {
        for (int x = 29; x <= 35; x++)
            pathmap[x + y * CELLS] = 0x02;
    }
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(BOX2,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    HARVEST_RANGE = 64.0f;
    HARVEST_SEARCH_RANGE = 1000.0f;
    HARVEST_TREE_DAMAGE = 1.0f;
    harvest_start(worker, interior);

    FOR_LOOP(i, 200) {
        worker->currentmove->think(worker);
        CM_ProcessPathJobs(65536);
        if (worker->goalentity == edge &&
            worker->currentmove &&
            !strcmp(worker->currentmove->animation, "attack"))
            break;
    }

    T_ASSERT(worker->goalentity == edge);
    T_ASSERT(worker->secondarygoal == edge);
    T_NOT_NULL(worker->currentmove);
    T_STREQ(worker->currentmove->animation, "attack");
    T_ASSERT(Vector2_distance(&worker->s.origin2, &edge->s.origin2) <= HARVEST_RANGE);
}

TEST(wc3_movement, lumber_tree_dying_during_approach_retargets_immediately) {
    LPEDICT worker = make_moving_unit(0.0f, 0.0f);
    LPEDICT dead = make_harvest_tree(400.0f, 0.0f, 100.0f);
    LPEDICT live = make_harvest_tree(100.0f, 0.0f, 100.0f);

    HARVEST_RANGE = 64.0f;
    HARVEST_SEARCH_RANGE = 1000.0f;
    harvest_start(worker, dead);
    dead->health.value = 0.0f;
    dead->svflags |= SVF_DEADMONSTER;

    worker->currentmove->think(worker);

    T_ASSERT(worker->goalentity == live);
    T_ASSERT(worker->secondarygoal == live);
}

/* Ahar slots 1=1 (damage/lumber per swing), 2=10 (capacity): 10 swings are
 * needed per trip. Drives the full cooldown+swing cycle. */
TEST(wc3_movement, lumber_worker_takes_ten_swings_per_trip) {
    LPEDICT worker = make_moving_unit(0.0f, 0.0f);
    LPEDICT tree = make_harvest_tree(20.0f, 0.0f, 500.0f);
    worker->attack1.damagePoint = 0.01f;
    tree->pain = test_tree_pain; tree->die = test_tree_die;
    tree_pained = 0; tree_died = false;
    HARVEST_RANGE = 64.0f; HARVEST_TREE_DAMAGE = 1.0f;
    HARVEST_LUMBER_CAPACITY = 10.0f; HARVEST_COOLDOWN = 0.01f;
    harvest_start(worker, tree);
    worker->currentmove->think(worker); /* ai_walktree → harvest_swing (within range) */
    /* Drive the first chop. */
    worker->wait = 0.01f;
    worker->currentmove->think(worker); /* ai_chop: lumber=1, tree-=1 */
    /* Cycle through cooldown+swing until capacity fills; expect exactly 9 more chops. */
    FOR_LOOP(i, 15) {
        if (worker->harvested_lumber >= HARVEST_LUMBER_CAPACITY) break;
        harvest_cooldown(worker);           /* anim end: <cap → cooldown state */
        worker->wait = 0.01f;
        worker->currentmove->think(worker); /* ai_cooldown → harvest_swing */
        worker->wait = 0.01f;
        worker->currentmove->think(worker); /* ai_chop */
    }
    T_EQ(tree_pained, 10);
    T_FEQ(worker->harvested_lumber, 10.0f, 0.01f);
    T_FEQ(tree->health.value, 490.0f, 0.01f);
    T_ASSERT(!tree_died);
}

/* A custom/non-even capacity must clamp the final successful chop instead of
 * allowing the worker to carry more lumber than the Harvest capacity. */
TEST(wc3_movement, lumber_final_chop_clamps_to_capacity) {
    LPEDICT worker = make_moving_unit(0.0f, 0.0f);
    LPEDICT tree = make_harvest_tree(20.0f, 0.0f, 100.0f);

    worker->attack1.damagePoint = 0.01f;
    HARVEST_RANGE = 64.0f;
    HARVEST_TREE_DAMAGE = 10.0f;
    HARVEST_LUMBER_CAPACITY = 25.0f;
    HARVEST_COOLDOWN = 0.01f;
    harvest_start(worker, tree);
    worker->currentmove->think(worker);

    FOR_LOOP(i, 3) {
        worker->wait = 0.01f;
        worker->currentmove->think(worker);
        if (i < 2) {
            harvest_cooldown(worker);
            worker->wait = 0.01f;
            worker->currentmove->think(worker);
        }
    }

    T_EQ(worker->harvested_lumber, 25);
    T_FEQ(tree->health.value, 70.0f, 0.01f);
    T_ASSERT(worker->s.renderfx & RF_HAS_LUMBER);
}

/* Harvest only awards carried lumber when the tree can actually take the
 * chop. Invulnerable destructibles reject G_DestructableApplyDamage. */
TEST(wc3_movement, lumber_invulnerable_tree_does_not_award_lumber) {
    LPEDICT worker = make_moving_unit(0.0f, 0.0f);
    LPEDICT tree = make_harvest_tree(20.0f, 0.0f, 100.0f);

    worker->attack1.damagePoint = 0.01f;
    tree->invulnerable = true;
    HARVEST_RANGE = 64.0f;
    HARVEST_TREE_DAMAGE = 10.0f;
    HARVEST_LUMBER_CAPACITY = 25.0f;
    harvest_start(worker, tree);
    worker->currentmove->think(worker);
    worker->wait = 0.01f;
    worker->currentmove->think(worker);

    T_EQ(worker->harvested_lumber, 0);
    T_FEQ(tree->health.value, 100.0f, 0.01f);
    T_ASSERT(!(worker->s.renderfx & RF_HAS_LUMBER));
}

/* A worker has one carried-resource presentation.  Starting to collect lumber
 * after gold must replace the gold bag rather than leaving both carry flags set. */
TEST(wc3_movement, lumber_chop_replaces_gold_carry_state) {
    LPEDICT worker = make_moving_unit(0.0f, 0.0f);
    LPEDICT tree = make_harvest_tree(20.0f, 0.0f, 100.0f);

    worker->attack1.damagePoint = 0.01f;
    worker->harvested_gold = 7;
    worker->s.renderfx |= RF_HAS_GOLD;
    HARVEST_RANGE = 64.0f;
    HARVEST_TREE_DAMAGE = 1.0f;
    HARVEST_LUMBER_CAPACITY = 10.0f;

    harvest_start(worker, tree);
    worker->currentmove->think(worker);
    worker->wait = 0.01f;
    worker->currentmove->think(worker);

    T_EQ(worker->harvested_gold, 0);
    T_EQ(worker->harvested_lumber, 1);
    T_ASSERT(!(worker->s.renderfx & RF_HAS_GOLD));
    T_ASSERT(worker->s.renderfx & RF_HAS_LUMBER);
}

/* Smart-clicking a tree after an interrupted partial lumber trip resumes the
 * same trip and preserves the amount already gathered. */
TEST(wc3_movement, lumber_smart_click_resumes_partial_trip) {
    LPEDICT worker = make_moving_unit(0.0f, 0.0f);
    LPEDICT tree = make_harvest_tree(20.0f, 0.0f, 100.0f);

    worker->data.UnitAbilities = &harvest_abilities;
    worker->attack1.damagePoint = 0.01f;
    worker->harvested_lumber = 3;
    worker->s.renderfx |= RF_HAS_LUMBER;
    HARVEST_RANGE = 64.0f;
    HARVEST_TREE_DAMAGE = 1.0f;
    HARVEST_LUMBER_CAPACITY = 10.0f;

    T_ASSERT(unit_issuetargetorder(worker, "smart", tree));
    T_EQ(worker->harvested_lumber, 3);
    worker->currentmove->think(worker);
    worker->wait = 0.01f;
    worker->currentmove->think(worker);

    T_EQ(worker->harvested_lumber, 4);
    T_ASSERT(worker->s.renderfx & RF_HAS_LUMBER);
}

/* Switching from lumber to gold keeps the lumber carry while travelling and
 * mining.  The first actual gold pickup replaces it atomically. */
TEST(wc3_movement, lumber_smart_click_gold_mine_switches_on_gold_pickup) {
    LPEDICT worker = make_moving_unit(0.0f, 0.0f);
    LPEDICT mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 0.0f, 0.0f);
    slkTestData_t *rows, *old_abilities = install_goldmine_test_data(&rows);

    worker->data.UnitAbilities = &harvest_abilities;
    worker->harvested_lumber = 5;
    worker->s.renderfx |= RF_HAS_LUMBER;
    setup_test_goldmine(mine, &test_goldmine_cap1, 100);
    HARVEST_GOLD_CAPACITY = 10.0f;

    T_ASSERT(unit_issuetargetorder(worker, "smart", mine));
    T_EQ(worker->harvested_lumber, 5);
    T_EQ(worker->harvested_gold, 0);
    T_ASSERT(worker->s.renderfx & RF_HAS_LUMBER);
    T_ASSERT(!(worker->s.renderfx & RF_HAS_GOLD));

    harvestgold_minegold(worker);
    harvestgold_walkback(worker);

    T_EQ(worker->harvested_lumber, 0);
    T_EQ(worker->harvested_gold, 10);
    T_ASSERT(!(worker->s.renderfx & RF_HAS_LUMBER));
    T_ASSERT(worker->s.renderfx & RF_HAS_GOLD);
    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

/* Switching from gold to lumber similarly keeps the gold while approaching
 * the tree.  Only a successful chop replaces the carried gold with lumber. */
TEST(wc3_movement, gold_smart_click_tree_switches_on_successful_chop) {
    LPEDICT worker = make_moving_unit(0.0f, 0.0f);
    LPEDICT tree = make_harvest_tree(20.0f, 0.0f, 100.0f);

    worker->data.UnitAbilities = &harvest_abilities;
    worker->attack1.damagePoint = 0.01f;
    worker->harvested_gold = 7;
    worker->s.renderfx |= RF_HAS_GOLD;
    HARVEST_RANGE = 64.0f;
    HARVEST_TREE_DAMAGE = 1.0f;
    HARVEST_LUMBER_CAPACITY = 10.0f;

    T_ASSERT(unit_issuetargetorder(worker, "smart", tree));
    T_EQ(worker->harvested_gold, 7);
    T_ASSERT(worker->s.renderfx & RF_HAS_GOLD);
    worker->currentmove->think(worker);
    T_EQ(worker->harvested_gold, 7);
    worker->wait = 0.01f;
    worker->currentmove->think(worker);

    T_EQ(worker->harvested_gold, 0);
    T_EQ(worker->harvested_lumber, 1);
    T_ASSERT(!(worker->s.renderfx & RF_HAS_GOLD));
    T_ASSERT(worker->s.renderfx & RF_HAS_LUMBER);
}

/* A worker already carrying gold honors the clicked mine first.  Reaching the
 * mine redirects the existing load to the nearest gold drop-off without
 * entering/mining, then deposit resumes the originally clicked mine. */
TEST(wc3_movement, gold_smart_click_gold_mine_visits_mine_then_returns_and_resumes) {
    LPEDICT worker = make_moving_unit(0.0f, 0.0f);
    LPEDICT hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 0.0f, 0.0f);
    LPEDICT mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 0.0f, 0.0f);
    slkTestData_t *rows, *old_abilities = install_goldmine_test_data(&rows);
    DWORD const old_gold = game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD];

    worker->data.UnitAbilities = &harvest_abilities;
    worker->harvested_gold = 7;
    worker->s.renderfx |= RF_HAS_GOLD;
    hall->s.player = worker->s.player;
    make_live_dropoff(hall, &return_gold_lumber_abilities);
    setup_test_goldmine(mine, &test_goldmine_cap1, 100);

    T_ASSERT(unit_issuetargetorder(worker, "smart", mine));
    T_ASSERT(worker->goalentity == mine);
    T_ASSERT(worker->secondarygoal == mine);
    T_EQ(worker->harvested_gold, 7);
    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD], old_gold);

    /* Reaching the clicked mine redirects the existing load without entering
     * the mine or collecting any additional gold. */
    worker->currentmove->think(worker);
    T_ASSERT(worker->goalentity == hall);
    T_ASSERT(worker->secondarygoal == mine);
    T_EQ(worker->harvested_gold, 7);
    T_EQ(mine->peonsinside, 0);
    T_ASSERT(!S_GoldMineWorkerIsInside(worker));
    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD], old_gold);

    /* The return completes immediately in this fixture because the hall is at
     * the worker position, then the original clicked mine becomes the goal. */
    worker->currentmove->think(worker);
    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD], old_gold + 7);
    T_EQ(worker->harvested_gold, 0);
    T_ASSERT(worker->goalentity == mine);
    T_ASSERT(worker->secondarygoal == mine);
    T_STREQ(worker->currentmove->animation, "walk");
    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

/* Resumable routing returns generation 0 until its shared flow job completes.
 * The bounded mover-owned accelerator is intentionally best-effort: a longer
 * detour may exceed its immediate work budget. Three miners must then hold their
 * starting positions instead of walking along stale facing, and resume once the
 * shared collision-sized field becomes available. */
TEST(wc3_movement, gold_three_workers_hold_while_shared_route_is_pending) {
    enum { CELLS = 64, WORKERS = 3 };
    BYTE pathmap[CELLS * CELLS] = {0};
    LPEDICT mine;
    LPEDICT workers[WORKERS];
    VECTOR2 origin[WORKERS];
    slkTestData_t *rows, *old_abilities;

    /* make_moving_unit() resets the shared entity array for isolated tests.
     * This test needs three workers and their mine alive at the same time, so
     * create the common world once and initialize each worker in-place. */
    reset_entities();
    setup_test_world();
    mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), -320.0f, 0.0f);
    workers[0] = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 320.0f, -64.0f);
    workers[1] = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 352.0f,   0.0f);
    workers[2] = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 384.0f,  64.0f);

    /* Block the direct westward line but leave a reachable opening north of
     * the workers so the shared mine route requires a resumable flow field. */
    FOR_LOOP(y, CELLS)
        pathmap[32 + y * CELLS] = 0x02;
    pathmap[32 + 40 * CELLS] = 0;
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(BOX2,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    old_abilities = install_goldmine_test_data(&rows);
    mine->collision = 128.0f;
    mine->s.model = 1;
    mine->movetype = MOVETYPE_NONE;
    setup_test_goldmine(mine, &test_goldmine_cap1, 100);
    gi.LinkEntity(mine);

    FOR_LOOP(i, WORKERS) {
        workers[i]->movetype = MOVETYPE_STEP;
        workers[i]->stand = unit_stand;
        workers[i]->birth = unit_birth;
        workers[i]->die = unit_die;
        workers[i]->collision = 16.0f;
        workers[i]->health.value = workers[i]->health.max_value = 250.0f;
        workers[i]->unitinfo.MoveSpeed = 190.0f;
        workers[i]->s.angle = 0.0f; /* stale facing points east, away from mine */
        unit_stand(workers[i]);
        origin[i] = workers[i]->s.origin2;
        harvest_gold_start(workers[i], mine);
    }

    FOR_LOOP(i, WORKERS) {
        workers[i]->currentmove->think(workers[i]);
        T_FEQ(Vector2_distance(&workers[i]->s.origin2, &origin[i]), 0.0f, 0.001f);
        T_EQ(workers[i]->movement.flow_generation, 0);
        T_ASSERT(!workers[i]->movement.flow_direct);
        T_ASSERT(!workers[i]->movement.path_valid);
    }

    CM_ProcessPathJobs(65536);
    FOR_LOOP(i, WORKERS) {
        workers[i]->currentmove->think(workers[i]);
        T_ASSERT(workers[i]->movement.flow_generation != 0);
        T_ASSERT(!workers[i]->movement.path_valid);
        T_ASSERT(Vector2_distance(&workers[i]->s.origin2, &origin[i]) > 0.001f);
    }

    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

/* A Town Hall is a blocked footprint, not a reachable centre point.  The
 * interaction walker should first take a collision-sized edge lane instead of
 * waiting for a point-flow toward the blocked centre.  This reproduces the
 * Human02 return stall where a Peasant could sit more than 100 units from the
 * footprint until another worker vacated the shared centre-directed lane. */
TEST(wc3_movement, gold_return_prefers_direct_footprint_edge_lane) {
    enum { CELLS = 64 };
    BYTE pathmap[CELLS * CELLS] = {0};
    LPEDICT worker = make_moving_unit(0.0f, 0.0f);
    LPEDICT mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), -400.0f, 0.0f);
    LPEDICT hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 320.0f, 0.0f);
    pathTex_t *hall_pathtex = movement_make_goldmine_pathtex();
    VECTOR2 const origin = worker->s.origin2;
    FLOAT const before = 192.0f;

    worker->collision = 16.0f;
    worker->unitinfo.MoveSpeed = 190.0f;
    worker->harvested_gold = 10;
    worker->s.renderfx |= RF_HAS_GOLD;
    worker->secondarygoal = mine;
    hall->collision = 64.0f;
    hall->s.model = 1;
    hall->s.player = worker->s.player;
    hall->pathtex = hall_pathtex;
    make_live_dropoff(hall, &return_gold_lumber_abilities);
    gi.LinkEntity(worker);
    gi.LinkEntity(hall);

    /* 320 world units maps to cell 42 in this fixture.  Mirror the 8x8
     * no-walk centre of movement_make_goldmine_pathtex(). */
    for (int y = 28; y < 36; y++) {
        for (int x = 38; x < 46; x++)
            pathmap[x + y * CELLS] = 0x02;
    }
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(BOX2,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    T_ASSERT(harvest_gold_return_to(worker, hall));
    T_FEQ(CM_DistanceToPathingFootprint(hall, &worker->s.origin2), before, 0.01f);
    worker->currentmove->think(worker);

    T_ASSERT(worker->movement.flow_direct);
    T_ASSERT(worker->s.origin2.x > origin.x);
    T_ASSERT(CM_DistanceToPathingFootprint(hall, &worker->s.origin2) < before);
    gi.MemFree(hall_pathtex);
}

/* Local collision can move a returner away from the edge lane that was nearest
 * on the previous think.  Re-select from the current position: retaining one
 * lane for the whole return leg makes packed Peasants steer back across the
 * Town Hall footprint and oscillate around one another. */
TEST(wc3_movement, gold_return_reselects_footprint_edge_after_displacement) {
    enum { CELLS = 64 };
    BYTE pathmap[CELLS * CELLS] = {0};
    LPEDICT worker = make_moving_unit(0.0f, 0.0f);
    LPEDICT mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), -400.0f, 0.0f);
    LPEDICT hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 320.0f, 0.0f);
    pathTex_t *hall_pathtex = movement_make_goldmine_pathtex();
    VECTOR2 const displaced = { 640.0f, 160.0f };
    VECTOR2 expected, expected_dir, actual_dir;
    FLOAT step, route_band;

    worker->collision = 16.0f;
    worker->unitinfo.MoveSpeed = 190.0f;
    worker->harvested_gold = 10;
    worker->s.renderfx |= RF_HAS_GOLD;
    worker->secondarygoal = mine;
    hall->collision = 64.0f;
    hall->s.model = 1;
    hall->s.player = worker->s.player;
    hall->pathtex = hall_pathtex;
    make_live_dropoff(hall, &return_gold_lumber_abilities);
    gi.LinkEntity(worker);
    gi.LinkEntity(hall);

    for (int y = 28; y < 36; y++) {
        for (int x = 38; x < 46; x++)
            pathmap[x + y * CELLS] = 0x02;
    }
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(BOX2,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    T_ASSERT(harvest_gold_return_to(worker, hall));
    worker->currentmove->think(worker);

    /* Simulate collision avoidance having displaced this worker to the other
     * side of the drop-off without restarting the Harvest order. */
    worker->s.origin2 = displaced;
    gi.LinkEntity(worker);
    step = unit_movedistance(worker);
    route_band = worker->collision + step +
                 CM_PathCellWorldSize() * 1.41421356237f;
    T_ASSERT(CM_FindApproachPointToFootprintForRadius(
        hall, &worker->s.origin2, route_band, worker->collision, &expected));
    T_ASSERT(CM_LineIsWalkableForRadius(
        &worker->s.origin2, &expected, worker->collision));
    expected_dir = Vector2_sub(&expected, &worker->s.origin2);
    Vector2_normalize(&expected_dir);

    worker->currentmove->think(worker);
    actual_dir = MAKE(VECTOR2, cosf(worker->movement.heading),
                               sinf(worker->movement.heading));
    T_ASSERT(Vector2_dot(&expected_dir, &actual_dir) > 0.99f);
    gi.MemFree(hall_pathtex);
}

/* Gold return can miss the shared cache independently of mine approach. The
 * bounded mover route is best-effort; when this long detour exceeds that local
 * accelerator, Return Resources must hold rather than use stale facing, then
 * resume from the shared collision-sized field when its job completes. */
TEST(wc3_movement, gold_return_holds_while_shared_route_is_pending) {
    enum { CELLS = 64 };
    BYTE pathmap[CELLS * CELLS] = {0};
    LPEDICT worker = make_moving_unit(320.0f, 0.0f);
    LPEDICT mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 500.0f, 0.0f);
    LPEDICT hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), -320.0f, 0.0f);
    VECTOR2 origin;

    FOR_LOOP(y, CELLS)
        pathmap[32 + y * CELLS] = 0x02;
    pathmap[32 + 40 * CELLS] = 0;
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(BOX2,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    worker->collision = 16.0f;
    worker->unitinfo.MoveSpeed = 190.0f;
    worker->s.angle = 0.0f; /* stale facing points east, away from hall */
    worker->harvested_gold = 10;
    worker->s.renderfx |= RF_HAS_GOLD;
    worker->secondarygoal = mine;
    hall->collision = 64.0f;
    hall->s.model = 1;
    hall->s.player = worker->s.player;
    make_live_dropoff(hall, &return_gold_lumber_abilities);
    gi.LinkEntity(hall);

    T_ASSERT(harvest_gold_return_to(worker, hall));
    origin = worker->s.origin2;
    worker->currentmove->think(worker);

    T_FEQ(Vector2_distance(&worker->s.origin2, &origin), 0.0f, 0.001f);
    T_EQ(worker->movement.flow_generation, 0);
    T_ASSERT(!worker->movement.flow_direct);
    T_ASSERT(!worker->movement.path_valid);

    CM_ProcessPathJobs(65536);
    worker->currentmove->think(worker);
    T_ASSERT(worker->movement.flow_generation != 0);
    T_ASSERT(!worker->movement.path_valid);
    T_ASSERT(Vector2_distance(&worker->s.origin2, &origin) > 0.001f);
}

/* Right-click is also the cancel gesture for an active targeted command.
 * Leaving Harvest target mode armed lets the next left-click on an idle worker
 * be consumed as the old target click, so the previous worker group stays
 * selected and a following lumber Smart order retasks that entire group. */
TEST(wc3_movement, harvest_target_mode_right_click_cancel_prevents_stale_group_retask) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(LPEDICT) = gi.unicast;
    LPEDICT clent = &g_edicts[0];
    LPGAMECLIENT client = clent->client;
    LPEDICT miner1, miner2, idle, tree;
    char tree_number[16];
    LPCSTR cancel_command[] = { "smartpoint", "256", "256" };
    LPCSTR harvest_command[] = { "smart", tree_number };

    setup_test_world();
    gi.Write = movement_noop_write;
    gi.unicast = movement_noop_unicast;
    miner1 = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0.0f, 0.0f);
    miner2 = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 32.0f, 0.0f);
    idle = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 64.0f, 0.0f);
    tree = make_harvest_tree(160.0f, 0.0f, 100.0f);
    miner1->data.UnitAbilities = miner2->data.UnitAbilities = idle->data.UnitAbilities = &harvest_abilities;
    G_SelectEntity(client, miner1);
    G_SelectEntity(client, miner2);
    client->menu.on_entity_selected = harvest_menu_selecttarget;

    G_ClientCommand(clent, 3, cancel_command);

    T_NULL(client->menu.on_entity_selected);
    T_NULL(client->menu.on_location_selected);
    T_NULL(miner1->goalentity);
    T_NULL(miner2->goalentity);

    /* Selection UI rebuilds the portrait/info panel, which is outside this
     * movement test fixture. Once target mode is proven cleared, update the
     * selected set directly and verify the next Smart order cannot reach the
     * old miner group through a stale callback. */
    G_DeselectEntity(client, miner1);
    G_DeselectEntity(client, miner2);
    G_SelectEntity(client, idle);
    T_ASSERT(!G_IsEntitySelected(client, miner1));
    T_ASSERT(!G_IsEntitySelected(client, miner2));
    T_ASSERT(G_IsEntitySelected(client, idle));

    snprintf(tree_number, sizeof(tree_number), "%u", (unsigned)tree->s.number);
    G_ClientCommand(clent, 2, harvest_command);
    T_ASSERT(idle->goalentity == tree);
    T_ASSERT(idle->secondarygoal == tree);
    T_NULL(miner1->goalentity);
    T_NULL(miner2->goalentity);

    gi.Write = old_write;
    gi.unicast = old_unicast;
}

/* Entity Smart/right-click uses the same cancel contract as ground Smart. */
TEST(wc3_movement, harvest_target_mode_right_click_entity_cancels_without_order) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(LPEDICT) = gi.unicast;
    LPEDICT clent = &g_edicts[0];
    LPGAMECLIENT client = clent->client;
    LPEDICT worker, tree;
    char tree_number[16];
    LPCSTR command[] = { "smart", tree_number };

    setup_test_world();
    gi.Write = movement_noop_write;
    gi.unicast = movement_noop_unicast;
    worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0.0f, 0.0f);
    tree = make_harvest_tree(160.0f, 0.0f, 100.0f);
    worker->data.UnitAbilities = &harvest_abilities;
    G_SelectEntity(client, worker);
    client->menu.on_entity_selected = harvest_menu_selecttarget;
    snprintf(tree_number, sizeof(tree_number), "%u", (unsigned)tree->s.number);

    G_ClientCommand(clent, 2, command);

    T_NULL(client->menu.on_entity_selected);
    T_NULL(client->menu.on_location_selected);
    T_NULL(worker->goalentity);
    T_NULL(worker->secondarygoal);

    gi.Write = old_write;
    gi.unicast = old_unicast;
}

/* Reissuing Harvest while already full remembers the requested tree but begins
 * return immediately, so no extra over-capacity chop can occur. */
TEST(wc3_movement, lumber_full_worker_returns_before_new_chop) {
    LPEDICT worker = make_moving_unit(0.0f, 0.0f);
    LPEDICT tree = make_harvest_tree(100.0f, 0.0f, 100.0f);
    LPEDICT hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 300.0f, 0.0f);

    hall->s.player = worker->s.player;
    make_live_dropoff(hall, &return_gold_lumber_abilities);
    worker->harvested_lumber = 10;
    worker->s.renderfx |= RF_HAS_LUMBER;
    HARVEST_LUMBER_CAPACITY = 10.0f;

    harvest_start(worker, tree);

    T_ASSERT(worker->secondarygoal == tree);
    T_ASSERT(worker->goalentity == hall);
    T_STREQ(worker->currentmove->animation, "walk");
    T_EQ(worker->harvested_lumber, 10);
}

/* The capacity-filling chop must fell the tree before return starts.  After
 * depositing, the worker must reject that dead tree and select the next one. */
TEST(wc3_movement, lumber_lethal_trip_fells_then_selects_next_tree) {
    reset_entities();
    setup_test_world();
    LPEDICT worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0.0f, 0.0f);
    worker->movetype = MOVETYPE_STEP; worker->stand = unit_stand; worker->die = unit_die;
    worker->collision = 0.0f; worker->attack1.damagePoint = 0.01f;
    LPEDICT tree1 = make_harvest_tree(20.0f, 0.0f, 10.0f);
    tree1->s.model = G_RegisterModel("Doodads\\Terrain\\LordaeronTree\\LordaeronTree0.mdx");
    LPEDICT tree2 = make_harvest_tree(30.0f, 0.0f, 500.0f);
    LPEDICT hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 0.0f, 0.0f);
    hall->s.player = worker->s.player;
    make_live_dropoff(hall, &return_gold_lumber_abilities);
    HARVEST_RANGE = 64.0f; HARVEST_TREE_DAMAGE = 1.0f;
    HARVEST_LUMBER_CAPACITY = 10.0f; HARVEST_COOLDOWN = 0.01f; HARVEST_SEARCH_RANGE = 1000.0f;
    MSGTRACE trace = {0};
    T_ASSERT(G_SubscribeMessage(trace_message, &trace));
    harvest_start(worker, tree1);
    worker->currentmove->think(worker); /* enter the first swing */
    FOR_LOOP(i, 10) {
        worker->wait = 0.01f;
        worker->currentmove->think(worker); /* chop */
        harvest_cooldown(worker);           /* cooldown, or return on chop ten */
        if (i < 9) {
            worker->wait = 0.01f;
            worker->currentmove->think(worker); /* start the next swing */
        }
    }
    worker->currentmove->think(worker); /* deposit and select tree2 */
    worker->currentmove->think(worker); /* begin chopping tree2 */
    G_UnsubscribeMessage(trace_message, &trace);

    T_FEQ(tree1->health.value, 0.0f, 0.01f);
    T_ASSERT(tree1->svflags & SVF_DEADMONSTER);
    T_STREQ(tree1->currentmove->animation, "death");
    if (tree1->animation) {
        T_STREQ(tree1->animation->name, "death");
        T_EQ(tree1->s.frame, tree1->animation->interval[0]);
    } else {
        T_EQ(tree1->s.frame, 0);
    }
    T_ASSERT(worker->goalentity == tree2);
    T_ASSERT(worker->secondarygoal == tree2);
    T_EQ(trace.count, 17);
    T_EQ(trace.msg[12].type, GAME_MSG_HARVEST_TREE_FELLED);
    T_EQ(trace.msg[12].target, tree1->s.number);
    T_EQ(trace.msg[13].type, GAME_MSG_HARVEST_RETURN_LUMBER);
    T_EQ(trace.msg[13].target, hall->s.number);
    T_EQ(trace.msg[14].type, GAME_MSG_HARVEST_DEPOSIT_LUMBER);
    T_EQ(trace.msg[15].type, GAME_MSG_HARVEST_RESUME_LUMBER);
    T_EQ(trace.msg[15].target, tree2->s.number);
    T_EQ(trace.msg[16].type, GAME_MSG_HARVEST_START_CHOP);
    T_EQ(trace.msg[16].target, tree2->s.number);
}

/* With no live tree left, depositing lumber ends in stand and emits no false
 * resume transition naming the felled tree. */
TEST(wc3_movement, lumber_deposit_without_live_tree_stops) {
    LPEDICT worker = make_moving_unit(0.0f, 0.0f);
    LPEDICT tree = make_harvest_tree(20.0f, 0.0f, 1.0f);
    LPEDICT hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 0.0f, 0.0f);
    worker->attack1.damagePoint = 0.01f;
    hall->s.player = worker->s.player;
    make_live_dropoff(hall, &return_gold_lumber_abilities);
    HARVEST_RANGE = 64.0f; HARVEST_TREE_DAMAGE = 1.0f; HARVEST_LUMBER_CAPACITY = 1.0f;
    MSGTRACE trace = {0};
    T_ASSERT(G_SubscribeMessage(trace_message, &trace));
    harvest_start(worker, tree);
    worker->currentmove->think(worker);
    worker->wait = 0.01f; worker->currentmove->think(worker);
    harvest_cooldown(worker);
    worker->currentmove->think(worker);
    G_UnsubscribeMessage(trace_message, &trace);

    T_ASSERT(worker->goalentity == NULL);
    T_ASSERT(worker->secondarygoal == NULL);
    T_STREQ(worker->currentmove->animation, "stand");
    T_EQ(trace.count, 6);
    T_EQ(trace.msg[4].type, GAME_MSG_HARVEST_RETURN_LUMBER);
    T_EQ(trace.msg[5].type, GAME_MSG_HARVEST_DEPOSIT_LUMBER);
}

/* A manual return may carry lumber without a remembered tree target. */
TEST(wc3_movement, lumber_manual_return_without_tree_stops) {
    LPEDICT worker = make_moving_unit(0.0f, 0.0f);
    LPEDICT hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 0.0f, 0.0f);
    hall->s.player = worker->s.player;
    make_live_dropoff(hall, &return_gold_lumber_abilities);
    worker->harvested_lumber = 1;
    worker->s.renderfx |= RF_HAS_LUMBER;
    harvest_walkback(worker);
    worker->currentmove->think(worker);

    T_ASSERT(worker->goalentity == NULL);
    T_ASSERT(worker->secondarygoal == NULL);
    T_EQ(worker->harvested_lumber, 0);
    T_STREQ(worker->currentmove->animation, "stand");
}

/* If the remembered tree dies while the worker is away, replacement-tree
 * selection is centered on that forest rather than the return building. */
TEST(wc3_movement, lumber_dead_previous_tree_searches_near_old_tree) {
    LPEDICT worker = make_moving_unit(0.0f, 0.0f);
    LPEDICT old_tree = make_harvest_tree(-400.0f, 0.0f, 100.0f);
    LPEDICT forest_tree = make_harvest_tree(-450.0f, 0.0f, 100.0f);
    LPEDICT dropoff_tree = make_harvest_tree(50.0f, 0.0f, 100.0f);
    LPEDICT hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 0.0f, 0.0f);

    hall->s.player = worker->s.player;
    make_live_dropoff(hall, &return_gold_lumber_abilities);
    old_tree->health.value = 0.0f;
    worker->harvested_lumber = 10;
    worker->s.renderfx |= RF_HAS_LUMBER;
    worker->secondarygoal = old_tree;
    HARVEST_SEARCH_RANGE = 1000.0f;

    harvest_walkback(worker);
    worker->currentmove->think(worker);

    T_ASSERT(worker->goalentity == forest_tree);
    T_ASSERT(worker->secondarygoal == forest_tree);
    T_ASSERT(worker->goalentity != dropoff_tree);
    T_EQ(worker->harvested_lumber, 0);
}

/* Smart-targeting a compatible drop-off while carrying lumber honors the
 * building the player clicked instead of silently choosing another nearer one. */
TEST(wc3_movement, lumber_smart_click_returns_to_clicked_dropoff) {
    LPEDICT worker = make_moving_unit(0.0f, 0.0f);
    LPEDICT hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 100.0f, 0.0f);
    LPEDICT mill = alloc_test_unit(MAKEFOURCC('h','l','u','m'), 500.0f, 0.0f);

    worker->data.UnitAbilities = &harvest_abilities;
    hall->s.player = mill->s.player = worker->s.player;
    make_live_dropoff(hall, &return_gold_lumber_abilities);
    make_live_dropoff(mill, &return_lumber_abilities);
    worker->harvested_lumber = 10;
    worker->s.renderfx |= RF_HAS_LUMBER;

    T_ASSERT(unit_issuetargetorder(worker, "smart", mill));
    T_ASSERT(worker->goalentity == mill);
    T_EQ(worker->harvested_lumber, 10);
}

/* A large Town Hall footprint can block the next step before the old +5u
 * lumber deposit tolerance is reached. Deposit at contact plus one simulation
 * step so the worker does not get stuck against the building pathing map. */
TEST(wc3_movement, lumber_return_deposits_at_next_step_contact) {
    LPEDICT worker = make_moving_unit(0.0f, 0.0f);
    LPEDICT tree = make_harvest_tree(-400.0f, 0.0f, 100.0f);
    LPEDICT hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 220.0f, 0.0f);
    DWORD const old_lumber = game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_LUMBER];

    worker->collision = 16.0f; worker->unitinfo.MoveSpeed = 190.0f;
    hall->collision = 192.0f; hall->s.model = 1; hall->s.player = worker->s.player;
    make_live_dropoff(hall, &return_gold_lumber_abilities);
    gi.LinkEntity(worker); gi.LinkEntity(tree); gi.LinkEntity(hall);
    worker->harvested_lumber = 10;
    worker->s.renderfx |= RF_HAS_LUMBER;
    worker->secondarygoal = tree;

    harvest_walkback(worker);
    T_ASSERT(M_DistanceToGoal(worker) > worker->collision + hall->collision + 5.0f);
    T_ASSERT(M_DistanceToGoal(worker) <= worker->collision + hall->collision + unit_movedistance(worker));
    worker->s.renderfx |= RF_HAS_GOLD; /* stale opposite carry tag must not survive deposit */
    worker->currentmove->think(worker);

    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_LUMBER], old_lumber + 10);
    T_EQ(worker->harvested_lumber, 0);
    T_EQ(worker->harvested_gold, 0);
    T_ASSERT(!(worker->s.renderfx & RF_HAS_LUMBER));
    T_ASSERT(!(worker->s.renderfx & RF_HAS_GOLD));
    T_ASSERT(worker->goalentity == tree);
}


/* Returning lumber to a building with authored blocking pathing uses the same
 * generic point-route contract as mine entry.  Routing may approach the blocked
 * center, but the resource behavior owns the contact+step completion boundary. */
/* Lumber return uses the authored no-walk footprint as the physical deposit
 * boundary, matching gold return.  A drop-off can have a scalar collision
 * circle smaller than its pathing texture; in that case the worker must not
 * wait for or route toward the blocked model centre after it has already
 * reached the building footprint. */
TEST(wc3_movement, lumber_return_deposits_at_dropoff_footprint_corner) {
    enum { CELLS = 64 };
    BYTE pathmap[CELLS * CELLS] = {0};
    LPEDICT worker = make_moving_unit(170.0f, 170.0f);
    LPEDICT tree = make_harvest_tree(-400.0f, 0.0f, 100.0f);
    LPEDICT mill = alloc_test_unit(MAKEFOURCC('h','l','u','m'), 320.0f, 320.0f);
    pathTex_t *mill_pathtex = movement_make_goldmine_pathtex();
    DWORD const old_lumber = game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_LUMBER];

    worker->collision = 16.0f;
    worker->unitinfo.MoveSpeed = 190.0f;
    mill->collision = 64.0f; /* deliberately smaller than authored footprint */
    mill->s.model = 1;
    mill->s.player = worker->s.player;
    mill->pathtex = mill_pathtex;
    make_live_dropoff(mill, &return_lumber_abilities);
    gi.LinkEntity(worker);
    gi.LinkEntity(tree);
    gi.LinkEntity(mill);

    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(BOX2,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    worker->harvested_lumber = 10;
    worker->s.renderfx |= RF_HAS_LUMBER;
    worker->secondarygoal = tree;
    harvest_walkback(worker);

    T_ASSERT(worker->goalentity == mill);
    T_ASSERT(M_DistanceToGoal(worker) >
             worker->collision + mill->collision + unit_movedistance(worker));
    T_ASSERT(CM_DistanceToPathingFootprint(mill, &worker->s.origin2) <=
             worker->collision + unit_movedistance(worker));

    worker->currentmove->think(worker);

    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_LUMBER], old_lumber + 10);
    T_EQ(worker->harvested_lumber, 0);
    T_ASSERT(!(worker->s.renderfx & RF_HAS_LUMBER));
    T_ASSERT(worker->goalentity == tree);
    gi.MemFree(mill_pathtex);
}

TEST(wc3_movement, lumber_return_reaches_blocked_townhall_footprint) {
    enum { CELLS = 64 };
    BYTE pathmap[CELLS * CELLS] = {0};
    LPEDICT worker = make_moving_unit(0.0f, 0.0f);
    LPEDICT tree = make_harvest_tree(-400.0f, 0.0f, 100.0f);
    LPEDICT hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 320.0f, 0.0f);
    DWORD const old_lumber = game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_LUMBER];

    worker->collision = 16.0f;
    worker->unitinfo.MoveSpeed = 190.0f;
    hall->collision = 192.0f;
    hall->s.model = 1;
    hall->movetype = MOVETYPE_NONE;
    hall->s.player = worker->s.player;
    make_live_dropoff(hall, &return_gold_lumber_abilities);
    gi.LinkEntity(worker);
    gi.LinkEntity(tree);
    gi.LinkEntity(hall);

    /* 12x12 Town Hall footprint centered on world (320,0). */
    for (int y = 26; y < 38; y++) {
        for (int x = 36; x < 48; x++)
            pathmap[x + y * CELLS] = 0x02;
    }
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(BOX2,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    worker->harvested_lumber = 10;
    worker->s.renderfx |= RF_HAS_LUMBER;
    worker->secondarygoal = tree;
    harvest_walkback(worker);

    FOR_LOOP(i, 80) {
        worker->currentmove->think(worker);
        CM_ProcessPathJobs(65536);
        if (!worker->harvested_lumber) break;
    }

    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_LUMBER], old_lumber + 10);
    T_EQ(worker->harvested_lumber, 0);
    T_ASSERT(!(worker->s.renderfx & RF_HAS_LUMBER));
    T_ASSERT(worker->goalentity == tree);
}

/* The old training helper checked only dynamic circles and could choose a
 * point inside the producer's baked pathing footprint. This reproduces the
 * Human02 trained-Peasant regression observed while validating resource return. */
TEST(wc3_movement, trained_unit_exit_skips_blocked_producer_footprint) {
    enum { CELLS = 64 };
    BYTE pathmap[CELLS * CELLS] = {0};
    LPEDICT producer = make_moving_unit(0.0f, 0.0f);
    LPEDICT trained = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0.0f, 0.0f);
    VECTOR2 exit;
    FLOAT angle;

    producer->class_id = MAKEFOURCC('h','t','o','w');
    producer->movetype = MOVETYPE_NONE;
    producer->collision = 192.0f;
    trained->collision = 16.0f;

    /* 16x16 no-walk cells centered on the producer model a large authored
     * building footprint. WPM bit 1 is the no-walk flag. */
    for (int y = 24; y < 40; y++) {
        for (int x = 24; x < 40; x++) {
            pathmap[x + y * CELLS] = 0x02;
        }
    }
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(BOX2,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    T_ASSERT(SP_FindUnitExitPosition(producer, trained, &exit, &angle));
    T_ASSERT(CM_PointIsPathableForRadius(&exit, trained->collision));
    T_ASSERT(Vector2_distance(&producer->s.origin2, &exit) > 256.0f);
}

/* Dynamic unit circles are also part of legal exit placement. The first
 * deterministic candidate is occupied, so the trained unit must pick another. */
TEST(wc3_movement, trained_unit_exit_skips_dynamic_blocker) {
    LPEDICT producer = make_moving_unit(0.0f, 0.0f);
    LPEDICT trained = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0.0f, 0.0f);
    LPEDICT blocker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), -64.0f, -64.0f);
    VECTOR2 exit;
    FLOAT angle;

    producer->movetype = MOVETYPE_NONE;
    trained->collision = 16.0f;
    blocker->movetype = MOVETYPE_STEP;
    blocker->collision = 16.0f;
    blocker->s.model = 1;

    T_ASSERT(SP_FindUnitExitPosition(producer, trained, &exit, &angle));
    T_ASSERT(Vector2_distance(&blocker->s.origin2, &exit) >=
             trained->collision + blocker->collision);
}

/* Completing the head of a multi-unit queue must preserve the next link.
 * unit_stand() clears the completed unit's build pointer, which is also the
 * queue link while that unit is waiting behind the producer. */
TEST(wc3_movement, trained_unit_completion_preserves_remaining_queue) {
    LPEDICT producer = make_moving_unit(0.0f, 0.0f);
    LPEDICT first = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0.0f, 0.0f);
    LPEDICT second = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0.0f, 0.0f);
    UnitBalance_t balance = { .buildTime = 1, .foodUsed = 2, .foodMade = 4 };
    LPGAMECLIENT client = &game.clients[0];

    producer->class_id = MAKEFOURCC('h','t','o','w');
    producer->movetype = MOVETYPE_NONE;
    producer->s.player = first->s.player = second->s.player = client->ps.number;
    first->stand = second->stand = unit_stand;
    first->data.UnitBalance = second->data.UnitBalance = &balance;
    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 100;
    first->health.max_value = second->health.max_value = 100.0f;
    first->health.value = 100.0f;
    second->health.value = 0.0f;
    first->training = second->training = true;
    first->s.renderfx |= RF_HIDDEN;
    second->s.renderfx |= RF_HIDDEN;
    first->build = second;
    producer->build = first;

    ai_train_build(producer);

    T_ASSERT(producer->build == second);
    T_NULL(first->build);
    T_ASSERT(!first->training);
    T_ASSERT(!(first->s.renderfx & RF_HIDDEN));
    T_ASSERT(second->training);
    T_ASSERT(second->s.renderfx & RF_HIDDEN);
    T_FEQ(second->health.value, 0.0f, 0.01f);
    T_EQ(first->food.used, 2);
    T_EQ(first->food.made, 4);
    T_EQ(second->food.used, 2);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_USED], 4);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP], 104);
}

/* A completed unit must remain hidden and queued when no legal exit exists;
 * revealing it on blocked pathing recreates the permanent stuck-unit bug. */
TEST(wc3_movement, trained_unit_waits_when_no_exit_position_exists) {
    enum { CELLS = 64 };
    BYTE pathmap[CELLS * CELLS];
    LPEDICT producer = make_moving_unit(0.0f, 0.0f);
    LPEDICT trained = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0.0f, 0.0f);

    memset(pathmap, 0x02, sizeof(pathmap));
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(BOX2,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    producer->class_id = MAKEFOURCC('h','t','o','w');
    producer->movetype = MOVETYPE_NONE;
    producer->build = trained;
    UnitBalance_t balance = { .buildTime = 1 };
    trained->data.UnitBalance = &balance;
    trained->collision = 16.0f;
    trained->health.max_value = 100.0f;
    trained->health.value = 100.0f;
    trained->s.renderfx |= RF_HIDDEN;

    ai_train_build(producer);

    T_ASSERT(producer->build == trained);
    T_ASSERT(trained->s.renderfx & RF_HIDDEN);
    T_FEQ(trained->s.origin2.x, 0.0f, 0.01f);
    T_FEQ(trained->s.origin2.y, 0.0f, 0.01f);
}

/* Lumber return is ability-driven and chooses the nearest compatible
 * same-owner building rather than preferring a Town Hall class. */
TEST(wc3_movement, lumber_return_prefers_nearer_lumber_mill) {
    LPEDICT worker = make_moving_unit(0.0f, 0.0f);
    LPEDICT hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 500.0f, 0.0f);
    LPEDICT mill = alloc_test_unit(MAKEFOURCC('h','l','u','m'), 100.0f, 0.0f);
    hall->s.player = mill->s.player = worker->s.player;
    make_live_dropoff(hall, &return_gold_lumber_abilities);
    make_live_dropoff(mill, &return_lumber_abilities);
    worker->harvested_lumber = 10;
    worker->s.renderfx |= RF_HAS_LUMBER;

    harvest_walkback(worker);

    T_ASSERT(worker->goalentity == mill);
    T_FEQ(worker->harvested_lumber, 10.0f, 0.01f);
}

/* If the chosen Lumber Mill dies during the trip, retain the carried lumber
 * and redirect to the nearest remaining compatible return building. */
TEST(wc3_movement, lumber_return_retargets_after_lumber_mill_dies) {
    LPEDICT worker = make_moving_unit(0.0f, 0.0f);
    LPEDICT hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 500.0f, 0.0f);
    LPEDICT mill = alloc_test_unit(MAKEFOURCC('h','l','u','m'), 100.0f, 0.0f);
    hall->s.player = mill->s.player = worker->s.player;
    make_live_dropoff(hall, &return_gold_lumber_abilities);
    make_live_dropoff(mill, &return_lumber_abilities);
    worker->unitinfo.MoveSpeed = 190.0f;
    worker->harvested_lumber = 10;
    worker->s.renderfx |= RF_HAS_LUMBER;

    harvest_walkback(worker);
    T_ASSERT(worker->goalentity == mill);
    mill->health.value = 0;
    worker->currentmove->think(worker);

    T_ASSERT(worker->goalentity == hall);
    T_FEQ(worker->harvested_lumber, 10.0f, 0.01f);
    T_ASSERT(worker->s.renderfx & RF_HAS_LUMBER);
}

/* The complete gold loop enters, exits carrying gold, deposits it, and resumes mining. */
TEST(wc3_movement, gold_worker_deposits_and_resumes_mining) {
    LPEDICT worker = make_moving_unit(0.0f, 0.0f);
    LPEDICT mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 400.0f, 0.0f);
    LPEDICT hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 0.0f, 0.0f);
    worker->collision = 16.0f; worker->unitinfo.MoveSpeed = 100.0f;
    mine->collision = 128.0f; mine->s.model = 1;
    hall->collision = 64.0f; hall->s.model = 1;
    make_live_dropoff(hall, &return_gold_lumber_abilities);
    setup_test_goldmine(mine, &test_goldmine_cap1, 100);
    gi.LinkEntity(worker); gi.LinkEntity(mine); gi.LinkEntity(hall);
    slkTestData_t *rows, *old_abilities = install_goldmine_test_data(&rows);
    HARVEST_GOLD_CAPACITY = 10.0f;
    DWORD const old_gold = game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD];
    MSGTRACE trace = {0};
    T_ASSERT(G_SubscribeMessage(trace_message, &trace));
    harvest_gold_start(worker, mine);

    FOR_LOOP(i, 100) {
        worker->currentmove->think(worker);
        if (game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD] > old_gold) break;
    }
    G_UnsubscribeMessage(trace_message, &trace);

    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD], old_gold + 10);
    T_EQ(worker->harvested_gold, 0);
    T_ASSERT(!(worker->s.renderfx & RF_HIDDEN));
    T_ASSERT(worker->secondarygoal == mine);
    T_EQ(trace.count, 5);
    T_EQ(trace.msg[0].type, GAME_MSG_HARVEST_MOVE_GOLD);
    T_EQ(trace.msg[1].type, GAME_MSG_HARVEST_ENTER_MINE);
    T_EQ(trace.msg[2].type, GAME_MSG_HARVEST_RETURN_GOLD);
    T_EQ(trace.msg[3].type, GAME_MSG_HARVEST_DEPOSIT_GOLD);
    T_EQ(trace.msg[4].type, GAME_MSG_HARVEST_RESUME_GOLD);
    FOR_LOOP(i, trace.count)
        T_EQ(trace.msg[i].actor, worker->s.number);
    T_EQ(trace.msg[0].target, mine->s.number);
    T_EQ(trace.msg[1].target, mine->s.number);
    T_EQ(trace.msg[2].target, hall->s.number);
    T_EQ(trace.msg[3].target, hall->s.number);
    T_EQ(trace.msg[4].target, mine->s.number);
    T_EQ(mine->resources, 90);
    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

/* Gold pickup replaces a prior lumber carry state.  RF_HAS_LUMBER used to
 * survive here, and the renderer checks lumber before gold, so the Peasant
 * continued to display the lumber-carry model while actually carrying gold. */
TEST(wc3_movement, gold_pickup_replaces_lumber_carry_state) {
    LPEDICT worker = make_moving_unit(0.0f, 0.0f);
    LPEDICT mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 0.0f, 0.0f);
    slkTestData_t *rows, *old_abilities = install_goldmine_test_data(&rows);

    setup_test_goldmine(mine, &test_goldmine_cap1, 100);
    gi.LinkEntity(mine);
    HARVEST_GOLD_CAPACITY = 10.0f;
    worker->harvested_lumber = 5;
    worker->s.renderfx |= RF_HAS_LUMBER;
    worker->goalentity = worker->secondarygoal = mine;

    harvestgold_minegold(worker);
    harvestgold_walkback(worker);

    T_EQ(worker->harvested_lumber, 0);
    T_EQ(worker->harvested_gold, 10);
    T_ASSERT(!(worker->s.renderfx & RF_HAS_LUMBER));
    T_ASSERT(worker->s.renderfx & RF_HAS_GOLD);
    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

/* A large Town Hall footprint can block the next step before the old +5u
 * deposit tolerance is reached. The interaction must complete at contact plus
 * one simulation step, just like entering a gold mine. */
TEST(wc3_movement, gold_return_deposits_at_next_step_contact) {
    LPEDICT worker = make_moving_unit(0.0f, 0.0f);
    LPEDICT mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), -400.0f, 0.0f);
    LPEDICT hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 220.0f, 0.0f);
    DWORD const old_gold = game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD];

    worker->collision = 16.0f; worker->unitinfo.MoveSpeed = 190.0f;
    mine->collision = 128.0f; mine->s.model = 1;
    hall->collision = 192.0f; hall->s.model = 1; hall->s.player = worker->s.player;
    make_live_dropoff(hall, &return_gold_lumber_abilities);
    setup_test_goldmine(mine, &test_goldmine_cap1, 100);
    gi.LinkEntity(worker); gi.LinkEntity(mine); gi.LinkEntity(hall);
    slkTestData_t *rows, *old_abilities = install_goldmine_test_data(&rows);
    HARVEST_GOLD_CAPACITY = 10.0f;
    worker->goalentity = mine; worker->secondarygoal = mine;
    harvestgold_minegold(worker);
    harvestgold_walkback(worker);
    T_ASSERT(M_DistanceToGoal(worker) > worker->collision + hall->collision + 5.0f);
    T_ASSERT(M_DistanceToGoal(worker) <= worker->collision + hall->collision + unit_movedistance(worker));
    worker->s.renderfx |= RF_HAS_LUMBER; /* stale opposite carry tag must not survive deposit */
    worker->currentmove->think(worker);

    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD], old_gold + 10);
    T_EQ(worker->harvested_lumber, 0);
    T_EQ(worker->harvested_gold, 0);
    T_ASSERT(!(worker->s.renderfx & RF_HAS_LUMBER));
    T_ASSERT(!(worker->s.renderfx & RF_HAS_GOLD));
    T_ASSERT(worker->goalentity == mine);
    T_EQ(mine->resources, 90);
    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

/* Return-to-building range must use the authored footprint, not only the
 * building's scalar collision circle.  At a Town Hall corner the Peasant can
 * be one legal step from the no-walk cells while centre distance is still well
 * outside collision+step; gold must deposit at that footprint edge. */
TEST(wc3_movement, gold_return_deposits_at_townhall_footprint_corner) {
    enum { CELLS = 64 };
    BYTE pathmap[CELLS * CELLS] = {0};
    LPEDICT worker = make_moving_unit(170.0f, 170.0f);
    LPEDICT mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), -400.0f, 0.0f);
    LPEDICT hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 320.0f, 320.0f);
    pathTex_t *hall_pathtex = movement_make_goldmine_pathtex();
    DWORD const old_gold = game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD];

    worker->collision = 16.0f;
    worker->unitinfo.MoveSpeed = 190.0f;
    mine->collision = 128.0f;
    mine->s.model = 1;
    hall->collision = 64.0f; /* deliberately smaller than its authored footprint */
    hall->s.model = 1;
    hall->s.player = worker->s.player;
    hall->pathtex = hall_pathtex;
    make_live_dropoff(hall, &return_gold_lumber_abilities);
    setup_test_goldmine(mine, &test_goldmine_cap1, 100);
    gi.LinkEntity(worker);
    gi.LinkEntity(mine);
    gi.LinkEntity(hall);

    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(BOX2,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    slkTestData_t *rows, *old_abilities = install_goldmine_test_data(&rows);
    HARVEST_GOLD_CAPACITY = 10.0f;
    worker->goalentity = worker->secondarygoal = mine;
    harvestgold_minegold(worker);
    harvestgold_walkback(worker);

    T_ASSERT(worker->goalentity == hall);
    T_ASSERT(M_DistanceToGoal(worker) >
             worker->collision + hall->collision + unit_movedistance(worker));
    T_ASSERT(CM_DistanceToPathingFootprint(hall, &worker->s.origin2) <=
             worker->collision + unit_movedistance(worker));

    worker->currentmove->think(worker);

    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD], old_gold + 10);
    T_EQ(worker->harvested_gold, 0);
    T_ASSERT(!(worker->s.renderfx & RF_HAS_GOLD));
    T_ASSERT(worker->goalentity == mine);
    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
    gi.MemFree(hall_pathtex);
}

/* A lumber-only return ability is incompatible with carried gold even when it
 * is closer than a gold+lumber return building. */
TEST(wc3_movement, gold_return_rejects_nearer_lumber_only_dropoff) {
    LPEDICT worker = make_moving_unit(0.0f, 0.0f);
    LPEDICT mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), -400.0f, 0.0f);
    LPEDICT hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 500.0f, 0.0f);
    LPEDICT mill = alloc_test_unit(MAKEFOURCC('h','l','u','m'), 100.0f, 0.0f);
    hall->s.player = mill->s.player = worker->s.player;
    make_live_dropoff(hall, &return_gold_lumber_abilities);
    make_live_dropoff(mill, &return_lumber_abilities);
    setup_test_goldmine(mine, &test_goldmine_cap1, 100);
    gi.LinkEntity(worker); gi.LinkEntity(mine); gi.LinkEntity(hall); gi.LinkEntity(mill);
    slkTestData_t *rows, *old_abilities = install_goldmine_test_data(&rows);
    HARVEST_GOLD_CAPACITY = 10.0f;
    worker->goalentity = worker->secondarygoal = mine;
    harvestgold_minegold(worker); /* registers worker in mine */

    harvestgold_walkback(worker);

    T_ASSERT(worker->goalentity == hall);
    T_FEQ(worker->harvested_gold, 10.0f, 0.01f);
    T_ASSERT(worker->s.renderfx & RF_HAS_GOLD);
    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

/* Unsubscription is part of the callback lifetime contract. */
TEST(wc3_movement, gameplay_message_unsubscribe_stops_delivery) {
    LPEDICT worker = make_moving_unit(0.0f, 0.0f);
    MSGTRACE trace = {0};
    T_ASSERT(G_SubscribeMessage(trace_message, &trace));
    G_PublishMessage(worker, GAME_MSG_HARVEST_MOVE_GOLD, worker);
    G_UnsubscribeMessage(trace_message, &trace);
    G_PublishMessage(worker, GAME_MSG_HARVEST_ENTER_MINE, worker);
    T_EQ(trace.count, 1);
}

/* Duplicate subscriptions are idempotent, and exhaustion is explicit. */
TEST(wc3_movement, gameplay_message_subscription_capacity_is_bounded) {
    LPEDICT worker = make_moving_unit(0.0f, 0.0f);
    MSGTRACE trace[MAX_MESSAGE_SUBSCRIBERS + 1] = {0};
    T_ASSERT(G_SubscribeMessage(trace_message, &trace[0]));
    T_ASSERT(G_SubscribeMessage(trace_message, &trace[0]));
    FOR_LOOP(i, MAX_MESSAGE_SUBSCRIBERS - 1)
        T_ASSERT(G_SubscribeMessage(trace_message, &trace[i + 1]));
    T_ASSERT(!G_SubscribeMessage(trace_message, &trace[MAX_MESSAGE_SUBSCRIBERS]));
    G_PublishMessage(worker, GAME_MSG_HARVEST_MOVE_GOLD, worker);
    FOR_LOOP(i, MAX_MESSAGE_SUBSCRIBERS) {
        T_EQ(trace[i].count, 1);
        G_UnsubscribeMessage(trace_message, &trace[i]);
    }
}

/* -----------------------------------------------------------------------
 * order_move tests
 * --------------------------------------------------------------------- */

TEST(wc3_movement, order_move_sets_goalentity) {
    LPEDICT unit = make_moving_unit(0.0f, 0.0f);
    LPEDICT wp   = alloc_test_unit(0, 30.0f, 0.0f); /* reuse edict as waypoint */
    order_move(unit, wp);
    T_ASSERT(unit->goalentity == wp);
}

TEST(wc3_movement, order_move_sets_walk_animation) {
    LPEDICT unit = make_moving_unit(0.0f, 0.0f);
    LPEDICT wp   = alloc_test_unit(0, 30.0f, 0.0f);
    order_move(unit, wp);
    T_NOT_NULL(unit->currentmove);
    T_STREQ(unit->currentmove->animation, "walk");
}

/* -----------------------------------------------------------------------
 * Waypoint_add tests
 * --------------------------------------------------------------------- */

TEST(wc3_movement, waypoint_add_sets_origin) {
    VECTOR2 dest = {128.0f, 256.0f};
    LPEDICT wp = Waypoint_add(&dest);
    T_NOT_NULL(wp);
    T_FEQ(wp->s.origin.x, 128.0f, 0.01f);
    T_FEQ(wp->s.origin.y, 256.0f, 0.01f);
}

/* -----------------------------------------------------------------------
 * unit_movedistance tests
 * --------------------------------------------------------------------- */

TEST(wc3_movement, unit_movedistance_matches_formula) {
    /* unit_movedistance = 10 * speed / FRAMETIME */
    LPEDICT unit = make_moving_unit(0.0f, 0.0f);
    FLOAT expected = 10.0f * G_UnitBalance(MAKEFOURCC('h','p','e','a'))->speed / (FLOAT)FRAMETIME;
    T_FEQ(unit_movedistance(unit), expected, 0.01f);
}

TEST(wc3_movement, unit_movedistance_uses_scripted_move_speed) {
    LPEDICT unit = make_moving_unit(0.0f, 0.0f);
    unit->unitinfo.MoveSpeed = 300.0f;

    FLOAT expected = 10.0f * 300.0f / (FLOAT)FRAMETIME;
    T_FEQ(unit_movedistance(unit), expected, 0.01f);
}

/* -----------------------------------------------------------------------
 * M_DistanceToGoal tests
 * --------------------------------------------------------------------- */

TEST(wc3_movement, distance_to_goal_along_x_axis) {
    LPEDICT unit = make_moving_unit(0.0f, 0.0f);
    LPEDICT wp   = alloc_test_unit(0, 100.0f, 0.0f);
    unit->goalentity = wp;
    T_FEQ(M_DistanceToGoal(unit), 100.0f, 0.01f);
}

TEST(wc3_movement, distance_to_goal_diagonal) {
    LPEDICT unit = make_moving_unit(0.0f, 0.0f);
    LPEDICT wp   = alloc_test_unit(0, 30.0f, 40.0f); /* 3-4-5 right triangle → 50 */
    unit->goalentity = wp;
    T_FEQ(M_DistanceToGoal(unit), 50.0f, 0.1f);
}

TEST(wc3_movement, distance_to_goal_zero_when_at_goal) {
    LPEDICT unit = make_moving_unit(10.0f, 10.0f);
    LPEDICT wp   = alloc_test_unit(0, 10.0f, 10.0f);
    unit->goalentity = wp;
    T_FEQ(M_DistanceToGoal(unit), 0.0f, 0.01f);
}

/* Human01 LT05 is a walkable 32x32 destructable above river terrain; ground snapping must retain its deck Z. */
TEST(wc3_movement, ground_unit_stands_on_walkable_bridge_surface) {
    static DestructableData_t const bridge_data = { .walkable = true };
    struct { WORD width, height; COLOR32 map[4]; } bridge_path = { .width = 2, .height = 2 };
    LPEDICT unit = make_moving_unit(0.0f, 0.0f);
    LPEDICT bridge = G_Spawn();
    FLOAT const terrain = CM_GetHeightAtPoint(0.0f, 0.0f);

    bridge->class_id = MAKEFOURCC('L', 'T', '0', '5');
    bridge->data.DestructableData = &bridge_data;
    bridge->destructable.initialized = bridge->destructable.placement_solid = true;
    bridge->pathtex = (pathTex_t *)&bridge_path;
    bridge->s.origin = MAKE(VECTOR3, 0.0f, 0.0f, terrain + 64.0f);
    G_RegisterGroundSurface(bridge);
    M_CheckGround(unit);
    T_FEQ(unit->s.origin.z, terrain + 64.0f, 0.01f);

    unit->s.origin.x = CM_PathCellWorldSize() * 2.0f;
    M_CheckGround(unit);
    T_FEQ(unit->s.origin.z, CM_GetHeightAtPoint(unit->s.origin.x, unit->s.origin.y), 0.01f);
}

/* WPM water stays unwalkable; only the explicitly passable bridge lane may connect its banks. */
TEST(wc3_movement, water_is_blocked_except_at_authored_bridge_lane) {
    BYTE pathmap[15] = { 0 };
    VECTOR2 const from = { 0.5f, 1.5f }, target = { 4.5f, 1.5f };

    pathmap[2] = pathmap[12] = 2;
    setup_test_pathmap(5, 3, pathmap);
    T_ASSERT(CM_LineIsWalkable(&from, &target));
    pathmap[7] = 2;
    setup_test_pathmap(5, 3, pathmap);
    T_ASSERT(!CM_LineIsWalkable(&from, &target));
}

/* -----------------------------------------------------------------------
 * ai_walk / movement frame tests
 *
 * ai_walk is static inside s_move.c; it is accessed via the think
 * function-pointer stored in move_move_walk.  After calling order_move
 * we invoke ent->currentmove->think() to simulate one game frame.
 * ===================================================================== */

TEST(wc3_movement, unit_moves_closer_to_goal_after_one_frame) {
    LPEDICT unit = make_moving_unit(0.0f, 0.0f);
    /* Place waypoint within NAVI_THRESHOLD so direct vector math is used
     * and we don't need the heatmap mock to return a meaningful direction. */
    VECTOR2 dest = {40.0f, 0.0f};
    unit_issueorder(unit, "move", &dest);
    T_NOT_NULL(unit->currentmove);
    T_NOT_NULL(unit->currentmove->think);

    FLOAT dist_before = M_DistanceToGoal(unit);
    unit->currentmove->think(unit);
    FLOAT dist_after = M_DistanceToGoal(unit);

    T_ASSERT(dist_after < dist_before);
}

TEST(wc3_movement, unit_reaches_goal_and_transitions_to_stand) {
    LPEDICT unit = make_moving_unit(0.0f, 0.0f);
    /* Distance = 40, move_distance ≈ 27.  After two frames the unit
     * should have arrived (40 - 27 = 13 < 27) and called stand(). */
    VECTOR2 dest = {40.0f, 0.0f};
    unit_issueorder(unit, "move", &dest);

    /* Run up to 10 frames — should arrive well within that. */
    for (int i = 0; i < 10; i++) {
        if (!unit->currentmove || !unit->currentmove->think) break;
        if (strcmp(unit->currentmove->animation, "walk") != 0) break;
        unit->currentmove->think(unit);
    }

    T_STREQ(unit->currentmove->animation, "stand");
}

TEST(wc3_movement, unit_position_changes_after_move_frame) {
    LPEDICT unit = make_moving_unit(0.0f, 0.0f);
    VECTOR2 dest = {40.0f, 0.0f};
    unit_issueorder(unit, "move", &dest);

    FLOAT x0 = unit->s.origin2.x;
    unit->currentmove->think(unit);

    /* Unit must have moved in the X direction. */
    T_ASSERT(unit->s.origin2.x > x0);
}

/* Route generation must expand obstacles by the mover radius, matching the
 * move-time collision test.  The point route hugs this wall too closely; a
 * Peasant-sized route has room to detour above it and reach the destination. */
TEST(wc3_movement, move_order_detours_with_unit_collision_radius) {
    enum { CELLS = 16 };
    BYTE pathmap[CELLS * CELLS] = {0};
    LPEDICT unit = make_moving_unit(80.0f, 240.0f);
    VECTOR2 dest = {432.0f, 240.0f};

    for (int y = 3; y <= 12; y++)
        pathmap[y * CELLS + 7] = 2;
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(BOX2, .min = {0.0f, 0.0f}, .max = {512.0f, 512.0f}));
    unit->collision = 16.0f;
    unit->unitinfo.MoveSpeed = 80.0f;
    gi.LinkEntity(unit);
    order_move(unit, Waypoint_add(&dest));

    for (int frame = 0; frame < 200 && unit->currentmove->think; frame++) {
        unit->currentmove->think(unit);
        CM_ProcessPathJobs(4096);
        T_ASSERT(CM_PointIsPathableForRadius(&unit->s.origin2, unit->collision));
    }

    T_STREQ(unit->currentmove->animation, "stand");
    T_FEQ(Vector2_distance(&unit->s.origin2, &dest), 0.0f, 0.01f);
}

/* A click in another static connected component cannot be reached.  Retail
 * movement still advances as far as collision permits, then settles at the
 * closest boundary instead of freezing at the order origin or walking forever. */
TEST(wc3_movement, unreachable_move_settles_at_closest_boundary) {
    enum { CELLS = 16 };
    BYTE pathmap[CELLS * CELLS] = {0};
    LPEDICT unit = make_moving_unit(80.0f, 240.0f);
    VECTOR2 const start = unit->s.origin2;
    VECTOR2 dest = {432.0f, 240.0f};

    for (int y = 0; y < CELLS; y++)
        pathmap[y * CELLS + 7] = 2;
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(BOX2, .min = {0.0f, 0.0f}, .max = {512.0f, 512.0f}));
    unit->collision = 16.0f;
    unit->unitinfo.MoveSpeed = 80.0f;
    gi.LinkEntity(unit);
    order_move(unit, Waypoint_add(&dest));

    for (int frame = 0; frame < 200 && unit->currentmove->think; frame++) {
        unit->currentmove->think(unit);
        CM_ProcessPathJobs(4096);
        T_ASSERT(CM_PointIsPathableForRadius(&unit->s.origin2, unit->collision));
    }

    T_STREQ(unit->currentmove->animation, "stand");
    T_ASSERT(unit->s.origin2.x > start.x);
    T_ASSERT(unit->s.origin2.x < 7.0f * 32.0f);
    T_ASSERT(Vector2_distance(&unit->s.origin2, &dest) < Vector2_distance(&start, &dest));
}

/* Immobile is a single movement/facing contract, not just a command-menu filter. */
TEST(wc3_movement, immobile_unit_neither_moves_nor_rotates) {
    LPEDICT unit = make_moving_unit(0.0f, 0.0f);
    LPEDICT wp = alloc_test_unit(0, 100.0f, 100.0f);
    VECTOR2 const origin = unit->s.origin2;
    FLOAT const angle = unit->s.angle;
    unit->aiflags |= AI_IMMOBILE;
    unit->goalentity = wp;

    unit_changeangle(unit);
    unit_moveindirection(unit);

    T_FEQ(unit->s.origin2.x, origin.x, 0.01f);
    T_FEQ(unit->s.origin2.y, origin.y, 0.01f);
    T_FEQ(unit->s.angle, angle, 0.01f);
}

TEST(wc3_movement, immobile_unit_rejects_ground_move_order) {
    LPEDICT unit = make_moving_unit(0.0f, 0.0f);
    VECTOR2 dest = {100.0f, 0.0f};
    unit->aiflags |= AI_IMMOBILE;

    T_ASSERT(!unit_issueorder(unit, "move", &dest));
    T_NULL(unit->goalentity);
    T_STREQ(unit->currentmove->animation, "stand");
}

TEST(wc3_movement, unit_does_not_overshoot_goal) {
    LPEDICT unit = make_moving_unit(0.0f, 0.0f);
    VECTOR2 dest = {40.0f, 0.0f};
    unit_issueorder(unit, "move", &dest);

    /* Run frames until the unit stands. */
    for (int i = 0; i < 20; i++) {
        if (!unit->currentmove || !unit->currentmove->think) break;
        if (strcmp(unit->currentmove->animation, "walk") != 0) break;
        unit->currentmove->think(unit);
    }

    /* After reaching the goal the unit should be exactly at the waypoint,
     * which keeps scripted cutscene units from visibly stopping short. */
    FLOAT dist = M_DistanceToGoal(unit);
    T_FEQ(dist, 0.0f, 0.01f);
}

TEST(wc3_movement, group_move_assigns_distinct_reserved_destinations) {
    reset_entities();
    LPEDICT clent = alloc_test_unit(0, 0.0f, 0.0f);
    clent->client = &game.clients[0];

    LPEDICT a = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0.0f, 0.0f);
    LPEDICT b = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 20.0f, 0.0f);
    LPEDICT c = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0.0f, 20.0f);
    LPEDICT units[] = { a, b, c };

    FOR_LOOP(i, 3) {
        units[i]->collision = 16.0f;
        units[i]->selected = 1 << clent->client->ps.number;
        units[i]->stand = unit_stand;
        unit_stand(units[i]);
    }

    VECTOR2 dest = {100.0f, 100.0f};
    T_ASSERT(move_selectlocation(clent, &dest));

    T_NOT_NULL(a->goalentity);
    T_NOT_NULL(b->goalentity);
    T_NOT_NULL(c->goalentity);
    T_NOT_NULL(a->goalentity->secondarygoal);
    T_ASSERT(a->goalentity->secondarygoal == b->goalentity->secondarygoal);
    T_ASSERT(a->goalentity->secondarygoal == c->goalentity->secondarygoal);
    T_ASSERT(Vector2_distance(&a->goalentity->s.origin2, &b->goalentity->s.origin2) >= 32.0f);
    T_ASSERT(Vector2_distance(&a->goalentity->s.origin2, &c->goalentity->s.origin2) >= 32.0f);
    T_ASSERT(Vector2_distance(&b->goalentity->s.origin2, &c->goalentity->s.origin2) >= 32.0f);
}

TEST(wc3_movement, group_move_ignores_selected_buildings) {
    reset_entities();
    LPEDICT clent = alloc_test_unit(0, 0.0f, 0.0f);
    clent->client = &game.clients[0];

    LPEDICT building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 0.0f, 0.0f);
    LPEDICT peasant = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 20.0f, 0.0f);

    building->collision = 64.0f;
    building->aiflags |= AI_IMMOBILE;
    building->selected = 1 << clent->client->ps.number;
    building->stand = unit_stand;
    unit_stand(building);

    peasant->collision = 16.0f;
    peasant->selected = 1 << clent->client->ps.number;
    peasant->stand = unit_stand;
    unit_stand(peasant);

    VECTOR2 dest = {100.0f, 100.0f};
    T_ASSERT(move_selectlocation(clent, &dest));

    T_NULL(building->goalentity);
    T_NOT_NULL(peasant->goalentity);
}

/* A mixed-speed group travels at its slowest member's speed so it stays
 * together instead of stringing out (WC3 group movement). */
TEST(wc3_movement, group_move_travels_at_slowest_member_speed) {
    reset_entities();
    LPEDICT clent = alloc_test_unit(0, 0.0f, 0.0f);
    clent->client = &game.clients[0];

    LPEDICT fast = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0.0f, 0.0f);
    LPEDICT slow = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 20.0f, 0.0f);
    fast->unitinfo.MoveSpeed = 300.0f;
    slow->unitinfo.MoveSpeed = 100.0f;
    LPEDICT units[] = { fast, slow };
    FOR_LOOP(i, 2) {
        units[i]->collision = 16.0f;
        units[i]->selected = 1 << clent->client->ps.number;
        units[i]->stand = unit_stand;
        unit_stand(units[i]);
    }

    VECTOR2 dest = {400.0f, 0.0f};
    T_ASSERT(move_selectlocation(clent, &dest));

    /* Both units adopt the slowest member's speed for the group move... */
    T_FEQ(fast->movement.group_speed, 100.0f, 0.01f);
    T_FEQ(slow->movement.group_speed, 100.0f, 0.01f);
    /* ...so the fast unit's per-frame travel is capped to the slow speed. */
    T_FEQ(unit_movedistance(fast), 10.0f * 100.0f / (FLOAT)FRAMETIME, 0.01f);
}

/* A lone unit keeps its own speed (no group cap). */
TEST(wc3_movement, single_unit_move_keeps_own_speed) {
    LPEDICT unit = make_moving_unit(0.0f, 0.0f);
    unit->unitinfo.MoveSpeed = 300.0f;
    VECTOR2 dest = {200.0f, 0.0f};
    unit_issueorder(unit, "move", &dest);

    T_FEQ(unit->movement.group_speed, 0.0f, 0.01f);
    T_FEQ(unit_movedistance(unit), 10.0f * 300.0f / (FLOAT)FRAMETIME, 0.01f);
}

TEST(wc3_movement, plain_move_uses_collision_sized_static_route) {
    enum { CELLS = 64 };
    BYTE pathmap[CELLS * CELLS] = {0};
    LPEDICT unit = make_moving_unit(-320.0f, 0.0f);
    VECTOR2 dest = {320.0f, 0.0f};

    unit->collision = 16.0f; /* one 32u path-cell radius in this fixture */
    unit->unitinfo.MoveSpeed = 190.0f;

    /* A one-cell opening is traversable by a point route but not by this
     * mover's 3x3 collision footprint.  Plain move must use the latter. */
    for (int y = 0; y < CELLS; y++)
        pathmap[32 + y * CELLS] = 0x02;
    pathmap[32 + 32 * CELLS] = 0;
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(BOX2,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    T_ASSERT(unit_issueorder(unit, "move", &dest));
    unit->currentmove->think(unit); /* queues the resumable radius field */
    CM_ProcessPathJobs(65536);
    unit->currentmove->think(unit); /* completed field retargets the private waypoint */

    T_STREQ(unit->currentmove->animation, "walk");
    T_ASSERT(!unit->movement.flow_unreachable);
    T_ASSERT(unit->goalentity->s.origin2.x > unit->s.origin2.x);
    T_ASSERT(unit->goalentity->s.origin2.x < 0.0f);
}

TEST(wc3_movement, blocked_move_keeps_order_alive_away_from_goal) {
    LPEDICT unit = make_moving_unit(0.0f, 0.0f);
    VECTOR2 origin = unit->s.origin2;
    VECTOR2 dest = {400.0f, 0.0f};
    unit_issueorder(unit, "move", &dest);

    /* Budget exceeds MOVE_BLOCKED_FRAMES.  A distant plain move must remain
     * active: another unit may be the temporary blocker and retail keeps the
     * right-click order alive until the route can make progress. */
    for (int i = 0; i < 30; i++) {
        if (!unit->currentmove || strcmp(unit->currentmove->animation, "walk") != 0) {
            break;
        }
        unit->currentmove->think(unit);
        unit->s.origin2 = origin;
        unit->s.origin.x = origin.x;
        unit->s.origin.y = origin.y;
        unit->bounds.min.x = unit->s.origin2.x - unit->collision;
    unit->bounds.min.y = unit->s.origin2.y - unit->collision;
    unit->bounds.max.x = unit->s.origin2.x + unit->collision;
    unit->bounds.max.y = unit->s.origin2.y + unit->collision;
    }

    T_STREQ(unit->currentmove->animation, "walk");
}

TEST(wc3_movement, near_goal_jitter_settles_to_stand) {
    LPEDICT unit = make_moving_unit(0.0f, 0.0f);
    VECTOR2 dest = {100.0f, 0.0f};
    /* Keep the fixture inside the settle band but beyond arrival tolerance for
     * both ROC and TFT, whose archive-backed Peasant move speeds differ. */
    unit->s.origin2.x = dest.x - unit_movedistance(unit) - 6.0f;
    unit->s.origin.x = unit->s.origin2.x;
    gi.LinkEntity(unit);
    VECTOR2 jitter = unit->s.origin2;
    unit_issueorder(unit, "move", &dest);

    for (int i = 0; i < 10; i++) {
        if (!unit->currentmove || strcmp(unit->currentmove->animation, "walk") != 0) {
            break;
        }
        unit->currentmove->think(unit);
        unit->s.origin2 = jitter;
        unit->s.origin.x = jitter.x;
        unit->s.origin.y = jitter.y;
        unit->bounds.min.x = unit->s.origin2.x - unit->collision;
    unit->bounds.min.y = unit->s.origin2.y - unit->collision;
    unit->bounds.max.x = unit->s.origin2.x + unit->collision;
    unit->bounds.max.y = unit->s.origin2.y + unit->collision;
    }

    T_STREQ(unit->currentmove->animation, "stand");
}

TEST(wc3_movement, unit_stops_when_goal_is_occupied) {
    LPEDICT unit = make_moving_unit(0.0f, 0.0f);
    LPEDICT blocker = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100.0f, 0.0f);
    VECTOR2 dest = {100.0f, 0.0f};

    unit->collision = 16.0f;
    blocker->collision = 16.0f;
    blocker->s.model = 1;            /* non-hollow so it is a collision obstacle */
    blocker->stand = unit_stand;
    blocker->movetype = MOVETYPE_NONE;
    unit_stand(blocker);
    /* Collision is assigned after allocation, so link both fixtures with their final radii. */
    gi.LinkEntity(unit);
    gi.LinkEntity(blocker);

    unit_issueorder(unit, "move", &dest);

    /* Move-time collision blocks the unit short of the occupied goal (it never
     * steps into the blocker), then the blocked-frame accumulator settles it to
     * stand.  No post-move solver is involved any more.  Track the closest the
     * unit ever comes to the goal: it should reach right up against the blocker
     * (just outside the combined collision radius) but never inside it. */
    FLOAT min_goal_dist = M_DistanceToGoal(unit);
    for (int i = 0; i < 40; i++) {
        if (!unit->currentmove || strcmp(unit->currentmove->animation, "walk") != 0) {
            break;
        }
        unit->currentmove->think(unit);
        FLOAT d = M_DistanceToGoal(unit);
        if (d < min_goal_dist) min_goal_dist = d;
    }

    FLOAT combined = unit->collision + blocker->collision;
    T_STREQ(unit->currentmove->animation, "stand");/* settled, didn't walk forever */
    T_ASSERT(min_goal_dist >= combined - 1.0f);                    /* never penetrated the blocker */
    T_ASSERT(min_goal_dist <= combined + unit_movedistance(unit)); /* but reached right up to it */
}

/* Without a town hall the worker exits the mine carrying gold but has nowhere
 * to go: it stops in stand state.  The RETURN_GOLD, DEPOSIT_GOLD, and
 * RESUME_GOLD messages must NOT be published. */
TEST(wc3_movement, gold_worker_stops_when_no_townhall) {
    LPEDICT worker = make_moving_unit(0.0f, 0.0f);
    LPEDICT mine   = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 400.0f, 0.0f);
    worker->collision = 16.0f; worker->unitinfo.MoveSpeed = 100.0f;
    mine->collision = 128.0f; mine->s.model = 1; mine->movetype = MOVETYPE_NONE;
    setup_test_goldmine(mine, &test_goldmine_cap1, 100);
    gi.LinkEntity(mine);
    slkTestData_t *rows, *old_abilities = install_goldmine_test_data(&rows);
    HARVEST_GOLD_CAPACITY = 10.0f;

    MSGTRACE trace = {0};
    T_ASSERT(G_SubscribeMessage(trace_message, &trace));
    harvest_gold_start(worker, mine);

    /* Drive until harvested_gold is set (harvestgold_walkback fired). */
    FOR_LOOP(i, 100) {
        worker->currentmove->think(worker);
        if (worker->harvested_gold > 0) break;
    }
    G_UnsubscribeMessage(trace_message, &trace);

    T_ASSERT(worker->harvested_gold > 0);           /* gold carried, not deposited */
    T_ASSERT(worker->s.renderfx & RF_HAS_GOLD);     /* visual bag still on worker */
    T_ASSERT(!(worker->s.renderfx & RF_HIDDEN));    /* not inside mine */
    T_STREQ(worker->currentmove->animation, "stand");
    /* Only MOVE_GOLD and ENTER_MINE — no return/deposit/resume. */
    T_EQ((int)trace.count, 2);
    T_EQ(trace.msg[0].type, GAME_MSG_HARVEST_MOVE_GOLD);
    T_EQ(trace.msg[1].type, GAME_MSG_HARVEST_ENTER_MINE);
    T_EQ(mine->resources, 90);
    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

/* A second worker ordered to mine when the mine is already at capacity waits
 * outside.  When the first worker exits, it wakes the second, which enters
 * immediately without a new walk order from the player. */
TEST(wc3_movement, gold_mine_queues_second_worker_when_at_capacity) {
    LPEDICT worker1 = make_moving_unit(0.0f, 0.0f);
    LPEDICT mine    = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 400.0f, 0.0f);
    worker1->collision = 16.0f; worker1->unitinfo.MoveSpeed = 100.0f;
    mine->collision = 128.0f; mine->s.model = 1; mine->movetype = MOVETYPE_NONE;
    setup_test_goldmine(mine, &test_goldmine_cap1, 100);
    gi.LinkEntity(mine);
    slkTestData_t *rows, *old_abilities = install_goldmine_test_data(&rows);
    HARVEST_GOLD_CAPACITY = 10.0f;

    /* Worker1 walks to and enters the mine. */
    harvest_gold_start(worker1, mine);
    FOR_LOOP(i, 60) {
        worker1->currentmove->think(worker1);
        if (worker1->s.renderfx & RF_HIDDEN) break;
    }
    T_ASSERT(worker1->s.renderfx & RF_HIDDEN);
    T_EQ((int)mine->peonsinside, 1);

    /* Worker2: wire and place at the mine entrance so it reaches immediately. */
    LPEDICT worker2 = alloc_test_unit(MAKEFOURCC('h','p','e','a'),
                                      mine->s.origin2.x - mine->collision - 16.0f,
                                      mine->s.origin2.y);
    worker2->movetype = MOVETYPE_STEP;
    worker2->stand    = unit_stand;
    worker2->die      = unit_die;
    worker2->collision = 16.0f;
    worker2->health.value = worker2->health.max_value = 250.0f;
    worker2->unitinfo.MoveSpeed = 100.0f;
    unit_stand(worker2);
    gi.LinkEntity(worker2);

    harvest_gold_start(worker2, mine);
    worker2->currentmove->think(worker2); /* immediately at mine — enters wait state */

    T_ASSERT(!(worker2->s.renderfx & RF_HIDDEN));   /* waiting outside */
    T_EQ((int)mine->peonsinside, 1);                /* still only worker1 */
    T_STREQ(worker2->currentmove->animation, "stand");

    /* Worker1 exits; harvestgold_walkback wakes worker2 in the same call. */
    worker1->currentmove->think(worker1);
    T_ASSERT(worker2->s.renderfx & RF_HIDDEN);   /* worker2 now inside */
    T_EQ((int)mine->peonsinside, 1);             /* worker1 left (−1) worker2 entered (+1) */
    T_ASSERT(!(worker1->s.renderfx & RF_HIDDEN));/* worker1 exited */
    T_ASSERT(worker1->s.renderfx & RF_HAS_GOLD); /* worker1 carrying gold */
    T_EQ(mine->resources, 90);
    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

/* Stock Agld has one internal mining slot. Six assigned workers may all keep
 * Harvest orders, but only one may ever be registered/hidden inside. */
TEST(wc3_movement, gold_mine_stock_capacity_never_exceeds_one_with_six_workers) {
    reset_entities();
    setup_test_world();
    slkTestData_t *rows, *old_abilities = install_goldmine_test_data(&rows);
    LPEDICT mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 0.0f, 0.0f);
    setup_test_goldmine(mine, &test_goldmine_stock, 12500);
    HARVEST_GOLD_CAPACITY = 10.0f;

    T_EQ(S_GoldMineCapacity(mine), 1);
    FOR_LOOP(i, 6) {
        LPEDICT worker = add_gold_worker(150.0f + (FLOAT)i, 0.0f);
        worker->goalentity = worker->secondarygoal = mine;
        harvestgold_minegold(worker);
        T_ASSERT(mine->peonsinside <= 1);
        if (i == 0) {
            T_ASSERT(S_GoldMineWorkerIsInside(worker));
            T_ASSERT(worker->s.renderfx & RF_HIDDEN);
        } else {
            T_ASSERT(!S_GoldMineWorkerIsInside(worker));
            T_ASSERT(!(worker->s.renderfx & RF_HIDDEN));
            T_STREQ(worker->currentmove->animation, "stand");
        }
    }
    T_EQ(mine->peonsinside, 1);

    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

/* Capacity, duration, and initial gold come from the specific Agld-derived
 * ability on each mine rather than process-wide globals. */
TEST(wc3_movement, gold_mines_keep_independent_custom_capacity_duration_and_gold) {
    reset_entities();
    setup_test_world();
    slkTestData_t *rows, *old_abilities = install_goldmine_test_data(&rows);
    LPEDICT mine1 = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 0.0f, 0.0f);
    LPEDICT mine2 = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 500.0f, 0.0f);
    setup_test_goldmine(mine1, &test_goldmine_cap1, 0);
    setup_test_goldmine(mine2, &test_goldmine_cap2, 0);
    S_GoldMineInitUnit(mine1);
    S_GoldMineInitUnit(mine2);

    T_EQ(mine1->resources, 100);
    T_EQ(mine2->resources, 200);
    T_EQ(S_GoldMineCapacity(mine1), 1);
    T_EQ(S_GoldMineCapacity(mine2), 2);
    T_FEQ(S_GoldMineMiningDuration(mine1), 0.01f, 0.001f);
    T_FEQ(S_GoldMineMiningDuration(mine2), 2.0f, 0.001f);

    LPEDICT a = add_gold_worker(0.0f, 0.0f);
    LPEDICT b = add_gold_worker(0.0f, 0.0f);
    LPEDICT c = add_gold_worker(500.0f, 0.0f);
    LPEDICT d = add_gold_worker(500.0f, 0.0f);
    LPEDICT e = add_gold_worker(500.0f, 0.0f);
    a->goalentity = b->goalentity = mine1;
    c->goalentity = d->goalentity = e->goalentity = mine2;
    harvestgold_minegold(a);
    harvestgold_minegold(b);
    harvestgold_minegold(c);
    harvestgold_minegold(d);
    harvestgold_minegold(e);

    T_EQ(mine1->peonsinside, 1);
    T_EQ(mine2->peonsinside, 2);
    T_FEQ(c->wait, 2.0f, 0.001f);
    T_ASSERT(!S_GoldMineWorkerIsInside(b));
    T_ASSERT(!S_GoldMineWorkerIsInside(e));

    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

/* Inside membership is authoritative: duplicate entry cannot increment the
 * mine twice, ordinary orders are rejected, and exit restores protection and
 * unregisters exactly once. */
TEST(wc3_movement, gold_miner_inside_is_non_orderable_and_unregisters_once) {
    reset_entities();
    setup_test_world();
    slkTestData_t *rows, *old_abilities = install_goldmine_test_data(&rows);
    LPEDICT mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 0.0f, 0.0f);
    LPEDICT worker = add_gold_worker(0.0f, 0.0f);
    VECTOR2 point = { 100.0f, 100.0f };
    setup_test_goldmine(mine, &test_goldmine_cap1, 100);
    worker->goalentity = worker->secondarygoal = mine;
    HARVEST_GOLD_CAPACITY = 10.0f;

    harvestgold_minegold(worker);
    T_EQ(mine->peonsinside, 1);
    T_ASSERT(worker->invulnerable);
    T_ASSERT(S_GoldMineWorkerIsInside(worker));
    harvestgold_minegold(worker);
    T_EQ(mine->peonsinside, 1);
    T_ASSERT(!unit_issueimmediateorder(worker, "stop"));
    T_ASSERT(!unit_issueorder(worker, "move", &point));
    T_ASSERT(!unit_issuetargetorder(worker, "attack", mine));
    T_EQ(mine->peonsinside, 1);

    harvestgold_walkback(worker);
    T_EQ(mine->peonsinside, 0);
    T_ASSERT(!S_GoldMineWorkerIsInside(worker));
    T_ASSERT(!worker->invulnerable);
    T_ASSERT(!(worker->s.renderfx & RF_HIDDEN));
    T_EQ(worker->harvested_gold, 10);
    harvestgold_walkback(worker); /* cannot unregister/decrement twice */
    T_EQ(mine->peonsinside, 0);

    LPEDICT removed = add_gold_worker(0.0f, 0.0f);
    removed->goalentity = removed->secondarygoal = mine;
    harvestgold_minegold(removed);
    T_EQ(mine->peonsinside, 1);
    G_FreeEdict(removed);
    T_EQ(mine->peonsinside, 0);

    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

/* The final trip is clamped to remaining mine gold. Draining the mine to zero
 * depletes it and prevents an already-waiting worker from entering. */
TEST(wc3_movement, gold_mine_partial_final_trip_depletes_and_rejects_waiter) {
    reset_entities();
    setup_test_world();
    slkTestData_t *rows, *old_abilities = install_goldmine_test_data(&rows);
    LPEDICT mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 0.0f, 0.0f);
    LPEDICT miner = add_gold_worker(0.0f, 0.0f);
    LPEDICT waiter = add_gold_worker(0.0f, 0.0f);
    setup_test_goldmine(mine, &test_goldmine_cap1, 6);
    HARVEST_GOLD_CAPACITY = 10.0f;
    miner->goalentity = miner->secondarygoal = mine;
    waiter->goalentity = waiter->secondarygoal = mine;

    harvestgold_minegold(miner);
    harvestgold_minegold(waiter);
    T_EQ(mine->peonsinside, 1);
    T_STREQ(waiter->currentmove->animation, "stand");

    harvestgold_walkback(miner);
    T_EQ(miner->harvested_gold, 6);
    T_EQ(mine->resources, 0);
    T_EQ(mine->peonsinside, 0);
    T_ASSERT(M_IsDead(mine));
    T_ASSERT(!S_GoldMineWorkerIsInside(waiter));
    T_ASSERT(!(waiter->s.renderfx & RF_HIDDEN));
    T_STREQ(waiter->currentmove->animation, "stand");

    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

/* -----------------------------------------------------------------------
 * Suite runner
 * --------------------------------------------------------------------- */

#endif /* BZ_TESTS */
