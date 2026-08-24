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

extern FLOAT MINING_CAPACITY;
extern FLOAT MINING_DURATION;
extern FLOAT HARVEST_GOLD_CAPACITY;
extern FLOAT HARVEST_TREE_DAMAGE;
extern FLOAT HARVEST_RANGE;

static BOOL tree_died;
static DWORD tree_pained;
static void test_tree_die(LPEDICT tree, LPEDICT attacker) { (void)tree; (void)attacker; tree_died = true; }
static void test_tree_pain(LPEDICT tree) { (void)tree; tree_pained++; }

/* Gold workers enter at the mine boundary; the mine's collision footprint must
 * not strand them just outside the older fixed interaction radius. */
TEST(wc3_movement, gold_worker_enters_large_mine_footprint) {
    LPEDICT worker = make_moving_unit(0.0f, 0.0f);
    LPEDICT mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 400.0f, 0.0f);
    worker->collision = 16.0f;
    worker->unitinfo.MoveSpeed = 100.0f;
    mine->collision = 192.0f;
    mine->s.model = 1;
    mine->movetype = MOVETYPE_NONE;
    gi.LinkEntity(worker);
    gi.LinkEntity(mine);
    MINING_CAPACITY = 5.0f;
    MINING_DURATION = 1.0f;
    harvest_gold_start(worker, mine);

    FOR_LOOP(i, 40) {
        worker->currentmove->think(worker);
        if (worker->s.renderfx & RF_HIDDEN) break;
    }

    T_ASSERT(worker->s.renderfx & RF_HIDDEN);
    T_EQ(mine->peonsinside, 1);
}

/* A final chop equal to the remaining life must run the tree's death callback,
 * which owns its fall animation and pathing removal. */
TEST(wc3_movement, lumber_final_chop_fells_tree) {
    LPEDICT worker = make_moving_unit(0.0f, 0.0f);
    LPEDICT tree = alloc_test_unit(MAKEFOURCC('L','T','l','t'), 20.0f, 0.0f);
    worker->attack1.damagePoint = 0.01f;
    tree->targtype = TARG_TREE;
    tree->health.value = 10.0f;
    tree->health.max_value = 10.0f;
    tree->die = test_tree_die;
    tree_died = false;
    HARVEST_RANGE = 64.0f;
    HARVEST_TREE_DAMAGE = 10.0f;
    harvest_start(worker, tree);

    worker->currentmove->think(worker);
    worker->wait = 0.01f;
    worker->currentmove->think(worker);

    T_ASSERT(tree_died);
    T_FEQ(tree->health.value, 0.0f, 0.01f);
    T_FEQ(worker->harvested_lumber, 10.0f, 0.01f);
}

/* Non-lethal chops damage but do not fell a living tree. */
TEST(wc3_movement, lumber_nonlethal_chop_keeps_tree_standing) {
    LPEDICT worker = make_moving_unit(0.0f, 0.0f);
    LPEDICT tree = alloc_test_unit(MAKEFOURCC('L','T','l','t'), 20.0f, 0.0f);
    tree->targtype = TARG_TREE;
    tree->health.value = 11.0f;
    tree->health.max_value = 11.0f;
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

/* The complete gold loop enters, exits carrying gold, deposits it, and resumes mining. */
TEST(wc3_movement, gold_worker_deposits_and_resumes_mining) {
    LPEDICT worker = make_moving_unit(0.0f, 0.0f);
    LPEDICT mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 400.0f, 0.0f);
    LPEDICT hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 0.0f, 0.0f);
    worker->collision = 16.0f; worker->unitinfo.MoveSpeed = 100.0f;
    mine->collision = 192.0f; mine->s.model = 1;
    hall->collision = 64.0f; hall->s.model = 1;
    gi.LinkEntity(worker); gi.LinkEntity(mine); gi.LinkEntity(hall);
    MINING_CAPACITY = 5.0f; MINING_DURATION = 0.01f; HARVEST_GOLD_CAPACITY = 10.0f;
    DWORD const old_gold = game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD];
    harvest_gold_start(worker, mine);

    FOR_LOOP(i, 100) {
        worker->currentmove->think(worker);
        if (game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD] > old_gold) break;
    }

    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD], old_gold + 10);
    T_EQ(worker->harvested_gold, 0);
    T_ASSERT(!(worker->s.renderfx & RF_HIDDEN));
    T_ASSERT(worker->secondarygoal == mine);
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
    FLOAT expected = 10.0f * UNIT_SPEED(MAKEFOURCC('h','p','e','a')) / (FLOAT)FRAMETIME;
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
    T_FEQ(fast->move_group_speed, 100.0f, 0.01f);
    T_FEQ(slow->move_group_speed, 100.0f, 0.01f);
    /* ...so the fast unit's per-frame travel is capped to the slow speed. */
    T_FEQ(unit_movedistance(fast), 10.0f * 100.0f / (FLOAT)FRAMETIME, 0.01f);
}

/* A lone unit keeps its own speed (no group cap). */
TEST(wc3_movement, single_unit_move_keeps_own_speed) {
    LPEDICT unit = make_moving_unit(0.0f, 0.0f);
    unit->unitinfo.MoveSpeed = 300.0f;
    VECTOR2 dest = {200.0f, 0.0f};
    unit_issueorder(unit, "move", &dest);

    T_FEQ(unit->move_group_speed, 0.0f, 0.01f);
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

/* -----------------------------------------------------------------------
 * Suite runner
 * --------------------------------------------------------------------- */

#endif /* BZ_TESTS */
