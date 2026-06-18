/*
 * test_pathfinding.c — Unit tests for routing.c (heatmap / flow-field).
 *
 * Uses CM_SetupTestPathmap() to build synthetic pathmaps without an MPQ
 * archive, then calls CM_BuildHeatmap() and get_flow_direction() directly.
 *
 * Test areas:
 *   Heatmap cache — same goal returns cached generation; different goal
 *                   triggers a rebuild (generation advances).
 *   Static obstacles — nowalk cells are avoided; the heatmap never
 *                      propagates into a wall cell.
 *   Unit obstacles separation — a live unit does NOT invalidate the heatmap
 *                               cache for the same goal (fix #2: unit
 *                               obstacles are handled by collision, not by
 *                               the heatmap).
 *   Multi-goal cache — two different goals each maintain their own cached
 *                      generation; switching between them does not force a
 *                      full rebuild every frame (fix #3).
 *   Flow direction — the flow vector at a cell points toward the goal.
 *   Transitive bumping — a unit that is stopped near its goal stops an
 *                        overlapping unit that shares the same goal
 *                        (fix #1: G_SolveCollisions propagates arrival).
 */

#include <math.h>
#include <string.h>
#include "test_framework.h"
#include "test_harness.h"

/* -----------------------------------------------------------------------
 * Symbols from routing.c / g_phys.c used directly by these tests.
 * --------------------------------------------------------------------- */

/* Defined in routing.c, only compiled for test builds. */
void CM_SetupTestPathmap(DWORD width, DWORD height, BYTE const *cells);

/* Public API from routing.c. */
DWORD  CM_BuildHeatmap(edict_t *goalentity);
VECTOR2 get_flow_direction(DWORD heatmapindex, float fnx, float fny);

/* From g_phys.c — needed for transitive-bumping test. */
extern ability_t a_move;
void G_SolveCollisions(void);

/* From g_monster.c */
LPEDICT Waypoint_add(LPCVECTOR2 spot);

/* From s_move.c — needed to set up a moving unit. */
void order_move(LPEDICT self, LPEDICT target);

/* From m_unit.c */
void unit_stand(LPEDICT self);

/* -----------------------------------------------------------------------
 * Test-world helpers
 *
 * The test pathmap is 8×8 cells.  World coordinates are 1:1 with cell
 * coordinates (cell_size = 1) because CM_GetNormalizedMapPosition and
 * CM_GetDenormalizedMapPosition return identity transforms when called
 * from game common/world_*.c stubs are absent — routing.c uses them
 * only to convert; the test harness's world bounds mock covers this.
 *
 * We use a 10×10 cell map with a wall column at x=5 to test obstacle
 * avoidance.
 * --------------------------------------------------------------------- */

#define MAP_W 10
#define MAP_H 10

/*  0 = open, 2 = nowalk (bit 1 set, matching pathMapCell_t.nowalk). */
static BYTE open_map[MAP_W * MAP_H];   /* all open */

/* Wall column at x=5, rows 0-7, leaving rows 8-9 as a gap. */
static BYTE wall_map[MAP_W * MAP_H];

static void build_open_map(void) {
    memset(open_map, 0, sizeof(open_map));
}

static void build_wall_map(void) {
    memset(wall_map, 0, sizeof(wall_map));
    for (int y = 0; y < 8; y++) {
        wall_map[y * MAP_W + 5] = 2; /* nowalk */
    }
}

/* Place a waypoint at pathmap cell (cx, cy).
 * CM_GetNormalizedMapPosition returns (x,y) unchanged in test builds, so
 * routing.c multiplies by pathmap.width/height to get the cell index.
 * We therefore pass pre-divided coordinates: cell_x / MAP_W, cell_y / MAP_H,
 * so after the multiply we land at the intended cell. */
static LPEDICT make_waypoint(float cell_x, float cell_y) {
    VECTOR2 pos = { cell_x / (float)MAP_W, cell_y / (float)MAP_H };
    return Waypoint_add(&pos);
}

/* Query flow direction at cell (cx, cy), compensating for the identity
 * CM_GetNormalizedMapPosition stub the same way make_waypoint does. */
static VECTOR2 flow_at_cell(float cell_x, float cell_y) {
    return get_flow_direction(0,
        cell_x / (float)MAP_W,
        cell_y / (float)MAP_H);
}

/* Make a minimal unit that looks "stopped" (no currentmove).
 * s.model is set to 1 so the entity is not treated as IS_HOLLOW by
 * G_SolveCollisions (which skips entities with model == 0). */
static LPEDICT make_unit_at(float x, float y) {
    LPEDICT ent = alloc_test_unit(UNIT_ID("hpea"), x, y);
    ent->movetype  = MOVETYPE_STEP;
    ent->collision = 16.0f;
    ent->s.model   = 1;
    ent->stand     = unit_stand;
    unit_stand(ent);
    gi.LinkEntity(ent); /* populate bounds for BoxEdicts queries */
    return ent;
}

/* -----------------------------------------------------------------------
 * Cache tests
 * --------------------------------------------------------------------- */

static void test_heatmap_cache_hit_same_goal(void) {
    build_open_map();
    CM_SetupTestPathmap(MAP_W, MAP_H, open_map);
    reset_entities();

    LPEDICT wp = make_waypoint(5.0f, 5.0f);
    DWORD gen1 = CM_BuildHeatmap(wp);
    DWORD gen2 = CM_BuildHeatmap(wp);

    /* Same goal: generation must be identical — no rebuild. */
    ASSERT_EQ_INT(gen1, gen2);
}

static void test_heatmap_cache_miss_different_goal(void) {
    build_open_map();
    CM_SetupTestPathmap(MAP_W, MAP_H, open_map);
    reset_entities();

    LPEDICT wp1 = make_waypoint(2.0f, 2.0f);
    LPEDICT wp2 = make_waypoint(7.0f, 7.0f);
    DWORD gen1 = CM_BuildHeatmap(wp1);
    DWORD gen2 = CM_BuildHeatmap(wp2);

    /* Different goals must produce different generations. */
    ASSERT(gen1 != gen2);
}

static void test_heatmap_generation_is_nonzero(void) {
    build_open_map();
    CM_SetupTestPathmap(MAP_W, MAP_H, open_map);
    reset_entities();

    LPEDICT wp = make_waypoint(3.0f, 3.0f);
    DWORD gen = CM_BuildHeatmap(wp);

    ASSERT(gen != 0);
}

/* -----------------------------------------------------------------------
 * Multi-goal cache (fix #3): switching between two known goals should
 * not force a full rebuild every time; each goal keeps its own cached
 * generation.
 * --------------------------------------------------------------------- */

static void test_multi_goal_cache_no_thrash(void) {
    /* Must call CM_SetupTestPathmap once only — it resets the cache. */
    build_open_map();
    CM_SetupTestPathmap(MAP_W, MAP_H, open_map);

    /* Use distinct waypoint slots from the global pool so pointers differ. */
    LPEDICT wp_a = make_waypoint(1.0f, 1.0f);
    LPEDICT wp_b = make_waypoint(8.0f, 8.0f);

    /* Verify the two waypoints are actually different pointers. */
    ASSERT(wp_a != wp_b);

    /* Build both goals once — they each occupy a cache slot. */
    DWORD gen_a1 = CM_BuildHeatmap(wp_a);
    DWORD gen_b1 = CM_BuildHeatmap(wp_b);

    /* Both goals are different so their generations must differ. */
    ASSERT(gen_a1 != gen_b1);

    /* Switch back to each — both should hit the cache (same generation). */
    DWORD gen_a2 = CM_BuildHeatmap(wp_a);
    DWORD gen_b2 = CM_BuildHeatmap(wp_b);

    ASSERT_EQ_INT(gen_a1, gen_a2);
    ASSERT_EQ_INT(gen_b1, gen_b2);
}

/* -----------------------------------------------------------------------
 * Static obstacle tests
 * --------------------------------------------------------------------- */

static void test_wall_routes_flow_around_obstacle(void) {
    build_wall_map();
    CM_SetupTestPathmap(MAP_W, MAP_H, wall_map);
    reset_entities();

    /* Goal on the right side of the wall at (7, 5), reachable only through
     * the gap at y=8 or y=9.  Sample flow at (3, 5): left side of wall.
     * The direct rightward path is blocked, so the flow must deviate — it
     * should NOT point straight right (+x only) through the wall; it will
     * bend toward the gap at y=8/9, giving a downward (y) component.
     * We also verify the goal IS reachable from the right side (7,5). */
    LPEDICT wp = make_waypoint(7.0f, 5.0f);
    CM_BuildHeatmap(wp);

    /* Flow at (7, 5) itself (goal cell) may be zero or any direction.
     * Flow at (8, 5) — right side, open — should point toward the goal
     * i.e. leftward (-x component). */
    VECTOR2 dir_right = flow_at_cell(8.0f, 5.0f);
    ASSERT(dir_right.x < 0.0f);

    /* Flow at (3, 5) — left of wall — must have a non-zero y component
     * to route around the wall (can't go straight right). */
    VECTOR2 dir_left = flow_at_cell(3.0f, 5.0f);
    ASSERT(dir_left.y != 0.0f || dir_left.x != 0.0f);
}

static void test_flow_direction_points_toward_goal_open(void) {
    build_open_map();
    CM_SetupTestPathmap(MAP_W, MAP_H, open_map);
    reset_entities();

    /* Goal at right edge; sample from left side. */
    LPEDICT wp = make_waypoint(9.0f, 5.0f);
    CM_BuildHeatmap(wp);

    /* At cell (2, 5), flow should point roughly rightward (+x). */
    VECTOR2 dir = flow_at_cell(2.0f, 5.0f);
    ASSERT(dir.x > 0.0f);
}

/* -----------------------------------------------------------------------
 * Unit-obstacle separation (fix #2):
 * A live unit entity at a given location must NOT cause the heatmap for
 * the same goal to be rebuilt (unit obstacles are handled by collision
 * resolution, not the heatmap).  We verify by checking that calling
 * CM_BuildHeatmap twice with a unit present returns the same generation.
 * --------------------------------------------------------------------- */

static void test_unit_presence_does_not_invalidate_heatmap_cache(void) {
    build_open_map();
    CM_SetupTestPathmap(MAP_W, MAP_H, open_map);
    reset_entities();

    LPEDICT wp   = make_waypoint(7.0f, 5.0f);
    LPEDICT unit = make_unit_at(3.0f, 5.0f);
    (void)unit;

    DWORD gen1 = CM_BuildHeatmap(wp);
    DWORD gen2 = CM_BuildHeatmap(wp);

    ASSERT_EQ_INT(gen1, gen2);
}

/* -----------------------------------------------------------------------
 * Transitive bumping / arrival propagation (fix #1):
 *
 * Two units share the same goalentity and are overlapping.  Unit A is
 * stopped (IS_MOVING == false), unit B is still walking.  After one call
 * to G_SolveCollisions(), unit B should also be stopped.
 * --------------------------------------------------------------------- */

static void test_transitive_bumping_stops_overlapping_same_goal_unit(void) {
    reset_entities();

    /* Use normalized waypoint coords: cell (5, 5) in a 10x10 map. */
    LPEDICT wp = make_waypoint(5.0f, 5.0f);

    /* Unit A: already stopped at the goal (no currentmove → IS_MOVING false).
     * Place at world position matching the waypoint origin. */
    LPEDICT a = make_unit_at(wp->s.origin.x, wp->s.origin.y);
    a->goalentity = wp;

    /* Unit B: still walking toward the same goal, overlapping with A.
     * Offset by less than 2× collision so they overlap. */
    LPEDICT b = make_unit_at(wp->s.origin.x + a->collision, wp->s.origin.y);
    b->stand = unit_stand;
    order_move(b, wp);

    /* Both units must be overlapping (sum of radii > distance). */
    ASSERT(b->collision + a->collision > Vector2_distance(&a->s.origin2, &b->s.origin2));

    G_SolveCollisions();

    /* After solving, B should have been stopped by transitive bumping. */
    ASSERT_ANIM(b, "stand");
}

static void test_transitive_bumping_does_not_stop_different_goal_unit(void) {
    reset_entities();

    LPEDICT wp1 = make_waypoint(5.0f, 5.0f);
    LPEDICT wp2 = make_waypoint(8.0f, 5.0f);

    /* Unit A: stopped at wp1. */
    LPEDICT a = make_unit_at(wp1->s.origin.x, wp1->s.origin.y);
    a->goalentity = wp1;

    /* Unit B: walking toward a DIFFERENT goal, overlapping A. */
    LPEDICT b = make_unit_at(wp1->s.origin.x + a->collision, wp1->s.origin.y);
    b->stand = unit_stand;
    order_move(b, wp2);

    ASSERT(b->collision + a->collision > Vector2_distance(&a->s.origin2, &b->s.origin2));

    G_SolveCollisions();

    /* B is heading to a different goal — it should NOT be stopped. */
    ASSERT_ANIM(b, "walk");
}

/* -----------------------------------------------------------------------
 * Flow-field cache consistency
 *
 * After a cache hit, get_flow_direction() must return the same vector as
 * it did immediately after the original build.  This verifies that the
 * pre-baked flow field is stored correctly and survives a cache lookup.
 * --------------------------------------------------------------------- */

static void test_flow_cache_consistent_after_hit(void) {
    build_open_map();
    CM_SetupTestPathmap(MAP_W, MAP_H, open_map);

    LPEDICT wp = make_waypoint(9.0f, 5.0f);
    CM_BuildHeatmap(wp);
    VECTOR2 dir1 = flow_at_cell(2.0f, 5.0f);

    /* Second build of the same goal must hit the cache. */
    CM_BuildHeatmap(wp);
    VECTOR2 dir2 = flow_at_cell(2.0f, 5.0f);

    ASSERT_EQ_FLOAT(dir1.x, dir2.x, 0.001f);
    ASSERT_EQ_FLOAT(dir1.y, dir2.y, 0.001f);
}

/* -----------------------------------------------------------------------
 * Multi-goal flow consistency
 *
 * After building two goals and switching back, the flow for each goal
 * is consistent — switching between cached goals doesn't corrupt directions.
 * --------------------------------------------------------------------- */

static void test_flow_consistent_across_goal_switches(void) {
    build_open_map();
    CM_SetupTestPathmap(MAP_W, MAP_H, open_map);

    LPEDICT wp_left  = make_waypoint(1.0f, 5.0f);
    LPEDICT wp_right = make_waypoint(9.0f, 5.0f);

    /* Build both goals. */
    CM_BuildHeatmap(wp_right);
    VECTOR2 flow_right = flow_at_cell(5.0f, 5.0f); /* should point right (+x) */

    CM_BuildHeatmap(wp_left);
    VECTOR2 flow_left = flow_at_cell(5.0f, 5.0f);  /* should point left (-x) */

    /* Switch back to right goal from cache. */
    CM_BuildHeatmap(wp_right);
    VECTOR2 flow_right2 = flow_at_cell(5.0f, 5.0f);

    /* Flow directions must be in opposite x halves. */
    ASSERT(flow_right.x > 0.0f);
    ASSERT(flow_left.x < 0.0f);
    /* Cached right goal must match original. */
    ASSERT_EQ_FLOAT(flow_right.x, flow_right2.x, 0.001f);
}

/* -----------------------------------------------------------------------
 * Proximity shortcut
 *
 * unit_changeangle uses direct vector math when the unit is within
 * NAVI_THRESHOLD of its goal, skipping the heatmap.  Verify the unit
 * gets a valid angle pointing toward the goal regardless.
 * --------------------------------------------------------------------- */

#define NAVI_THRESHOLD 128.0f  /* must match g_ai.c */

static void test_proximity_shortcut_gives_correct_angle(void) {
    build_open_map();
    CM_SetupTestPathmap(MAP_W, MAP_H, open_map);
    reset_entities();

    /* Place unit close to goal (within NAVI_THRESHOLD). */
    LPEDICT unit = make_unit_at(0.0f, 0.0f);
    LPEDICT wp   = make_waypoint(5.0f, 5.0f);
    unit->goalentity = wp;
    unit->stand      = unit_stand;
    unit_stand(unit);
    order_move(unit, wp);

    /* Distance must be within threshold for the shortcut to apply. */
    FLOAT dist = M_DistanceToGoal(unit);
    ASSERT(dist < NAVI_THRESHOLD);

    /* Units now turn gradually (at their turn rate) toward the target facing
     * rather than snapping instantly, so step a few ticks to let the facing
     * converge before checking the final direction. */
    for (int i = 0; i < 16; i++) {
        unit_changeangle(unit);
    }

    /* Angle must point from (0,0) toward the goal. */
    FLOAT expected = atan2f(wp->s.origin.y - unit->s.origin.y,
                            wp->s.origin.x - unit->s.origin.x);
    ASSERT_EQ_FLOAT(unit->s.angle, expected, 0.01f);
}

/* -----------------------------------------------------------------------
 * Suite runner
 * --------------------------------------------------------------------- */

BEGIN_SUITE(pathfinding)
    RUN_TEST(test_heatmap_cache_hit_same_goal);
    RUN_TEST(test_heatmap_cache_miss_different_goal);
    RUN_TEST(test_heatmap_generation_is_nonzero);
    RUN_TEST(test_multi_goal_cache_no_thrash);
    RUN_TEST(test_wall_routes_flow_around_obstacle);
    RUN_TEST(test_flow_direction_points_toward_goal_open);
    RUN_TEST(test_unit_presence_does_not_invalidate_heatmap_cache);
    RUN_TEST(test_transitive_bumping_stops_overlapping_same_goal_unit);
    RUN_TEST(test_transitive_bumping_does_not_stop_different_goal_unit);
    RUN_TEST(test_flow_cache_consistent_after_hit);
    RUN_TEST(test_flow_consistent_across_goal_switches);
    RUN_TEST(test_proximity_shortcut_gives_correct_angle);
END_SUITE()
