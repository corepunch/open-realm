#ifdef BZ_TESTS
#include "test.h"
#include "../skills/s_skills.h"

#define LS_ABILITY MAKEFOURCC('A','U','l','s')
#define LS_UNIT MAKEFOURCC('h','f','o','o') /* non-stock UnitID tests data resolution */

/* DataA=3, DataB=0.25, Dur=5 and UnitID=hfoo intentionally differ from
 * stock AUls 20/0.2/30/uloc. The cast must use the production ability row. */
static char const locust_test_slk[] =
    "ID;PWXL;N;EBB;Y4;X14\n"
    "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
    "C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"Dur1\"\nC;Y1;X6;K\"DataA1\"\n"
    "C;Y1;X7;K\"DataB1\"\nC;Y1;X8;K\"DataC1\"\nC;Y1;X9;K\"DataD1\"\n"
    "C;Y1;X10;K\"DataE1\"\nC;Y1;X11;K\"UnitID1\"\nC;Y1;X12;K\"Area1\"\n"
    "C;Y1;X13;K\"Cost1\"\nC;Y1;X14;K\"Cool1\"\n"
    "C;Y2;X1;K\"AUls\"\nC;Y2;X2;K\"AUls\"\nC;Y2;X3;K1\n"
    "C;Y2;X4;K\"\"\nC;Y2;X5;K5\nC;Y2;X6;K3\nC;Y2;X7;K0.25\n"
    "C;Y2;X8;K2\nC;Y2;X9;K0.5\nC;Y2;X10;K13\nC;Y2;X11;K\"hfoo\"\n"
    "C;Y2;X12;K420\nC;Y2;X13;K0\nC;Y2;X14;K0\n"
    "C;Y3;X1;K\"AOls\"\nC;Y3;X2;K\"AOls\"\nC;Y3;X3;K1\n"
    "C;Y3;X4;K\"\"\nC;Y3;X5;K8\nC;Y3;X6;K2\nC;Y3;X7;K0.5\n"
    "C;Y3;X8;K1\nC;Y3;X9;K0.25\nC;Y3;X10;K7\nC;Y3;X11;K\"hfoo\"\n"
    "C;Y3;X12;K300\nC;Y3;X13;K0\nC;Y3;X14;K0\nE\n";

static UnitWeapons_t const locust_test_weapon = { .attacksEnabled = 1 };

typedef struct { slkTestData_t *rows, *old; edict_t *caster; } locustFix_t;

static locustFix_t locust_setup(void) {
    locustFix_t fix;
    reset_entities(); setup_test_world(); level.time = 1000;
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    fix.rows = parse_slk_string(locust_test_slk);
    fix.old = G_SetSLKRows("AbilityData", fix.rows);
    fix.caster = alloc_test_unit(MAKEFOURCC('U','c','r','l'), 256, 256);
    fix.caster->s.player = 0;
    fix.caster->svflags |= SVF_MONSTER;
    fix.caster->targtype = TARG_GROUND;
    fix.caster->mana.value = fix.caster->mana.max_value = 300;
    fix.caster->health.value = fix.caster->health.max_value = 500;
    fix.caster->unit_state->ranks[0] = MAKE(heroability_t, .code = LS_ABILITY, .level = 1);
    return fix;
}
static void locust_done(locustFix_t fix) {
    G_SetSLKRows("AbilityData", fix.old);
    free_slk_rows(fix.rows);
}
static edict_t *locust_controller(edict_t const *caster) {
    FILTER_EDICTS(ent, ent->inuse && ent->think == locust_swarm_think && ent->owner == caster)
        return ent;
    return NULL;
}
static uint32_t locust_member_count(edict_t const *controller) {
    uint32_t count = 0;
    FILTER_EDICTS(ent, ent->inuse && G_GetLocustMember(ent)) {
        locustMember_t const *member = G_GetLocustMember(ent);
        if (member->controller == controller &&
            member->controller_spawn_time == controller->spawn_time) count++;
    }
    return count;
}

static uint32_t locust_all_members(void) {
    uint32_t count = 0;
    FILTER_EDICTS(ent, ent->inuse && G_GetLocustMember(ent)) count++;
    return count;
}

TEST(wc3_spell, locust_swarm_release_uses_authored_schedule_and_current_caster_position) {
    locustFix_t fix = locust_setup();
    abilityitem_t item = S_AbilityItem(LS_ABILITY);
    abilityCall_t call = MAKE(abilityCall_t, .item = &item);
    edict_t *controller, *first = NULL, *second = NULL;
    locustSwarm_t *state;
    T_NOT_NULL(item.ability);
    T_ASSERT(!(item.ability->flags & AB_CHANNEL));
    T_ASSERT(S_AbilityMessage(fix.caster, A_VALIDATE, &call));
    T_ASSERT(S_AbilityMessage(fix.caster, A_EXECUTE, &call));
    controller = locust_controller(fix.caster);
    T_NOT_NULL(controller);
    if (!controller) goto cleanup;
    state = G_GetLocustSwarm(controller);
    T_NOT_NULL(state);
    if (!state) goto cleanup;
    T_EQ(state->total, 3u);
    T_EQ(state->release_interval_ms, 250u);
    T_EQ(state->end_ms, 6000u);
    T_EQ(state->unit_id, LS_UNIT);
    T_FEQ(state->area, 420.0f, 0.001f);
    T_FEQ(state->target_cap, 2.0f, 0.001f);
    T_FEQ(state->heal_factor, 0.5f, 0.001f);
    T_FEQ(state->return_threshold, 13.0f, 0.001f);
    T_EQ(locust_member_count(controller), 0u);
    level.time = 1000; G_RunEntities();
    T_EQ(locust_member_count(controller), 1u);
    FILTER_EDICTS(ent, G_GetLocustMember(ent) && G_GetLocustMember(ent)->controller == controller) {
        first = ent; break;
    }
    T_NOT_NULL(first);
    if (!first) goto cleanup;
    T_EQ(first->summon_ability, LS_ABILITY);
    T_EQ(first->class_id, LS_UNIT);
    T_EQ(first->owner, fix.caster);
    /* The controller, not individual BTLF timers, owns swarm lifetime. */
    T_NULL(unit_findstatus(first, MAKEFOURCC('B','T','L','F')));
    /* Moving the caster must not cancel the persistent controller. */
    fix.caster->s.origin2 = (vec2_t){ 720, 720 };
    level.time = 1249; G_RunEntities();
    T_EQ(locust_member_count(controller), 1u);
    level.time = 1250; G_RunEntities();
    T_EQ(locust_member_count(controller), 2u);
    FILTER_EDICTS(ent, G_GetLocustMember(ent) &&
                  G_GetLocustMember(ent)->controller == controller &&
                  G_GetLocustMember(ent)->state == 1u) { second = ent; break; }
    T_NOT_NULL(second);
    if (second) T_ASSERT(Vector2_distance(&second->s.origin2, &fix.caster->s.origin2) >= 0.0f);
    T_ASSERT(controller->inuse);
    /* A scheduler delay catches up, without ever duplicating release slots. */
    level.time = 1500; G_RunEntities();
    T_EQ(locust_member_count(controller), 3u);
    T_EQ(state->released, 3u);
    level.time = 1800; G_RunEntities();
    T_EQ(locust_member_count(controller), 3u);
cleanup:
    locust_done(fix);
}

TEST(wc3_spell, locust_swarm_expiry_and_owner_recycling_remove_only_owned_members) {
    locustFix_t fix = locust_setup();
    abilityitem_t item = S_AbilityItem(LS_ABILITY);
    abilityCall_t call = MAKE(abilityCall_t, .item = &item);
    edict_t *controller;
    T_ASSERT(S_AbilityMessage(fix.caster, A_EXECUTE, &call));
    controller = locust_controller(fix.caster);
    T_NOT_NULL(controller);
    if (!controller) goto cleanup;
    level.time = 1500; G_RunEntities();
    T_EQ(locust_member_count(controller), 3u);
    level.time = 6000; G_RunEntities();
    G_RunDeferredFrees();
    T_ASSERT(!controller->inuse);
    T_EQ(locust_all_members(), 0u); /* no frozen corpse entities */
    level.time = 7000;
    T_ASSERT(S_AbilityMessage(fix.caster, A_EXECUTE, &call));
    controller = locust_controller(fix.caster);
    T_NOT_NULL(controller);
    if (!controller) goto cleanup;
    level.time = 7000; G_RunEntities();
    T_EQ(locust_member_count(controller), 1u);
    /* A caster pointer with the wrong generation must not own the swarm. */
    fix.caster->spawn_time++;
    level.time = 7101; G_RunEntities();
    G_RunDeferredFrees();
    T_ASSERT(!controller->inuse);
    T_EQ(locust_all_members(), 0u);
cleanup:
    locust_done(fix);
}

TEST(wc3_save, locust_swarm_controller_and_members_survive_mid_release_save) {
    locustFix_t fix = locust_setup();
    abilityitem_t item = S_AbilityItem(LS_ABILITY);
    abilityCall_t call = MAKE(abilityCall_t, .item = &item);
    cstring_t filename = Test_TempPath("wc3-locust-midrelease.bin");
    edict_t *controller;
    locustSwarm_t *state;
    T_ASSERT(S_AbilityMessage(fix.caster, A_EXECUTE, &call));
    controller = locust_controller(fix.caster);
    T_NOT_NULL(controller);
    if (!controller) goto cleanup;
    level.time = 1300; G_RunEntities();
    T_EQ(locust_member_count(controller), 2u);
    T_ASSERT(WriteGame(filename));
    state = G_GetLocustSwarm(controller);
    state->released = 0;
    state->next_release_ms = 0;
    T_ASSERT(ReadGame(filename));
    controller = locust_controller(fix.caster);
    T_NOT_NULL(controller);
    if (!controller) goto cleanup;
    state = G_GetLocustSwarm(controller);
    T_NOT_NULL(state);
    if (!state) goto cleanup;
    T_EQ(state->released, 2u);
    T_EQ(state->next_release_ms, 1500u);
    T_EQ(locust_member_count(controller), 2u);
    level.time = 1750; G_RunEntities();
    T_EQ(locust_member_count(controller), 3u);
    remove(filename);
cleanup:
    locust_done(fix);
}

/* Locust combat is deliberately driven through the actual summon/member
 * production callbacks; targets are ordinary attackable units with known
 * health, rather than a mocked spell damage receiver. */
static edict_t *locust_test_enemy(float x, float y) {
    edict_t *ent = alloc_test_unit(MAKEFOURCC('h','f','o','o'), x, y);
    ent->s.player = 1;
    ent->svflags |= SVF_MONSTER;
    ent->targtype = TARG_GROUND;
    ent->health.value = ent->health.max_value = 300.0f;
    ent->unit_state->armor_value = 0.0f;
    return ent;
}

static edict_t *locust_test_member(edict_t const *controller, uint32_t index) {
    FILTER_EDICTS(ent, ent->inuse && G_GetLocustMember(ent) &&
                  G_GetLocustMember(ent)->controller == controller &&
                  G_GetLocustMember(ent)->release_index == index) {
        ent->data.UnitWeapons = (UnitWeapons_t *)&locust_test_weapon;
        ent->unit_state->attack1.type = ATK_NORMAL;
        ent->unit_state->attack1.targetsAllowed = WC3_TARGET_FLAG_GROUND;
        ent->unit_state->attack1.damageBase = 25.0f;
        ent->unit_state->attack1.sidesPerDie = 1;
        ent->unit_state->attack1.range = 90.0f;
        ent->unit_state->attack1.cooldown = 1.0f;
        return ent;
    }
    return NULL;
}

/* Regression: the member's own thinker replaces monster_think, so simply
 * issuing a Move order leaves spawned locusts immobile. Even with no targets,
 * orbit steering must write both the simulation and render positions. */
TEST(wc3_spell, locust_swarm_idle_members_fly_around_moving_caster) {
    locustFix_t fix = locust_setup();
    abilityitem_t item = S_AbilityItem(LS_ABILITY);
    abilityCall_t call = MAKE(abilityCall_t, .item = &item);
    edict_t *controller, *locust;
    vec2_t before, after;
    T_ASSERT(S_AbilityMessage(fix.caster, A_EXECUTE, &call));
    controller = locust_controller(fix.caster);
    T_NOT_NULL(controller);
    if (!controller) goto cleanup;
    level.time = 1250; G_RunEntities();
    locust = locust_test_member(controller, 0);
    T_NOT_NULL(locust);
    if (!locust) goto cleanup;
    locust->unitinfo.MoveSpeed = 300.0f;
    before = locust->s.origin2;
    level.time = 1350; G_RunEntities();
    after = locust->s.origin2;
    T_ASSERT(Vector2_distance(&after, &before) > 0.1f);
    T_FEQ(locust->s.origin.x, after.x, 0.001f);
    T_FEQ(locust->s.origin.y, after.y, 0.001f);
    T_EQ(G_GetLocustMember(locust)->state, 1u); /* no enemy to acquire */
    fix.caster->s.origin2 = (vec2_t){ 650, 650 };
    level.time = 1450; G_RunEntities();
    T_ASSERT(Vector2_distance(&locust->s.origin2, &after) > 0.1f);
cleanup:
    locust_done(fix);
}

/* Retail footage shows members dispersed far beyond a small fixed orbit and
 * continuously changing direction/radius without opponents nearby. */
TEST(wc3_spell, locust_swarm_idle_flight_roams_broadly_without_stalling) {
    locustFix_t fix = locust_setup();
    abilityitem_t item = S_AbilityItem(LS_ABILITY);
    abilityCall_t call = MAKE(abilityCall_t, .item = &item);
    edict_t *controller, *locust;
    vec2_t old_position;
    float greatest_radius = 0.0f, least_radius = FLT_MAX;
    uint32_t moved_steps = 0;
    T_ASSERT(S_AbilityMessage(fix.caster, A_EXECUTE, &call));
    controller = locust_controller(fix.caster);
    T_NOT_NULL(controller);
    if (!controller) goto cleanup;
    level.time = 1250; G_RunEntities();
    locust = locust_test_member(controller, 0);
    T_NOT_NULL(locust);
    if (!locust) goto cleanup;
    locust->unitinfo.MoveSpeed = 300.0f;
    old_position = locust->s.origin2;
    for (uint32_t step = 1; step <= 35; step++) {
        float radius;
        level.time = 1250 + step * 100;
        G_RunEntities();
        if (Vector2_distance(&old_position, &locust->s.origin2) > 0.1f) moved_steps++;
        old_position = locust->s.origin2;
        radius = Vector2_distance(&locust->s.origin2, &fix.caster->s.origin2);
        greatest_radius = MAX(radius, greatest_radius);
        least_radius = MIN(radius, least_radius);
        T_ASSERT(radius <= G_GetLocustSwarm(controller)->area + 15.0f);
        T_EQ(G_GetLocustMember(locust)->state, 1u);
    }
    T_ASSERT(moved_steps >= 30u);
    T_ASSERT(greatest_radius > 110.0f);
    T_ASSERT(greatest_radius - least_radius > 20.0f);
cleanup:
    locust_done(fix);
}

/* Combat deaths should still dispatch death events normally, but without
 * monster_think the model must be retired rather than frozen as a corpse. */
TEST(wc3_spell, locust_swarm_combat_death_does_not_leave_frozen_model) {
    locustFix_t fix = locust_setup();
    abilityitem_t item = S_AbilityItem(LS_ABILITY);
    abilityCall_t call = MAKE(abilityCall_t, .item = &item);
    edict_t *controller, *locust;
    T_ASSERT(S_AbilityMessage(fix.caster, A_EXECUTE, &call));
    controller = locust_controller(fix.caster);
    T_NOT_NULL(controller);
    if (!controller) goto cleanup;
    level.time = 1250; G_RunEntities();
    locust = locust_test_member(controller, 0);
    T_NOT_NULL(locust);
    if (!locust) goto cleanup;
    unit_die(locust, NULL);
    T_ASSERT(M_IsDead(locust));
    level.time = 1350;
    locust_member_think(locust);
    T_ASSERT(G_IsDeferredFree(locust));
    G_RunDeferredFrees();
    T_ASSERT(!locust->inuse);
cleanup:
    locust_done(fix);
}

TEST(wc3_spell, locust_swarm_chases_out_of_reach_enemy_and_keeps_last_reservation) {
    locustFix_t fix = locust_setup();
    abilityitem_t item = S_AbilityItem(LS_ABILITY);
    abilityCall_t call = MAKE(abilityCall_t, .item = &item);
    edict_t *controller, *locust, *enemy;
    locustMember_t *state;
    vec2_t before;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    memset(level.alliances, 0, sizeof(level.alliances));
    T_ASSERT(S_AbilityMessage(fix.caster, A_EXECUTE, &call));
    controller = locust_controller(fix.caster);
    T_NOT_NULL(controller);
    if (!controller) goto cleanup;
    level.time = 1250; G_RunEntities();
    locust = locust_test_member(controller, 0);
    T_NOT_NULL(locust);
    if (!locust) goto cleanup;
    locust->unitinfo.MoveSpeed = 250.0f;
    G_GetLocustSwarm(controller)->target_cap = 1.0f;
    G_GetLocustSwarm(controller)->return_threshold = 999.0f;
    enemy = locust_test_enemy(475, 256);
    before = locust->s.origin2;
    level.time = 1350; locust_member_think(locust);
    state = G_GetLocustMember(locust);
    T_NOT_NULL(state);
    if (!state) goto cleanup;
    T_EQ(state->state, 2u);
    T_ASSERT(state->target == enemy);
    T_ASSERT(Vector2_distance(&locust->s.origin2, &enemy->s.origin2) <
             Vector2_distance(&before, &enemy->s.origin2));
    /* Occupancy=1 and cap=1 is VALID for the locust that owns the slot. */
    level.time = 1450; locust_member_think(locust);
    T_EQ(state->state, 2u);
    T_ASSERT(state->target == enemy);
cleanup:
    locust_done(fix);
}

TEST(wc3_spell, locust_swarm_return_flight_moves_without_monster_think) {
    locustFix_t fix = locust_setup();
    abilityitem_t item = S_AbilityItem(LS_ABILITY);
    abilityCall_t call = MAKE(abilityCall_t, .item = &item);
    edict_t *controller, *locust;
    locustMember_t *state;
    vec2_t before;
    T_ASSERT(S_AbilityMessage(fix.caster, A_EXECUTE, &call));
    controller = locust_controller(fix.caster);
    T_NOT_NULL(controller);
    if (!controller) goto cleanup;
    level.time = 1250; G_RunEntities();
    locust = locust_test_member(controller, 0);
    T_NOT_NULL(locust);
    if (!locust) goto cleanup;
    locust->unitinfo.MoveSpeed = 300.0f;
    state = G_GetLocustMember(locust);
    state->state = 3u;
    state->damage_credit = 20.0f;
    fix.caster->health.value = 400.0f;
    locust->s.origin2 = (vec2_t){ 600, 256 };
    locust->s.origin.x = 600;
    locust->s.origin.y = 256;
    before = locust->s.origin2;
    level.time = 1350; locust_member_think(locust);
    T_ASSERT(Vector2_distance(&locust->s.origin2, &fix.caster->s.origin2) <
             Vector2_distance(&before, &fix.caster->s.origin2));
    for (uint32_t i = 0; i < 20 && state->state == 3u; i++) {
        level.time += 100;
        locust_member_think(locust);
    }
    T_EQ(state->state, 1u);
    T_FEQ(fix.caster->health.value, 410.0f, 0.05f);
cleanup:
    locust_done(fix);
}

TEST(wc3_spell, locust_swarm_reserves_only_authored_attackers_per_target) {
    locustFix_t fix = locust_setup();
    abilityitem_t item = S_AbilityItem(LS_ABILITY);
    abilityCall_t call = MAKE(abilityCall_t, .item = &item);
    edict_t *controller, *first, *second, *third, *near, *far;
    locustMember_t *a, *b, *c;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    memset(level.alliances, 0, sizeof(level.alliances));
    T_ASSERT(S_AbilityMessage(fix.caster, A_EXECUTE, &call));
    controller = locust_controller(fix.caster);
    T_NOT_NULL(controller);
    if (!controller) goto cleanup;
    level.time = 1900; locust_swarm_think(controller);
    first = locust_test_member(controller, 0);
    second = locust_test_member(controller, 1);
    third = locust_test_member(controller, 2);
    T_NOT_NULL(first); T_NOT_NULL(second); T_NOT_NULL(third);
    if (!first || !second || !third) goto cleanup;
    near = locust_test_enemy(330, 256);
    far = locust_test_enemy(410, 256);
    G_GetLocustSwarm(controller)->return_threshold = 999.0f;
    G_GetLocustSwarm(controller)->area = 600.0f;
    level.time = 2000;
    locust_member_think(first);
    locust_member_think(second);
    locust_member_think(third);
    a = G_GetLocustMember(first);
    b = G_GetLocustMember(second);
    c = G_GetLocustMember(third);
    T_EQ(a->state, 2u);
    T_EQ(b->state, 2u);
    T_EQ(c->state, 2u);
    /* All three would prefer the closer enemy, but DataC=2. */
    T_ASSERT(a->target == near);
    T_ASSERT(b->target == near);
    T_ASSERT(c->target == far);
    /* A disappearing target drops its reservation safely. */
    G_FreeEdict(near);
    level.time = 2300;
    locust_member_think(first);
    T_ASSERT(!a->target || a->target != near);
cleanup:
    locust_done(fix);
}

TEST(wc3_spell, locust_swarm_heals_from_actual_attack_damage_after_return) {
    locustFix_t fix = locust_setup();
    abilityitem_t item = S_AbilityItem(LS_ABILITY);
    abilityCall_t call = MAKE(abilityCall_t, .item = &item);
    edict_t *controller, *locust, *enemy;
    locustMember_t *member;
    float hp_before, hp_lost, heal;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    memset(level.alliances, 0, sizeof(level.alliances));
    fix.caster->health.value = 400.0f;
    T_ASSERT(S_AbilityMessage(fix.caster, A_EXECUTE, &call));
    controller = locust_controller(fix.caster);
    T_NOT_NULL(controller);
    if (!controller) goto cleanup;
    level.time = 1300; locust_swarm_think(controller);
    locust = locust_test_member(controller, 0);
    T_NOT_NULL(locust);
    if (!locust) goto cleanup;
    locust->unit_state->attack1.damageBase = 25.0f;
    locust->unit_state->attack1.numberOfDice = 0;
    locust->unit_state->attack1.cooldown = 1.0f;
    locust->unit_state->attack1.range = 90.0f;
    enemy = locust_test_enemy(locust->s.origin2.x + 32.0f, locust->s.origin2.y);
    member = G_GetLocustMember(locust);
    G_GetLocustSwarm(controller)->return_threshold = 1.0f;
    hp_before = enemy->health.value;
    level.time = 1400; locust_member_think(locust);
    hp_lost = hp_before - enemy->health.value;
    T_ASSERT(hp_lost > 0.0f);
    T_EQ(member->state, 3u); /* return flight begins after actual damage */
    heal = hp_lost * 0.5f; /* synthetic ability DataD */
    T_FEQ(member->damage_credit, hp_lost, 0.01f);
    for (uint32_t step = 1; step <= 24 && member->state == 3u; step++) {
        level.time = 1400 + step * 100;
        locust_member_think(locust);
    }
    T_EQ(member->state, 1u);
    T_FEQ(member->damage_credit, 0.0f, 0.001f);
    T_FEQ(fix.caster->health.value, 400.0f + heal, 0.02f);
cleanup:
    locust_done(fix);
}

TEST(wc3_spell, locust_swarm_invulnerable_and_allied_targets_do_not_grant_healing) {
    locustFix_t fix = locust_setup();
    abilityitem_t item = S_AbilityItem(LS_ABILITY);
    abilityCall_t call = MAKE(abilityCall_t, .item = &item);
    edict_t *controller, *locust, *immune, *ally;
    locustMember_t *member;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    memset(level.alliances, 0, sizeof(level.alliances));
    T_ASSERT(S_AbilityMessage(fix.caster, A_EXECUTE, &call));
    controller = locust_controller(fix.caster);
    T_NOT_NULL(controller);
    if (!controller) goto cleanup;
    level.time = 1250; locust_swarm_think(controller);
    locust = locust_test_member(controller, 0);
    T_NOT_NULL(locust);
    if (!locust) goto cleanup;
    immune = locust_test_enemy(locust->s.origin2.x + 25, locust->s.origin2.y);
    G_SetUnitFlag(immune, UNIT_FLAG_INVULNERABLE, true);
    ally = locust_test_enemy(locust->s.origin2.x + 35, locust->s.origin2.y);
    ally->s.player = 0;
    member = G_GetLocustMember(locust);
    member->next_acquire_ms = 1400;
    member->damage_credit = 0.0f;
    level.time = 1400; locust_member_think(locust);
    T_ASSERT(member->target != immune && member->target != ally);
    T_FEQ(immune->health.value, 300.0f, 0.001f);
    T_FEQ(ally->health.value, 300.0f, 0.001f);
cleanup:
    locust_done(fix);
}

TEST(wc3_save, locust_swarm_member_target_and_return_credit_survive_save_load) {
    locustFix_t fix = locust_setup();
    abilityitem_t item = S_AbilityItem(LS_ABILITY);
    abilityCall_t call = MAKE(abilityCall_t, .item = &item);
    cstring_t filename = Test_TempPath("wc3-locust-combat-save.bin");
    edict_t *controller, *locust, *enemy;
    locustMember_t *member;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    memset(level.alliances, 0, sizeof(level.alliances));
    T_ASSERT(S_AbilityMessage(fix.caster, A_EXECUTE, &call));
    controller = locust_controller(fix.caster);
    T_NOT_NULL(controller);
    if (!controller) goto cleanup;
    level.time = 1250; locust_swarm_think(controller);
    locust = locust_test_member(controller, 0);
    T_NOT_NULL(locust);
    if (!locust) goto cleanup;
    enemy = locust_test_enemy(400, 256);
    member = G_GetLocustMember(locust);
    member->state = 2u;
    member->target = enemy;
    member->target_spawn_time = enemy->spawn_time;
    member->damage_credit = 12.5f;
    member->next_attack_ms = 2345;
    T_ASSERT(WriteGame(filename));
    member->state = 0u;
    member->target = NULL;
    member->damage_credit = 0.0f;
    T_ASSERT(ReadGame(filename));
    member = G_GetLocustMember(locust);
    T_NOT_NULL(member);
    if (member) {
        T_EQ(member->state, 2u);
        T_ASSERT(member->target == enemy);
        T_EQ(member->target_spawn_time, enemy->spawn_time);
        T_FEQ(member->damage_credit, 12.5f, 0.001f);
        T_EQ(member->next_attack_ms, 2345u);
    }
    T_ASSERT(locust->think == locust_member_think);
    remove(filename);
cleanup:
    locust_done(fix);
}

/* The 30-second duration ends attacks and releases, but outstanding HP credit
 * must still be delivered if its owner can reach the caster before recall's
 * bounded grace deadline. No member may seek again after expiry. */
TEST(wc3_spell, locust_swarm_expiry_recalls_then_heals_without_redeploying) {
    locustFix_t fix = locust_setup();
    abilityitem_t item = S_AbilityItem(LS_ABILITY);
    abilityCall_t call = MAKE(abilityCall_t, .item = &item);
    edict_t *controller, *member_unit;
    locustMember_t *member;
    uint32_t released_before_expiry;
    T_ASSERT(S_AbilityMessage(fix.caster, A_EXECUTE, &call));
    controller = locust_controller(fix.caster);
    T_NOT_NULL(controller);
    if (!controller) goto cleanup;
    level.time = 1250; G_RunEntities();
    member_unit = locust_test_member(controller, 0);
    T_NOT_NULL(member_unit);
    if (!member_unit) goto cleanup;
    member = G_GetLocustMember(member_unit);
    released_before_expiry = G_GetLocustSwarm(controller)->released;
    fix.caster->health.value = 400.0f;
    member->damage_credit = 17.25f;
    member->state = 2u; /* currently attacking at expiry */
    member_unit->s.origin2 = fix.caster->s.origin2; /* at return arrival */
    G_GetLocustSwarm(controller)->end_ms = 1300;
    level.time = 1300; locust_swarm_think(controller);
    if (member_unit->inuse) locust_member_think(member_unit);
    T_ASSERT(G_GetLocustSwarm(controller)->recalling);
    G_RunDeferredFrees();
    T_EQ(G_GetLocustSwarm(controller)->released, released_before_expiry);
    T_FEQ(fix.caster->health.value, 408.625f, 0.01f);
    T_ASSERT(!member_unit->inuse); /* credited member is removed after healing */
    level.time = 1400; G_RunEntities();
    G_RunDeferredFrees();
    T_ASSERT(!controller->inuse); /* controller exits after final returned locust */
cleanup:
    locust_done(fix);
}

TEST(wc3_spell, locust_swarm_recast_replaces_existing_swarm_only) {
    locustFix_t fix = locust_setup();
    abilityitem_t item = S_AbilityItem(LS_ABILITY);
    abilityCall_t call = MAKE(abilityCall_t, .item = &item);
    edict_t *old, *replacement;
    uint32_t old_generation;
    T_ASSERT(S_AbilityMessage(fix.caster, A_EXECUTE, &call));
    old = locust_controller(fix.caster);
    T_NOT_NULL(old);
    if (!old) goto cleanup;
    level.time = 1000; locust_swarm_think(old);
    T_EQ(locust_member_count(old), 1u);
    old_generation = old->spawn_time;
    T_ASSERT(S_AbilityMessage(fix.caster, A_EXECUTE, &call));
    replacement = locust_controller(fix.caster);
    T_NOT_NULL(replacement);
    T_ASSERT(replacement && replacement != old);
    T_ASSERT(!old->inuse);
    G_RunDeferredFrees();
    T_EQ(locust_all_members(), 0u);
    if (replacement) {
        T_ASSERT(replacement->spawn_time != old_generation || replacement != old);
        level.time = 1000; G_RunEntities();
        T_EQ(locust_member_count(replacement), 1u);
    }
cleanup:
    locust_done(fix);
}

TEST(wc3_spell, locust_swarm_spell_relocation_and_death_cancel_immediately) {
    locustFix_t fix = locust_setup();
    abilityitem_t item = S_AbilityItem(LS_ABILITY);
    abilityCall_t call = MAKE(abilityCall_t, .item = &item);
    edict_t *controller;
    vec2_t destination = { 900, 900 };
    T_ASSERT(S_AbilityMessage(fix.caster, A_EXECUTE, &call));
    controller = locust_controller(fix.caster);
    T_NOT_NULL(controller);
    if (!controller) goto cleanup;
    level.time = 1000; locust_swarm_think(controller);
    T_EQ(locust_member_count(controller), 1u);
    /* Scripted position movement is not itself a spell teleport. */
    fix.caster->s.origin2 = (vec2_t){ 750, 750 };
    T_ASSERT(controller->inuse);
    S_SpellRelocateUnit(fix.caster, LS_ABILITY, &destination);
    T_ASSERT(!controller->inuse);
    G_RunDeferredFrees();
    T_EQ(locust_all_members(), 0u);
    level.time = 1000;
    T_ASSERT(S_AbilityMessage(fix.caster, A_EXECUTE, &call));
    controller = locust_controller(fix.caster);
    T_NOT_NULL(controller);
    if (!controller) goto cleanup;
    locust_swarm_think(controller);
    T_EQ(locust_member_count(controller), 1u);
    unit_die(fix.caster, NULL);
    T_ASSERT(!controller->inuse);
    G_RunDeferredFrees();
    T_EQ(locust_all_members(), 0u);
cleanup:
    locust_done(fix);
}

TEST(wc3_save, locust_swarm_recall_phase_and_deadline_round_trip) {
    locustFix_t fix = locust_setup();
    abilityitem_t item = S_AbilityItem(LS_ABILITY);
    abilityCall_t call = MAKE(abilityCall_t, .item = &item);
    cstring_t filename = Test_TempPath("wc3-locust-recalling.bin");
    edict_t *controller, *member_unit;
    locustSwarm_t *swarm;
    locustMember_t *member;
    T_ASSERT(S_AbilityMessage(fix.caster, A_EXECUTE, &call));
    controller = locust_controller(fix.caster);
    T_NOT_NULL(controller);
    if (!controller) goto cleanup;
    level.time = 1000; G_RunEntities();
    member_unit = locust_test_member(controller, 0);
    T_NOT_NULL(member_unit);
    if (!member_unit) goto cleanup;
    member = G_GetLocustMember(member_unit);
    T_NOT_NULL(member);
    if (!member) goto cleanup;
    /* Keep a member out of return reach to snapshot the mid-recall thinker. */
    member_unit->s.origin2 = (vec2_t){ 900, 900 };
    member->damage_credit = 10.0f;
    G_GetLocustSwarm(controller)->end_ms = 1300;
    level.time = 1300; locust_swarm_think(controller);
    swarm = G_GetLocustSwarm(controller);
    T_ASSERT(swarm->recalling);
    T_EQ(swarm->recall_deadline_ms, 9300u);
    T_ASSERT(WriteGame(filename));
    swarm->recalling = false;
    swarm->recall_deadline_ms = 0;
    T_ASSERT(ReadGame(filename));
    controller = locust_controller(fix.caster);
    T_NOT_NULL(controller);
    if (controller) {
        swarm = G_GetLocustSwarm(controller);
        T_ASSERT(swarm && swarm->recalling);
        if (swarm) T_EQ(swarm->recall_deadline_ms, 9300u);
        T_ASSERT(member_unit->think == locust_member_think);
        level.time = 9400; G_RunEntities();
        G_RunDeferredFrees();
        T_ASSERT(!controller->inuse);
        T_EQ(locust_all_members(), 0u); /* recall grace removes all remaining members */
    }
    remove(filename);
cleanup:
    locust_done(fix);
}

/* The same simulation handles the campaign alias, but its actual row (not
 * AUls's three-count fixture) must determine release timing and parameters. */
TEST(wc3_spell, voodoo_spirits_alias_uses_own_swarm_data) {
    locustFix_t fix = locust_setup();
    uint32_t const alias = MAKEFOURCC('A','O','l','s');
    abilityitem_t item = S_AbilityItem(alias);
    abilityCall_t call = MAKE(abilityCall_t, .item = &item);
    edict_t *controller;
    locustSwarm_t *state;
    T_NOT_NULL(item.ability);
    if (!item.ability) goto cleanup;
    T_ASSERT(item.ability->proc == CAbilityLocustSwarm);
    fix.caster->unit_state->ranks[0] = MAKE(heroability_t, .code = alias, .level = 1);
    T_ASSERT(S_AbilityMessage(fix.caster, A_VALIDATE, &call));
    T_ASSERT(S_AbilityMessage(fix.caster, A_EXECUTE, &call));
    controller = locust_controller(fix.caster);
    T_NOT_NULL(controller);
    if (!controller) goto cleanup;
    state = G_GetLocustSwarm(controller);
    T_NOT_NULL(state);
    if (!state) goto cleanup;
    T_EQ(state->ability, alias);
    T_EQ(state->total, 2u);
    T_EQ(state->release_interval_ms, 500u);
    T_FEQ(state->target_cap, 1.0f, 0.001f);
    T_FEQ(state->area, 300.0f, 0.001f);
    T_FEQ(state->heal_factor, 0.25f, 0.001f);
    T_FEQ(state->return_threshold, 7.0f, 0.001f);
    level.time = 1000; G_RunEntities();
    T_EQ(locust_member_count(controller), 1u);
    level.time = 1500; G_RunEntities();
    T_EQ(locust_member_count(controller), 2u);
cleanup:
    locust_done(fix);
}

/* Locust visual setup must not invent an animation for custom UnitIDs with no
 * available Walk sequence, and must not stop release when art is unavailable. */
TEST(wc3_spell, locust_swarm_member_walk_presentation_is_optional) {
    locustFix_t fix = locust_setup();
    abilityitem_t item = S_AbilityItem(LS_ABILITY);
    abilityCall_t call = MAKE(abilityCall_t, .item = &item);
    edict_t *controller;
    bool found = false;
    T_ASSERT(S_AbilityMessage(fix.caster, A_EXECUTE, &call));
    controller = locust_controller(fix.caster);
    T_NOT_NULL(controller);
    if (!controller) goto cleanup;
    level.time = 1250; G_RunEntities();
    FILTER_EDICTS(locust, locust->inuse && G_GetLocustMember(locust) &&
                  G_GetLocustMember(locust)->controller == controller) {
        found = true;
        T_ASSERT(locust->think == locust_member_think);
        if (G_GetUnitAnimation(locust, "Walk")) {
            T_NOT_NULL(locust->currentmove);
            T_NOT_NULL(locust->animation);
            T_ASSERT(G_AnimationHasPrimary(locust->animation, "walk"));
        }
        break;
    }
    T_ASSERT(found);
cleanup:
    locust_done(fix);
}

#endif
