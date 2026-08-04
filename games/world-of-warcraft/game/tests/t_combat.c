/*
 * t_combat.c — In-engine combat tests (see plans/lightweight-testing.md).
 *
 * These run inside the real WoW game module.  The +test boot calls
 * SV_InitGameProgs, so gi/globals are live and G_RegisterModel resolves real
 * animations from the mounted archives (run via `make test-wow-engine`, which
 * passes -data).  Test entities occupy edicts [1..2]; globals.num_edicts is set
 * to 3 so a corpse spawned on death lands in a free slot instead of clobbering
 * a test edict.
 */
#ifdef BZ_TESTS

#include "test.h"
#include "game/g_wow_local.h"

#include <string.h>

/* Register the player model once; it carries the Attack/Pain/Death animations
 * the AI code needs to start a swing and hold a death pose. */
static int combat_model(void) {
    static int model = -1;
    if (model < 0) model = G_RegisterModel(WOW_PLAYER_MODEL);
    return model;
}

/* Two hostile creatures 2 units apart (inside WOW_MELEE_RANGE), attacker locked
 * onto the target.  Returns real edicts driven by the real AI think function. */
static void combat_prepare(LPEDICT *attacker_out, LPEDICT *target_out) {
    int model = combat_model();
    LPEDICT attacker = &wow_edicts[1];
    LPEDICT target = &wow_edicts[2];
    wowEntityLocal_t *al, *tl;

    memset(wow_edicts, 0, sizeof(wow_edicts));
    memset(wow_entity_locals, 0, sizeof(wow_entity_locals));
    globals.num_edicts = 3;
    wow_spawns_this_frame = 0;

    attacker->inuse = target->inuse = true;
    attacker->s.number = 1;
    target->s.number = 2;
    attacker->s.model = target->s.model = model;
    attacker->svflags = target->svflags = SVF_MONSTER;
    attacker->think = target->think = Wow_RunCreatureFrame;
    attacker->pain = target->pain = Wow_AIPain;
    attacker->attack = target->attack = Wow_AIAttack;
    attacker->s.origin2 = (VECTOR2){ 0.0f, 0.0f };
    target->s.origin2 = (VECTOR2){ 2.0f, 0.0f };

    al = Wow_EntityLocal(attacker);
    tl = Wow_EntityLocal(target);
    al->health = 5;
    al->enemy = target;
    tl->health = 3;

    *attacker_out = attacker;
    *target_out = target;
}

TEST(wow_combat, attack_applies_damage_at_damage_point) {
    LPEDICT attacker, target;
    wowEntityLocal_t *al, *tl;
    DWORD hp;

    combat_prepare(&attacker, &target);
    al = Wow_EntityLocal(attacker);
    tl = Wow_EntityLocal(target);
    /* Explicit timing makes the swing deterministic regardless of the model's
     * animation split. */
    al->attack_damage_point = 250;
    al->attack_backswing = 450;
    /* Clear the target's attack so a hit plays a pain reaction instead of
     * retaliating (Wow_ApplyDamage prefers retaliation when possible). */
    target->attack = NULL;

    Wow_AIAttack(attacker);
    T_ASSERT(al->attack_damage_time > 0); /* swing started (attack anim resolved) */

    hp = tl->health;
    while (al->attack_damage_time > 0) {
        T_EQ(tl->health, hp); /* no damage before the damage point */
        Wow_AIAdvanceLockedFrame(attacker);
    }
    T_EQ(tl->health, hp - 1); /* damage lands exactly at the damage point */
    T_ASSERT(tl->pain_time > 0); /* and triggers the target's pain */
}

TEST(wow_combat, explicit_timing_overrides_animation_split) {
    LPEDICT attacker, target;
    wowEntityLocal_t *al;

    combat_prepare(&attacker, &target);
    al = Wow_EntityLocal(attacker);
    al->attack_damage_point = 120;
    al->attack_backswing = 180;

    Wow_AIAttack(attacker);
    T_EQ(al->attack_damage_time, 120);
    T_EQ(al->attack_backswing_time, 180);
    T_EQ(al->attack_time, 300);
}

TEST(wow_combat, lethal_attack_triggers_death_state) {
    LPEDICT attacker, target;
    wowEntityLocal_t *al, *tl;

    combat_prepare(&attacker, &target);
    al = Wow_EntityLocal(attacker);
    tl = Wow_EntityLocal(target);
    tl->health = 1;
    al->attack_damage_point = 100;
    al->attack_backswing = 100;

    Wow_AIAttack(attacker);
    while (al->attack_damage_time > 0) Wow_AIAdvanceLockedFrame(attacker);

    T_ASSERT(tl->dead);
    T_EQ(tl->health, 0);
    T_ASSERT(target->svflags & SVF_DEADMONSTER);
}

TEST(wow_combat, dead_entity_ignores_pain_and_attack) {
    LPEDICT attacker, target;
    wowEntityLocal_t *tl;

    combat_prepare(&attacker, &target);
    tl = Wow_EntityLocal(target);
    Wow_AIDie(target, attacker);

    Wow_AIPain(target);
    Wow_AIAttack(target);

    T_ASSERT(tl->dead);
    T_EQ(tl->pain_time, 0);
    T_EQ(tl->attack_damage_time, 0);
    T_EQ(tl->attack_backswing_time, 0);
}

TEST(wow_combat, death_holds_terminal_frame) {
    LPEDICT attacker, target;
    wowEntityLocal_t *tl;
    DWORD terminal;

    combat_prepare(&attacker, &target);
    tl = Wow_EntityLocal(target);
    Wow_AIDie(target, attacker);
    T_NOT_NULL(tl->animation); /* real death animation resolved from the model */
    terminal = tl->animation->interval[1];

    for (int i = 0; i < 30; i++) Wow_AIAdvanceLockedFrame(target);
    T_EQ(target->s.frame, terminal - 1);
}

#endif /* BZ_TESTS */
