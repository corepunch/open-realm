#ifdef BZ_TESTS
#include <stddef.h>
/* Forward declarations — defined in t_slk.c, compiled together in unity build. */
slkTestData_t *parse_slk_string(const char *slk_text);
void free_slk_rows(slkTestData_t *rows);
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
 *   G_AttackDamage       — representative attack×defense table cells (pierce/
 *                          small=2.0, normal/medium=1.5, siege/fort=1.5,
 *                          magic/large=2.0, chaos passthrough, hero/fort=0.5),
 *                          data-driven armor/type constants, Divine reduction,
 *                          exponential negative armor, minimum-1 clamp
 *   Ability lookup       — FindAbilityByClassname hit/miss,
 *                          command-name/rawcode resolution, GetAbilityByIndex,
 *                          GetAbilityIndex
 *   player_pay           — deducts gold on success,
 *                          refuses when gold insufficient,
 *                          refuses when lumber insufficient,
 *                          NULL player guard
 *   unit_add_build_queue — single item, chained items
 *   Quest system         — G_MakeQuest fields, set/query, G_RemoveQuest
 *   G_PublishEvent       — queue write/read using ring-buffer semantics
 */

#include "test.h"
#include "../g_local.h"

/* Helpers defined in t_utils.c */
LPEDICT alloc_test_unit(DWORD class_id, FLOAT x, FLOAT y);
void reset_entities(void);
void setup_test_world(void);


#include "../game/skills/s_skills.h"

/* Forward declarations for internal functions not in any public header. */
BOOL  player_pay(LPPLAYER ps, DWORD project);
void  T_Damage(LPEDICT target, LPEDICT attacker, int damage);
int   G_AttackDamage(LPEDICT attacker, LPEDICT target, int base);
void  attack_melee(LPEDICT self);
void  attack_melee_cooldown(LPEDICT self);
void  attack_ranged_cooldown(LPEDICT self);
BOOL  attack_menu_selecttarget(LPEDICT ent, LPEDICT target);
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
 * Ability presentation effects
 * ========================================================================== */

TEST(wc3_effects, ability_effect_art_selects_requested_entry_and_last_fallback) {
    DWORD const holy_light = MAKEFOURCC('A','H','h','b');

    setup_test_world();
    T_STREQ(G_AbilityEffectArt(holy_light, WC3_EFFECT_TARGET, 0),
            "TestUI\\Models\\anim_pulse.mdx");
    T_STREQ(G_AbilityEffectArt(holy_light, WC3_EFFECT_TARGET, 1),
            "TestUI\\Models\\quad_sprite.mdx");
    T_STREQ(G_AbilityEffectArt(holy_light, WC3_EFFECT_TARGET, 99),
            "TestUI\\Models\\quad_sprite.mdx");
    T_STREQ(G_AbilityEffectArt(holy_light, WC3_EFFECT_SPECIAL, 0),
            "TestUI\\Models\\panel_sprite.mdx");
    T_STREQ(G_AbilityEffectArt(holy_light, WC3_EFFECT_AREA_EFFECT, 0),
            "TestUI\\Models\\ui_panel.mdx");
    T_NULL(G_AbilityEffectArt(holy_light, WC3_EFFECT_LIGHTNING, 0));

    T_STREQ(G_AbilityEffectArt(MAKEFOURCC('B','i','m','l'), WC3_EFFECT_TARGET, 0),
            "TestUI\\Models\\anim_pulse.mdx");
    T_STREQ(G_AbilityEffectArt(MAKEFOURCC('B','i','m','l'), WC3_EFFECT_SPECIAL, 0),
            "TestUI\\Models\\panel_sprite.mdx");
    T_STREQ(G_AbilityEffectArt(MAKEFOURCC('B','i','m','l'), WC3_EFFECT_EFFECT, 0),
            "TestUI\\Models\\quad_sprite.mdx");
}

/* ==========================================================================
 * T_Damage
 * ========================================================================== */

TEST(wc3_combat, tdamage_reduces_health) {
    LPEDICT target   = make_combat_unit(MAKEFOURCC('h','f','o','o'), 420.0f, 0.0f, 0.0f);
    LPEDICT attacker = make_combat_unit(MAKEFOURCC('h','p','e','a'), 250.0f, 50.0f, 0.0f);
    _die_call_count  = 0;

    T_Damage(target, attacker, 100);

    T_FEQ(target->health.value, 320.0f, 0.01f);
    T_EQ(_die_call_count, 0);
}

TEST(wc3_combat, tdamage_lethal_calls_die) {
    LPEDICT target   = make_combat_unit(MAKEFOURCC('h','f','o','o'), 100.0f, 0.0f, 0.0f);
    LPEDICT attacker = make_combat_unit(MAKEFOURCC('h','p','e','a'), 250.0f, 50.0f, 0.0f);
    _die_call_count  = 0;
    _die_last_attacker = NULL;

    T_Damage(target, attacker, 100);

    T_EQ(_die_call_count, 1);
    T_ASSERT(target->health.value == 0.0f);
    T_ASSERT(_die_last_attacker == attacker);
}

TEST(wc3_combat, tdamage_lethal_resets_attacker_to_stand) {
    LPEDICT target   = make_combat_unit(MAKEFOURCC('h','f','o','o'), 50.0f, 0.0f, 0.0f);
    LPEDICT attacker = make_combat_unit(MAKEFOURCC('h','p','e','a'), 250.0f, 50.0f, 0.0f);
    _die_call_count  = 0;

    T_Damage(target, attacker, 100);

    /* After a kill the attacker's move should be the stand animation. */
    T_NOT_NULL(attacker->currentmove);
    T_STREQ(attacker->currentmove->animation, "stand");
}

TEST(wc3_combat, tdamage_non_lethal_does_not_call_die) {
    LPEDICT target   = make_combat_unit(MAKEFOURCC('h','f','o','o'), 420.0f, 0.0f, 0.0f);
    LPEDICT attacker = make_combat_unit(MAKEFOURCC('h','p','e','a'), 250.0f, 50.0f, 0.0f);
    _die_call_count  = 0;

    T_Damage(target, attacker, 1);

    T_EQ(_die_call_count, 0);
    T_ASSERT(target->health.value > 0.0f);
}

TEST(wc3_combat, tdamage_invulnerable_ignores_damage) {
    LPEDICT target   = make_combat_unit(MAKEFOURCC('h','f','o','o'), 420.0f, 0.0f, 0.0f);
    LPEDICT attacker = make_combat_unit(MAKEFOURCC('h','p','e','a'), 250.0f, 50.0f, 0.0f);
    target->invulnerable = true;
    _die_call_count = 0;

    T_Damage(target, attacker, 9999);

    T_FEQ(target->health.value, 420.0f, 0.01f);
    T_EQ(_die_call_count, 0);
}

TEST(wc3_combat, friendly_damage_does_not_trigger_counterattack) {
    LPEDICT target   = make_combat_unit(MAKEFOURCC('h','f','o','o'), 420.0f, 0.0f, 0.0f);
    LPEDICT attacker = make_combat_unit(MAKEFOURCC('h','f','o','o'), 420.0f, 50.0f, 0.0f);
    target->s.player = 0;
    attacker->s.player = 0;
    target->attack1.type = ATK_NORMAL;

    T_Damage(target, attacker, 1);

    T_ASSERT(target->goalentity != attacker);
}

/* Explicit Attack-button targeting deliberately differs from Smart/right-click:
 * it may force-fire on friendly units and buildings. */
TEST(wc3_combat, attack_button_accepts_owned_building) {
    LPEDICT clent = &g_edicts[0];
    LPEDICT attacker = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0.0f, 0.0f);
    LPEDICT building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 100.0f, 0.0f);

    clent->s.player = 0;
    attacker->s.player = 0;
    building->s.player = 0;
    attacker->selected = 1 << clent->client->ps.number;
    building->svflags |= SVF_MONSTER;
    building->health.value = 100.0f;
    building->health.max_value = 100.0f;

    T_ASSERT(attack_menu_selecttarget(clent, building));
    T_ASSERT(attacker->goalentity == building);
}

TEST(wc3_combat, attack_button_accepts_owned_nonbuilding_unit) {
    LPEDICT clent = &g_edicts[0];
    LPEDICT attacker = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0.0f, 0.0f);
    LPEDICT friendly = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 100.0f, 0.0f);

    clent->s.player = 0;
    attacker->s.player = 0;
    friendly->s.player = 0;
    attacker->selected = 1 << clent->client->ps.number;
    friendly->svflags |= SVF_MONSTER;
    friendly->health.value = 100.0f;
    friendly->health.max_value = 100.0f;

    T_ASSERT(attack_menu_selecttarget(clent, friendly));
    T_ASSERT(attacker->goalentity == friendly);
}

TEST(wc3_combat, attack_button_accepts_allied_unit) {
    LPEDICT clent = &g_edicts[0];
    LPEDICT attacker = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0.0f, 0.0f);
    LPEDICT friendly = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 100.0f, 0.0f);

    clent->s.player = 0;
    attacker->s.player = 0;
    friendly->s.player = 1;
    level.alliances[0][1] = 1;
    attacker->selected = 1 << clent->client->ps.number;
    friendly->svflags |= SVF_MONSTER;
    friendly->health.value = 100.0f;
    friendly->health.max_value = 100.0f;

    T_ASSERT(attack_menu_selecttarget(clent, friendly));
    T_ASSERT(attacker->goalentity == friendly);
}

TEST(wc3_combat, attack_button_does_not_order_unit_to_attack_itself) {
    LPEDICT clent = &g_edicts[0];
    LPEDICT unit = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0.0f, 0.0f);

    clent->s.player = 0;
    unit->s.player = 0;
    unit->selected = 1 << clent->client->ps.number;
    unit->svflags |= SVF_MONSTER;
    unit->health.value = 100.0f;
    unit->health.max_value = 100.0f;

    T_ASSERT(!attack_menu_selecttarget(clent, unit));
    T_NULL(unit->goalentity);
}

/* Building attacks use the authored no-walk footprint as their interaction
 * boundary.  A melee unit beside a large building may be well outside attack
 * range of the building centre while already being in range of its wall. */
TEST(wc3_combat, attack_owned_building_starts_at_pathing_footprint_range) {
    enum { W = 8, H = 8 };
    LPEDICT attacker;
    LPEDICT building;
    pathTex_t *pathtex;

    setup_test_world();
    attacker = make_combat_unit(MAKEFOURCC('h','f','o','o'), 420.0f, 150.0f, 0.0f);
    building = make_combat_unit(MAKEFOURCC('h','b','a','r'), 1500.0f, 0.0f, 0.0f);
    attacker->s.player = 0;
    building->s.player = 0;
    attacker->collision = 16.0f;
    attacker->attack1.type = ATK_NORMAL;
    attacker->attack1.weapon = WPN_NORMAL;
    attacker->attack1.range = 90.0f;

    pathtex = gi.MemAlloc(sizeof(*pathtex) + W * H * sizeof(COLOR32));
    T_NOT_NULL(pathtex);
    pathtex->width = W;
    pathtex->height = H;
    FOR_LOOP(i, W * H)
        pathtex->map[i] = (COLOR32){ 0, 0, 255, 255 };
    building->pathtex = pathtex;

    /* Centre distance is intentionally beyond melee range. */
    T_ASSERT(Vector2_distance(&attacker->s.origin2, &building->s.origin2) >
             attacker->attack1.range);
    T_ASSERT(CM_DistanceToPathingFootprint(building, &attacker->s.origin2) <=
             attacker->collision + attacker->attack1.range);

    order_attack(attacker, building);
    T_STREQ(attacker->currentmove->animation, "walk");
    attacker->currentmove->think(attacker);

    T_STREQ(attacker->currentmove->animation, "attack");
    gi.MemFree(pathtex);
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

TEST(wc3_combat, mmoveframe_no_animation_is_noop) {
    LPEDICT ent      = make_combat_unit(MAKEFOURCC('h','p','e','a'), 250.0f, 0.0f, 0.0f);
    ent->animation   = NULL;
    ent->currentmove = &_stub_move;
    ent->s.frame     = 0;

    M_MoveFrame(ent);

    T_EQ((int)ent->s.frame, 0);
}

TEST(wc3_combat, mmoveframe_hold_frame_flag_inhibits) {
    LPEDICT ent      = make_combat_unit(MAKEFOURCC('h','p','e','a'), 250.0f, 0.0f, 0.0f);
    attach_stub_anim(ent);
    ent->currentmove = &_stub_move;
    ent->s.frame     = 100;
    ent->aiflags    |= AI_HOLD_FRAME;

    M_MoveFrame(ent);

    T_EQ((int)ent->s.frame, 100);
}

TEST(wc3_combat, mmoveframe_normal_advance) {
    /* FRAMETIME = 100, animation interval [0, 300].
     * Start at frame 50 → next frame = 150 (still inside interval). */
    LPEDICT ent      = make_combat_unit(MAKEFOURCC('h','p','e','a'), 250.0f, 0.0f, 0.0f);
    attach_stub_anim(ent);
    ent->currentmove = &_stub_move;
    ent->s.frame     = 50;
    _endfunc_called  = 0;

    M_MoveFrame(ent);

    T_EQ((int)ent->s.frame, 150);
    T_EQ(_endfunc_called, 0);
}

TEST(wc3_combat, mmoveframe_at_end_calls_endfunc_and_wraps) {
    /* Start at frame 250 → next = 350 >= 300 (end) → endfunc, wrap to 0. */
    LPEDICT ent      = make_combat_unit(MAKEFOURCC('h','p','e','a'), 250.0f, 0.0f, 0.0f);
    attach_stub_anim(ent);
    ent->currentmove = &_stub_move;
    ent->s.frame     = 250;
    _endfunc_called  = 0;

    M_MoveFrame(ent);

    T_EQ(_endfunc_called, 1);
    /* Without AI_HOLD_FRAME the frame resets to interval[0]. */
    T_EQ((int)ent->s.frame, 0);
}

TEST(wc3_combat, mmoveframe_out_of_range_frame_resets) {
    /* frame > interval[1] → clamped to interval[0]. */
    LPEDICT ent      = make_combat_unit(MAKEFOURCC('h','p','e','a'), 250.0f, 0.0f, 0.0f);
    attach_stub_anim(ent);
    ent->currentmove = &_stub_move;
    ent->s.frame     = 9999;
    _endfunc_called  = 0;

    M_MoveFrame(ent);

    T_EQ((int)ent->s.frame, 0);
    T_EQ(_endfunc_called, 0);
}

/* ==========================================================================
 * G_RunEntity
 * ========================================================================== */

TEST(wc3_combat, runentity_stat_fields_updated) {
    LPEDICT ent      = make_combat_unit(MAKEFOURCC('h','f','o','o'), 420.0f, 0.0f, 0.0f);
    /* Manually set health below max so we get a non-trivial compressed value. */
    ent->health.max_value = 400.0f;
    ent->health.value     = 200.0f;   /* 50% → 127 */
    ent->mana.max_value   = 100.0f;
    ent->mana.value       = 100.0f;   /* 100% → 255 */
    ent->movetype         = MOVETYPE_NONE;

    G_RunEntity(ent);

    T_EQ((int)ent->s.stats[ENT_HEALTH], 127);
    T_EQ((int)ent->s.stats[ENT_MANA],   255);
}

TEST(wc3_combat, runentity_ability_index_from_currentmove) {
    /* Use order_move to place the entity into the walk state.  The walk
     * umove_t has ability == &a_move, whose index in abilitylist[] is
     * non-zero (a_stop is at index 0).  This ensures the assertion
     * would catch G_RunEntity hard-coding s.ability = 0. */
    LPEDICT ent      = make_combat_unit(MAKEFOURCC('h','p','e','a'), 250.0f, 0.0f, 0.0f);
    ent->movetype    = MOVETYPE_NONE;
    VECTOR2 dest     = MAKE(VECTOR2, 100.0f, 100.0f);
    LPEDICT waypoint = Waypoint_add(&dest);
    order_move(ent, waypoint);  /* sets currentmove->ability = &a_move */
    T_NOT_NULL(ent->currentmove);
    T_NOT_NULL(ent->currentmove->ability);

    G_RunEntity(ent);

    DWORD expected = GetAbilityIndex(ent->currentmove->ability);
    T_ASSERT(expected != 0);  /* a_move is not the first entry (a_stop is) */
    T_EQ((int)ent->s.ability, (int)expected);
}

/* Hit-point regeneration (WC3 'uhpr'/'uhrt'): a wounded "always"-regen unit
 * heals by rate * frametime each frame; "none" never heals; healing caps at
 * max HP.  Test data (test_harness.c): hfoo regenHP 0.5 "always", hbar "none". */
TEST(wc3_combat, runentity_hp_regen_always) {
    LPEDICT ent           = make_combat_unit(MAKEFOURCC('h','f','o','o'), 420.0f, 0.0f, 0.0f);
    ent->health.max_value = 420.0f;
    ent->health.value     = 200.0f;
    ent->mana.max_value   = 0.0f;
    ent->movetype         = MOVETYPE_NONE;

    G_RunEntity(ent);

    T_FEQ(ent->health.value, 200.0f + G_UnitBalance(ent->class_id)->healthRegen *
          (FRAMETIME / 1000.0f), 0.0001f);
}

TEST(wc3_combat, runentity_hp_regen_caps_at_max) {
    LPEDICT ent           = make_combat_unit(MAKEFOURCC('h','f','o','o'), 420.0f, 0.0f, 0.0f);
    ent->health.max_value = 420.0f;
    ent->health.value     = 419.99f;   /* less than one frame's regen from full */
    ent->movetype         = MOVETYPE_NONE;

    G_RunEntity(ent);

    T_FEQ(ent->health.value, 420.0f, 0.0001f);
}

TEST(wc3_combat, runentity_hp_regen_none_does_not_heal) {
    /* regenType "none" must not heal even though regenHP is positive. */
    LPEDICT ent           = make_combat_unit(MAKEFOURCC('h','b','a','r'), 1500.0f, 0.0f, 0.0f);
    ent->health.max_value = 1500.0f;
    ent->health.value     = 1000.0f;
    ent->movetype         = MOVETYPE_NONE;

    G_RunEntity(ent);

    T_FEQ(ent->health.value, 1000.0f, 0.0001f);
}

/* Hero attribute -> derived-stat scaling (G_RecomputeHeroStats).  Test hero
 * "Hpal" (test_harness.c) has real Paladin bases: realHP 650, realM 255,
 * realdef 3.9, STR 22 / INT 17 / AGI 13.  Per WC3: +25 HP/STR, +15 mana/INT,
 * +0.3 armor/AGI. */
TEST(wc3_combat, hero_strength_adds_hp) {
    LPEDICT hero          = make_combat_unit(MAKEFOURCC('H','p','a','l'), 650.0f, 0.0f, 0.0f);
    hero->hero.str        = 22;            /* base */
    hero->health.max_value = 650.0f;
    hero->health.value     = 650.0f;

    hero->hero.str = 25;                   /* +3 STR */
    G_RecomputeHeroStats(hero);

    T_FEQ(hero->health.max_value, 650.0f + 3 * 25.0f, 0.01f); /* 725 */
    T_FEQ(hero->health.value,     650.0f + 3 * 25.0f, 0.01f); /* heals by gain */
}

TEST(wc3_combat, hero_intelligence_adds_mana) {
    LPEDICT hero        = make_combat_unit(MAKEFOURCC('H','p','a','l'), 650.0f, 0.0f, 0.0f);
    hero->hero.intel    = 17;              /* base */
    hero->mana.max_value = 255.0f;
    hero->mana.value     = 255.0f;

    hero->hero.intel = 20;                 /* +3 INT */
    G_RecomputeHeroStats(hero);

    T_FEQ(hero->mana.max_value, 255.0f + 3 * 15.0f, 0.01f); /* 300 */
}

TEST(wc3_combat, hero_agility_adds_armor) {
    LPEDICT hero       = make_combat_unit(MAKEFOURCC('H','p','a','l'), 650.0f, 0.0f, 0.0f);
    hero->hero.agi     = 13;               /* base */
    hero->armor_value  = 3.9f;

    hero->hero.agi = 23;                    /* +10 AGI */
    G_RecomputeHeroStats(hero);

    T_FEQ(hero->armor_value, 3.9f + 10 * 0.3f, 0.01f); /* 6.9 */
}

/* A hero's Strength adds +0.05 HP regen/sec per point; Intelligence adds +0.05
 * mana regen/sec per point (on top of the unit's base regen). */
TEST(wc3_combat, hero_strength_hp_regen_bonus) {
    LPEDICT h            = make_combat_unit(MAKEFOURCC('H','p','a','l'), 650.0f, 0.0f, 0.0f);
    h->hero.str          = 22;
    h->health.max_value  = 650.0f; h->health.value = 600.0f;  /* wounded */
    h->mana.max_value    = 0.0f;                              /* no mana regen */
    h->movetype          = MOVETYPE_NONE;

    G_RunEntity(h);

    T_FEQ(h->health.value, 600.0f + (G_UnitBalance(h->class_id)->healthRegen + 22 * 0.05f) *
          (FRAMETIME / 1000.0f), 0.001f);
}

TEST(wc3_combat, hero_intelligence_mana_regen_bonus) {
    LPEDICT h          = make_combat_unit(MAKEFOURCC('H','p','a','l'), 650.0f, 0.0f, 0.0f);
    h->hero.intel      = 17;
    h->health.max_value = 650.0f; h->health.value = 650.0f;   /* full -> no HP regen */
    h->mana.max_value  = 255.0f; h->mana.value = 100.0f;
    h->movetype        = MOVETYPE_NONE;

    G_RunEntity(h);

    T_FEQ(h->mana.value, 100.0f + 17 * 0.05f * (FRAMETIME / 1000.0f), 0.001f);
}

TEST(wc3_combat, hero_primary_attribute_adds_damage) {
    /* Hpal's Primary is STR, so attack damage rises +1 per Strength point. */
    LPEDICT h     = make_combat_unit(MAKEFOURCC('H','p','a','l'), 650.0f, 0.0f, 0.0f);
    h->hero.str   = 22;
    G_RecomputeHeroStats(h);
    FLOAT const dmg0 = h->attack1.damageBase;

    h->hero.str = 30;            /* +8 Strength */
    G_RecomputeHeroStats(h);

    T_FEQ(h->attack1.damageBase, dmg0 + 8.0f, 0.01f);
}

TEST(wc3_combat, hero_recompute_preserves_attack_and_armor_modifiers) {
    LPEDICT h = make_combat_unit(MAKEFOURCC('H','p','a','l'), 650.0f, 0.0f, 0.0f);
    h->hero.str = 22;
    h->hero.agi = 13;
    h->attack1.permanentDamageBonus = 2.0f;
    h->attack1.temporaryDamageBonus = 5.0f;
    h->permanent_armor_bonus = 2.0f;
    h->temporary_armor_bonus = 3.0f;

    G_RecomputeHeroStats(h);
    T_FEQ(h->attack1.temporaryDamageBonus, 5.0f, 0.001f);
    T_EQ(h->attack1.damageBase, h->data.UnitWeapons->attack1.damageBase + 22 + 2);
    T_FEQ(h->armor_value, h->data.UnitBalance->armor + 5.0f, 0.001f);

    h->hero.str = 30;
    h->hero.agi = 23;
    G_RecomputeHeroStats(h);
    T_FEQ(h->attack1.temporaryDamageBonus, 5.0f, 0.001f);
    T_EQ(h->attack1.damageBase, h->data.UnitWeapons->attack1.damageBase + 30 + 2);
    T_FEQ(h->armor_value, h->data.UnitBalance->armor + 10 * 0.3f + 5.0f, 0.001f);
}

TEST(wc3_combat, hero_stats_noop_for_non_hero) {
    /* Footman has no attributes — recompute must leave its stats untouched. */
    LPEDICT u            = make_combat_unit(MAKEFOURCC('h','f','o','o'), 420.0f, 0.0f, 0.0f);
    u->health.max_value  = 420.0f;
    u->health.value      = 300.0f;
    u->hero.str          = 99;             /* bogus; must be ignored */

    G_RecomputeHeroStats(u);

    T_FEQ(u->health.max_value, 420.0f, 0.01f);
    T_FEQ(u->health.value,     300.0f, 0.01f);
}

/* Hero XP / leveling (verified vs WC3 1.29).  XP-to-reach-L = 50*L*(L+1)-100;
 * attributes = base + trunc((L-1)*perLevel).  Hpal test hero: STR/INT/AGI per
 * level 2.7 / 1.8 / 1.5. */
TEST(wc3_combat, hero_xp_for_level_table) {
    T_EQ((int)G_HeroXPForLevel(1), 0);
    T_EQ((int)G_HeroXPForLevel(2), 200);
    T_EQ((int)G_HeroXPForLevel(3), 500);
    T_EQ((int)G_HeroXPForLevel(4), 900);
    T_EQ((int)G_HeroXPForLevel(10), 5400);
}

TEST(wc3_combat, hero_level_for_xp) {
    T_EQ((int)G_HeroLevelForXP(0),   1);
    T_EQ((int)G_HeroLevelForXP(199), 1);
    T_EQ((int)G_HeroLevelForXP(200), 2);
    T_EQ((int)G_HeroLevelForXP(499), 2);
    T_EQ((int)G_HeroLevelForXP(500), 3);
    T_EQ((int)G_HeroLevelForXP(99999999), 10); /* capped at MaxHeroLevel */
}

TEST(wc3_combat, hero_apply_level_truncates_attributes) {
    LPEDICT h            = make_combat_unit(MAKEFOURCC('H','p','a','l'), 650.0f, 0.0f, 0.0f);
    h->health.max_value  = 650.0f; h->health.value = 650.0f;
    h->mana.max_value    = 255.0f; h->mana.value   = 255.0f;
    h->armor_value       = 3.9f;

    G_HeroApplyLevel(h, 3);  /* steps=2: STR+trunc(5.4)=5, INT+trunc(3.6)=3, AGI+trunc(3.0)=3 */

    T_EQ((int)h->hero.str,   27);
    T_EQ((int)h->hero.intel, 20);
    T_EQ((int)h->hero.agi,   16);
    T_EQ((int)h->hero.level, 3);
    T_FEQ(h->health.max_value, 650.0f + 5 * 25.0f, 0.01f); /* 775 */
    T_FEQ(h->mana.max_value,   255.0f + 3 * 15.0f, 0.01f); /* 300 */
    T_FEQ(h->armor_value,      3.9f + 3 * 0.3f,    0.01f); /* 4.8 */
}

TEST(wc3_combat, hero_setxp_levels_up) {
    LPEDICT h           = make_combat_unit(MAKEFOURCC('H','p','a','l'), 650.0f, 0.0f, 0.0f);
    h->health.max_value = 650.0f; h->health.value = 650.0f;
    h->mana.max_value   = 255.0f; h->mana.value   = 255.0f;
    h->hero.level       = 1;
    h->hero.skillpoints = 1;

    G_HeroSetXP(h, 500);  /* crosses the level-3 threshold */

    T_EQ((int)h->hero.level, 3);
    T_EQ((int)h->hero.xp,    500);
    T_EQ((int)h->hero.skillpoints, 3);
    T_FEQ(h->health.max_value, 775.0f, 0.01f);
}

/* XP-on-kill (G_GrantKillXP). Mounted ROC/TFT data supplies the victim level;
 * a victim outside HeroExpRange awards nothing. */
TEST(wc3_combat, grant_kill_xp_awards_base) {
    LPEDICT killer = make_combat_unit(MAKEFOURCC('H','p','a','l'), 650.0f, 0.0f, 0.0f);
    killer->s.player = 0; killer->hero.level = 1; killer->hero.xp = 0;
    LPEDICT victim = make_combat_unit(MAKEFOURCC('h','f','o','o'), 420.0f, 0.0f, 0.0f);
    victim->s.player = 1;

    G_GrantKillXP(victim, killer);

    T_ASSERT(killer->hero.xp == 25 || killer->hero.xp == 30); /* ROC / TFT MiscData formulas */
}

TEST(wc3_combat, grant_kill_xp_out_of_range) {
    LPEDICT killer = make_combat_unit(MAKEFOURCC('H','p','a','l'), 650.0f, 0.0f, 0.0f);
    killer->s.player = 0; killer->hero.level = 1; killer->hero.xp = 0;
    LPEDICT victim = make_combat_unit(MAKEFOURCC('h','f','o','o'), 420.0f, 5000.0f, 0.0f);
    victim->s.player = 1;

    G_GrantKillXP(victim, killer);

    T_EQ((int)killer->hero.xp, 0); /* > HeroExpRange (1200) away */
}

TEST(wc3_combat, grant_kill_xp_splits_between_nearby_owned_heroes) {
    LPEDICT killer = make_combat_unit(MAKEFOURCC('H','p','a','l'), 650.0f, 0.0f, 0.0f);
    /* The test archive has one hero row; a second Hpal still proves per-entity XP splitting. */
    LPEDICT ally = make_combat_unit(MAKEFOURCC('H','p','a','l'), 650.0f, 100.0f, 0.0f);
    LPEDICT victim = make_combat_unit(MAKEFOURCC('h','f','o','o'), 420.0f, 0.0f, 0.0f);
    killer->s.player = ally->s.player = 0;
    killer->hero.level = ally->hero.level = 1;
    killer->hero.xp = ally->hero.xp = 0;
    victim->s.player = 1;

    G_GrantKillXP(victim, killer);

    T_ASSERT(killer->hero.xp > 0);
    T_EQ((int)killer->hero.xp, (int)ally->hero.xp);
    T_ASSERT(killer->hero.xp < 25);
}

TEST(wc3_combat, grant_kill_xp_honors_directional_shared_xp) {
    LPEDICT killer = make_combat_unit(MAKEFOURCC('h','f','o','o'), 420.0f, 0.0f, 0.0f);
    LPEDICT alliedHero = make_combat_unit(MAKEFOURCC('H','p','a','l'), 650.0f, 100.0f, 0.0f);
    LPEDICT victim = make_combat_unit(MAKEFOURCC('h','f','o','o'), 420.0f, 0.0f, 0.0f);
    killer->s.player = 0;
    alliedHero->s.player = 1;
    alliedHero->hero.level = 1;
    alliedHero->hero.xp = 0;
    victim->s.player = 2;
    memset(level.alliances, 0, sizeof(level.alliances));

    G_GrantKillXP(victim, killer);
    T_EQ((int)alliedHero->hero.xp, 0);

    level.alliances[1][0] |= 1 << ALLIANCE_SHARED_XP;
    G_GrantKillXP(victim, killer);
    T_EQ((int)alliedHero->hero.xp, 0); /* reverse direction does not grant killer-side sharing */

    level.alliances[0][1] |= 1 << ALLIANCE_SHARED_XP;
    G_GrantKillXP(victim, killer);
    T_ASSERT(alliedHero->hero.xp > 0);
}

TEST(wc3_combat, grant_kill_xp_does_not_reward_passive_ally_kill) {
    LPEDICT hero = make_combat_unit(MAKEFOURCC('H','p','a','l'), 650.0f, 0.0f, 0.0f);
    LPEDICT victim = make_combat_unit(MAKEFOURCC('h','f','o','o'), 420.0f, 0.0f, 0.0f);
    hero->s.player = 0;
    hero->hero.level = 1;
    hero->hero.xp = 0;
    victim->s.player = 1;
    memset(level.alliances, 0, sizeof(level.alliances));
    level.alliances[0][1] |= 1 << ALLIANCE_PASSIVE;

    G_GrantKillXP(victim, hero);

    T_EQ((int)hero->hero.xp, 0);
}

TEST(wc3_combat, attack_completion_resumes_persistent_follow) {
    LPEDICT follower = make_combat_unit(MAKEFOURCC('H','p','a','l'), 650.0f, 0.0f, 0.0f);
    LPEDICT leader = make_combat_unit(MAKEFOURCC('h','f','o','o'), 420.0f, 100.0f, 0.0f);
    LPEDICT enemy = make_combat_unit(MAKEFOURCC('h','g','r','u'), 100.0f, 50.0f, 0.0f);
    follower->s.player = leader->s.player = 0;
    enemy->s.player = 1;

    order_follow(follower, leader);
    T_ASSERT(follower->movement.follow_target == leader);
    order_attack(follower, enemy);
    T_Damage(enemy, follower, 100);

    T_ASSERT(follower->movement.follow_target == leader);
    T_ASSERT(follower->goalentity == leader);
    T_ASSERT(follower->currentmove && follower->currentmove->ability == &a_move);
}

/* unit_learnability (used by the SelectHeroSkill native): learning an ability
 * adds it at level 1; learning it again raises its level; a second ability
 * takes its own slot. */
TEST(wc3_combat, hero_learn_skill) {
    LPEDICT h = make_combat_unit(MAKEFOURCC('H','p','a','l'), 650.0f, 0.0f, 0.0f);
    memset(h->heroabilities, 0, sizeof(h->heroabilities));

    unit_learnability(h, MAKEFOURCC('A','H','h','b'));   /* Holy Light */
    T_EQ((int)h->heroabilities[0].code, (int)MAKEFOURCC('A','H','h','b'));
    T_EQ((int)h->heroabilities[0].level, 1);

    unit_learnability(h, MAKEFOURCC('A','H','h','b'));   /* upgrade to level 2 */
    T_EQ((int)h->heroabilities[0].level, 2);

    unit_learnability(h, MAKEFOURCC('A','H','d','s'));   /* Divine Shield in slot 1 */
    T_EQ((int)h->heroabilities[1].code, (int)MAKEFOURCC('A','H','d','s'));
    T_EQ((int)h->heroabilities[1].level, 1);
}

static const char slk_hero_skill_progression[] =
    "ID;PWXL;N;E\n"
    "C;Y1;X1;K\"alias\"\n"
    "C;Y1;X2;K\"code\"\n"
    "C;Y1;X3;K\"levels\"\n"
    "C;Y1;X4;K\"reqLevel\"\n"
    "C;Y1;X5;K\"levelSkip\"\n"
    "C;Y2;X1;K\"AHhb\"\n"
    "C;Y2;X2;K\"AHhb\"\n"
    "C;Y2;X3;K3\n"
    "C;Y2;X4;K1\n"
    "C;Y2;X5;K2\n"
    "C;Y3;X1;K\"AHds\"\n"
    "C;Y3;X2;K\"AHds\"\n"
    "C;Y3;X3;K1\n"
    "C;Y3;X4;K6\n"
    "C;Y3;X5;K0\n"
    "C;Y4;X1;K\"AHtb\"\n"
    "C;Y4;X2;K\"AHtb\"\n"
    "C;Y4;X3;K2\n"
    "C;Y4;X4;K1\n"
    "C;Y4;X5;K0\n"
    "E\n";

TEST(wc3_combat, hero_progression_initializes_preleveled_map_point_budget) {
    LPEDICT h = make_combat_unit(MAKEFOURCC('H','p','a','l'), 650.0f, 0.0f, 0.0f);

    memset(h->heroabilities, 0, sizeof(h->heroabilities));
    h->hero.level = 3;
    h->hero.skillpoints = 0;
    h->heroabilities[0].code = MAKEFOURCC('A','H','h','b');
    h->heroabilities[0].level = 1;

    G_HeroInitializeProgression(h);
    T_EQ((int)h->hero.level, 3);
    T_EQ((int)h->hero.skillpoints, 2);

    h->hero.level = 0;
    h->hero.skillpoints = 0;
    memset(h->heroabilities, 0, sizeof(h->heroabilities));
    G_HeroInitializeProgression(h);
    T_EQ((int)h->hero.level, 1);
    T_EQ((int)h->hero.skillpoints, 1);
}

TEST(wc3_combat, hero_skill_progression_uses_candidate_points_level_and_max_rank) {
    UnitAbilities_t const tree = { .abilList = "AInv", .heroAbilList = "AHhb,AHds,AHtb" };
    slkTestData_t *rows = parse_slk_string(slk_hero_skill_progression);
    slkTestData_t *old_abilities = G_SetSLKRows("AbilityData", rows);
    LPEDICT h = make_combat_unit(MAKEFOURCC('H','p','a','l'), 650.0f, 0.0f, 0.0f);
    DWORD const holy = MAKEFOURCC('A','H','h','b');
    DWORD const shield = MAKEFOURCC('A','H','d','s');
    DWORD const thunder = MAKEFOURCC('A','H','t','b');
    DWORD next = 0, required = 0;

    h->data.UnitAbilities = &tree;
    h->hero.level = 1;
    h->hero.skillpoints = 1;
    memset(h->heroabilities, 0, sizeof(h->heroabilities));

    T_EQ((int)G_UnitAbilityLevel(h, MAKEFOURCC('A','I','n','v')), 1);
    T_EQ((int)G_HeroSkillState(h, holy, &next, &required), HERO_SKILL_AVAILABLE);
    T_EQ((int)next, 1);
    T_EQ((int)required, 1);
    T_ASSERT(G_HeroLearnSkill(h, holy));
    T_EQ((int)G_UnitAbilityLevel(h, holy), 1);
    T_EQ((int)h->hero.skillpoints, 0);

    T_EQ((int)G_HeroSkillState(h, holy, &next, &required), HERO_SKILL_NO_POINTS);
    h->hero.skillpoints = 1;
    T_EQ((int)G_HeroSkillState(h, holy, &next, &required), HERO_SKILL_LEVEL_LOCKED);
    T_EQ((int)next, 2);
    T_EQ((int)required, 3);

    h->hero.level = 3;
    T_ASSERT(G_HeroLearnSkill(h, holy));
    T_EQ((int)G_UnitAbilityLevel(h, holy), 2);

    h->hero.level = 5;
    h->hero.skillpoints = 1;
    T_ASSERT(G_HeroLearnSkill(h, holy));
    T_EQ((int)G_UnitAbilityLevel(h, holy), 3);
    h->hero.skillpoints = 1;
    T_EQ((int)G_HeroSkillState(h, holy, &next, &required), HERO_SKILL_MAXED);

    h->hero.level = 1;
    h->hero.skillpoints = 1;
    T_ASSERT(G_HeroLearnSkill(h, thunder));
    h->hero.level = 2;
    h->hero.skillpoints = 1;
    T_EQ((int)G_HeroSkillState(h, thunder, &next, &required), HERO_SKILL_LEVEL_LOCKED);
    T_EQ((int)required, 3);
    h->hero.level = 3;
    T_EQ((int)G_HeroSkillState(h, thunder, &next, &required), HERO_SKILL_AVAILABLE);

    h->hero.level = 5;
    T_EQ((int)G_HeroSkillState(h, shield, &next, &required), HERO_SKILL_LEVEL_LOCKED);
    T_EQ((int)required, 6);
    h->hero.level = 6;
    T_EQ((int)G_HeroSkillState(h, shield, &next, &required), HERO_SKILL_AVAILABLE);
    T_EQ((int)G_HeroSkillState(h, MAKEFOURCC('A','H','w','e'), NULL, NULL), HERO_SKILL_ABSENT);

    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

/* ReviveHero: a dead hero comes back to life at the given point with HP/mana
 * from the revive factors (defaults: full life, no mana). */
TEST(wc3_combat, hero_revive) {
    LPGAMECLIENT client = &game.clients[0];
    LPEDICT h           = make_combat_unit(MAKEFOURCC('H','p','a','l'), 650.0f, 0.0f, 0.0f);
    h->health.max_value = 650.0f; h->health.value = 0.0f;   /* dead */
    h->mana.max_value   = 255.0f; h->mana.value   = 0.0f;
    h->svflags         |= SVF_DEADMONSTER;
    h->s.player         = client->ps.number;
    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 100;
    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_USED] = 0;

    G_ReviveHero(h, 100.0f, 200.0f);

    T_FEQ(h->health.value, 650.0f, 0.01f);   /* HeroReviveLifeFactor 1.0 */
    T_FEQ(h->mana.value,   0.0f,   0.01f);   /* HeroReviveManaFactor 0.0 */
    T_ASSERT((h->svflags & SVF_DEADMONSTER) == 0);       /* alive again */
    T_FEQ(h->s.origin2.x, 100.0f, 0.01f);
    T_FEQ(h->s.origin2.y, 200.0f, 0.01f);
    T_EQ(h->food.used, h->data.UnitBalance->foodUsed);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_USED], h->data.UnitBalance->foodUsed);
}

TEST(wc3_combat, hero_levelup_fires_event) {
    LPEDICT h           = make_combat_unit(MAKEFOURCC('H','p','a','l'), 650.0f, 0.0f, 0.0f);
    h->hero.level       = 1;
    h->health.max_value = 650.0f; h->health.value = 650.0f;
    level.events.write  = 0;
    level.events.read   = 0;

    G_HeroSetXP(h, 500);  /* level 1 -> 3: two level-ups -> two events */

    T_EQ((int)level.events.write, 2);
    T_EQ((int)level.events.queue[0].type, EVENT_PLAYER_HERO_LEVEL);
    T_ASSERT(level.events.queue[0].edict == h);
    T_EQ((int)level.events.queue[1].type, EVENT_PLAYER_HERO_LEVEL);
}

/* Attack timing: the post-swing recovery is cooldown - damagePoint, so the full
 * attack cycle (windup + recovery) equals WC3's "Cooldown Time". */
TEST(wc3_combat, attack_recovery_excludes_damage_point) {
    LPEDICT u              = make_combat_unit(MAKEFOURCC('h','f','o','o'), 420.0f, 0.0f, 0.0f);
    u->attack1.cooldown    = 1.5f;
    u->attack1.damagePoint = 0.3f;
    attack_melee_cooldown(u);
    T_FEQ(u->wait, 1.2f, 0.001f);   /* 1.5 - 0.3 */

    u->attack1.cooldown    = 2.0f;
    u->attack1.damagePoint = 0.5f;
    attack_ranged_cooldown(u);
    T_FEQ(u->wait, 1.5f, 0.001f);   /* 2.0 - 0.5 */

    /* damagePoint >= cooldown clamps recovery to zero. */
    u->attack1.cooldown    = 0.4f;
    u->attack1.damagePoint = 0.5f;
    attack_melee_cooldown(u);
    T_FEQ(u->wait, 0.0f, 0.001f);
}

/* A hero's Agility increases attack speed (+2%/point), dividing the windup and
 * recovery so the whole cycle speeds up. */
TEST(wc3_combat, attack_speed_scales_with_agility) {
    LPEDICT h              = make_combat_unit(MAKEFOURCC('H','p','a','l'), 650.0f, 0.0f, 0.0f);
    h->hero.agi            = 20;       /* +40% -> divisor 1.4 */
    h->attack1.cooldown    = 1.5f;
    h->attack1.damagePoint = 0.3f;

    attack_melee_cooldown(h);
    T_FEQ(h->wait, (1.5f - 0.3f) / 1.4f, 0.001f);   /* recovery scaled */

    attack_melee(h);
    T_FEQ(h->wait, 0.3f / 1.4f, 0.001f);            /* windup scaled */
}

TEST(wc3_combat, attack_speed_agility_bonus_caps_at_five_times) {
    LPEDICT h = make_combat_unit(MAKEFOURCC('H','p','a','l'), 650.0f, 0.0f, 0.0f);
    h->hero.agi = 1000;
    h->attack1.cooldown = 1.5f;
    h->attack1.damagePoint = 0.3f;

    attack_melee_cooldown(h);
    T_FEQ(h->wait, (1.5f - 0.3f) / 5.0f, 0.001f);
    attack_melee(h);
    T_FEQ(h->wait, 0.3f / 5.0f, 0.001f);
}

/* ==========================================================================
 * G_AttackDamage — attack×defense table and armor reduction
 *
 * Defense type indices (matches defense_type[] in g_monster.c):
 *   0=small 1=medium 2=large 3=fort 4=normal 5=hero 6=divine 7=none
 * Attack type indices (ATK_ enum in g_local.h):
 *   0=none 1=normal 2=pierce 3=siege 4=spells 5=chaos 6=magic 7=hero
 * ========================================================================== */

static LPEDICT make_attacker(DWORD atk_type) {
    LPEDICT a = make_combat_unit(MAKEFOURCC('h','f','o','o'), 100.0f, 0.0f, 0.0f);
    a->attack1.type = atk_type;
    return a;
}

static LPEDICT make_target(DWORD def_type, FLOAT armor) {
    LPEDICT t = make_combat_unit(MAKEFOURCC('h','f','o','o'), 1000.0f, 50.0f, 0.0f);
    t->defense_type = def_type;
    t->armor_value  = armor;
    return t;
}

/* Pierce vs small = 2.0× (infantry shredded by arrows). */
TEST(wc3_combat, attack_damage_pierce_vs_small) {
    LPEDICT a = make_attacker(ATK_PIERCE);
    LPEDICT t = make_target(0 /* small */, 0.0f);
    T_EQ(G_AttackDamage(a, t, 100), 200);
}

/* Normal vs medium = 1.5× (footmen effective vs soldiers). */
TEST(wc3_combat, attack_damage_normal_vs_medium) {
    LPEDICT a = make_attacker(ATK_NORMAL);
    LPEDICT t = make_target(1 /* medium */, 0.0f);
    T_EQ(G_AttackDamage(a, t, 100), 150);
}

/* Siege vs fort = 1.5× (catapults effective vs buildings). */
TEST(wc3_combat, attack_damage_siege_vs_fort) {
    LPEDICT a = make_attacker(ATK_SIEGE);
    LPEDICT t = make_target(3 /* fort */, 0.0f);
    T_EQ(G_AttackDamage(a, t, 100), 150);
}

/* Magic vs large = 2.0× (spells shred large units). */
TEST(wc3_combat, attack_damage_magic_vs_large) {
    LPEDICT a = make_attacker(ATK_MAGIC);
    LPEDICT t = make_target(2 /* large */, 0.0f);
    T_EQ(G_AttackDamage(a, t, 100), 200);
}

/* Chaos ignores defense type — always 1.0× regardless of armor type. */
TEST(wc3_combat, attack_damage_chaos_passthrough) {
    LPEDICT a = make_attacker(ATK_CHAOS);
    T_EQ(G_AttackDamage(a, make_target(0, 0.0f), 100), 100); /* small  */
    T_EQ(G_AttackDamage(a, make_target(2, 0.0f), 100), 100); /* large  */
    T_EQ(G_AttackDamage(a, make_target(3, 0.0f), 100), 100); /* fort   */
    T_EQ(G_AttackDamage(a, make_target(5, 0.0f), 100), 100); /* hero   */
}

/* Hero attack vs fort = 0.5× (heroes less effective vs buildings). */
TEST(wc3_combat, attack_damage_hero_vs_fort) {
    LPEDICT a = make_attacker(ATK_HERO);
    LPEDICT t = make_target(3 /* fort */, 0.0f);
    T_EQ(G_AttackDamage(a, t, 100), 50);
}

/* Divine defense takes only 5% from ordinary attack classes; Chaos remains 100%. */
TEST(wc3_combat, attack_damage_divine_uses_wc3_multiplier) {
    LPEDICT normal = make_attacker(ATK_NORMAL);
    LPEDICT chaos = make_attacker(ATK_CHAOS);
    LPEDICT divine = make_target(6 /* divine */, 0.0f);
    T_EQ(G_AttackDamage(normal, divine, 100), 5);
    T_EQ(G_AttackDamage(chaos, divine, 100), 100);
}

/* Active Misc/war3mapMisc values override the stock fallback table/coefficient. */
TEST(wc3_combat, attack_damage_uses_loaded_gameplay_constants) {
    BOOL const old_loaded = game.constants.combatConstantsLoaded;
    FLOAT const old_mult = game.constants.damageBonus[ATK_NORMAL][4];
    FLOAT const old_armor = game.constants.defenseArmor;
    LPEDICT a = make_attacker(ATK_NORMAL);
    LPEDICT t = make_target(4 /* normal */, 2.0f);

    game.constants.combatConstantsLoaded = true;
    game.constants.damageBonus[ATK_NORMAL][4] = 1.25f;
    game.constants.defenseArmor = 0.10f;
    T_EQ(G_AttackDamage(a, t, 100), 104); /* 125 / 1.2 = 104.16... */

    game.constants.combatConstantsLoaded = old_loaded;
    game.constants.damageBonus[ATK_NORMAL][4] = old_mult;
    game.constants.defenseArmor = old_armor;
}

/* Armor reduction: dmg / (1 + armor * 0.06). 100 base, 2 armor: 100/1.12 ≈ 89. */
TEST(wc3_combat, attack_damage_armor_reduces_damage) {
    LPEDICT a = make_attacker(ATK_NORMAL);
    LPEDICT t = make_target(4 /* normal */, 2.0f);
    int result = G_AttackDamage(a, t, 100);
    T_ASSERT(result >= 88 && result <= 90);
}

/* Negative armor uses Warsmash's exponential WC3 curve: 2-(1-K)^(-armor).
 * At -10 armor and K=.06 this is about 1.4614x, clearly distinct from the old
 * reciprocal approximation (~1.375x). */
TEST(wc3_combat, attack_damage_negative_armor_uses_exponential_curve) {
    LPEDICT a = make_attacker(ATK_NORMAL);
    LPEDICT t = make_target(4 /* normal */, -10.0f);
    int result = G_AttackDamage(a, t, 100);
    T_ASSERT(result >= 145 && result <= 147);
}

/* Minimum 1: even a tiny base through heavy armor can't go below 1. */
TEST(wc3_combat, attack_damage_minimum_one) {
    LPEDICT a = make_attacker(ATK_PIERCE);
    LPEDICT t = make_target(5 /* hero, pierce=0.5× */, 100.0f); /* enormous armor */
    T_EQ(G_AttackDamage(a, t, 1), 1);
}

/* Zero armor: multiplier applied cleanly with no reduction. */
TEST(wc3_combat, attack_damage_zero_armor_no_reduction) {
    LPEDICT a = make_attacker(ATK_NORMAL);
    LPEDICT t = make_target(4 /* normal */, 0.0f);
    T_EQ(G_AttackDamage(a, t, 100), 100);
}

/* ==========================================================================
 * Ability lookup
 * ========================================================================== */

TEST(wc3_combat, find_ability_stop) {
    ability_t const *a = FindAbilityByClassname(STR_CmdStop);
    T_NOT_NULL(a);
}

TEST(wc3_combat, find_ability_move) {
    ability_t const *a = FindAbilityByClassname(STR_CmdMove);
    T_NOT_NULL(a);
}

TEST(wc3_combat, command_lookup_preserves_engine_command_name) {
    ability_t const *a = FindAbilityForCommand(STR_CmdBuild);
    T_NOT_NULL(a);
    T_ASSERT(a == FindAbilityByClassname(STR_CmdBuild));
    T_EQ(GetAbilityIndex(a), FindAbilityIndex(STR_CmdBuild));
}

TEST(wc3_combat, find_ability_unknown_returns_null) {
    ability_t const *a = FindAbilityByClassname("NotAnAbility");
    T_NULL(a);
}

TEST(wc3_combat, get_ability_by_index_zero) {
    /* Index 0 is always the stop ability (first entry in abilitylist[]). */
    ability_t const *a = GetAbilityByIndex(0);
    T_NOT_NULL(a);
    T_ASSERT(a == FindAbilityByClassname(STR_CmdStop));
}

TEST(wc3_combat, get_ability_by_index_out_of_range) {
    ability_t const *a = GetAbilityByIndex(9999);
    T_NULL(a);
}

TEST(wc3_combat, get_ability_index_roundtrip) {
    ability_t const *a   = FindAbilityByClassname(STR_CmdMove);
    DWORD             idx = FindAbilityIndex(STR_CmdMove);
    T_ASSERT(GetAbilityByIndex(idx) == a);
}

TEST(wc3_combat, registered_reference_ability_codes) {
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
        T_NOT_NULL(FindAbilityByClassname(codes[i]));
    }
}

static const char slk_ability_helpers[] =
    "ID;PWXL;N;EBB;Y4;X13\n"
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
    "C;Y4;X1;K\"Ahrp\"\n"
    "C;Y4;X2;K\"Arep\"\n"
    "E\n";

TEST(wc3_combat, ability_dispatch_extension_is_append_only) {
    T_ASSERT(offsetof(ability_t, item_use) > offsetof(ability_t, spell));
}

TEST(wc3_combat, command_lookup_resolves_fourcc_ability_alias) {
    slkTestData_t *rows = parse_slk_string(slk_ability_helpers);
    slkTestData_t *old_abilities = G_SetSLKRows("AbilityData", rows);
    ability_t const *resolved = FindAbilityForCommand("Ahrp");

    T_NOT_NULL(resolved);
    T_ASSERT(resolved == FindAbilityByClassname("Arep"));

    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

static const char slk_ability_helpers_roc[] =
    "ID;PWXL;N;EBB;Y3;X4\n"
    "C;Y1;X1;K\"alias\"\n"
    "C;Y1;X2;K\"Data11\"\n"
    "C;Y1;X3;K\"Data12\"\n"
    "C;Y1;X4;K\"Data13\"\n"
    "C;Y2;X1;K\"Ahar\"\n"
    "C;Y2;X2;K1\n"
    "C;Y2;X3;K10\n"
    "C;Y2;X4;K10\n"
    "C;Y3;X1;K\"Agld\"\n"
    "C;Y3;X2;K12500\n"
    "C;Y3;X3;K1\n"
    "C;Y3;X4;K1\n"
    "E\n";

/* ROC and TFT store the same semantic ability slots under different headers. */
TEST(wc3_combat, ability_data_resolves_roc_and_tft_columns) {
    slkTestData_t *rows = parse_slk_string(slk_ability_helpers_roc);
    slkTestData_t *old_abilities = G_SetSLKRows("AbilityData", rows);
    T_FEQ(AB_Data("Ahar", 1, 1), 1.0f, 0.01f);
    T_FEQ(AB_Data("Ahar", 1, 2), 10.0f, 0.01f);
    T_FEQ(S_SpellData(MAKEFOURCC('A','h','a','r'), 1, 3), 10.0f, 0.01f);
    free_slk_rows(rows);

    rows = parse_slk_string(slk_ability_helpers);
    G_SetSLKRows("AbilityData", rows);
    T_FEQ(AB_Data("AHtb", 1, 1), 100.0f, 0.01f);
    T_FEQ(AB_Data("AHtb", 1, 2), 55.0f, 0.01f);
    T_FEQ(S_SpellData(MAKEFOURCC('A','H','t','b'), 1, 5), 42.0f, 0.01f);
    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

/* Gold-mine tuning stays in AbilityData instead of being copied into shared
 * process-wide globals. Agl2 remains a marker without its own initializer. */
TEST(wc3_combat, gold_mine_data_remains_authoritative_in_ability_rows) {
    slkTestData_t *rows = parse_slk_string(slk_ability_helpers_roc);
    slkTestData_t *old_abilities = G_SetSLKRows("AbilityData", rows);
    T_FEQ(AB_Data("Agld", 1, 1), 12500.0f, 0.01f);
    T_FEQ(AB_Data("Agld", 1, 2), 1.0f, 0.01f);
    T_FEQ(AB_Data("Agld", 1, 3), 1.0f, 0.01f);
    T_NULL(a_goldmine.init);
    T_NULL(a_goldmine_overlayed.init);
    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

TEST(wc3_combat, spell_helpers_read_slk_fields) {
    slkTestData_t *rows = parse_slk_string(slk_ability_helpers);
    slkTestData_t *old_abilities = G_SetSLKRows("AbilityData", rows);
    DWORD thunder = MAKEFOURCC('A', 'H', 't', 'b');
    DWORD water = MAKEFOURCC('A', 'H', 'w', 'e');

    T_FEQ(S_SpellNumber(thunder, ABILITY_NUMBER_COST, 1), 75.0f, 0.01f);
    T_FEQ(S_SpellRange(thunder, 1), 600.0f, 0.01f);
    T_FEQ(S_SpellDuration(thunder, 1, true), 3.0f, 0.01f);
    T_FEQ(S_SpellData(thunder, 1, 1), 100.0f, 0.01f); /* DataA1 */
    T_FEQ(S_SpellData(thunder, 1, 2), 55.0f, 0.01f);  /* DataB1 */
    /* index 5 -> DataE1: the columns are Data<Letter><Level>, and index must
     * reach past D (the old code built numeric "Data15" and clamped index to 4,
     * so every DataE+ read — e.g. Shadow Strike's Initial Damage — returned 0). */
    T_FEQ(S_SpellData(thunder, 1, 5), 42.0f, 0.01f);  /* DataE1 */
    T_EQ((int)S_SpellUnitId(water, 1), (int)MAKEFOURCC('h','w','a','t'));

    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

TEST(wc3_combat, spell_mana_and_cooldown) {
    slkTestData_t *rows = parse_slk_string(slk_ability_helpers);
    slkTestData_t *old_abilities = G_SetSLKRows("AbilityData", rows);
    DWORD thunder = MAKEFOURCC('A', 'H', 't', 'b');
    LPEDICT caster = make_combat_unit(MAKEFOURCC('h','p','e','a'), 250.0f, 0.0f, 0.0f);
    caster->mana.value = 100.0f;
    caster->mana.max_value = 100.0f;
    level.time = 1000;
    T_ASSERT(S_SpellCooldownReady(caster, thunder));
    T_ASSERT(S_SpellSpendMana(caster, thunder, 1));
    T_FEQ(caster->mana.value, 25.0f, 0.01f);
    T_ASSERT(!S_SpellSpendMana(caster, thunder, 1));

    S_SpellStartCooldown(caster, thunder, 1);
    T_ASSERT(!S_SpellCooldownReady(caster, thunder));
    /* Just used -> full cooldown shade on the command-card icon. */
    T_FEQ(S_SpellCooldownFraction(caster, thunder, 1), 1.0f, 0.01f);
    level.time += 4500; /* halfway through the 9s cooldown */
    T_FEQ(S_SpellCooldownFraction(caster, thunder, 1), 0.5f, 0.01f);
    level.time += 4501; /* past the end */
    unit_updatestatuses(caster);
    T_ASSERT(S_SpellCooldownReady(caster, thunder));
    /* Ready again -> no shade. */
    T_FEQ(S_SpellCooldownFraction(caster, thunder, 1), 0.0f, 0.01f);

    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

TEST(wc3_combat, timed_stun_status_expires_without_touching_pause) {
    LPEDICT ent = make_combat_unit(MAKEFOURCC('h','f','o','o'), 420.0f, 0.0f, 0.0f);
    ent->paused = true;
    level.time = 100;

    unit_addtimedstatus(ent, "Bstu", 1, 0.05f);

    T_ASSERT(ent->stunned);
    T_ASSERT(ent->paused);
    level.time = 151;
    unit_updatestatuses(ent);
    T_ASSERT(!ent->stunned);
    T_ASSERT(ent->paused);
}

TEST(wc3_combat, timed_life_status_kills_unit) {
    LPEDICT ent = make_combat_unit(MAKEFOURCC('h','f','o','o'), 420.0f, 0.0f, 0.0f);
    _die_call_count = 0;
    level.time = 200;

    unit_addtimedstatus(ent, "BTLF", 1, 0.05f);
    level.time = 251;
    unit_updatestatuses(ent);

    T_FEQ(ent->health.value, 0.0f, 0.01f);
    T_EQ(_die_call_count, 1);
}

TEST(wc3_combat, timed_status_bar_keeps_duration_and_uses_last_eligible_status) {
    LPEDICT ent = make_combat_unit(MAKEFOURCC('h','f','o','o'), 420.0f, 0.0f, 0.0f);
    heroabilitystatus_t const *status;

    level.time = 1000;
    unit_addtimedstatus(ent, "BTLF", 1, 20.0f);
    status = unit_findtimedbarstatus(ent);
    T_NOT_NULL(status);
    T_EQ(status->code, MAKEFOURCC('B','T','L','F'));
    T_EQ(status->duration_ms, 20000);
    T_FEQ(unit_statusremainingfraction(status), 1.0f, 0.0001f);

    level.time = 11000;
    T_FEQ(unit_statusremainingfraction(status), 0.5f, 0.0001f);

    /* Bmil is also a timed-status-bar buff; the later slot owns Warsmash's
     * single countdown presentation when both are present. */
    unit_addtimedstatus(ent, "Bmil", 1, 45.0f);
    status = unit_findtimedbarstatus(ent);
    T_NOT_NULL(status);
    T_EQ(status->code, MAKEFOURCC('B','m','i','l'));
    T_EQ(status->duration_ms, 45000);
    T_FEQ(unit_statusremainingfraction(status), 1.0f, 0.0001f);

    T_ASSERT(unit_statusshowstimedbar(MAKEFOURCC('B','T','L','F')));
    T_ASSERT(unit_statusshowstimedbar(MAKEFOURCC('B','m','i','l')));
    T_ASSERT(!unit_statusshowstimedbar(MAKEFOURCC('B','s','t','u')));
}

/* ==========================================================================
 * player_pay
 * ========================================================================== */

/* Costs come from the mounted ROC/TFT UnitBalance table. */

TEST(wc3_combat, player_pay_deducts_gold) {
    LPPLAYER p = &game.clients[0].ps;
    p->stats[PLAYERSTATE_RESOURCE_GOLD]   = 200;
    p->stats[PLAYERSTATE_RESOURCE_LUMBER] = 0;

    DWORD unit = MAKEFOURCC('h','p','e','a');
    LONG gold = G_UnitBalance(unit)->goldCost;
    BOOL ok = player_pay(p, unit);

    T_ASSERT(ok);
    T_EQ((int)p->stats[PLAYERSTATE_RESOURCE_GOLD], 200 - gold);
}

TEST(wc3_combat, player_pay_insufficient_gold_fails) {
    LPPLAYER p = &game.clients[0].ps;
    p->stats[PLAYERSTATE_RESOURCE_GOLD]   = 50;  /* need 75 */
    p->stats[PLAYERSTATE_RESOURCE_LUMBER] = 0;

    BOOL ok = player_pay(p, MAKEFOURCC('h','p','e','a'));

    T_ASSERT(!ok);
    T_EQ((int)p->stats[PLAYERSTATE_RESOURCE_GOLD], 50); /* unchanged */
}

TEST(wc3_combat, player_pay_insufficient_lumber_fails) {
    LPPLAYER p = &game.clients[0].ps;
    DWORD project = MAKEFOURCC('h','b','a','r');
    LONG gold = G_UnitBalance(project)->goldCost, lumber = G_UnitBalance(project)->lumberCost;
    T_ASSERT(gold > 0 && lumber > 0);
    p->stats[PLAYERSTATE_RESOURCE_GOLD]   = gold;
    p->stats[PLAYERSTATE_RESOURCE_LUMBER] = lumber - 1;

    BOOL ok = player_pay(p, project);

    T_ASSERT(!ok);
    T_EQ((int)p->stats[PLAYERSTATE_RESOURCE_GOLD], gold);
    T_EQ((int)p->stats[PLAYERSTATE_RESOURCE_LUMBER], lumber - 1);
}

TEST(wc3_combat, player_pay_null_player_fails) {
    BOOL ok = player_pay(NULL, MAKEFOURCC('h','p','e','a'));
    T_ASSERT(!ok);
}

/* ==========================================================================
 * unit_add_build_queue
 * ========================================================================== */

TEST(wc3_combat, build_queue_first_item) {
    LPEDICT producer = make_combat_unit(MAKEFOURCC('h','p','e','a'), 250.0f, 0.0f, 0.0f);
    LPEDICT item1    = make_combat_unit(MAKEFOURCC('h','f','o','o'), 420.0f, 10.0f, 0.0f);
    producer->build  = NULL;

    unit_add_build_queue(producer, item1);

    T_ASSERT(producer->build == item1);
    T_NULL(item1->build);
}

TEST(wc3_combat, build_queue_chained_items) {
    LPEDICT producer = make_combat_unit(MAKEFOURCC('h','p','e','a'), 250.0f, 0.0f, 0.0f);
    LPEDICT item1    = make_combat_unit(MAKEFOURCC('h','f','o','o'), 420.0f, 10.0f, 0.0f);
    LPEDICT item2    = make_combat_unit(MAKEFOURCC('h','f','o','o'), 420.0f, 20.0f, 0.0f);
    producer->build  = NULL;
    item1->build     = NULL;
    item2->build     = NULL;

    unit_add_build_queue(producer, item1);
    unit_add_build_queue(producer, item2);

    T_ASSERT(producer->build == item1);
    T_ASSERT(item1->build    == item2);
    T_NULL(item1->currentmove);
    T_NULL(item2->currentmove);
    T_NULL(item2->build);
}

TEST(wc3_combat, build_queue_three_items_linked) {
    LPEDICT producer = make_combat_unit(MAKEFOURCC('h','p','e','a'), 250.0f, 0.0f, 0.0f);
    LPEDICT item1    = make_combat_unit(MAKEFOURCC('h','f','o','o'), 420.0f, 10.0f, 0.0f);
    LPEDICT item2    = make_combat_unit(MAKEFOURCC('h','f','o','o'), 420.0f, 20.0f, 0.0f);
    LPEDICT item3    = make_combat_unit(MAKEFOURCC('h','f','o','o'), 420.0f, 30.0f, 0.0f);
    producer->build = item1->build = item2->build = item3->build = NULL;

    unit_add_build_queue(producer, item1);
    unit_add_build_queue(producer, item2);
    unit_add_build_queue(producer, item3);

    T_ASSERT(producer->build == item1);
    T_ASSERT(item1->build    == item2);
    T_ASSERT(item2->build    == item3);
    T_NULL(item3->build);
}

/* ==========================================================================
 * Quest system
 * ========================================================================== */

TEST(wc3_combat, quest_make_non_null) {
    LPQUEST q = G_MakeQuest();
    T_NOT_NULL(q);
    G_RemoveQuest(q);
}

TEST(wc3_combat, quest_fields_default_state) {
    LPQUEST q = G_MakeQuest();
    T_ASSERT(!q->completed);
    T_ASSERT(!q->failed);
    T_ASSERT(!q->discovered);
    T_ASSERT(!q->required);
    T_ASSERT(q->enabled);
    G_RemoveQuest(q);
}

TEST(wc3_combat, quest_set_title) {
    LPQUEST q = G_MakeQuest();
    q->title = strdup("Defeat the Lich King");
    T_STREQ(q->title, "Defeat the Lich King");
    G_RemoveQuest(q);
}

TEST(wc3_combat, quest_set_completed) {
    LPQUEST q = G_MakeQuest();
    q->completed = true;
    T_ASSERT(q->completed);
    G_RemoveQuest(q);
}

TEST(wc3_combat, quest_set_failed) {
    LPQUEST q = G_MakeQuest();
    q->failed = true;
    T_ASSERT(q->failed);
    G_RemoveQuest(q);
}

TEST(wc3_combat, quest_remove_clears_from_list) {
    /* Reset the quest list to a known-empty state. */
    memset(level.quests, 0, sizeof(level.quests));
    LPQUEST q = G_MakeQuest();
    T_ASSERT(q && q->inuse);

    G_RemoveQuest(q);

    /* After removing the only quest the list must be empty. */
    T_ASSERT(!q->inuse);
}

TEST(wc3_combat, quest_discovered_required_enabled_flags) {
    LPQUEST q = G_MakeQuest();
    q->discovered = true;
    q->required   = true;
    q->enabled    = true;
    T_ASSERT(q->discovered);
    T_ASSERT(q->required);
    T_ASSERT(q->enabled);
    G_RemoveQuest(q);
}

TEST(wc3_combat, quest_item_create_non_null) {
    LPQUEST q = G_MakeQuest();
    LPQUESTITEM item = &q->items[q->num_items++]; item->inuse = true;
    T_NOT_NULL(item);
    G_RemoveQuest(q);
}

TEST(wc3_combat, quest_item_set_description) {
    LPQUEST q = G_MakeQuest();
    LPQUESTITEM item = &q->items[q->num_items++]; item->inuse = true;
    item->description = strdup("Kill 10 footmen");
    T_STREQ(item->description, "Kill 10 footmen");
    G_RemoveQuest(q);
}

TEST(wc3_combat, quest_item_set_completed) {
    LPQUEST q = G_MakeQuest();
    LPQUESTITEM item = &q->items[q->num_items++]; item->inuse = true;
    item->completed = true;
    T_ASSERT(item->completed);
    G_RemoveQuest(q);
}

TEST(wc3_combat, quest_item_defaults_incomplete) {
    LPQUEST q = G_MakeQuest();
    LPQUESTITEM item = &q->items[q->num_items++]; item->inuse = true;
    T_ASSERT(!item->completed);
    G_RemoveQuest(q);
}

TEST(wc3_combat, quest_multiple_items_linked) {
    LPQUEST q = G_MakeQuest();
    LPQUESTITEM a = &q->items[q->num_items++]; a->inuse = true;
    LPQUESTITEM b = &q->items[q->num_items++]; b->inuse = true;
    /* Both items must be reachable from the quest's bounded storage. */
    BOOL found_a = false, found_b = false;
    FOR_EACH_QUESTITEM(q, it) {
        if (it == a) found_a = true;
        if (it == b) found_b = true;
    }
    T_ASSERT(found_a);
    T_ASSERT(found_b);
    G_RemoveQuest(q);
}

TEST(wc3_combat, quest_multiple_quests_in_list) {
    memset(level.quests, 0, sizeof(level.quests));
    LPQUEST q1 = G_MakeQuest();
    LPQUEST q2 = G_MakeQuest();
    T_NOT_NULL(q1);
    T_NOT_NULL(q2);
    /* Both quests must be reachable from the fixed quest slots. */
    BOOL found_q1 = false, found_q2 = false;
    FOR_EACH_QUEST(q) {
        if (q == q1) found_q1 = true;
        if (q == q2) found_q2 = true;
    }
    T_ASSERT(found_q1);
    T_ASSERT(found_q2);
    G_RemoveQuest(q1);
    G_RemoveQuest(q2);
    T_ASSERT(!q1->inuse && !q2->inuse);
}

/* ==========================================================================
 * G_PublishEvent
 * ========================================================================== */

TEST(wc3_combat, publish_event_fills_queue) {
    /* Reset the event queue. */
    level.events.write = 0;
    level.events.read  = 0;

    LPEDICT ent = make_combat_unit(MAKEFOURCC('h','p','e','a'), 250.0f, 0.0f, 0.0f);
    GAMEEVENT *evt = G_PublishEvent(ent, EVENT_UNIT_DEATH);

    T_NOT_NULL(evt);
    T_EQ((int)evt->type, (int)EVENT_UNIT_DEATH);
}

TEST(wc3_combat, publish_event_sequential) {
    level.events.write = 0;
    level.events.read  = 0;

    LPEDICT ent = make_combat_unit(MAKEFOURCC('h','p','e','a'), 250.0f, 0.0f, 0.0f);
    G_PublishEvent(ent, EVENT_UNIT_DEATH);
    GAMEEVENT *evt2 = G_PublishEvent(ent, EVENT_PLAYER_UNIT_TRAIN_FINISH);

    T_NOT_NULL(evt2);
    T_EQ((int)evt2->type, (int)EVENT_PLAYER_UNIT_TRAIN_FINISH);
}

/* ==========================================================================
 * Win conditions — EVENT_PLAYER_VICTORY / EVENT_PLAYER_DEFEAT
 * ========================================================================== */

TEST(wc3_combat, publish_event_player_victory) {
    level.events.write = 0;
    level.events.read  = 0;

    LPEDICT ent = make_combat_unit(MAKEFOURCC('h','p','e','a'), 250.0f, 0.0f, 0.0f);
    GAMEEVENT *evt = G_PublishEvent(ent, EVENT_PLAYER_VICTORY);

    T_NOT_NULL(evt);
    T_EQ((int)evt->type, (int)EVENT_PLAYER_VICTORY);
    T_EQ(evt->edict, ent);
}

TEST(wc3_combat, publish_event_player_defeat) {
    level.events.write = 0;
    level.events.read  = 0;

    LPEDICT ent = make_combat_unit(MAKEFOURCC('h','p','e','a'), 250.0f, 0.0f, 0.0f);
    GAMEEVENT *evt = G_PublishEvent(ent, EVENT_PLAYER_DEFEAT);

    T_NOT_NULL(evt);
    T_EQ((int)evt->type, (int)EVENT_PLAYER_DEFEAT);
    T_EQ(evt->edict, ent);
}

TEST(wc3_combat, victory_and_defeat_are_distinct_event_types) {
    T_ASSERT(EVENT_PLAYER_VICTORY != EVENT_PLAYER_DEFEAT);
    T_EQ((int)EVENT_PLAYER_VICTORY, 14);
    T_EQ((int)EVENT_PLAYER_DEFEAT,  13);
}

/* ==========================================================================
 * Suite runner
 * ========================================================================== */

#endif /* BZ_TESTS */
