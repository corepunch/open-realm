#ifdef BZ_TESTS
#include "test.h"
#include "../skills/s_skills.h"

#define BZ_TEST_WARP MAKEFOURCC('Z','w','r','p')

edict_t *alloc_test_unit(uint32_t class_id, float x, float y);
void reset_entities(void);
void setup_test_world(void);
slkTestData_t *parse_slk_string(char const *text);
void free_slk_rows(slkTestData_t *rows);
bool run_test_jass(cstring_t src);

/* Deliberately non-stock rectangle values prove that Wrp1/Wrp2
 * (AbilityData DataA/DataB) are consumed instead of a hard-coded radius.
 * A stock Awrp row is included because UnitAddAbility rejects rawcodes that
 * do not exist in the synthetic AbilityData fixture. */
static char const waygate_slk[] =
    "ID;PWXL;N;EBB;Y3;X5\n"
    "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
    "C;Y1;X4;K\"DataA1\"\nC;Y1;X5;K\"DataB1\"\n"
    "C;Y2;X1;K\"Zwrp\"\nC;Y2;X2;K\"Awrp\"\nC;Y2;X3;K\"1\"\n"
    "C;Y2;X4;K\"160\"\nC;Y2;X5;K\"80\"\n"
    "C;Y3;X1;K\"Awrp\"\nC;Y3;X2;K\"Awrp\"\nC;Y3;X3;K\"1\"\n"
    "C;Y3;X4;K\"160\"\nC;Y3;X5;K\"80\"\nE\n";

typedef struct {
    slkTestData_t *rows, *old;
    edict_t *gate;
    edict_t *unit;
} wayFix_t;

static wayFix_t waygate_setup(float unit_x, float unit_y) {
    wayFix_t fix = {0};
    vector2_t destination = { 400.0f, 320.0f };

    reset_entities(); setup_test_world(); level.time = 1000;
    fix.rows = parse_slk_string(waygate_slk);
    fix.old = G_SetSLKRows("AbilityData", fix.rows);
    fix.gate = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0.0f, 0.0f);
    fix.unit = alloc_test_unit(MAKEFOURCC('h','p','e','a'), unit_x, unit_y);
    fix.gate->svflags |= SVF_MONSTER;
    fix.unit->svflags |= SVF_MONSTER;
    fix.gate->targtype = TARG_STRUCTURE;
    fix.unit->targtype = TARG_GROUND;
    fix.unit->stand = unit_stand;
    fix.unit->die = unit_die;
    fix.unit->think = monster_think;
    fix.unit->movetype = MOVETYPE_STEP;
    fix.unit->unitinfo.MoveSpeed = 300.0f;
    unit_stand(fix.unit);
    fix.unit->collision = 16.0f;
    T_ASSERT(G_ActorAddSkill(fix.gate, BZ_TEST_WARP));
    S_WaygateSetDestination(fix.gate, &destination);
    S_WaygateSetActive(fix.gate, true);
    return fix;
}

static void waygate_done(wayFix_t fix) {
    G_SetSLKRows("AbilityData", fix.old);
    free_slk_rows(fix.rows);
}

static void assert_no_waygate_order(edict_t const *unit) {
    T_NULL(unit->movement.waygate_target);
    T_NULL(unit->movement.waygate_goal);
    T_EQ(unit->movement.waygate_target_spawn_time, 0);
}

TEST(wc3_waygate, runtime_state_and_activation_animation) {
    wayFix_t fix = waygate_setup(96.0f, 0.0f);
    vector2_t destination = {0};

    T_ASSERT(S_WaygateIsGate(fix.gate));
    T_ASSERT(S_WaygateIsActive(fix.gate));
    T_ASSERT(strstr(fix.gate->animation_props, "alternate") != NULL);
    T_ASSERT(S_WaygateGetDestination(fix.gate, &destination));
    T_FEQ(destination.x, 400.0f, 0.001f);
    T_FEQ(destination.y, 320.0f, 0.001f);

    S_WaygateSetActive(fix.gate, false);
    T_ASSERT(!S_WaygateIsActive(fix.gate));
    T_ASSERT(strstr(fix.gate->animation_props, "alternate") == NULL);
    waygate_done(fix);
}

TEST(wc3_waygate, smart_use_reads_rectangular_authored_data_and_teleports) {
    wayFix_t fix = waygate_setup(70.0f, 30.0f); /* inside 160x80, but outside a 40-radius circle */

    T_ASSERT(G_IssueUnitTargetOrder(fix.unit, "smart", fix.gate, false, 0));
    T_FEQ(fix.unit->s.origin2.x, 400.0f, 0.001f);
    T_FEQ(fix.unit->s.origin2.y, 320.0f, 0.001f);
    assert_no_waygate_order(fix.unit);
    waygate_done(fix);
}

TEST(wc3_waygate, blocked_destination_cancels_without_raw_position_fallback) {
    uint8_t blocked[64 * 64];
    wayFix_t fix = waygate_setup(70.0f, 0.0f);
    vector2_t before = fix.unit->s.origin2;

    memset(blocked, CM_PATHING_UNWALKABLE, sizeof(blocked));
    setup_test_pathmap(64, 64, blocked);
    T_ASSERT(G_IssueUnitTargetOrder(fix.unit, "smart", fix.gate, false, 0));
    T_FEQ(fix.unit->s.origin2.x, before.x, 0.001f);
    T_FEQ(fix.unit->s.origin2.y, before.y, 0.001f);
    assert_no_waygate_order(fix.unit);
    waygate_done(fix);
}

TEST(wc3_waygate, disabled_gate_does_not_consume_smart_as_waygate) {
    wayFix_t fix = waygate_setup(70.0f, 0.0f);
    vector2_t before = fix.unit->s.origin2;

    S_WaygateSetActive(fix.gate, false);
    T_ASSERT(G_IssueUnitTargetOrder(fix.unit, "smart", fix.gate, false, 0));
    T_FEQ(fix.unit->s.origin2.x, before.x, 0.001f);
    T_FEQ(fix.unit->s.origin2.y, before.y, 0.001f);
    T_ASSERT(!fix.unit->currentmove || fix.unit->currentmove->proc != CAbilityWarp);
    assert_no_waygate_order(fix.unit);
    waygate_done(fix);
}

TEST(wc3_waygate, approach_revalidates_gate_generation_before_teleport) {
    wayFix_t fix = waygate_setup(300.0f, 0.0f);

    T_ASSERT(G_IssueUnitTargetOrder(fix.unit, "smart", fix.gate, false, 0));
    T_ASSERT(fix.unit->movement.waygate_target == fix.gate);
    T_NOT_NULL(fix.unit->movement.waygate_goal);
    T_EQ(fix.unit->movement.waygate_target_spawn_time, fix.gate->spawn_time);
    fix.gate->spawn_time++;
    T_NOT_NULL(fix.unit->currentmove);
    G_RunEntities();
    assert_no_waygate_order(fix.unit);
    T_FEQ(fix.unit->s.origin2.x, 300.0f, 0.001f);
    waygate_done(fix);
}

TEST(wc3_waygate, approach_rechecks_activation_before_traversal) {
    wayFix_t fix = waygate_setup(300.0f, 0.0f);

    T_ASSERT(G_IssueUnitTargetOrder(fix.unit, "smart", fix.gate, false, 0));
    S_WaygateSetActive(fix.gate, false);
    T_NOT_NULL(fix.unit->currentmove);
    G_RunEntities();
    T_FEQ(fix.unit->s.origin2.x, 300.0f, 0.001f);
    assert_no_waygate_order(fix.unit);
    waygate_done(fix);
}

TEST(wc3_waygate, approach_uses_live_destination_at_traversal_time) {
    wayFix_t fix = waygate_setup(300.0f, 0.0f);
    vector2_t destination = { 640.0f, -128.0f };

    T_ASSERT(G_IssueUnitTargetOrder(fix.unit, "smart", fix.gate, false, 0));
    S_WaygateSetDestination(fix.gate, &destination);
    fix.unit->s.origin2 = (vector2_t){ 70.0f, 0.0f };
    fix.unit->s.origin.x = 70.0f;
    fix.unit->s.origin.y = 0.0f;
    T_NOT_NULL(fix.unit->currentmove);
    G_RunEntities();
    T_FEQ(fix.unit->s.origin2.x, 640.0f, 0.001f);
    T_FEQ(fix.unit->s.origin2.y, -128.0f, 0.001f);
    assert_no_waygate_order(fix.unit);
    waygate_done(fix);
}

TEST(wc3_waygate, rejected_replacement_order_preserves_inflight_approach) {
    wayFix_t fix = waygate_setup(300.0f, 0.0f);
    edict_t *goal;

    T_ASSERT(G_IssueUnitTargetOrder(fix.unit, "smart", fix.gate, false, 0));
    goal = fix.unit->movement.waygate_goal;
    T_ASSERT(!G_IssueUnitTargetOrder(fix.unit, "attack", fix.unit, false, 0));
    T_ASSERT(fix.unit->movement.waygate_target == fix.gate);
    T_ASSERT(fix.unit->movement.waygate_goal == goal);
    T_ASSERT(fix.unit->goalentity == goal);
    T_NOT_NULL(fix.unit->currentmove);
    T_EQ(fix.unit->currentmove->proc, CAbilityWarp);
    waygate_done(fix);
}

TEST(wc3_waygate, accepted_point_order_replaces_inflight_approach) {
    wayFix_t fix = waygate_setup(300.0f, 0.0f);
    vector2_t destination = { 500.0f, 100.0f };

    T_ASSERT(G_IssueUnitTargetOrder(fix.unit, "smart", fix.gate, false, 0));
    T_ASSERT(G_IssueUnitPointOrder(fix.unit, "move", &destination, false, 0, 0.0f));
    assert_no_waygate_order(fix.unit);
    T_ASSERT(!fix.unit->currentmove || fix.unit->currentmove->proc != CAbilityWarp);
    waygate_done(fix);
}

TEST(wc3_waygate, accepted_target_order_replaces_inflight_approach) {
    wayFix_t fix = waygate_setup(300.0f, 0.0f);
    edict_t *friend = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 500.0f, 0.0f);
    friend->svflags |= SVF_MONSTER;

    T_ASSERT(G_IssueUnitTargetOrder(fix.unit, "smart", fix.gate, false, 0));
    T_ASSERT(G_IssueUnitTargetOrder(fix.unit, "move", friend, false, 0));
    assert_no_waygate_order(fix.unit);
    T_ASSERT(fix.unit->movement.follow_target == friend);
    waygate_done(fix);
}

TEST(wc3_waygate, shift_queued_order_does_not_interrupt_inflight_approach) {
    wayFix_t fix = waygate_setup(300.0f, 0.0f);
    vector2_t destination = { 500.0f, 100.0f };
    edict_t *goal;

    T_ASSERT(G_IssueUnitTargetOrder(fix.unit, "smart", fix.gate, false, 0));
    goal = fix.unit->movement.waygate_goal;
    T_ASSERT(G_IssueUnitPointOrder(fix.unit, "move", &destination, true, 0, 0.0f));
    T_EQ(G_UnitQueuedOrderCount(fix.unit), 1);
    T_ASSERT(fix.unit->movement.waygate_target == fix.gate);
    T_ASSERT(fix.unit->movement.waygate_goal == goal);
    T_ASSERT(fix.unit->goalentity == goal);
    waygate_done(fix);
}

TEST(wc3_waygate, stop_and_hold_replace_inflight_approach) {
    wayFix_t stop = waygate_setup(300.0f, 0.0f);
    T_ASSERT(G_IssueUnitTargetOrder(stop.unit, "smart", stop.gate, false, 0));
    T_ASSERT(unit_issueimmediateorder(stop.unit, "stop"));
    assert_no_waygate_order(stop.unit);
    waygate_done(stop);

    wayFix_t hold = waygate_setup(300.0f, 0.0f);
    T_ASSERT(G_IssueUnitTargetOrder(hold.unit, "smart", hold.gate, false, 0));
    T_ASSERT(unit_issueimmediateorder(hold.unit, "holdposition"));
    assert_no_waygate_order(hold.unit);
    T_ASSERT(hold.unit->movement.holding_position);
    waygate_done(hold);
}

TEST(wc3_waygate, accepted_instant_order_hook_retires_inflight_approach) {
    wayFix_t fix = waygate_setup(300.0f, 0.0f);

    T_ASSERT(G_IssueUnitTargetOrder(fix.unit, "smart", fix.gate, false, 0));
    T_ASSERT(S_UnitAbilityOrderAccepted(fix.unit, "test-instant-order"));
    assert_no_waygate_order(fix.unit);
    T_ASSERT(!fix.unit->currentmove || fix.unit->currentmove->proc != CAbilityWarp);
    waygate_done(fix);
}

TEST(wc3_waygate, jass_natives_preserve_destination_and_boolean_activation) {
    slkTestData_t *rows = parse_slk_string(waygate_slk);
    slkTestData_t *old = G_SetSLKRows("AbilityData", rows);

    T_ASSERT(run_test_jass(
        "globals\n"
        "  unit g = null\n"
        "endglobals\n"
        "function main takes nothing returns nothing\n"
        "  set g = CreateUnit(Player(0), 'hfoo', 0.0, 0.0, 0.0)\n"
        "  call BJassAssert(UnitAddAbility(g, 'Awrp'), \"Awrp fixture missing\")\n"
        "  call WaygateSetDestination(g, 123.5, -77.25)\n"
        "  call BJassAssert(WaygateGetDestinationX(g) == 123.5, \"waygate x\")\n"
        "  call BJassAssert(WaygateGetDestinationY(g) == -77.25, \"waygate y\")\n"
        "  call WaygateActivate(g, true)\n"
        "  call BJassAssert(WaygateIsActive(g), \"waygate active\")\n"
        "  call WaygateActivate(g, false)\n"
        "  call BJassAssert(not WaygateIsActive(g), \"waygate inactive\")\n"
        "endfunction\n"));
    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_save, waygate_state_and_inflight_approach_round_trip) {
    cstring_t filename = "/tmp/openwarcraft3-wc3-waygate-save.bin";
    wayFix_t fix = waygate_setup(300.0f, 0.0f);
    uint32_t const gate_number = fix.gate->s.number;
    uint32_t const unit_number = fix.unit->s.number;
    uint32_t goal_number;

    T_ASSERT(G_IssueUnitTargetOrder(fix.unit, "smart", fix.gate, false, 0));
    T_NOT_NULL(fix.unit->movement.waygate_goal);
    goal_number = fix.unit->movement.waygate_goal->s.number;
    T_ASSERT(WriteGame(filename));
    fix.gate->waygate.active = false;
    fix.gate->waygate.destination = (vector2_t){0};
    fix.gate->waygate.destination_set = false;
    fix.unit->movement.waygate_target = NULL;
    fix.unit->movement.waygate_goal = NULL;
    fix.unit->movement.waygate_target_spawn_time = 0;
    T_ASSERT(ReadGame(filename));
    fix.gate = g_edicts + gate_number;
    fix.unit = g_edicts + unit_number;
    T_ASSERT(fix.gate->waygate.active);
    T_ASSERT(fix.gate->waygate.destination_set);
    T_FEQ(fix.gate->waygate.destination.x, 400.0f, 0.001f);
    T_FEQ(fix.gate->waygate.destination.y, 320.0f, 0.001f);
    T_ASSERT(fix.unit->movement.waygate_target == fix.gate);
    T_ASSERT(fix.unit->movement.waygate_goal == g_edicts + goal_number);
    T_ASSERT(fix.unit->goalentity == fix.unit->movement.waygate_goal);
    T_EQ(fix.unit->movement.waygate_target_spawn_time, fix.gate->spawn_time);
    T_NOT_NULL(fix.unit->currentmove);
    T_EQ(fix.unit->currentmove->proc, CAbilityWarp);
    remove(filename);
    waygate_done(fix);
}

TEST(wc3_save, rejects_invalid_waygate_entity_references) {
    cstring_t filename = "/tmp/openwarcraft3-wc3-waygate-invalid-reference.bin";
    wayFix_t fix = waygate_setup(300.0f, 0.0f);
    edict_t *target, *goal;

    T_ASSERT(G_IssueUnitTargetOrder(fix.unit, "smart", fix.gate, false, 0));
    target = fix.unit->movement.waygate_target;
    goal = fix.unit->movement.waygate_goal;
    fix.unit->movement.waygate_target = (edict_t *)(uintptr_t)1;
    T_ASSERT(!WriteGame(filename));
    fix.unit->movement.waygate_target = target;
    fix.unit->movement.waygate_goal = (edict_t *)(uintptr_t)1;
    T_ASSERT(!WriteGame(filename));
    fix.unit->movement.waygate_goal = goal;
    waygate_done(fix);
}

#endif
