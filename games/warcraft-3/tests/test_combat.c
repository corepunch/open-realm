/*
 * test_combat.c — Tests for combat, animation, ability lookup, resources,
 *                 build queue, and quest system.
 *
 * Covered:
 *   T_Damage             — health reduction, lethal hit (calls die),
 *                          counter-attack trigger, attacker stand after kill
 *   M_MoveFrame          — normal advance, wrap at interval end (endfunc call,
 *                          frame reset), AI_HOLD_FRAME inhibits advance,
 *                          no animation → no-op
 *   G_RunEntity          — stat fields compressed after run,
 *                          ability index updated from currentmove (non-zero)
 *   Ability lookup       — FindAbilityByClassname hit/miss,
 *                          GetAbilityByIndex, GetAbilityIndex
 *   player_pay           — deducts gold on success,
 *                          refuses when gold insufficient,
 *                          refuses when lumber insufficient,
 *                          NULL player guard
 *   unit_add_build_queue — single item, chained items
 *   Quest system         — G_MakeQuest fields, set/query, G_RemoveQuest
 *   G_PublishEvent       — queue write/read using ring-buffer semantics
 */

#include "test_framework.h"
#include "test_harness.h"
#include "../game/skills/s_skills.h"

/* Forward declarations for internal functions not in any public header. */
BOOL  player_pay(LPPLAYER ps, DWORD project);
void  T_Damage(LPEDICT target, LPEDICT attacker, int damage);
void  attack_melee(LPEDICT self);
void  attack_melee_cooldown(LPEDICT self);
void  attack_ranged_cooldown(LPEDICT self);
void  M_MoveFrame(LPEDICT self);
void  G_RunEntity(LPEDICT ent);
void  unit_add_build_queue(LPEDICT self, LPEDICT item);
void  order_move(LPEDICT self, LPEDICT target);

/* ==========================================================================
 * Shared helpers
 * ========================================================================== */

/* Minimal die() stub that records calls without touching the move state. */
static int _die_call_count = 0;
static LPEDICT _die_last_attacker = NULL;
static void stub_die(LPEDICT self, LPEDICT attacker) {
    (void)self;
    _die_call_count++;
    _die_last_attacker = attacker;
}

static LPEDICT make_combat_unit(DWORD class_id, FLOAT hp, FLOAT x, FLOAT y) {
    LPEDICT ent       = alloc_test_unit(class_id, x, y);
    ent->health.value     = hp;
    ent->health.max_value = hp;
    ent->stand            = unit_stand;
    ent->die              = stub_die;
    ent->svflags         |= SVF_MONSTER;
    unit_stand(ent);
    return ent;
}

static animation_t _stub_anim = {
    .name       = "stand",
    .interval   = { 0, 300 }   /* 300 ms long animation */
};

/* Wire a real animation into an entity so M_MoveFrame has something to work with. */
static void attach_stub_anim(LPEDICT ent) {
    ent->animation = &_stub_anim;
}

/* ==========================================================================
 * T_Damage
 * ========================================================================== */

static void test_tdamage_reduces_health(void) {
    LPEDICT target   = make_combat_unit(UNIT_ID("hfoo"), 420.0f, 0.0f, 0.0f);
    LPEDICT attacker = make_combat_unit(UNIT_ID("hpea"), 250.0f, 50.0f, 0.0f);
    _die_call_count  = 0;

    T_Damage(target, attacker, 100);

    ASSERT_EQ_FLOAT(target->health.value, 320.0f, 0.01f);
    ASSERT_EQ_INT(_die_call_count, 0);
}

static void test_tdamage_lethal_calls_die(void) {
    LPEDICT target   = make_combat_unit(UNIT_ID("hfoo"), 100.0f, 0.0f, 0.0f);
    LPEDICT attacker = make_combat_unit(UNIT_ID("hpea"), 250.0f, 50.0f, 0.0f);
    _die_call_count  = 0;
    _die_last_attacker = NULL;

    T_Damage(target, attacker, 100);

    ASSERT_EQ_INT(_die_call_count, 1);
    ASSERT(target->health.value == 0.0f);
    ASSERT(_die_last_attacker == attacker);
}

static void test_tdamage_lethal_resets_attacker_to_stand(void) {
    LPEDICT target   = make_combat_unit(UNIT_ID("hfoo"), 50.0f, 0.0f, 0.0f);
    LPEDICT attacker = make_combat_unit(UNIT_ID("hpea"), 250.0f, 50.0f, 0.0f);
    _die_call_count  = 0;

    T_Damage(target, attacker, 100);

    /* After a kill the attacker's move should be the stand animation. */
    ASSERT_NOT_NULL(attacker->currentmove);
    ASSERT_STR_EQ(attacker->currentmove->animation, "stand");
}

static void test_tdamage_non_lethal_does_not_call_die(void) {
    LPEDICT target   = make_combat_unit(UNIT_ID("hfoo"), 420.0f, 0.0f, 0.0f);
    LPEDICT attacker = make_combat_unit(UNIT_ID("hpea"), 250.0f, 50.0f, 0.0f);
    _die_call_count  = 0;

    T_Damage(target, attacker, 1);

    ASSERT_EQ_INT(_die_call_count, 0);
    ASSERT(target->health.value > 0.0f);
}

static void test_tdamage_invulnerable_ignores_damage(void) {
    LPEDICT target   = make_combat_unit(UNIT_ID("hfoo"), 420.0f, 0.0f, 0.0f);
    LPEDICT attacker = make_combat_unit(UNIT_ID("hpea"), 250.0f, 50.0f, 0.0f);
    target->invulnerable = true;
    _die_call_count = 0;

    T_Damage(target, attacker, 9999);

    ASSERT_EQ_FLOAT(target->health.value, 420.0f, 0.01f);
    ASSERT_EQ_INT(_die_call_count, 0);
}

/* ==========================================================================
 * M_MoveFrame
 * ========================================================================== */

static int _endfunc_called = 0;
static void stub_endfunc(LPEDICT ent) {
    (void)ent;
    _endfunc_called++;
}

static umove_t _stub_move = { "stand", NULL, stub_endfunc, NULL };

static void test_mmoveframe_no_animation_is_noop(void) {
    LPEDICT ent      = make_combat_unit(UNIT_ID("hpea"), 250.0f, 0.0f, 0.0f);
    ent->animation   = NULL;
    ent->currentmove = &_stub_move;
    ent->s.frame     = 0;

    M_MoveFrame(ent);

    ASSERT_EQ_INT((int)ent->s.frame, 0);
}

static void test_mmoveframe_hold_frame_flag_inhibits(void) {
    LPEDICT ent      = make_combat_unit(UNIT_ID("hpea"), 250.0f, 0.0f, 0.0f);
    attach_stub_anim(ent);
    ent->currentmove = &_stub_move;
    ent->s.frame     = 100;
    ent->aiflags    |= AI_HOLD_FRAME;

    M_MoveFrame(ent);

    ASSERT_EQ_INT((int)ent->s.frame, 100);
}

static void test_mmoveframe_normal_advance(void) {
    /* FRAMETIME = 100, animation interval [0, 300].
     * Start at frame 50 → next frame = 150 (still inside interval). */
    LPEDICT ent      = make_combat_unit(UNIT_ID("hpea"), 250.0f, 0.0f, 0.0f);
    attach_stub_anim(ent);
    ent->currentmove = &_stub_move;
    ent->s.frame     = 50;
    _endfunc_called  = 0;

    M_MoveFrame(ent);

    ASSERT_EQ_INT((int)ent->s.frame, 150);
    ASSERT_EQ_INT(_endfunc_called, 0);
}

static void test_mmoveframe_at_end_calls_endfunc_and_wraps(void) {
    /* Start at frame 250 → next = 350 >= 300 (end) → endfunc, wrap to 0. */
    LPEDICT ent      = make_combat_unit(UNIT_ID("hpea"), 250.0f, 0.0f, 0.0f);
    attach_stub_anim(ent);
    ent->currentmove = &_stub_move;
    ent->s.frame     = 250;
    _endfunc_called  = 0;

    M_MoveFrame(ent);

    ASSERT_EQ_INT(_endfunc_called, 1);
    /* Without AI_HOLD_FRAME the frame resets to interval[0]. */
    ASSERT_EQ_INT((int)ent->s.frame, 0);
}

static void test_mmoveframe_out_of_range_frame_resets(void) {
    /* frame > interval[1] → clamped to interval[0]. */
    LPEDICT ent      = make_combat_unit(UNIT_ID("hpea"), 250.0f, 0.0f, 0.0f);
    attach_stub_anim(ent);
    ent->currentmove = &_stub_move;
    ent->s.frame     = 9999;
    _endfunc_called  = 0;

    M_MoveFrame(ent);

    ASSERT_EQ_INT((int)ent->s.frame, 0);
    ASSERT_EQ_INT(_endfunc_called, 0);
}

/* ==========================================================================
 * G_RunEntity
 * ========================================================================== */

static void test_runentity_stat_fields_updated(void) {
    LPEDICT ent      = make_combat_unit(UNIT_ID("hfoo"), 420.0f, 0.0f, 0.0f);
    /* Manually set health below max so we get a non-trivial compressed value. */
    ent->health.max_value = 400.0f;
    ent->health.value     = 200.0f;   /* 50% → 127 */
    ent->mana.max_value   = 100.0f;
    ent->mana.value       = 100.0f;   /* 100% → 255 */
    ent->movetype         = MOVETYPE_NONE;

    G_RunEntity(ent);

    ASSERT_EQ_INT((int)ent->s.stats[ENT_HEALTH], 127);
    ASSERT_EQ_INT((int)ent->s.stats[ENT_MANA],   255);
}

static void test_runentity_ability_index_from_currentmove(void) {
    /* Use order_move to place the entity into the walk state.  The walk
     * umove_t has ability == &a_move, whose index in abilitylist[] is
     * non-zero (a_stop is at index 0).  This ensures the assertion
     * would catch G_RunEntity hard-coding s.ability = 0. */
    LPEDICT ent      = make_combat_unit(UNIT_ID("hpea"), 250.0f, 0.0f, 0.0f);
    ent->movetype    = MOVETYPE_NONE;
    VECTOR2 dest     = MAKE(VECTOR2, 100.0f, 100.0f);
    LPEDICT waypoint = Waypoint_add(&dest);
    order_move(ent, waypoint);  /* sets currentmove->ability = &a_move */
    ASSERT_NOT_NULL(ent->currentmove);
    ASSERT_NOT_NULL(ent->currentmove->ability);

    G_RunEntity(ent);

    DWORD expected = GetAbilityIndex(ent->currentmove->ability);
    ASSERT(expected != 0);  /* a_move is not the first entry (a_stop is) */
    ASSERT_EQ_INT((int)ent->s.ability, (int)expected);
}

/* Hit-point regeneration (WC3 'uhpr'/'uhrt'): a wounded "always"-regen unit
 * heals by rate * frametime each frame; "none" never heals; healing caps at
 * max HP.  Test data (test_harness.c): hfoo regenHP 0.5 "always", hbar "none". */
static void test_runentity_hp_regen_always(void) {
    LPEDICT ent           = make_combat_unit(UNIT_ID("hfoo"), 420.0f, 0.0f, 0.0f);
    ent->health.max_value = 420.0f;
    ent->health.value     = 200.0f;
    ent->mana.max_value   = 0.0f;
    ent->movetype         = MOVETYPE_NONE;

    G_RunEntity(ent);

    ASSERT_EQ_FLOAT(ent->health.value, 200.0f + 0.5f * (FRAMETIME / 1000.0f), 0.0001f);
}

static void test_runentity_hp_regen_caps_at_max(void) {
    LPEDICT ent           = make_combat_unit(UNIT_ID("hfoo"), 420.0f, 0.0f, 0.0f);
    ent->health.max_value = 420.0f;
    ent->health.value     = 419.99f;   /* less than one frame's regen from full */
    ent->movetype         = MOVETYPE_NONE;

    G_RunEntity(ent);

    ASSERT_EQ_FLOAT(ent->health.value, 420.0f, 0.0001f);
}

static void test_runentity_hp_regen_none_does_not_heal(void) {
    /* regenType "none" must not heal even though regenHP is positive. */
    LPEDICT ent           = make_combat_unit(UNIT_ID("hbar"), 1500.0f, 0.0f, 0.0f);
    ent->health.max_value = 1500.0f;
    ent->health.value     = 1000.0f;
    ent->movetype         = MOVETYPE_NONE;

    G_RunEntity(ent);

    ASSERT_EQ_FLOAT(ent->health.value, 1000.0f, 0.0001f);
}

/* Hero attribute -> derived-stat scaling (G_RecomputeHeroStats).  Test hero
 * "Hpal" (test_harness.c) has real Paladin bases: realHP 650, realM 255,
 * realdef 3.9, STR 22 / INT 17 / AGI 13.  Per WC3: +25 HP/STR, +15 mana/INT,
 * +0.3 armor/AGI. */
static void test_hero_strength_adds_hp(void) {
    LPEDICT hero          = make_combat_unit(UNIT_ID("Hpal"), 650.0f, 0.0f, 0.0f);
    hero->hero.str        = 22;            /* base */
    hero->health.max_value = 650.0f;
    hero->health.value     = 650.0f;

    hero->hero.str = 25;                   /* +3 STR */
    G_RecomputeHeroStats(hero);

    ASSERT_EQ_FLOAT(hero->health.max_value, 650.0f + 3 * 25.0f, 0.01f); /* 725 */
    ASSERT_EQ_FLOAT(hero->health.value,     650.0f + 3 * 25.0f, 0.01f); /* heals by gain */
}

static void test_hero_intelligence_adds_mana(void) {
    LPEDICT hero        = make_combat_unit(UNIT_ID("Hpal"), 650.0f, 0.0f, 0.0f);
    hero->hero.intel    = 17;              /* base */
    hero->mana.max_value = 255.0f;
    hero->mana.value     = 255.0f;

    hero->hero.intel = 20;                 /* +3 INT */
    G_RecomputeHeroStats(hero);

    ASSERT_EQ_FLOAT(hero->mana.max_value, 255.0f + 3 * 15.0f, 0.01f); /* 300 */
}

static void test_hero_agility_adds_armor(void) {
    LPEDICT hero       = make_combat_unit(UNIT_ID("Hpal"), 650.0f, 0.0f, 0.0f);
    hero->hero.agi     = 13;               /* base */
    hero->armor_value  = 3.9f;

    hero->hero.agi = 23;                    /* +10 AGI */
    G_RecomputeHeroStats(hero);

    ASSERT_EQ_FLOAT(hero->armor_value, 3.9f + 10 * 0.3f, 0.01f); /* 6.9 */
}

/* A hero's Strength adds +0.05 HP regen/sec per point; Intelligence adds +0.05
 * mana regen/sec per point (on top of the unit's base regen). */
static void test_hero_strength_hp_regen_bonus(void) {
    LPEDICT h            = make_combat_unit(UNIT_ID("Hpal"), 650.0f, 0.0f, 0.0f);
    h->hero.str          = 22;
    h->health.max_value  = 650.0f; h->health.value = 600.0f;  /* wounded */
    h->mana.max_value    = 0.0f;                              /* no mana regen */
    h->movetype          = MOVETYPE_NONE;

    G_RunEntity(h);

    ASSERT_EQ_FLOAT(h->health.value, 600.0f + 22 * 0.05f * (FRAMETIME / 1000.0f), 0.001f);
}

static void test_hero_intelligence_mana_regen_bonus(void) {
    LPEDICT h          = make_combat_unit(UNIT_ID("Hpal"), 650.0f, 0.0f, 0.0f);
    h->hero.intel      = 17;
    h->health.max_value = 650.0f; h->health.value = 650.0f;   /* full -> no HP regen */
    h->mana.max_value  = 255.0f; h->mana.value = 100.0f;
    h->movetype        = MOVETYPE_NONE;

    G_RunEntity(h);

    ASSERT_EQ_FLOAT(h->mana.value, 100.0f + 17 * 0.05f * (FRAMETIME / 1000.0f), 0.001f);
}

static void test_hero_primary_attribute_adds_damage(void) {
    /* Hpal's Primary is STR, so attack damage rises +1 per Strength point. */
    LPEDICT h     = make_combat_unit(UNIT_ID("Hpal"), 650.0f, 0.0f, 0.0f);
    h->hero.str   = 22;
    G_RecomputeHeroStats(h);
    FLOAT const dmg0 = h->attack1.damageBase;

    h->hero.str = 30;            /* +8 Strength */
    G_RecomputeHeroStats(h);

    ASSERT_EQ_FLOAT(h->attack1.damageBase, dmg0 + 8.0f, 0.01f);
}

static void test_hero_stats_noop_for_non_hero(void) {
    /* Footman has no attributes — recompute must leave its stats untouched. */
    LPEDICT u            = make_combat_unit(UNIT_ID("hfoo"), 420.0f, 0.0f, 0.0f);
    u->health.max_value  = 420.0f;
    u->health.value      = 300.0f;
    u->hero.str          = 99;             /* bogus; must be ignored */

    G_RecomputeHeroStats(u);

    ASSERT_EQ_FLOAT(u->health.max_value, 420.0f, 0.01f);
    ASSERT_EQ_FLOAT(u->health.value,     300.0f, 0.01f);
}

/* Hero XP / leveling (verified vs WC3 1.29).  XP-to-reach-L = 50*L*(L+1)-100;
 * attributes = base + trunc((L-1)*perLevel).  Hpal test hero: STR/INT/AGI per
 * level 2.7 / 1.8 / 1.5. */
static void test_hero_xp_for_level_table(void) {
    ASSERT_EQ_INT((int)G_HeroXPForLevel(1), 0);
    ASSERT_EQ_INT((int)G_HeroXPForLevel(2), 200);
    ASSERT_EQ_INT((int)G_HeroXPForLevel(3), 500);
    ASSERT_EQ_INT((int)G_HeroXPForLevel(4), 900);
    ASSERT_EQ_INT((int)G_HeroXPForLevel(10), 5400);
}

static void test_hero_level_for_xp(void) {
    ASSERT_EQ_INT((int)G_HeroLevelForXP(0),   1);
    ASSERT_EQ_INT((int)G_HeroLevelForXP(199), 1);
    ASSERT_EQ_INT((int)G_HeroLevelForXP(200), 2);
    ASSERT_EQ_INT((int)G_HeroLevelForXP(499), 2);
    ASSERT_EQ_INT((int)G_HeroLevelForXP(500), 3);
    ASSERT_EQ_INT((int)G_HeroLevelForXP(99999999), 10); /* capped at MaxHeroLevel */
}

static void test_hero_apply_level_truncates_attributes(void) {
    LPEDICT h            = make_combat_unit(UNIT_ID("Hpal"), 650.0f, 0.0f, 0.0f);
    h->health.max_value  = 650.0f; h->health.value = 650.0f;
    h->mana.max_value    = 255.0f; h->mana.value   = 255.0f;
    h->armor_value       = 3.9f;

    G_HeroApplyLevel(h, 3);  /* steps=2: STR+trunc(5.4)=5, INT+trunc(3.6)=3, AGI+trunc(3.0)=3 */

    ASSERT_EQ_INT((int)h->hero.str,   27);
    ASSERT_EQ_INT((int)h->hero.intel, 20);
    ASSERT_EQ_INT((int)h->hero.agi,   16);
    ASSERT_EQ_INT((int)h->hero.level, 3);
    ASSERT_EQ_FLOAT(h->health.max_value, 650.0f + 5 * 25.0f, 0.01f); /* 775 */
    ASSERT_EQ_FLOAT(h->mana.max_value,   255.0f + 3 * 15.0f, 0.01f); /* 300 */
    ASSERT_EQ_FLOAT(h->armor_value,      3.9f + 3 * 0.3f,    0.01f); /* 4.8 */
}

static void test_hero_setxp_levels_up(void) {
    LPEDICT h           = make_combat_unit(UNIT_ID("Hpal"), 650.0f, 0.0f, 0.0f);
    h->health.max_value = 650.0f; h->health.value = 650.0f;
    h->mana.max_value   = 255.0f; h->mana.value   = 255.0f;
    h->hero.level       = 1;

    G_HeroSetXP(h, 500);  /* crosses the level-3 threshold */

    ASSERT_EQ_INT((int)h->hero.level, 3);
    ASSERT_EQ_INT((int)h->hero.xp,    500);
    ASSERT_EQ_FLOAT(h->health.max_value, 775.0f, 0.01f);
}

/* XP-on-kill (G_GrantKillXP).  With no map misc overrides the WC3 defaults
 * apply: GrantNormalXP 25, HeroExpRange 1200.  A level-1 hero killing a level-1
 * normal unit in range gains the full 25 XP; out of range gains nothing. */
static void test_grant_kill_xp_awards_base(void) {
    LPEDICT killer = make_combat_unit(UNIT_ID("Hpal"), 650.0f, 0.0f, 0.0f);
    killer->s.player = 0; killer->hero.level = 1; killer->hero.xp = 0;
    LPEDICT victim = make_combat_unit(UNIT_ID("hfoo"), 420.0f, 0.0f, 0.0f);
    victim->s.player = 1;

    G_GrantKillXP(victim, killer);

    ASSERT_EQ_INT((int)killer->hero.xp, 25);
}

static void test_grant_kill_xp_out_of_range(void) {
    LPEDICT killer = make_combat_unit(UNIT_ID("Hpal"), 650.0f, 0.0f, 0.0f);
    killer->s.player = 0; killer->hero.level = 1; killer->hero.xp = 0;
    LPEDICT victim = make_combat_unit(UNIT_ID("hfoo"), 420.0f, 5000.0f, 0.0f);
    victim->s.player = 1;

    G_GrantKillXP(victim, killer);

    ASSERT_EQ_INT((int)killer->hero.xp, 0); /* > HeroExpRange (1200) away */
}

/* unit_learnability (used by the SelectHeroSkill native): learning an ability
 * adds it at level 1; learning it again raises its level; a second ability
 * takes its own slot. */
static void test_hero_learn_skill(void) {
    LPEDICT h = make_combat_unit(UNIT_ID("Hpal"), 650.0f, 0.0f, 0.0f);
    memset(h->heroabilities, 0, sizeof(h->heroabilities));

    unit_learnability(h, UNIT_ID("AHhb"));   /* Holy Light */
    ASSERT_EQ_INT((int)h->heroabilities[0].code, (int)UNIT_ID("AHhb"));
    ASSERT_EQ_INT((int)h->heroabilities[0].level, 1);

    unit_learnability(h, UNIT_ID("AHhb"));   /* upgrade to level 2 */
    ASSERT_EQ_INT((int)h->heroabilities[0].level, 2);

    unit_learnability(h, UNIT_ID("AHds"));   /* Divine Shield in slot 1 */
    ASSERT_EQ_INT((int)h->heroabilities[1].code, (int)UNIT_ID("AHds"));
    ASSERT_EQ_INT((int)h->heroabilities[1].level, 1);
}

/* ReviveHero: a dead hero comes back to life at the given point with HP/mana
 * from the revive factors (defaults: full life, no mana). */
static void test_hero_revive(void) {
    LPEDICT h           = make_combat_unit(UNIT_ID("Hpal"), 650.0f, 0.0f, 0.0f);
    h->health.max_value = 650.0f; h->health.value = 0.0f;   /* dead */
    h->mana.max_value   = 255.0f; h->mana.value   = 0.0f;
    h->svflags         |= SVF_DEADMONSTER;

    G_ReviveHero(h, 100.0f, 200.0f);

    ASSERT_EQ_FLOAT(h->health.value, 650.0f, 0.01f);   /* HeroReviveLifeFactor 1.0 */
    ASSERT_EQ_FLOAT(h->mana.value,   0.0f,   0.01f);   /* HeroReviveManaFactor 0.0 */
    ASSERT((h->svflags & SVF_DEADMONSTER) == 0);       /* alive again */
    ASSERT_EQ_FLOAT(h->s.origin2.x, 100.0f, 0.01f);
    ASSERT_EQ_FLOAT(h->s.origin2.y, 200.0f, 0.01f);
}

static void test_hero_levelup_fires_event(void) {
    LPEDICT h           = make_combat_unit(UNIT_ID("Hpal"), 650.0f, 0.0f, 0.0f);
    h->hero.level       = 1;
    h->health.max_value = 650.0f; h->health.value = 650.0f;
    level.events.write  = 0;
    level.events.read   = 0;

    G_HeroSetXP(h, 500);  /* level 1 -> 3: two level-ups -> two events */

    ASSERT_EQ_INT((int)level.events.write, 2);
    ASSERT_EQ_INT((int)level.events.queue[0].type, EVENT_PLAYER_HERO_LEVEL);
    ASSERT(level.events.queue[0].edict == h);
    ASSERT_EQ_INT((int)level.events.queue[1].type, EVENT_PLAYER_HERO_LEVEL);
}

/* Attack timing: the post-swing recovery is cooldown - damagePoint, so the full
 * attack cycle (windup + recovery) equals WC3's "Cooldown Time". */
static void test_attack_recovery_excludes_damage_point(void) {
    LPEDICT u              = make_combat_unit(UNIT_ID("hfoo"), 420.0f, 0.0f, 0.0f);
    u->attack1.cooldown    = 1.5f;
    u->attack1.damagePoint = 0.3f;
    attack_melee_cooldown(u);
    ASSERT_EQ_FLOAT(u->wait, 1.2f, 0.001f);   /* 1.5 - 0.3 */

    u->attack1.cooldown    = 2.0f;
    u->attack1.damagePoint = 0.5f;
    attack_ranged_cooldown(u);
    ASSERT_EQ_FLOAT(u->wait, 1.5f, 0.001f);   /* 2.0 - 0.5 */

    /* damagePoint >= cooldown clamps recovery to zero. */
    u->attack1.cooldown    = 0.4f;
    u->attack1.damagePoint = 0.5f;
    attack_melee_cooldown(u);
    ASSERT_EQ_FLOAT(u->wait, 0.0f, 0.001f);
}

/* A hero's Agility increases attack speed (+2%/point), dividing the windup and
 * recovery so the whole cycle speeds up. */
static void test_attack_speed_scales_with_agility(void) {
    LPEDICT h              = make_combat_unit(UNIT_ID("Hpal"), 650.0f, 0.0f, 0.0f);
    h->hero.agi            = 20;       /* +40% -> divisor 1.4 */
    h->attack1.cooldown    = 1.5f;
    h->attack1.damagePoint = 0.3f;

    attack_melee_cooldown(h);
    ASSERT_EQ_FLOAT(h->wait, (1.5f - 0.3f) / 1.4f, 0.001f);   /* recovery scaled */

    attack_melee(h);
    ASSERT_EQ_FLOAT(h->wait, 0.3f / 1.4f, 0.001f);            /* windup scaled */
}

/* ==========================================================================
 * Ability lookup
 * ========================================================================== */

static void test_find_ability_stop(void) {
    ability_t const *a = FindAbilityByClassname(STR_CmdStop);
    ASSERT_NOT_NULL(a);
}

static void test_find_ability_move(void) {
    ability_t const *a = FindAbilityByClassname(STR_CmdMove);
    ASSERT_NOT_NULL(a);
}

static void test_find_ability_unknown_returns_null(void) {
    ability_t const *a = FindAbilityByClassname("NotAnAbility");
    ASSERT_NULL(a);
}

static void test_get_ability_by_index_zero(void) {
    /* Index 0 is always the stop ability (first entry in abilitylist[]). */
    ability_t const *a = GetAbilityByIndex(0);
    ASSERT_NOT_NULL(a);
    ASSERT(a == FindAbilityByClassname(STR_CmdStop));
}

static void test_get_ability_by_index_out_of_range(void) {
    ability_t const *a = GetAbilityByIndex(9999);
    ASSERT_NULL(a);
}

static void test_get_ability_index_roundtrip(void) {
    ability_t const *a   = FindAbilityByClassname(STR_CmdMove);
    DWORD             idx = FindAbilityIndex(STR_CmdMove);
    ASSERT(GetAbilityByIndex(idx) == a);
}

static void test_registered_reference_ability_codes(void) {
    static LPCSTR codes[] = {
        "AHhb", "AHwe", "AHbz", "AHtb", "ANfb", "Apxf", "AOsf",
        "Abun", "Astd", "AEim", "Aenc", "Aent", "Aegm", "Aeat",
        "Ambt", "ANch", "AIco", "AHca", "Agld", "Agl2", "Abgm",
        "Abli", "Aaha", "Artn", "Ahar", "Awha", "Ahrl", "ANcl",
        "AUcs", "AInv", "Arep", "Aren", "Arst", "Avul", "Apit",
        "Aneu", "Aall", "Acoi", "AIhe", "AIma", "AIat", "AIab",
        "AIim", "AIsm", "AIam", "AIxm", "AIde", "AIml", "AImm",
        "AIfs", "AImi", "AIem", "AIlm", "Acar", "Aloa", "Adro",
        "Adri", "Aroo"
    };

    FOR_LOOP(i, sizeof(codes) / sizeof(codes[0])) {
        ASSERT_NOT_NULL(FindAbilityByClassname(codes[i]));
    }
}

static const char slk_ability_helpers[] =
    "ID;PWXL;N;EBB;Y3;X13\n"
    "C;Y1;X1;K\"alias\"\n"
    "C;Y1;X2;K\"code\"\n"
    "C;Y1;X3;K\"targs\"\n"
    "C;Y1;X4;K\"Cost1\"\n"
    "C;Y1;X5;K\"Cool1\"\n"
    "C;Y1;X6;K\"Rng1\"\n"
    "C;Y1;X7;K\"Dur1\"\n"
    "C;Y1;X8;K\"DataA1\"\n"
    "C;Y1;X9;K\"DataB1\"\n"
    "C;Y1;X10;K\"UnitID1\"\n"
    "C;Y1;X11;K\"Area1\"\n"
    "C;Y1;X12;K\"HeroDur1\"\n"
    "C;Y1;X13;K\"DataE1\"\n"
    "C;Y2;X1;K\"AHtb\"\n"
    "C;Y2;X2;K\"AHtb\"\n"
    "C;Y2;X3;K\"air,ground,enemy,neutral\"\n"
    "C;Y2;X4;K\"75\"\n"
    "C;Y2;X5;K\"9\"\n"
    "C;Y2;X6;K\"600\"\n"
    "C;Y2;X7;K\"5\"\n"
    "C;Y2;X8;K\"100\"\n"
    "C;Y2;X9;K\"55\"\n"
    "C;Y2;X12;K\"3\"\n"
    "C;Y2;X13;K\"42\"\n"
    "C;Y3;X1;K\"AHwe\"\n"
    "C;Y3;X2;K\"AHwe\"\n"
    "C;Y3;X4;K\"140\"\n"
    "C;Y3;X5;K\"20\"\n"
    "C;Y3;X7;K\"75\"\n"
    "C;Y3;X9;K\"2\"\n"
    "C;Y3;X10;K\"hwat\"\n"
    "E\n";

static void test_spell_helpers_read_slk_fields(void) {
    sheetRow_t *old_abilities = game.config.abilities;
    sheetRow_t *rows = parse_slk_string(slk_ability_helpers);
    DWORD thunder = MAKEFOURCC('A', 'H', 't', 'b');
    DWORD water = MAKEFOURCC('A', 'H', 'w', 'e');

    game.config.abilities = rows;

    ASSERT_EQ_FLOAT(S_SpellNumber(thunder, "Cost", 1), 75.0f, 0.01f);
    ASSERT_EQ_FLOAT(S_SpellRange(thunder, 1), 600.0f, 0.01f);
    ASSERT_EQ_FLOAT(S_SpellDuration(thunder, 1, true), 3.0f, 0.01f);
    ASSERT_EQ_FLOAT(S_SpellData(thunder, 1, 1), 100.0f, 0.01f); /* DataA1 */
    ASSERT_EQ_FLOAT(S_SpellData(thunder, 1, 2), 55.0f, 0.01f);  /* DataB1 */
    /* index 5 -> DataE1: the columns are Data<Letter><Level>, and index must
     * reach past D (the old code built numeric "Data15" and clamped index to 4,
     * so every DataE+ read — e.g. Shadow Strike's Initial Damage — returned 0). */
    ASSERT_EQ_FLOAT(S_SpellData(thunder, 1, 5), 42.0f, 0.01f);  /* DataE1 */
    ASSERT_EQ_INT((int)S_SpellUnitId(water, 1), (int)UNIT_ID("hwat"));

    game.config.abilities = old_abilities;
    free_slk_rows(rows);
}

static void test_spell_mana_and_cooldown(void) {
    sheetRow_t *old_abilities = game.config.abilities;
    sheetRow_t *rows = parse_slk_string(slk_ability_helpers);
    DWORD thunder = MAKEFOURCC('A', 'H', 't', 'b');
    LPEDICT caster = make_combat_unit(UNIT_ID("hpea"), 250.0f, 0.0f, 0.0f);
    caster->mana.value = 100.0f;
    caster->mana.max_value = 100.0f;
    level.time = 1000;
    game.config.abilities = rows;

    ASSERT(S_SpellCooldownReady(caster, thunder));
    ASSERT(S_SpellSpendMana(caster, thunder, 1));
    ASSERT_EQ_FLOAT(caster->mana.value, 25.0f, 0.01f);
    ASSERT(!S_SpellSpendMana(caster, thunder, 1));

    S_SpellStartCooldown(caster, thunder, 1);
    ASSERT(!S_SpellCooldownReady(caster, thunder));
    /* Just used -> full cooldown shade on the command-card icon. */
    ASSERT_EQ_FLOAT(S_SpellCooldownFraction(caster, thunder, 1), 1.0f, 0.01f);
    level.time += 4500; /* halfway through the 9s cooldown */
    ASSERT_EQ_FLOAT(S_SpellCooldownFraction(caster, thunder, 1), 0.5f, 0.01f);
    level.time += 4501; /* past the end */
    unit_updatestatuses(caster);
    ASSERT(S_SpellCooldownReady(caster, thunder));
    /* Ready again -> no shade. */
    ASSERT_EQ_FLOAT(S_SpellCooldownFraction(caster, thunder, 1), 0.0f, 0.01f);

    game.config.abilities = old_abilities;
    free_slk_rows(rows);
}

static void test_timed_stun_status_expires_without_touching_pause(void) {
    LPEDICT ent = make_combat_unit(UNIT_ID("hfoo"), 420.0f, 0.0f, 0.0f);
    ent->paused = true;
    level.time = 100;

    unit_addtimedstatus(ent, "Bstu", 1, 0.05f);

    ASSERT(ent->stunned);
    ASSERT(ent->paused);
    level.time = 151;
    unit_updatestatuses(ent);
    ASSERT(!ent->stunned);
    ASSERT(ent->paused);
}

static void test_timed_life_status_kills_unit(void) {
    LPEDICT ent = make_combat_unit(UNIT_ID("hfoo"), 420.0f, 0.0f, 0.0f);
    _die_call_count = 0;
    level.time = 200;

    unit_addtimedstatus(ent, "BTLF", 1, 0.05f);
    level.time = 251;
    unit_updatestatuses(ent);

    ASSERT_EQ_FLOAT(ent->health.value, 0.0f, 0.01f);
    ASSERT_EQ_INT(_die_call_count, 1);
}

/* ==========================================================================
 * player_pay
 * ========================================================================== */

/* Test unit data provides:
 *   hpea — goldcost=75,  lumbercost=0
 *   hfoo — goldcost=135, lumbercost=20
 * These are registered in setup_test_unit_data() via the UnitBalance table. */

static void test_player_pay_deducts_gold(void) {
    LPPLAYER p = &game.clients[0].ps;
    p->stats[PLAYERSTATE_RESOURCE_GOLD]   = 200;
    p->stats[PLAYERSTATE_RESOURCE_LUMBER] = 0;

    BOOL ok = player_pay(p, UNIT_ID("hpea")); /* costs 75 gold, 0 lumber */

    ASSERT(ok);
    ASSERT_EQ_INT((int)p->stats[PLAYERSTATE_RESOURCE_GOLD], 125);
}

static void test_player_pay_insufficient_gold_fails(void) {
    LPPLAYER p = &game.clients[0].ps;
    p->stats[PLAYERSTATE_RESOURCE_GOLD]   = 50;  /* need 75 */
    p->stats[PLAYERSTATE_RESOURCE_LUMBER] = 0;

    BOOL ok = player_pay(p, UNIT_ID("hpea"));

    ASSERT(!ok);
    ASSERT_EQ_INT((int)p->stats[PLAYERSTATE_RESOURCE_GOLD], 50); /* unchanged */
}

static void test_player_pay_insufficient_lumber_fails(void) {
    LPPLAYER p = &game.clients[0].ps;
    p->stats[PLAYERSTATE_RESOURCE_GOLD]   = 200; /* enough for 135 */
    p->stats[PLAYERSTATE_RESOURCE_LUMBER] = 10;  /* need 20 */

    BOOL ok = player_pay(p, UNIT_ID("hfoo")); /* costs 135 gold, 20 lumber */

    ASSERT(!ok);
    ASSERT_EQ_INT((int)p->stats[PLAYERSTATE_RESOURCE_GOLD],   200); /* unchanged */
    ASSERT_EQ_INT((int)p->stats[PLAYERSTATE_RESOURCE_LUMBER],  10); /* unchanged */
}

static void test_player_pay_null_player_fails(void) {
    BOOL ok = player_pay(NULL, UNIT_ID("hpea"));
    ASSERT(!ok);
}

/* ==========================================================================
 * unit_add_build_queue
 * ========================================================================== */

static void test_build_queue_first_item(void) {
    LPEDICT producer = make_combat_unit(UNIT_ID("hpea"), 250.0f, 0.0f, 0.0f);
    LPEDICT item1    = make_combat_unit(UNIT_ID("hfoo"), 420.0f, 10.0f, 0.0f);
    producer->build  = NULL;

    unit_add_build_queue(producer, item1);

    ASSERT(producer->build == item1);
    ASSERT_NULL(item1->build);
}

static void test_build_queue_chained_items(void) {
    LPEDICT producer = make_combat_unit(UNIT_ID("hpea"), 250.0f, 0.0f, 0.0f);
    LPEDICT item1    = make_combat_unit(UNIT_ID("hfoo"), 420.0f, 10.0f, 0.0f);
    LPEDICT item2    = make_combat_unit(UNIT_ID("hfoo"), 420.0f, 20.0f, 0.0f);
    producer->build  = NULL;
    item1->build     = NULL;
    item2->build     = NULL;

    unit_add_build_queue(producer, item1);
    unit_add_build_queue(producer, item2);

    ASSERT(producer->build == item1);
    ASSERT(item1->build    == item2);
    ASSERT_NULL(item2->build);
}

static void test_build_queue_three_items_linked(void) {
    LPEDICT producer = make_combat_unit(UNIT_ID("hpea"), 250.0f, 0.0f, 0.0f);
    LPEDICT item1    = make_combat_unit(UNIT_ID("hfoo"), 420.0f, 10.0f, 0.0f);
    LPEDICT item2    = make_combat_unit(UNIT_ID("hfoo"), 420.0f, 20.0f, 0.0f);
    LPEDICT item3    = make_combat_unit(UNIT_ID("hfoo"), 420.0f, 30.0f, 0.0f);
    producer->build = item1->build = item2->build = item3->build = NULL;

    unit_add_build_queue(producer, item1);
    unit_add_build_queue(producer, item2);
    unit_add_build_queue(producer, item3);

    ASSERT(producer->build == item1);
    ASSERT(item1->build    == item2);
    ASSERT(item2->build    == item3);
    ASSERT_NULL(item3->build);
}

/* ==========================================================================
 * Quest system
 * ========================================================================== */

static void test_quest_make_non_null(void) {
    LPQUEST q = G_MakeQuest();
    ASSERT_NOT_NULL(q);
    G_RemoveQuest(q);
}

static void test_quest_fields_default_false(void) {
    LPQUEST q = G_MakeQuest();
    ASSERT(!q->completed);
    ASSERT(!q->failed);
    ASSERT(!q->discovered);
    ASSERT(!q->required);
    ASSERT(!q->enabled);
    G_RemoveQuest(q);
}

static void test_quest_set_title(void) {
    LPQUEST q = G_MakeQuest();
    q->title = strdup("Defeat the Lich King");
    ASSERT_STR_EQ(q->title, "Defeat the Lich King");
    G_RemoveQuest(q);
}

static void test_quest_set_completed(void) {
    LPQUEST q = G_MakeQuest();
    q->completed = true;
    ASSERT(q->completed);
    G_RemoveQuest(q);
}

static void test_quest_set_failed(void) {
    LPQUEST q = G_MakeQuest();
    q->failed = true;
    ASSERT(q->failed);
    G_RemoveQuest(q);
}

static void test_quest_remove_clears_from_list(void) {
    /* Reset the quest list to a known-empty state. */
    level.quests = NULL;
    LPQUEST q = G_MakeQuest();
    ASSERT_NOT_NULL(level.quests);

    G_RemoveQuest(q);

    /* After removing the only quest the list must be empty. */
    ASSERT_NULL(level.quests);
}

/* ==========================================================================
 * G_PublishEvent
 * ========================================================================== */

static void test_publish_event_fills_queue(void) {
    /* Reset the event queue. */
    level.events.write = 0;
    level.events.read  = 0;

    LPEDICT ent = make_combat_unit(UNIT_ID("hpea"), 250.0f, 0.0f, 0.0f);
    GAMEEVENT *evt = G_PublishEvent(ent, EVENT_UNIT_DEATH);

    ASSERT_NOT_NULL(evt);
    ASSERT_EQ_INT((int)evt->type, (int)EVENT_UNIT_DEATH);
}

static void test_publish_event_sequential(void) {
    level.events.write = 0;
    level.events.read  = 0;

    LPEDICT ent = make_combat_unit(UNIT_ID("hpea"), 250.0f, 0.0f, 0.0f);
    G_PublishEvent(ent, EVENT_UNIT_DEATH);
    GAMEEVENT *evt2 = G_PublishEvent(ent, EVENT_PLAYER_UNIT_TRAIN_FINISH);

    ASSERT_NOT_NULL(evt2);
    ASSERT_EQ_INT((int)evt2->type, (int)EVENT_PLAYER_UNIT_TRAIN_FINISH);
}

/* ==========================================================================
 * Suite runner
 * ========================================================================== */

BEGIN_SUITE(combat)
    /* T_Damage */
    RUN_TEST(test_tdamage_reduces_health);
    RUN_TEST(test_tdamage_lethal_calls_die);
    RUN_TEST(test_tdamage_lethal_resets_attacker_to_stand);
    RUN_TEST(test_tdamage_non_lethal_does_not_call_die);
    RUN_TEST(test_tdamage_invulnerable_ignores_damage);

    /* M_MoveFrame */
    RUN_TEST(test_mmoveframe_no_animation_is_noop);
    RUN_TEST(test_mmoveframe_hold_frame_flag_inhibits);
    RUN_TEST(test_mmoveframe_normal_advance);
    RUN_TEST(test_mmoveframe_at_end_calls_endfunc_and_wraps);
    RUN_TEST(test_mmoveframe_out_of_range_frame_resets);

    /* G_RunEntity */
    RUN_TEST(test_runentity_stat_fields_updated);
    RUN_TEST(test_runentity_ability_index_from_currentmove);
    RUN_TEST(test_runentity_hp_regen_always);
    RUN_TEST(test_runentity_hp_regen_caps_at_max);
    RUN_TEST(test_runentity_hp_regen_none_does_not_heal);
    RUN_TEST(test_hero_strength_adds_hp);
    RUN_TEST(test_hero_intelligence_adds_mana);
    RUN_TEST(test_hero_agility_adds_armor);
    RUN_TEST(test_hero_strength_hp_regen_bonus);
    RUN_TEST(test_hero_intelligence_mana_regen_bonus);
    RUN_TEST(test_hero_primary_attribute_adds_damage);
    RUN_TEST(test_hero_stats_noop_for_non_hero);
    RUN_TEST(test_hero_xp_for_level_table);
    RUN_TEST(test_hero_level_for_xp);
    RUN_TEST(test_hero_apply_level_truncates_attributes);
    RUN_TEST(test_hero_setxp_levels_up);
    RUN_TEST(test_grant_kill_xp_awards_base);
    RUN_TEST(test_grant_kill_xp_out_of_range);
    RUN_TEST(test_hero_learn_skill);
    RUN_TEST(test_hero_revive);
    RUN_TEST(test_hero_levelup_fires_event);
    RUN_TEST(test_attack_recovery_excludes_damage_point);
    RUN_TEST(test_attack_speed_scales_with_agility);

    /* Ability lookup */
    RUN_TEST(test_find_ability_stop);
    RUN_TEST(test_find_ability_move);
    RUN_TEST(test_find_ability_unknown_returns_null);
    RUN_TEST(test_get_ability_by_index_zero);
    RUN_TEST(test_get_ability_by_index_out_of_range);
    RUN_TEST(test_get_ability_index_roundtrip);
    RUN_TEST(test_registered_reference_ability_codes);
    RUN_TEST(test_spell_helpers_read_slk_fields);
    RUN_TEST(test_spell_mana_and_cooldown);
    RUN_TEST(test_timed_stun_status_expires_without_touching_pause);
    RUN_TEST(test_timed_life_status_kills_unit);

    /* player_pay */
    RUN_TEST(test_player_pay_deducts_gold);
    RUN_TEST(test_player_pay_insufficient_gold_fails);
    RUN_TEST(test_player_pay_insufficient_lumber_fails);
    RUN_TEST(test_player_pay_null_player_fails);

    /* unit_add_build_queue */
    RUN_TEST(test_build_queue_first_item);
    RUN_TEST(test_build_queue_chained_items);
    RUN_TEST(test_build_queue_three_items_linked);

    /* Quest system */
    RUN_TEST(test_quest_make_non_null);
    RUN_TEST(test_quest_fields_default_false);
    RUN_TEST(test_quest_set_title);
    RUN_TEST(test_quest_set_completed);
    RUN_TEST(test_quest_set_failed);
    RUN_TEST(test_quest_remove_clears_from_list);

    /* G_PublishEvent */
    RUN_TEST(test_publish_event_fills_queue);
    RUN_TEST(test_publish_event_sequential);
END_SUITE()
