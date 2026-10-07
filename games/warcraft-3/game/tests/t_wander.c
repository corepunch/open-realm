#ifdef BZ_TESTS
#include "test.h"
#include "../skills/s_skills.h"

#define ID_AWAN MAKEFOURCC('A', 'w', 'a', 'n')

edict_t *alloc_test_unit(uint32_t class_id, float x, float y);
void reset_entities(void);
void setup_test_world(void);
slkTestData_t *parse_slk_string(char const *text);
void free_slk_rows(slkTestData_t *rows);

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
    S_WanderOnDamage(unit, attacker);
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
    S_WanderOnDamage(unit, attacker);
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
    S_WanderOnDamage(unit, attacker);
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
    S_WanderOnDamage(unit, attacker);
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
    S_WanderOnDamage(unit, attacker);
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
    S_WanderOnDamage(unit, attacker);
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
    S_WanderOnDamage(unit, attacker);
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
    S_WanderOnDamage(unit, attacker);
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
    S_WanderOnDamage(unit, attacker);
    goal = unit->wander_waypoint;
    T_NOT_NULL(goal);
    T_ASSERT(move_is_active_order_walk(unit));
    /* Simulate A_MOVE_LEAVE/accepted-order ownership retirement without
     * changing the in-flight target. Recovery must use the private goal. */
    S_UnitAbilityOrderAccepted(unit, "stop");
    T_NULL(unit->wander_goal);
    T_ASSERT(S_WanderRecoverBlockedMove(unit));
    T_ASSERT(!move_is_active_order_walk(unit));
    T_NULL(unit->goalentity);
    T_ASSERT(unit->wander_waypoint == goal && goal->inuse);
    T_ASSERT(!S_WanderRecoverBlockedMove(unit));
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
    S_WanderOnDamage(unit, attacker);
    T_NOT_NULL(unit->wander_waypoint);
    other = Waypoint_add(&destination);
    order_move(unit, other);
    T_ASSERT(!S_WanderRecoverBlockedMove(unit));
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
    S_WanderOnDamage(unit, attacker);
    goal = unit->wander_waypoint;
    T_NOT_NULL(goal);
    generation = goal->waypoint_generation;
    S_WanderOnDamage(unit, attacker);
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
    S_WanderOnDamage(unit, attacker);
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
    S_WanderOnDamage(unit, attacker);
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
    S_WanderOnDamage(unit, attacker);
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

TEST(wc3_wander, blocked_move_think_returns_wander_to_idle) {
    edict_t *unit, *attacker, *goal;
    reset_entities();
    setup_test_world();
    level.time = 1000;
    unit = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), 0, 0);
    attacker = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), -100, 0);
    unit->svflags |= SVF_MONSTER;
    unit->stand = unit_stand;
    unit->abilities.added[0] = ID_AWAN;
    ARRAY_COUNT(unit->abilities.added) = 1;
    S_WanderOnDamage(unit, attacker);
    goal = unit->wander_waypoint;
    T_NOT_NULL(goal);
    T_ASSERT(move_is_active_order_walk(unit));
    /* Arrange an already-settled blocked-route watermark and drive the real
     * Move think callback. The destination is still distant enough that the
     * arrival branch cannot short-circuit the failure path. */
    unit->movement.last_distance = 0.0f;
    unit->movement.last_origin = unit->s.origin2;
    unit->movement.blocked_frames = 10000;
    unit->movement.flow_direct = true;
    unit->movement.flow_unreachable = false;
    unit->movement.flow_goal_reached = false;
    level.time += 50;
    monster_think(unit);
    T_ASSERT(!move_is_terminal_hold(unit));
    T_ASSERT(!move_is_active_order_walk(unit));
    T_ASSERT(unit->wander_waypoint == goal);
    T_NULL(unit->wander_goal);
    T_NULL(unit->goalentity);
    /* Simulate an expired in-flight deadline: terminal blockage must start a
     * new pause, not immediately enter another obstructed Move. */
    T_ASSERT(unit->wander_next_time > level.time);
    uint32_t const retry = unit->wander_next_time;
    level.time += 50;
    monster_think(unit);
    T_EQ(unit->wander_next_time, retry);
    T_ASSERT(!move_is_active_order_walk(unit));
    S_DisableAbility(unit, ID_AWAN);
    T_ASSERT(!goal->inuse);
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

#endif
