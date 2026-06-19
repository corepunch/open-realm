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
#include "test_framework.h"
#include "test_harness.h"

/* NAVI_THRESHOLD is the distance below which ai_walk uses direct
 * vector math rather than the heatmap flow field.  It is defined as a
 * #define in g_ai.c; we replicate the value here so test waypoints can
 * be placed within the threshold and avoid the BuildHeatmap mock path. */
#define NAVI_THRESHOLD 50.0f

/* -----------------------------------------------------------------------
 * Helpers
 * --------------------------------------------------------------------- */

/* Create a unit at (x, y) with the lifecycle callbacks and zero collision
 * (movement tests don't want unintended push-apart).  Resets entity pool
 * so each test starts from a clean slate. */
static LPEDICT make_moving_unit(FLOAT x, FLOAT y) {
    reset_entities();
    LPEDICT ent = alloc_test_unit(UNIT_ID("hpea"), x, y);
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

/* -----------------------------------------------------------------------
 * order_move tests
 * --------------------------------------------------------------------- */

static void test_order_move_sets_goalentity(void) {
    LPEDICT unit = make_moving_unit(0.0f, 0.0f);
    LPEDICT wp   = alloc_test_unit(0, 30.0f, 0.0f); /* reuse edict as waypoint */
    order_move(unit, wp);
    ASSERT(unit->goalentity == wp);
}

static void test_order_move_sets_walk_animation(void) {
    LPEDICT unit = make_moving_unit(0.0f, 0.0f);
    LPEDICT wp   = alloc_test_unit(0, 30.0f, 0.0f);
    order_move(unit, wp);
    ASSERT_NOT_NULL(unit->currentmove);
    ASSERT_ANIM(unit, "walk");
}

/* -----------------------------------------------------------------------
 * Waypoint_add tests
 * --------------------------------------------------------------------- */

static void test_waypoint_add_sets_origin(void) {
    VECTOR2 dest = {128.0f, 256.0f};
    LPEDICT wp = Waypoint_add(&dest);
    ASSERT_NOT_NULL(wp);
    ASSERT_EQ_FLOAT(wp->s.origin.x, 128.0f, 0.01f);
    ASSERT_EQ_FLOAT(wp->s.origin.y, 256.0f, 0.01f);
}

/* -----------------------------------------------------------------------
 * unit_movedistance tests
 * --------------------------------------------------------------------- */

static void test_unit_movedistance_matches_formula(void) {
    /* unit_movedistance = 10 * speed / FRAMETIME */
    LPEDICT unit = make_moving_unit(0.0f, 0.0f);
    FLOAT expected = 10.0f * UNIT_SPEED(UNIT_ID("hpea")) / (FLOAT)FRAMETIME;
    ASSERT_FLOAT_EQ(unit_movedistance(unit), expected);
}

static void test_unit_movedistance_uses_scripted_move_speed(void) {
    LPEDICT unit = make_moving_unit(0.0f, 0.0f);
    unit->unitinfo.MoveSpeed = 300.0f;

    FLOAT expected = 10.0f * 300.0f / (FLOAT)FRAMETIME;
    ASSERT_FLOAT_EQ(unit_movedistance(unit), expected);
}

/* -----------------------------------------------------------------------
 * M_DistanceToGoal tests
 * --------------------------------------------------------------------- */

static void test_distance_to_goal_along_x_axis(void) {
    LPEDICT unit = make_moving_unit(0.0f, 0.0f);
    LPEDICT wp   = alloc_test_unit(0, 100.0f, 0.0f);
    unit->goalentity = wp;
    ASSERT_FLOAT_EQ(M_DistanceToGoal(unit), 100.0f);
}

static void test_distance_to_goal_diagonal(void) {
    LPEDICT unit = make_moving_unit(0.0f, 0.0f);
    LPEDICT wp   = alloc_test_unit(0, 30.0f, 40.0f); /* 3-4-5 right triangle → 50 */
    unit->goalentity = wp;
    ASSERT_EQ_FLOAT(M_DistanceToGoal(unit), 50.0f, 0.1f);
}

static void test_distance_to_goal_zero_when_at_goal(void) {
    LPEDICT unit = make_moving_unit(10.0f, 10.0f);
    LPEDICT wp   = alloc_test_unit(0, 10.0f, 10.0f);
    unit->goalentity = wp;
    ASSERT_FLOAT_EQ(M_DistanceToGoal(unit), 0.0f);
}

/* -----------------------------------------------------------------------
 * ai_walk / movement frame tests
 *
 * ai_walk is static inside s_move.c; it is accessed via the think
 * function-pointer stored in move_move_walk.  After calling order_move
 * we invoke ent->currentmove->think() to simulate one game frame.
 * ===================================================================== */

static void test_unit_moves_closer_to_goal_after_one_frame(void) {
    LPEDICT unit = make_moving_unit(0.0f, 0.0f);
    /* Place waypoint within NAVI_THRESHOLD so direct vector math is used
     * and we don't need the heatmap mock to return a meaningful direction. */
    VECTOR2 dest = {40.0f, 0.0f};
    unit_issueorder(unit, "move", &dest);
    ASSERT_NOT_NULL(unit->currentmove);
    ASSERT_NOT_NULL(unit->currentmove->think);

    FLOAT dist_before = M_DistanceToGoal(unit);
    unit->currentmove->think(unit);
    FLOAT dist_after = M_DistanceToGoal(unit);

    ASSERT(dist_after < dist_before);
}

static void test_unit_reaches_goal_and_transitions_to_stand(void) {
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

    ASSERT_ANIM(unit, "stand");
}

static void test_unit_position_changes_after_move_frame(void) {
    LPEDICT unit = make_moving_unit(0.0f, 0.0f);
    VECTOR2 dest = {40.0f, 0.0f};
    unit_issueorder(unit, "move", &dest);

    FLOAT x0 = unit->s.origin2.x;
    unit->currentmove->think(unit);

    /* Unit must have moved in the X direction. */
    ASSERT(unit->s.origin2.x > x0);
}

static void test_unit_does_not_overshoot_goal(void) {
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
    ASSERT_EQ_FLOAT(dist, 0.0f, 0.01f);
}

static void test_group_move_assigns_distinct_reserved_destinations(void) {
    reset_entities();
    LPEDICT clent = alloc_test_unit(0, 0.0f, 0.0f);
    clent->client = &game.clients[0];

    LPEDICT a = alloc_test_unit(UNIT_ID("hpea"), 0.0f, 0.0f);
    LPEDICT b = alloc_test_unit(UNIT_ID("hpea"), 20.0f, 0.0f);
    LPEDICT c = alloc_test_unit(UNIT_ID("hpea"), 0.0f, 20.0f);
    LPEDICT units[] = { a, b, c };

    FOR_LOOP(i, 3) {
        units[i]->collision = 16.0f;
        units[i]->selected = 1 << clent->client->ps.number;
        units[i]->stand = unit_stand;
        unit_stand(units[i]);
    }

    VECTOR2 dest = {100.0f, 100.0f};
    ASSERT(move_selectlocation(clent, &dest));

    ASSERT_NOT_NULL(a->goalentity);
    ASSERT_NOT_NULL(b->goalentity);
    ASSERT_NOT_NULL(c->goalentity);
    ASSERT_NOT_NULL(a->goalentity->secondarygoal);
    ASSERT(a->goalentity->secondarygoal == b->goalentity->secondarygoal);
    ASSERT(a->goalentity->secondarygoal == c->goalentity->secondarygoal);
    ASSERT(Vector2_distance(&a->goalentity->s.origin2, &b->goalentity->s.origin2) >= 32.0f);
    ASSERT(Vector2_distance(&a->goalentity->s.origin2, &c->goalentity->s.origin2) >= 32.0f);
    ASSERT(Vector2_distance(&b->goalentity->s.origin2, &c->goalentity->s.origin2) >= 32.0f);
}

static void test_group_move_ignores_selected_buildings(void) {
    reset_entities();
    LPEDICT clent = alloc_test_unit(0, 0.0f, 0.0f);
    clent->client = &game.clients[0];

    LPEDICT building = alloc_test_unit(UNIT_ID("hbar"), 0.0f, 0.0f);
    LPEDICT peasant = alloc_test_unit(UNIT_ID("hpea"), 20.0f, 0.0f);

    building->collision = 64.0f;
    building->selected = 1 << clent->client->ps.number;
    building->stand = unit_stand;
    unit_stand(building);

    peasant->collision = 16.0f;
    peasant->selected = 1 << clent->client->ps.number;
    peasant->stand = unit_stand;
    unit_stand(peasant);

    VECTOR2 dest = {100.0f, 100.0f};
    ASSERT(move_selectlocation(clent, &dest));

    ASSERT_NULL(building->goalentity);
    ASSERT_NOT_NULL(peasant->goalentity);
}

/* A mixed-speed group travels at its slowest member's speed so it stays
 * together instead of stringing out (WC3 group movement). */
static void test_group_move_travels_at_slowest_member_speed(void) {
    reset_entities();
    LPEDICT clent = alloc_test_unit(0, 0.0f, 0.0f);
    clent->client = &game.clients[0];

    LPEDICT fast = alloc_test_unit(UNIT_ID("hpea"), 0.0f, 0.0f);
    LPEDICT slow = alloc_test_unit(UNIT_ID("hpea"), 20.0f, 0.0f);
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
    ASSERT(move_selectlocation(clent, &dest));

    /* Both units adopt the slowest member's speed for the group move... */
    ASSERT_EQ_FLOAT(fast->move_group_speed, 100.0f, 0.01f);
    ASSERT_EQ_FLOAT(slow->move_group_speed, 100.0f, 0.01f);
    /* ...so the fast unit's per-frame travel is capped to the slow speed. */
    ASSERT_FLOAT_EQ(unit_movedistance(fast), 10.0f * 100.0f / (FLOAT)FRAMETIME);
}

/* A lone unit keeps its own speed (no group cap). */
static void test_single_unit_move_keeps_own_speed(void) {
    LPEDICT unit = make_moving_unit(0.0f, 0.0f);
    unit->unitinfo.MoveSpeed = 300.0f;
    VECTOR2 dest = {200.0f, 0.0f};
    unit_issueorder(unit, "move", &dest);

    ASSERT_EQ_FLOAT(unit->move_group_speed, 0.0f, 0.01f);
    ASSERT_FLOAT_EQ(unit_movedistance(unit), 10.0f * 300.0f / (FLOAT)FRAMETIME);
}

static void test_blocked_move_stops_instead_of_walking_forever(void) {
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
        gi.LinkEntity(unit);
    }

    ASSERT_ANIM(unit, "stand");
}

static void test_near_goal_jitter_settles_to_stand(void) {
    LPEDICT unit = make_moving_unit(65.0f, 0.0f);
    VECTOR2 jitter = unit->s.origin2;
    VECTOR2 dest = {100.0f, 0.0f};
    unit_issueorder(unit, "move", &dest);

    for (int i = 0; i < 10; i++) {
        if (!unit->currentmove || strcmp(unit->currentmove->animation, "walk") != 0) {
            break;
        }
        unit->currentmove->think(unit);
        unit->s.origin2 = jitter;
        unit->s.origin.x = jitter.x;
        unit->s.origin.y = jitter.y;
        gi.LinkEntity(unit);
    }

    ASSERT_ANIM(unit, "stand");
}

static void test_unit_stops_when_goal_is_occupied(void) {
    LPEDICT unit = make_moving_unit(0.0f, 0.0f);
    LPEDICT blocker = alloc_test_unit(UNIT_ID("hfoo"), 100.0f, 0.0f);
    VECTOR2 dest = {100.0f, 0.0f};

    unit->collision = 16.0f;
    blocker->collision = 16.0f;
    blocker->s.model = 1;            /* non-hollow so it is a collision obstacle */
    blocker->stand = unit_stand;
    blocker->movetype = MOVETYPE_NONE;
    unit_stand(blocker);
    gi.LinkEntity(blocker);          /* refresh bounds to reflect collision */

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
    ASSERT_ANIM(unit, "stand");                                  /* settled, didn't walk forever */
    ASSERT(min_goal_dist >= combined - 1.0f);                    /* never penetrated the blocker */
    ASSERT(min_goal_dist <= combined + unit_movedistance(unit)); /* but reached right up to it */
}

/* -----------------------------------------------------------------------
 * Suite runner
 * --------------------------------------------------------------------- */

BEGIN_SUITE(movement)
    RUN_TEST(test_order_move_sets_goalentity);
    RUN_TEST(test_order_move_sets_walk_animation);
    RUN_TEST(test_waypoint_add_sets_origin);
    RUN_TEST(test_unit_movedistance_matches_formula);
    RUN_TEST(test_unit_movedistance_uses_scripted_move_speed);
    RUN_TEST(test_distance_to_goal_along_x_axis);
    RUN_TEST(test_distance_to_goal_diagonal);
    RUN_TEST(test_distance_to_goal_zero_when_at_goal);
    RUN_TEST(test_unit_moves_closer_to_goal_after_one_frame);
    RUN_TEST(test_unit_reaches_goal_and_transitions_to_stand);
    RUN_TEST(test_unit_position_changes_after_move_frame);
    RUN_TEST(test_unit_does_not_overshoot_goal);
    RUN_TEST(test_group_move_assigns_distinct_reserved_destinations);
    RUN_TEST(test_group_move_ignores_selected_buildings);
    RUN_TEST(test_group_move_travels_at_slowest_member_speed);
    RUN_TEST(test_single_unit_move_keeps_own_speed);
    RUN_TEST(test_blocked_move_stops_instead_of_walking_forever);
    RUN_TEST(test_near_goal_jitter_settles_to_stand);
    RUN_TEST(test_unit_stops_when_goal_is_occupied);
END_SUITE()
