#include "test_framework.h"

#include <stdlib.h>
#include <string.h>

#include "game/g_wow_local.h"

int _tests_run = 0;
int _tests_failed = 0;

struct game_import gi;
struct game_export globals;
edict_t wow_edicts[WOW_MAX_EDICTS];
wowEntityLocal_t wow_entity_locals[WOW_MAX_EDICTS];

static animation_t g_attack_anim = {
    .name = "Attack",
    .interval = { 0, 1000 },
};

static animation_t g_pain_anim = {
    .name = "Pain",
    .interval = { 0, 450 },
};

static animation_t g_death_anim = {
    .name = "Death",
    .interval = { 0, 900 },
};

static DWORD g_pain_calls = 0;

DWORD Wow_EntityIndex(LPCEDICT ent) {
    if (!ent || ent < wow_edicts || ent >= wow_edicts + WOW_MAX_EDICTS) {
        return WOW_MAX_EDICTS;
    }
    return (DWORD)(ent - wow_edicts);
}

wowEntityLocal_t *Wow_EntityLocal(LPCEDICT ent) {
    DWORD index = Wow_EntityIndex(ent);

    if (index >= WOW_MAX_EDICTS) {
        return NULL;
    }
    return &wow_entity_locals[index];
}

static LPCANIMATION Test_SelectAnimation(LPCSTR const *animation_names) {
    for (LPCSTR const *name = animation_names; name && *name; name++) {
        if (!strncasecmp(*name, "Attack", 6)) {
            return &g_attack_anim;
        }
        if (!strncasecmp(*name, "CombatWound", 11) ||
            !strncasecmp(*name, "StandWound", 10) ||
            !strncasecmp(*name, "Stun", 4) ||
            !strncasecmp(*name, "Pain", 4)) {
            return &g_pain_anim;
        }
        if (!strncasecmp(*name, "Death", 5) || !strncasecmp(*name, "Dead", 4)) {
            return &g_death_anim;
        }
        if (!strncasecmp(*name, "Stand", 5) || !strncasecmp(*name, "Ready", 5) ||
            !strncasecmp(*name, "Walk", 4) || !strncasecmp(*name, "Run", 3)) {
            return &g_attack_anim;
        }
    }
    return NULL;
}

BOOL Wow_SetEntityMoveFirstAnimation(LPEDICT ent, LPWOWMOVE move, LPCSTR const *animation_names) {
    wowEntityLocal_t *local = Wow_EntityLocal(ent);
    LPCANIMATION anim;

    if (!ent || !local || !move) {
        return false;
    }

    anim = Test_SelectAnimation(animation_names);
    if (!anim) {
        local->animation = NULL;
        local->currentmove = NULL;
        return false;
    }

    local->currentmove = move;
    local->animation = anim;
    ent->s.frame = anim->interval[0];
    return true;
}

BOOL Wow_SetEntityMove(LPEDICT ent, LPWOWMOVE move) {
    LPCSTR names[2];

    if (!move || !move->animation) {
        return false;
    }
    names[0] = move->animation;
    names[1] = NULL;
    return Wow_SetEntityMoveFirstAnimation(ent, move, names);
}

void Wow_AdvanceEntityFrame(LPEDICT ent) {
    wowEntityLocal_t *local = Wow_EntityLocal(ent);
    DWORD next_frame;

    if (!ent || !local || !local->animation) {
        return;
    }

    next_frame = ent->s.frame + FRAMETIME;
    if (next_frame >= local->animation->interval[1]) {
        ent->s.frame = local->animation->interval[0];
    } else {
        ent->s.frame = next_frame;
    }
}

LPEDICT Wow_SpawnCorpse(LPEDICT dead_entity) {
    (void)dead_entity;
    return NULL; /* stub: combat tests don't test corpse logic */
}

static void test_pain_stub(LPEDICT ent) {
    (void)ent;
    g_pain_calls++;
}

static void test_reset_world(void) {
    memset(wow_edicts, 0, sizeof(wow_edicts));
    memset(wow_entity_locals, 0, sizeof(wow_entity_locals));
    g_pain_calls = 0;
}

static void test_prepare_pair(LPEDICT *attacker_out, LPEDICT *target_out) {
    LPEDICT attacker;
    LPEDICT target;
    wowEntityLocal_t *attacker_local;
    wowEntityLocal_t *target_local;

    test_reset_world();

    attacker = &wow_edicts[0];
    target = &wow_edicts[1];
    attacker->inuse = true;
    target->inuse = true;
    attacker->s.number = 0;
    target->s.number = 1;
    attacker->s.origin2 = (VECTOR2){ 0.0f, 0.0f };
    target->s.origin2 = (VECTOR2){ 2.0f, 0.0f };
    attacker->pain = Wow_AIPain;
    attacker->attack = Wow_AIAttack;
    target->pain = test_pain_stub;

    attacker_local = Wow_EntityLocal(attacker);
    target_local = Wow_EntityLocal(target);
    attacker_local->kind = WOW_ENTITY_CREATURE;
    attacker_local->health = 5;
    attacker_local->enemy = target;
    target_local->kind = WOW_ENTITY_CREATURE;
    target_local->health = 3;

    *attacker_out = attacker;
    *target_out = target;
}

static void test_wow_attack_applies_damage_after_damage_point(void) {
    LPEDICT attacker;
    LPEDICT target;
    wowEntityLocal_t *attacker_local;
    wowEntityLocal_t *target_local;

    test_prepare_pair(&attacker, &target);
    attacker_local = Wow_EntityLocal(attacker);
    target_local = Wow_EntityLocal(target);

    Wow_AIAttack(attacker);

    ASSERT_EQ_INT((int)target_local->health, 3);
    ASSERT_EQ_INT((int)attacker_local->attack_damage_time, 350);
    ASSERT_EQ_INT((int)attacker_local->attack_backswing_time, 650);

    Wow_AIAdvanceLockedFrame(attacker);
    Wow_AIAdvanceLockedFrame(attacker);
    Wow_AIAdvanceLockedFrame(attacker);
    ASSERT_EQ_INT((int)target_local->health, 3);
    ASSERT_EQ_INT((int)g_pain_calls, 0);

    Wow_AIAdvanceLockedFrame(attacker);
    ASSERT_EQ_INT((int)target_local->health, 2);
    ASSERT_EQ_INT((int)g_pain_calls, 1);

    Wow_AIAdvanceLockedFrame(attacker);
    Wow_AIAdvanceLockedFrame(attacker);
    ASSERT_EQ_INT((int)target_local->health, 2);
    ASSERT_EQ_INT((int)g_pain_calls, 1);
}

static void test_wow_attack_uses_explicit_timing_over_animation_split(void) {
    LPEDICT attacker;
    LPEDICT target;
    wowEntityLocal_t *attacker_local;

    test_prepare_pair(&attacker, &target);
    attacker_local = Wow_EntityLocal(attacker);
    attacker_local->attack_damage_point = 120;
    attacker_local->attack_backswing = 180;

    Wow_AIAttack(attacker);

    ASSERT_EQ_INT((int)attacker_local->attack_damage_time, 120);
    ASSERT_EQ_INT((int)attacker_local->attack_backswing_time, 180);
    ASSERT_EQ_INT((int)attacker_local->attack_time, 300);
}

static void test_wow_attack_lethal_triggers_death_state(void) {
    LPEDICT attacker;
    LPEDICT target;
    wowEntityLocal_t *target_local;

    test_prepare_pair(&attacker, &target);
    target_local = Wow_EntityLocal(target);
    target_local->health = 1;

    Wow_AIAttack(attacker);
    Wow_AIAdvanceLockedFrame(attacker);
    Wow_AIAdvanceLockedFrame(attacker);
    Wow_AIAdvanceLockedFrame(attacker);
    Wow_AIAdvanceLockedFrame(attacker);

    ASSERT(target_local->dead);
    ASSERT_EQ_INT((int)target_local->health, 0);
    ASSERT((target->svflags & SVF_DEADMONSTER) != 0);
}

static void test_wow_dead_entity_ignores_pain_and_attack(void) {
    LPEDICT attacker;
    LPEDICT target;
    wowEntityLocal_t *target_local;

    test_prepare_pair(&attacker, &target);
    target_local = Wow_EntityLocal(target);
    Wow_AIDie(target, attacker);

    Wow_AIPain(target);
    Wow_AIAttack(target);

    ASSERT(target_local->dead);
    ASSERT_EQ_INT((int)target_local->pain_time, 0);
    ASSERT_EQ_INT((int)target_local->attack_damage_time, 0);
    ASSERT_EQ_INT((int)target_local->attack_backswing_time, 0);
}

static void test_wow_death_holds_terminal_frame(void) {
    LPEDICT attacker;
    LPEDICT target;

    test_prepare_pair(&attacker, &target);
    Wow_AIDie(target, attacker);

    FOR_LOOP(i, 20) {
        (void)Wow_AIAdvanceLockedFrame(target);
    }

    ASSERT_EQ_INT((int)target->s.frame, (int)(g_death_anim.interval[1] - 1));
}

int main(void) {
    RUN_TEST(test_wow_attack_applies_damage_after_damage_point);
    RUN_TEST(test_wow_attack_uses_explicit_timing_over_animation_split);
    RUN_TEST(test_wow_attack_lethal_triggers_death_state);
    RUN_TEST(test_wow_dead_entity_ignores_pain_and_attack);
    RUN_TEST(test_wow_death_holds_terminal_frame);
    TEST_RESULTS();
}
