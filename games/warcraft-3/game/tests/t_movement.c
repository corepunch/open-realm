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

static UnitAbilities_t const return_gold_lumber_abilities = { .abilList = "Argl" };
static UnitAbilities_t const return_lumber_abilities = { .abilList = "Arlm" };

static void make_live_dropoff(LPEDICT building, UnitAbilities_t const *abilities) {
    building->UnitAbilities = abilities;
    building->health.value = building->health.max_value = 1000.0f;
}

slkTestData_t *parse_slk_string(const char *slk_text);
void free_slk_rows(slkTestData_t *rows);


extern FLOAT HARVEST_GOLD_CAPACITY;
extern FLOAT HARVEST_TREE_DAMAGE;
extern FLOAT HARVEST_LUMBER_CAPACITY;
extern FLOAT HARVEST_RANGE;
extern FLOAT HARVEST_COOLDOWN;
extern FLOAT HARVEST_SEARCH_RANGE;
extern void harvest_cooldown(LPEDICT);

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
    mine->UnitAbilities = abilities;
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
    harvest_start(worker, tree);

    worker->currentmove->think(worker);
    worker->wait = 0.01f;
    worker->currentmove->think(worker);

    T_ASSERT(!tree_died);
    T_EQ(tree_pained, 1);
    T_FEQ(tree->health.value, 1.0f, 0.01f);
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
    worker->currentmove->think(worker);

    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_LUMBER], old_lumber + 10);
    T_EQ(worker->harvested_lumber, 0);
    T_ASSERT(!(worker->s.renderfx & RF_HAS_LUMBER));
    T_ASSERT(worker->goalentity == tree);
}


/* Returning lumber to a building with authored blocking pathing uses the same
 * generic point-route contract as mine entry.  Routing may approach the blocked
 * center, but the resource behavior owns the contact+step completion boundary. */
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
    trained->UnitBalance = &balance;
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
    worker->currentmove->think(worker);

    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD], old_gold + 10);
    T_EQ(worker->harvested_gold, 0);
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

TEST(wc3_movement, blocked_move_stops_instead_of_walking_forever) {
    LPEDICT unit = make_moving_unit(0.0f, 0.0f);
    VECTOR2 origin = unit->s.origin2;
    VECTOR2 dest = {400.0f, 0.0f};
    unit_issueorder(unit, "move", &dest);

    /* Budget exceeds MOVE_BLOCKED_FRAMES so the pinned (stuck) unit gives up. */
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

    T_STREQ(unit->currentmove->animation, "stand");
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
