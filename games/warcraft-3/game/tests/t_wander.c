#ifdef BZ_TESTS
#include "test.h"
#include "../skills/s_skills.h"

#define ID_AWAN MAKEFOURCC('A', 'w', 'a', 'n')

edict_t *alloc_test_unit(uint32_t class_id, float x, float y);
void reset_entities(void);
void setup_test_world(void);
slkTestData_t *parse_slk_string(char const *text);
void free_slk_rows(slkTestData_t *rows);
bool run_test_jass(cstring_t src);

static void test_wander_damage(edict_t *unit, edict_t *attacker) {
    abilityCall_t call = MAKE(abilityCall_t, .attacker = attacker);
    S_UnitAbilityEventWithCall(unit, A_DAMAGED, &call);
}

static bool test_wander_blocked_move(edict_t *unit) {
    return S_UnitAbilityEvent(unit, A_MOVE_BLOCKED);
}

/* A started map always owns a JASS VM; the server frame pumps its timers and
 * events unconditionally, so frame-driven tests load an empty map script. */
static void wander_setup_frame_world(void) {
    reset_entities();
    setup_test_world();
    level.time = 1000;
    T_ASSERT(run_test_jass("function main takes nothing returns nothing\nendfunction\n"));
}

/* Real per-frame entry point. The test gi.GetTime hook returns level.time,
 * so advancing it and calling globals.RunFrame() runs G_RunEntities, the
 * resumable path jobs and the rest of the server frame exactly as play does. */
static void wander_run_frames(uint32_t frames) {
    bool const started = level.started, scripts = level.scriptsStarted;
    level.started = level.scriptsStarted = true;
    FOR_LOOP(i, frames) { level.time += FRAMETIME; globals.RunFrame(); }
    level.started = started; level.scriptsStarted = scripts;
}

static void wander_run_frames_until_standing(edict_t *unit) {
    for (int frame = 0; frame < 400 && move_is_active_order_walk(unit); frame++) wander_run_frames(1);
}

/* A critter the scheduler drives itself: think, stand and die callbacks are
 * the production ones, and the unit is linked so collision sees it. */
static edict_t *make_scheduled_critter(float x, float y) {
    edict_t *unit = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), x, y);
    unit->svflags |= SVF_MONSTER;
    unit->stand = unit_stand;
    unit->think = monster_think;
    unit->die = unit_die;
    unit->collision = 16.0f;
    unit->unitinfo.MoveSpeed = 320.0f;
    unit->abilities.added[0] = ID_AWAN;
    ARRAY_COUNT(unit->abilities.added) = 1;
    gi.LinkEntity(unit);
    unit_stand(unit);
    return unit;
}

TEST(wc3_wander, authored_ability_dispatches_innate_idle) {
    edict_t *unit;
    reset_entities();
    setup_test_world();
    level.time = 1000;
    unit = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), 0, 0);
    unit->svflags |= SVF_MONSTER;
    unit->stand = unit_stand;
    unit->abilities.added[0] = ID_AWAN;
    ARRAY_COUNT(unit->abilities.added) = 1;
    T_ASSERT(G_ActorHasSkill(unit, "Awan"));
    T_ASSERT(S_UnitAbilityEvent(unit, A_IDLE));
    T_ASSERT(unit->wander_next_time >= level.time + 8000);
    T_ASSERT(unit->wander_next_time <= level.time + 10000);
    T_ASSERT(S_UnitAbilityEvent(unit, A_NO_RETALIATE));
    /* Removing the authored ability must stop innate idle consumption. */
    unit->abilities.added[0] = 0;
    ARRAY_COUNT(unit->abilities.added) = 0;
    T_ASSERT(!G_ActorHasSkill(unit, "Awan"));
    T_ASSERT(!S_UnitAbilityEvent(unit, A_NO_RETALIATE));
}

TEST(wc3_wander, separate_units_have_separate_deterministic_schedules) {
    edict_t *a, *b;
    reset_entities();
    setup_test_world();
    level.time = 1000;
    a = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), 0, 0);
    b = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), 500, 0);
    a->abilities.added[0] = b->abilities.added[0] = ID_AWAN;
    ARRAY_COUNT(a->abilities.added) = ARRAY_COUNT(b->abilities.added) = 1;
    T_ASSERT(S_UnitAbilityEvent(a, A_IDLE));
    T_ASSERT(S_UnitAbilityEvent(b, A_IDLE));
    T_NE(a->wander_random_state, b->wander_random_state);
    T_ASSERT(a->wander_next_time != 0 && b->wander_next_time != 0);
}
TEST(wc3_wander, scheduler_ticks_schedule_only_while_idle_and_eligible) {
    edict_t *unit;
    reset_entities();
    setup_test_world();
    level.time = 1000;
    unit = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), 0, 0);
    unit->svflags |= SVF_MONSTER;
    unit->stand = unit_stand;
    unit->abilities.added[0] = ID_AWAN;
    ARRAY_COUNT(unit->abilities.added) = 1;
    unit_stand(unit);
    unit->paused = true;
    monster_think(unit);
    T_EQ(unit->wander_next_time, 0);
    unit->paused = false;
    monster_think(unit);
    T_ASSERT(unit->wander_next_time >= level.time + 8000);
    T_ASSERT(unit->wander_next_time <= level.time + 10000);
    /* Accepted explicit orders retire ownership even without a move change. */
    unit->wander_goal = unit;
    unit->wander_goal_generation = 21;
    S_UnitAbilityOrderAccepted(unit, "holdposition");
    T_NULL(unit->wander_goal);
    T_EQ(unit->wander_goal_generation, 0);
}

TEST(wc3_wander, retreat_does_not_override_external_move) {
    edict_t *unit, *attacker, *goal;
    vec2_t destination = {256.0f, 0.0f};
    reset_entities();
    setup_test_world();
    level.time = 1000;
    unit = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), 0, 0);
    attacker = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), -64, 0);
    unit->abilities.added[0] = ID_AWAN;
    ARRAY_COUNT(unit->abilities.added) = 1;
    goal = Waypoint_add(&destination);
    order_move(unit, goal);
    T_ASSERT(move_is_active_order_walk(unit));
    {
        abilityCall_t call = MAKE(abilityCall_t, .attacker = attacker);
        S_UnitAbilityEventWithCall(unit, A_DAMAGED, &call);
    }
    T_ASSERT(unit->goalentity == goal);
    T_NULL(unit->wander_goal);
}

TEST(wc3_wander, recycled_waypoint_does_not_authorize_flee_override) {
    edict_t *unit, *attacker, *goal;
    vec2_t destination = {256.0f, 0.0f};
    reset_entities();
    setup_test_world();
    level.time = 1000;
    unit = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), 0, 0);
    attacker = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), -64, 0);
    unit->abilities.added[0] = ID_AWAN;
    ARRAY_COUNT(unit->abilities.added) = 1;
    goal = Waypoint_add(&destination);
    order_move(unit, goal);
    unit->wander_goal = goal;
    unit->wander_goal_generation = goal->waypoint_generation;
    /* Simulate a slot reused by the shared waypoint ring. */
    goal->waypoint_generation++;
    test_wander_damage(unit, attacker);
    T_ASSERT(unit->goalentity == goal);
}

TEST(wc3_wander, disable_cancels_only_owned_move) {
    edict_t *unit, *attacker, *owned_goal, *external_goal;
    vec2_t destination = {256.0f, 0.0f};
    reset_entities();
    setup_test_world();
    unit = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), 0, 0);
    attacker = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), -100, 0);
    unit->abilities.added[0] = ID_AWAN;
    ARRAY_COUNT(unit->abilities.added) = 1;
    test_wander_damage(unit, attacker);
    owned_goal = unit->wander_waypoint;
    T_NOT_NULL(owned_goal);
    T_ASSERT(unit->goalentity == owned_goal);
    T_ASSERT(move_is_active_order_walk(unit));
    S_DisableAbility(unit, ID_AWAN);
    T_NULL(unit->wander_waypoint);
    T_NULL(unit->wander_goal);
    T_ASSERT(!owned_goal->inuse);
    T_ASSERT(!move_is_active_order_walk(unit));

    /* Disabling Wander a second time must leave an unrelated Move alone. */
    external_goal = Waypoint_add(&destination);
    order_move(unit, external_goal);
    T_ASSERT(move_is_active_order_walk(unit));
    S_DisableAbility(unit, ID_AWAN);
    T_ASSERT(move_is_active_order_walk(unit));
    T_ASSERT(unit->goalentity == external_goal);
}

TEST(wc3_wander, damage_does_not_interrupt_explicit_non_move_order) {
    edict_t *unit, *attacker;
    umove_t explicit_attack = { "attack", NULL, NULL, CAbilityAttack };
    reset_entities();
    setup_test_world();
    unit = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), 0, 0);
    attacker = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), -100, 0);
    unit->abilities.added[0] = ID_AWAN;
    ARRAY_COUNT(unit->abilities.added) = 1;
    unit->currentmove = &explicit_attack;
    T_ASSERT(G_UnitHasActiveOrder(unit));
    test_wander_damage(unit, attacker);
    T_ASSERT(unit->currentmove == &explicit_attack);
    T_NULL(unit->wander_waypoint);
    T_NULL(unit->wander_goal);
}

TEST(wc3_wander, idle_does_not_start_while_an_explicit_order_is_active) {
    umove_t explicit_attack = { "attack", NULL, NULL, CAbilityAttack };
    edict_t *unit;
    reset_entities();
    setup_test_world();
    unit = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), 0, 0);
    unit->abilities.added[0] = ID_AWAN;
    ARRAY_COUNT(unit->abilities.added) = 1;
    unit->currentmove = &explicit_attack;
    T_ASSERT(G_UnitHasActiveOrder(unit));
    T_ASSERT(S_UnitAbilityEvent(unit, A_IDLE));
    T_EQ(unit->wander_next_time, 0);
    T_NULL(unit->wander_waypoint);
}

TEST(wc3_wander, active_goal_round_trips_with_generation) {
    cstring_t filename = "/tmp/openwarcraft3-wc3-wander-goal.bin";
    edict_t *unit, *goal;
    vec2_t destination = {256.0f, 0.0f};
    reset_entities();
    setup_test_world();
    unit = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), 0, 0);
    unit->abilities.added[0] = ID_AWAN;
    ARRAY_COUNT(unit->abilities.added) = 1;
    goal = Waypoint_add(&destination);
    order_move(unit, goal);
    unit->wander_goal = goal;
    unit->wander_goal_generation = goal->waypoint_generation;
    unit->wander_next_time = 9400;
    unit->wander_random_state = 12345;
    T_ASSERT(WriteGame(filename));
    unit->wander_goal = NULL;
    unit->wander_goal_generation = 0;
    unit->wander_next_time = 0;
    unit->wander_random_state = 0;
    T_ASSERT(ReadGame(filename));
    T_ASSERT(unit->wander_goal == goal);
    T_EQ(unit->wander_goal_generation, goal->waypoint_generation);
    T_EQ(unit->wander_next_time, 9400);
    T_EQ(unit->wander_random_state, 12345);
    remove(filename);
}

TEST(wc3_wander, private_destination_cannot_be_recycled_by_shared_ring) {
    edict_t *unit, *attacker, *private_goal;
    reset_entities();
    setup_test_world();
    unit = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), 0, 0);
    attacker = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), -100, 0);
    unit->abilities.added[0] = ID_AWAN;
    ARRAY_COUNT(unit->abilities.added) = 1;
    test_wander_damage(unit, attacker);
    private_goal = unit->wander_waypoint;
    T_NOT_NULL(private_goal);
    T_ASSERT(unit->goalentity == private_goal);
    vec2_t const before = private_goal->s.origin2;
    for (int i = 0; i < 512; i++) {
        vec2_t other = { (float)i * 3, 400.0f };
        (void)Waypoint_add(&other);
    }
    T_ASSERT(unit->goalentity == private_goal);
    T_FEQ(private_goal->s.origin2.x, before.x, 0.001f);
    T_FEQ(private_goal->s.origin2.y, before.y, 0.001f);
    /* Removing the ability ends only its own movement and frees the goal. */
    S_DisableAbility(unit, ID_AWAN);
    T_NULL(unit->wander_waypoint);
    T_NULL(unit->wander_goal);
    T_ASSERT(!move_is_active_order_walk(unit));
}

TEST(wc3_wander, stable_goal_survives_save_load) {
    cstring_t filename = "/tmp/openwarcraft3-wc3-wander-private.bin";
    edict_t *unit, *attacker, *goal;
    reset_entities();
    setup_test_world();
    unit = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), 0, 0);
    attacker = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), -100, 0);
    unit->abilities.added[0] = ID_AWAN;
    ARRAY_COUNT(unit->abilities.added) = 1;
    test_wander_damage(unit, attacker);
    goal = unit->wander_waypoint;
    T_NOT_NULL(goal);
    T_ASSERT(WriteGame(filename));
    T_ASSERT(ReadGame(filename));
    T_ASSERT(unit->wander_waypoint == goal);
    T_ASSERT(unit->wander_goal == goal);
    T_ASSERT(unit->goalentity == goal);
    T_EQ(unit->wander_goal_generation, goal->waypoint_generation);
    remove(filename);
}

TEST(wc3_wander, disabling_after_ownership_retired_frees_stale_private_goal) {
    edict_t *unit, *attacker, *goal;
    reset_entities();
    setup_test_world();
    unit = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), 0, 0);
    attacker = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), -100, 0);
    unit->abilities.added[0] = ID_AWAN;
    ARRAY_COUNT(unit->abilities.added) = 1;
    test_wander_damage(unit, attacker);
    goal = unit->wander_waypoint;
    T_NOT_NULL(goal);
    /* Model an accepted-order transition that retires ownership while the
     * old Move still references the private destination. */
    S_UnitAbilityOrderAccepted(unit, "stop");
    T_NULL(unit->wander_goal);
    T_ASSERT(unit->goalentity == goal);
    S_DisableAbility(unit, ID_AWAN);
    T_NULL(unit->wander_waypoint);
    T_ASSERT(unit->goalentity != goal);
    T_ASSERT(!goal->inuse);
    T_ASSERT(!move_is_active_order_walk(unit));
}

TEST(wc3_wander, ordinary_external_move_survives_private_goal_cleanup) {
    edict_t *unit, *attacker, *goal;
    vec2_t destination = { 250.0f, 100.0f };
    reset_entities();
    setup_test_world();
    unit = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), 0, 0);
    attacker = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), -100, 0);
    unit->abilities.added[0] = ID_AWAN;
    ARRAY_COUNT(unit->abilities.added) = 1;
    test_wander_damage(unit, attacker);
    T_NOT_NULL(unit->wander_waypoint);
    goal = Waypoint_add(&destination);
    order_move(unit, goal);
    T_ASSERT(unit->goalentity == goal);
    S_DisableAbility(unit, ID_AWAN);
    T_NULL(unit->wander_waypoint);
    T_ASSERT(unit->goalentity == goal);
    T_ASSERT(move_is_active_order_walk(unit));
}

TEST(wc3_wander, scheduler_starts_actual_wander_move) {
    edict_t *unit;
    reset_entities();
    setup_test_world();
    level.time = 1000;
    unit = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), 0, 0);
    unit->svflags |= SVF_MONSTER;
    unit->stand = unit_stand;
    unit->abilities.added[0] = ID_AWAN;
    ARRAY_COUNT(unit->abilities.added) = 1;
    unit_stand(unit);
    monster_think(unit);
    T_ASSERT(unit->wander_next_time > level.time);
    level.time = unit->wander_next_time;
    monster_think(unit);
    /* This fixture has no guaranteed walkable terrain. A rejected destination
     * must leave the unit standing with a future retry, rather than claiming
     * that a manually forced stand proves a successful pathfinding arrival. */
    if (unit->wander_waypoint) {
        T_ASSERT(unit->goalentity == unit->wander_waypoint);
        T_ASSERT(move_is_active_order_walk(unit));
    } else {
        T_ASSERT(unit->wander_next_time > level.time);
    }
    S_DisableAbility(unit, ID_AWAN);
    T_NULL(unit->wander_waypoint);
}

TEST(wc3_wander, combat_damage_uses_wander_defensive_response) {
    edict_t *unit, *attacker;
    float before;
    reset_entities();
    setup_test_world();
    unit = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), 0, 0);
    attacker = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), -100, 0);
    unit->abilities.added[0] = ID_AWAN;
    ARRAY_COUNT(unit->abilities.added) = 1;
    before = unit->health.value;
    T_ASSERT(before > 1);
    T_Damage(unit, attacker, 1);
    T_ASSERT(unit->health.value < before);
    T_ASSERT(S_UnitAbilityEvent(unit, A_NO_RETALIATE));
    T_NOT_NULL(unit->wander_waypoint);
    T_ASSERT(unit->goalentity == unit->wander_waypoint);
    T_ASSERT(move_is_active_order_walk(unit));
}

TEST(wc3_wander, blocked_private_move_returns_idle_after_ownership_retirement) {
    edict_t *unit, *attacker, *goal;
    reset_entities();
    setup_test_world();
    unit = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), 0, 0);
    attacker = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), -100, 0);
    unit->abilities.added[0] = ID_AWAN;
    ARRAY_COUNT(unit->abilities.added) = 1;
    test_wander_damage(unit, attacker);
    goal = unit->wander_waypoint;
    T_NOT_NULL(goal);
    T_ASSERT(move_is_active_order_walk(unit));
    /* Simulate A_MOVE_LEAVE/accepted-order ownership retirement without
     * changing the in-flight target. Recovery must use the private goal. */
    S_UnitAbilityOrderAccepted(unit, "stop");
    T_NULL(unit->wander_goal);
    T_ASSERT(test_wander_blocked_move(unit));
    T_ASSERT(!move_is_active_order_walk(unit));
    T_NULL(unit->goalentity);
    T_ASSERT(unit->wander_waypoint == goal && goal->inuse);
    T_ASSERT(!test_wander_blocked_move(unit));
    T_ASSERT(unit->wander_next_time > level.time);
    S_DisableAbility(unit, ID_AWAN);
    T_ASSERT(!goal->inuse);
}

TEST(wc3_wander, blocked_recovery_does_not_intercept_external_move) {
    edict_t *unit, *attacker, *other;
    vec2_t destination = { 400.0f, 60.0f };
    reset_entities();
    setup_test_world();
    unit = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), 0, 0);
    attacker = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), -100, 0);
    unit->abilities.added[0] = ID_AWAN;
    ARRAY_COUNT(unit->abilities.added) = 1;
    test_wander_damage(unit, attacker);
    T_NOT_NULL(unit->wander_waypoint);
    other = Waypoint_add(&destination);
    order_move(unit, other);
    T_ASSERT(!test_wander_blocked_move(unit));
    T_ASSERT(move_is_active_order_walk(unit));
    T_ASSERT(unit->goalentity == other);
    S_DisableAbility(unit, ID_AWAN);
    T_ASSERT(unit->goalentity == other);
}

TEST(wc3_wander, repeated_damage_retargets_private_goal_without_leak) {
    edict_t *unit, *attacker, *goal;
    uint32_t generation;
    reset_entities();
    setup_test_world();
    unit = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), 0, 0);
    attacker = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), -100, 0);
    unit->abilities.added[0] = ID_AWAN;
    ARRAY_COUNT(unit->abilities.added) = 1;
    test_wander_damage(unit, attacker);
    goal = unit->wander_waypoint;
    T_NOT_NULL(goal);
    generation = goal->waypoint_generation;
    test_wander_damage(unit, attacker);
    T_ASSERT(unit->wander_waypoint == goal);
    T_ASSERT(unit->goalentity == goal);
    T_ASSERT(move_is_active_order_walk(unit));
    T_ASSERT(goal->waypoint_generation != generation);
    S_DisableAbility(unit, ID_AWAN);
    T_ASSERT(!goal->inuse);
}

TEST(wc3_wander, saved_private_destination_continues_after_reload) {
    cstring_t filename = "/tmp/openwarcraft3-wc3-wander-continue.bin";
    edict_t *unit, *attacker, *goal;
    reset_entities();
    setup_test_world();
    unit = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), 0, 0);
    attacker = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), -100, 0);
    unit->abilities.added[0] = ID_AWAN;
    ARRAY_COUNT(unit->abilities.added) = 1;
    test_wander_damage(unit, attacker);
    goal = unit->wander_waypoint;
    T_NOT_NULL(goal);
    T_ASSERT(WriteGame(filename));
    T_ASSERT(ReadGame(filename));
    T_ASSERT(unit->wander_waypoint == goal);
    T_ASSERT(unit->goalentity == goal);
    T_ASSERT(move_is_active_order_walk(unit));
    /* Let the actual movement procedure run after restoring the live order.
     * A synthetic world may lack routes; it must never reuse/free the goal
     * while the mover still references it. */
    unit->svflags |= SVF_MONSTER;
    unit->stand = unit_stand;
    for (int tick = 0; tick < 8 && move_is_active_order_walk(unit); tick++) {
        level.time += 50;
        monster_think(unit);
        T_ASSERT(goal->inuse);
        T_ASSERT(unit->wander_waypoint == goal);
    }
    S_DisableAbility(unit, ID_AWAN);
    T_NULL(unit->wander_waypoint);
    T_ASSERT(!goal->inuse);
    remove(filename);
}


/* The all-walkable synthetic map lets the real Move think callback complete a
 * short private destination. Do not substitute unit_stand() for arrival. */
static void wander_tick_until_arrival(edict_t *unit, edict_t *goal) {
    bool translated = false;
    vec2_t const start = unit->s.origin2;
    for (int tick = 0; tick < 400 && move_is_active_order_walk(unit); ++tick) {
        level.time += 50;
        monster_think(unit);
        if (Vector2_distance(&unit->s.origin2, &start) > 0.01f)
            translated = true;
        T_ASSERT(goal->inuse);
    }
    T_ASSERT(translated);
    T_ASSERT(!move_is_active_order_walk(unit));
    T_ASSERT(unit->wander_waypoint == goal);
    T_NULL(unit->wander_goal);
}

TEST(wc3_wander, real_move_arrival_then_scheduler_rearms_wander) {
    edict_t *unit, *attacker, *goal;
    reset_entities();
    setup_test_world();
    level.time = 1000;
    unit = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), 0, 0);
    attacker = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), -100, 0);
    unit->svflags |= SVF_MONSTER;
    unit->stand = unit_stand;
    unit->unitinfo.MoveSpeed = 320.0f;
    unit->abilities.added[0] = ID_AWAN;
    ARRAY_COUNT(unit->abilities.added) = 1;
    test_wander_damage(unit, attacker);
    goal = unit->wander_waypoint;
    T_NOT_NULL(goal);
    T_ASSERT(move_is_active_order_walk(unit));
    wander_tick_until_arrival(unit, goal);
    T_ASSERT(unit->wander_next_time > level.time);
    level.time = unit->wander_next_time;
    monster_think(unit);
    T_ASSERT(unit->wander_next_time > level.time);
    T_ASSERT(unit->wander_waypoint == goal);
    S_DisableAbility(unit, ID_AWAN);
    T_ASSERT(!goal->inuse);
}

TEST(wc3_wander, save_load_move_reaches_goal_and_rearms) {
    cstring_t filename = "/tmp/openwarcraft3-wc3-wander-arrival.bin";
    edict_t *unit, *attacker, *goal;
    reset_entities();
    setup_test_world();
    level.time = 1000;
    unit = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), 0, 0);
    attacker = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), -100, 0);
    unit->svflags |= SVF_MONSTER;
    unit->stand = unit_stand;
    unit->unitinfo.MoveSpeed = 320.0f;
    unit->abilities.added[0] = ID_AWAN;
    ARRAY_COUNT(unit->abilities.added) = 1;
    test_wander_damage(unit, attacker);
    goal = unit->wander_waypoint;
    T_NOT_NULL(goal);
    T_ASSERT(WriteGame(filename));
    T_ASSERT(ReadGame(filename));
    T_ASSERT(unit->wander_waypoint == goal);
    T_ASSERT(unit->goalentity == goal);
    T_ASSERT(move_is_active_order_walk(unit));
    wander_tick_until_arrival(unit, goal);
    T_ASSERT(unit->wander_next_time > level.time);
    S_DisableAbility(unit, ID_AWAN);
    T_ASSERT(!goal->inuse);
    remove(filename);
}

/* A genuine blocked route: the destination was free when chosen, then another
 * unit parks on it. The critter stops at contact inside Move's near-goal settle
 * band, Move's own blocked-frame watermark reaches terminal Hold, and
 * A_MOVE_BLOCKED hands the unit back to idle with a fresh deadline. */
TEST(wc3_wander, frame_scheduler_recovers_from_dynamically_blocked_route) {
    edict_t *unit, *attacker, *blocker, *goal;
    vec2_t destination;
    uint32_t retry;
    wander_setup_frame_world();
    unit = make_scheduled_critter(0, 0);
    attacker = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), -100, 0);
    wander_run_frames(1);
    T_Damage(unit, attacker, 1);
    goal = unit->wander_waypoint;
    T_NOT_NULL(goal);
    T_ASSERT(move_is_active_order_walk(unit));
    destination = goal->s.origin2;
    T_ASSERT(Vector2_distance(&destination, &unit->s.origin2) > 100.0f);
    /* Radius 24: contact at 40 units is beyond the 32 + 4 arrival corridor
     * but inside the 32 + 16 + 8 settle band, so Move cannot snap onto the
     * goal and must settle as blocked. */
    blocker = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), destination.x, destination.y);
    blocker->s.model = 1; /* IS_HOLLOW() ignores model-less edicts for collision */
    blocker->collision = 24.0f;
    blocker->bounds = MAKE(box2_t, .min = {destination.x - 24.0f, destination.y - 24.0f},
                                   .max = {destination.x + 24.0f, destination.y + 24.0f});
    gi.LinkEntity(blocker);

    wander_run_frames_until_standing(unit);
    T_ASSERT(!move_is_active_order_walk(unit));
    T_ASSERT(!move_is_terminal_hold(unit));
    T_ASSERT(Vector2_distance(&unit->s.origin2, &destination) >= 40.0f - 0.5f);
    T_ASSERT(Vector2_distance(&unit->s.origin2, &destination) < 100.0f);
    T_ASSERT(unit->wander_waypoint == goal && goal->inuse);
    T_NULL(unit->wander_goal);
    T_NULL(unit->goalentity);
    /* Terminal blockage starts a new pause instead of retrying on the next
     * idle tick, even though the in-flight deadline may already have passed. */
    T_ASSERT(unit->wander_next_time > level.time);
    retry = unit->wander_next_time;
    wander_run_frames(1);
    T_EQ(unit->wander_next_time, retry);
    T_ASSERT(!move_is_active_order_walk(unit));
    S_DisableAbility(unit, ID_AWAN);
    T_ASSERT(!goal->inuse);
}

/* Idle scheduling through the real server frame: the first idle tick arms
 * the 8-10 s deadline, nothing moves before it, the Move begins at it, and
 * arrival re-arms the next deadline. */
TEST(wc3_wander, frame_scheduler_starts_wander_after_delay_and_rearms) {
    edict_t *unit;
    vec2_t start;
    uint32_t armed_at;
    wander_setup_frame_world();
    unit = make_scheduled_critter(0, 0);
    start = unit->s.origin2;
    wander_run_frames(1);
    armed_at = level.time;
    T_ASSERT(unit->wander_next_time >= armed_at + 8000);
    T_ASSERT(unit->wander_next_time <= armed_at + 10000);
    while (level.time + FRAMETIME < unit->wander_next_time) {
        wander_run_frames(1);
        if (move_is_active_order_walk(unit)) break;
    }
    T_ASSERT(!move_is_active_order_walk(unit));
    T_ASSERT(Vector2_distance(&unit->s.origin2, &start) < 0.01f);
    for (int attempt = 0; attempt < 3 && !move_is_active_order_walk(unit); attempt++) {
        while (level.time < unit->wander_next_time && !move_is_active_order_walk(unit)) wander_run_frames(1);
        wander_run_frames(1);
    }
    T_ASSERT(move_is_active_order_walk(unit));
    T_ASSERT(level.time >= armed_at + 8000);
    T_NOT_NULL(unit->wander_waypoint);
    T_ASSERT(unit->goalentity == unit->wander_waypoint);
    T_ASSERT(unit->wander_goal == unit->wander_waypoint);
    wander_run_frames_until_standing(unit);
    T_ASSERT(!move_is_active_order_walk(unit));
    T_ASSERT(Vector2_distance(&unit->s.origin2, &start) > 16.0f);
    T_NULL(unit->wander_goal);
    T_ASSERT(unit->wander_waypoint && unit->wander_waypoint->inuse);
    T_ASSERT(unit->wander_next_time > level.time);
    S_DisableAbility(unit, ID_AWAN);
    T_NULL(unit->wander_waypoint);
}

/* A player's Move order outlives every wander deadline and a real hit. */
TEST(wc3_wander, frame_scheduler_does_not_override_player_move) {
    edict_t *unit, *attacker, *goal;
    vec2_t destination = { 900.0f, 0.0f };
    wander_setup_frame_world();
    unit = make_scheduled_critter(0, 0);
    attacker = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), -100, 0);
    unit->unitinfo.MoveSpeed = 40.0f; /* 4 units per frame: the order lasts well past 10 s */
    goal = Waypoint_add(&destination);
    order_move(unit, goal);
    wander_run_frames(60);
    T_ASSERT(move_is_active_order_walk(unit));
    T_Damage(unit, attacker, 1);
    wander_run_frames(60);
    T_ASSERT(move_is_active_order_walk(unit));
    T_ASSERT(unit->goalentity == goal);
    T_NULL(unit->wander_waypoint);
    T_NULL(unit->wander_goal);
    T_ASSERT(unit->s.origin2.x > 200.0f);
}

/* The landed-attack path (S_ResolveAttackHit -> T_Damage -> A_DAMAGED) makes
 * an idle critter flee away from the attacker, finish that Move through the
 * scheduler without retaliating, and re-arm wandering. */
TEST(wc3_wander, frame_scheduler_flees_landed_attack_and_rearms) {
    edict_t *unit, *attacker;
    float before;
    wander_setup_frame_world();
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[PLAYER_NEUTRAL_PASSIVE].playerType = kPlayerTypeNeutral;
    unit = make_scheduled_critter(0, 0);
    unit->s.player = PLAYER_NEUTRAL_PASSIVE;
    attacker = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), -100, 0);
    attacker->s.player = 0;
    attacker->svflags |= SVF_MONSTER;
    wander_run_frames(1);
    before = unit->health.value;
    S_ResolveAttackHit(attacker, unit, 5);
    T_ASSERT(unit->health.value < before);
    T_ASSERT(move_is_active_order_walk(unit));
    T_NOT_NULL(unit->wander_waypoint);
    T_ASSERT(unit->goalentity == unit->wander_waypoint);
    T_ASSERT(unit->wander_waypoint->s.origin2.x > 100.0f);
    wander_run_frames_until_standing(unit);
    T_ASSERT(!move_is_active_order_walk(unit));
    T_ASSERT(unit->s.origin2.x > 100.0f);
    T_ASSERT(!G_UnitHasActiveOrder(unit));
    T_ASSERT(unit->wander_next_time > level.time);
    S_DisableAbility(unit, ID_AWAN);
}


/* Drive autonomous decisions through monster_think: no damage/flee helper or
 * synthetic stand/arrival calls. An all-walkable test map is supplied by the
 * existing shared fixture. */
TEST(wc3_wander, autonomous_idle_move_arrival_second_move) {
    edict_t *unit;
    vec2_t initial, first_arrival;
    uint32_t first_generation;
    reset_entities();
    setup_test_world();
    level.time = 1000;
    unit = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), 0, 0);
    unit->svflags |= SVF_MONSTER;
    unit->stand = unit_stand;
    unit->unitinfo.MoveSpeed = 320.0f;
    unit->abilities.added[0] = ID_AWAN;
    ARRAY_COUNT(unit->abilities.added) = 1;
    unit_stand(unit);
    initial = unit->s.origin2;
    monster_think(unit);
    T_ASSERT(unit->wander_next_time > level.time);
    for (int attempt = 0; attempt < 8 && !move_is_active_order_walk(unit); ++attempt) {
        level.time = unit->wander_next_time;
        monster_think(unit);
    }
    T_ASSERT(move_is_active_order_walk(unit));
    T_NOT_NULL(unit->wander_waypoint);
    T_ASSERT(unit->goalentity == unit->wander_waypoint);
    first_generation = unit->wander_waypoint->waypoint_generation;
    wander_tick_until_arrival(unit, unit->wander_waypoint);
    first_arrival = unit->s.origin2;
    T_ASSERT(Vector2_distance(&first_arrival, &initial) > 0.01f);
    T_ASSERT(unit->wander_next_time > level.time);
    for (int attempt = 0; attempt < 8 && !move_is_active_order_walk(unit); ++attempt) {
        level.time = unit->wander_next_time;
        monster_think(unit);
    }
    T_ASSERT(move_is_active_order_walk(unit));
    T_ASSERT(unit->wander_waypoint->waypoint_generation != first_generation);
    wander_tick_until_arrival(unit, unit->wander_waypoint);
    T_ASSERT(Vector2_distance(&unit->s.origin2, &first_arrival) > 0.01f);
    S_DisableAbility(unit, ID_AWAN);
    T_NULL(unit->wander_waypoint);
}

TEST(wc3_wander, autonomous_move_leaves_lumber_mill_collision_edge) {
    edict_t *mill, *unit;
    vec2_t const origin = { 116.0f, 0.0f };
    float const contact_distance = 116.0f;
    reset_entities();
    setup_test_world();
    level.time = 1000;
    mill = alloc_test_unit(MAKEFOURCC('h', 'l', 'u', 'm'), 0, 0);
    unit = alloc_test_unit(MAKEFOURCC('n', 'w', 'l', 't'), origin.x, origin.y);
    mill->s.player = unit->s.player = 0;
    mill->collision = 100.0f;
    mill->bounds = MAKE(box2_t, .min = {-100.0f, -100.0f}, .max = {100.0f, 100.0f});
    mill->runtime.flags |= UNIT_BALANCE_BUILDING;
    mill->s.flags |= EF_BUILDING;
    unit->collision = 16.0f;
    unit->unitinfo.MoveSpeed = 320.0f;
    unit->stand = unit_stand;
    unit->svflags |= SVF_MONSTER;
    unit->abilities.added[0] = ID_AWAN;
    ARRAY_COUNT(unit->abilities.added) = 1;
    gi.LinkEntity(mill);
    gi.LinkEntity(unit);
    T_FEQ(Vector2_distance(&unit->s.origin2, &mill->s.origin2), contact_distance, 0.01f);
    unit_stand(unit);
    unit->wander_random_state = 529;
    unit->wander_next_time = level.time;
    monster_think(unit);
    T_ASSERT(move_is_active_order_walk(unit));
    T_NOT_NULL(unit->wander_waypoint);
    T_ASSERT(Vector2_distance(&unit->wander_waypoint->s.origin2, &mill->s.origin2) > contact_distance + 32.0f);
    wander_tick_until_arrival(unit, unit->wander_waypoint);
    T_ASSERT(Vector2_distance(&unit->s.origin2, &mill->s.origin2) > contact_distance + 1.0f);
    S_DisableAbility(unit, ID_AWAN);
}

/* Alias discovery uses authored AbilityData, rather than matching only Awan
 * on the unit. Disable must release the exact custom-code ability's state. */
TEST(wc3_wander, custom_wander_alias_enable_disable_lifecycle) {
    char const slk[] =
        "ID;PWXL;N;E\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y2;X1;K\"Awan\"\nC;Y2;X2;K\"Awan\"\n"
        "C;Y3;X1;K\"A0wn\"\nC;Y3;X2;K\"Awan\"\nE\n";
    uint32_t const alias = MAKEFOURCC('A', '0', 'w', 'n');
    slkTestData_t *rows = parse_slk_string(slk);
    slkTestData_t *old = G_SetSLKRows("AbilityData", rows);
    edict_t *unit;
    reset_entities();
    setup_test_world();
    level.time = 1000;
    unit = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), 0, 0);
    unit->svflags |= SVF_MONSTER;
    unit->stand = unit_stand;
    unit->abilities.added[0] = alias;
    ARRAY_COUNT(unit->abilities.added) = 1;
    S_EnableAbility(unit, alias);
    unit_stand(unit);
    monster_think(unit);
    T_ASSERT(unit->wander_next_time > level.time);
    T_ASSERT(S_UnitAbilityEvent(unit, A_NO_RETALIATE));
    S_DisableAbility(unit, alias);
    T_EQ(unit->wander_next_time, 0);
    T_NULL(unit->wander_waypoint);
    unit->abilities.added[0] = 0;
    ARRAY_COUNT(unit->abilities.added) = 0;
    T_ASSERT(!S_UnitAbilityEvent(unit, A_NO_RETALIATE));
    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

/* The flee direction is deterministic (away from the attacker), so a building
 * placed on that line shows exactly which retreat distance the clearance rule
 * accepted. A destroyed structure still occupies its edict but no longer
 * blocks pathing, so it must not push the destination back. */
TEST(wc3_wander, destroyed_building_does_not_restrict_wander_destination) {
    edict_t *mill, *unit, *attacker;
    reset_entities();
    setup_test_world();
    level.time = 1000;
    /* 192-unit retreat lands 138 units from the mill centre: inside the
     * 16 + 100 + 32 clearance band, outside the 100 + 16 collision contact. */
    mill = alloc_test_unit(MAKEFOURCC('h', 'l', 'u', 'm'), 330.0f, 0.0f);
    unit = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), 0.0f, 0.0f);
    attacker = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), -100.0f, 0.0f);
    mill->collision = 100.0f;
    mill->bounds = MAKE(box2_t, .min = {230.0f, -100.0f}, .max = {430.0f, 100.0f});
    mill->runtime.flags |= UNIT_BALANCE_BUILDING;
    mill->s.flags |= EF_BUILDING;
    unit->collision = 16.0f;
    unit->abilities.added[0] = ID_AWAN;
    ARRAY_COUNT(unit->abilities.added) = 1;
    gi.LinkEntity(mill);
    gi.LinkEntity(unit);

    test_wander_damage(unit, attacker);
    T_NOT_NULL(unit->wander_waypoint);
    T_FEQ(unit->wander_waypoint->s.origin2.x, 96.0f, 0.01f);

    /* Model a destroyed structure as KillUnit/unit_die leave it. */
    mill->health.value = 0.0f;
    mill->svflags |= SVF_DEADMONSTER;
    test_wander_damage(unit, attacker);
    T_FEQ(unit->wander_waypoint->s.origin2.x, 192.0f, 0.01f);
    S_DisableAbility(unit, ID_AWAN);
}

static uint32_t wander_lethal_hit_edict_growth(bool with_wander) {
    edict_t *unit, *attacker;
    uint32_t before;
    reset_entities();
    setup_test_world();
    level.time = 1000;
    unit = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), 0.0f, 0.0f);
    attacker = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), -100.0f, 0.0f);
    unit->svflags |= SVF_MONSTER;
    unit->stand = unit_stand;
    unit->die = unit_die;
    if (with_wander) {
        unit->abilities.added[0] = ID_AWAN;
        ARRAY_COUNT(unit->abilities.added) = 1;
    }
    unit_stand(unit);
    before = globals.num_edicts;
    T_Damage(unit, attacker, (int)ceilf(unit->health.value));
    T_ASSERT(M_IsDead(unit));
    T_NULL(unit->wander_waypoint);
    T_ASSERT(!move_is_active_order_walk(unit));
    return globals.num_edicts - before;
}

/* A killing blow must not start a flee Move whose private waypoint death
 * immediately tears down again. Compare edict growth against a unit without
 * Awan so whatever unit_die itself allocates is factored out. */
TEST(wc3_wander, lethal_hit_does_not_spawn_flee_waypoint) {
    uint32_t const plain = wander_lethal_hit_edict_growth(false);
    T_EQ(wander_lethal_hit_edict_growth(true), plain);
}

#endif
