#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
#include "../skills/s_skills.h"

static cstring_t const impale_slk =
    "ID;PWXL;N;EBB;Y3;X13\n"
    "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
    "C;Y1;X4;K\"DataA1\"\nC;Y1;X5;K\"DataB1\"\nC;Y1;X6;K\"DataC1\"\n"
    "C;Y1;X7;K\"Area1\"\nC;Y1;X8;K\"BuffID1\"\nC;Y1;X9;K\"Dur1\"\n"
    "C;Y1;X10;K\"HeroDur1\"\nC;Y1;X11;K\"levels\"\nC;Y1;X12;K\"Rng1\"\nC;Y1;X13;K\"DataD1\"\n"
    "C;Y2;X1;K\"AUim\"\nC;Y2;X2;K\"AUim\"\nC;Y2;X3;K\"ground,enemy,organic\"\n"
    "C;Y2;X4;K600\nC;Y2;X5;K0.6\nC;Y2;X6;K73\nC;Y2;X7;K80\n"
    "C;Y2;X8;K\"BUim\"\nC;Y2;X9;K2\nC;Y2;X10;K1\nC;Y2;X11;K1\nC;Y2;X12;K700\nC;Y2;X13;K0.8\n"
    "C;Y3;X1;K\"ACmp\"\nC;Y3;X2;K\"ACmp\"\nC;Y3;X3;K\"ground,enemy,organic\"\n"
    "C;Y3;X4;K400\nC;Y3;X5;K0.4\nC;Y3;X6;K37\nC;Y3;X7;K100\n"
    "C;Y3;X8;K\"BUim\"\nC;Y3;X9;K1\nC;Y3;X10;K1\nC;Y3;X11;K1\nC;Y3;X12;K700\nC;Y3;X13;K0.5\nE\n";

/* Newer editor data (1.32.6+) exposes DataE/Uninterruptible and
 * DataF/Airborne Units Vulnerable. Separate fixtures ensure legacy missing
 * columns do not silently inherit the modern defaults. */
static cstring_t const impale_modern_slk =
    "ID;PWXL;N;EBB;Y3;X15\n"
    "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
    "C;Y1;X4;K\"DataA1\"\nC;Y1;X5;K\"DataB1\"\nC;Y1;X6;K\"DataC1\"\n"
    "C;Y1;X7;K\"Area1\"\nC;Y1;X8;K\"BuffID1\"\nC;Y1;X9;K\"Dur1\"\n"
    "C;Y1;X10;K\"HeroDur1\"\nC;Y1;X11;K\"levels\"\nC;Y1;X12;K\"Rng1\"\n"
    "C;Y1;X13;K\"DataD1\"\nC;Y1;X14;K\"DataE1\"\nC;Y1;X15;K\"DataF1\"\n"
    "C;Y2;X1;K\"AUim\"\nC;Y2;X2;K\"AUim\"\nC;Y2;X3;K\"ground,enemy,organic\"\n"
    "C;Y2;X4;K600\nC;Y2;X5;K0.6\nC;Y2;X6;K73\nC;Y2;X7;K80\n"
    "C;Y2;X8;K\"BUim\"\nC;Y2;X9;K2\nC;Y2;X10;K1\nC;Y2;X11;K1\n"
    "C;Y2;X12;K700\nC;Y2;X13;K0.8\nC;Y2;X14;K1\nC;Y2;X15;K1\n"
    "C;Y3;X1;K\"ACmp\"\nC;Y3;X2;K\"ACmp\"\nC;Y3;X3;K\"ground,enemy,organic\"\n"
    "C;Y3;X4;K400\nC;Y3;X5;K0.4\nC;Y3;X6;K37\nC;Y3;X7;K100\n"
    "C;Y3;X8;K\"BUim\"\nC;Y3;X9;K1\nC;Y3;X10;K1\nC;Y3;X11;K1\n"
    "C;Y3;X12;K700\nC;Y3;X13;K0.5\nC;Y3;X14;K1\nC;Y3;X15;K1\nE\n";

static UnitBalance_t const impale_no_regen = { .healthRegenType = "none" };
static UnitWeapons_t const impale_test_weapon = { .attacksEnabled = 1 };

static edict_t *impale_unit(float x, float y, uint32_t player, TARGTYPE type) {
    edict_t *unit = alloc_test_unit(MAKEFOURCC('h','f','o','o'), x, y);
    unit->s.player = player;
    unit->targtype = type;
    unit->svflags |= SVF_MONSTER;
    unit->data.UnitBalance = (UnitBalance_t *)&impale_no_regen;
    unit->health.value = unit->health.max_value = 500;
    return unit;
}

static bool impale_cast(edict_t *caster, cstring_t code, vec2_t point) {
    abilityitem_t item = S_AbilityItem(FS_SLKKey(code));
    spellTarget_t target = MAKE(spellTarget_t, .type = SPELL_TARGET_POINT, .point = point);
    abilityCall_t call = MAKE(abilityCall_t, .item = &item, .target = &target);
    return S_AbilityMessage(caster, A_EXECUTE, &call) != 0;
}

TEST(wc3_spell, impale_approach_range_query_accepts_null_caster) {
    slkTestData_t *rows = parse_slk_string(impale_slk), *old;
    abilityitem_t item;
    abilityCall_t call;
    reset_entities(); setup_test_world();
    old = G_SetSLKRows("AbilityData", rows);
    item = S_AbilityItem(MAKEFOURCC('A','U','i','m'));
    call = MAKE(abilityCall_t, .item = &item, .spell_approach_range = 700.0f);
    T_NOT_NULL(item.ability);
    if (item.ability && item.ability->proc) {
        T_ASSERT(item.ability->proc(NULL, A_SPELL_APPROACH_RANGE, &call));
        T_FEQ(call.spell_approach_range, 700.0f, 0.001f);
    } else {
        T_ASSERT(false);
    }
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, impale_wave_hits_in_authored_travel_order_once_per_cast) {
    slkTestData_t *rows = parse_slk_string(impale_slk), *old;
    edict_t *caster, *near, *middle, *far, *behind, *beyond, *side;
    edict_t *ally, *flying, *mechanical, *wave = NULL;

    reset_entities(); setup_test_world();
    old = G_SetSLKRows("AbilityData", rows);
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    caster = impale_unit(0, 0, 0, TARG_GROUND);
    near = impale_unit(80, 0, 1, TARG_GROUND);
    middle = impale_unit(260, 0, 1, TARG_GROUND);
    far = impale_unit(490, 0, 1, TARG_GROUND);
    behind = impale_unit(-80, 0, 1, TARG_GROUND);
    beyond = impale_unit(680, 0, 1, TARG_GROUND);
    side = impale_unit(260, 140, 1, TARG_GROUND);
    ally = impale_unit(150, 0, 0, TARG_GROUND);
    flying = impale_unit(210, 0, 1, TARG_AIR);
    mechanical = impale_unit(320, 0, 1, TARG_MECHANICAL);
    level.time = 1000;
    T_ASSERT(impale_cast(caster, "AUim", (vec2_t){ 700, 0 }));
    FILTER_EDICTS(ent, ent->think == impale_wave_think) { wave = ent; break; }
    T_NOT_NULL(wave);
    if (!wave) goto cleanup;
    T_NOT_NULL(G_GetImpaleWave(wave));
    T_FEQ(near->health.value, 500, 0.001f);
    level.time = 1100; G_RunEntities();
    T_FEQ(near->health.value, 427, 0.001f);
    T_FEQ(middle->health.value, 500, 0.001f);
    T_FEQ(far->health.value, 500, 0.001f);
    level.time = 1200; G_RunEntities();
    T_FEQ(near->health.value, 427, 0.001f);
    T_FEQ(middle->health.value, 500, 0.001f);
    level.time = 1300; G_RunEntities();
    T_FEQ(middle->health.value, 427, 0.001f);
    T_FEQ(far->health.value, 500, 0.001f);
    level.time = 1600; G_RunEntities();
    T_FEQ(far->health.value, 427, 0.001f);
    T_FEQ(near->health.value, 427, 0.001f);
    T_FEQ(middle->health.value, 427, 0.001f);
    T_FEQ(behind->health.value, 500, 0.001f);
    T_FEQ(beyond->health.value, 500, 0.001f);
    T_FEQ(side->health.value, 500, 0.001f);
    T_FEQ(ally->health.value, 500, 0.001f);
    T_FEQ(flying->health.value, 500, 0.001f);
    T_FEQ(mechanical->health.value, 500, 0.001f);
    T_ASSERT(!wave->inuse);
cleanup:
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, impale_creep_alias_uses_its_own_wave_data_and_owner_identity) {
    slkTestData_t *rows = parse_slk_string(impale_slk), *old;
    edict_t *caster, *hit, *out_of_range, *wave = NULL;
    reset_entities(); setup_test_world();
    old = G_SetSLKRows("AbilityData", rows);
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    caster = impale_unit(0, 0, 0, TARG_GROUND);
    hit = impale_unit(150, 0, 1, TARG_GROUND);
    out_of_range = impale_unit(450, 0, 1, TARG_GROUND);
    level.time = 1000;
    T_ASSERT(impale_cast(caster, "ACmp", (vec2_t){ 700, 0 }));
    FILTER_EDICTS(ent, ent->think == impale_wave_think) { wave = ent; break; }
    T_NOT_NULL(wave);
    if (!wave) goto cleanup;
    T_EQ(G_GetImpaleWave(wave)->ability, MAKEFOURCC('A','C','m','p'));
    T_FEQ(G_GetImpaleWave(wave)->distance, 400, 0.001f);
    level.time = 1200; G_RunEntities();
    T_FEQ(hit->health.value, 463, 0.001f);
    T_FEQ(out_of_range->health.value, 500, 0.001f);
    /* Reusing the caster's entity identity must cancel the pending wave. */
    caster->spawn_time++;
    level.time = 1300; G_RunEntities();
    T_ASSERT(!wave->inuse);
    T_FEQ(out_of_range->health.value, 500, 0.001f);
cleanup:
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, impale_rejects_zero_direction_or_nonpositive_travel_data) {
    slkTestData_t *rows = parse_slk_string(impale_slk), *old;
    edict_t *caster;
    abilityitem_t item;
    spellTarget_t target;
    abilityCall_t call;
    reset_entities(); setup_test_world();
    old = G_SetSLKRows("AbilityData", rows);
    caster = impale_unit(0, 0, 0, TARG_GROUND);
    item = S_AbilityItem(MAKEFOURCC('A','U','i','m'));
    target = MAKE(spellTarget_t, .type = SPELL_TARGET_POINT, .point = caster->s.origin2);
    call = MAKE(abilityCall_t, .item = &item, .target = &target);
    T_ASSERT(!S_AbilityMessage(caster, A_VALIDATE, &call));
    T_ASSERT(!S_AbilityMessage(caster, A_EXECUTE, &call));
    FILTER_EDICTS(ent, ent->think == impale_wave_think)
        T_ASSERT(false);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* This test drives the real travelling wave, timed status scheduler, physics
 * height resolver, and monster control lock, rather than a mock stun helper. */
TEST(wc3_spell, impale_stun_blocks_actions_after_airtime_and_lift_lands) {
    slkTestData_t *rows = parse_slk_string(impale_slk), *old;
    edict_t *caster, *victim;
    float base;
    reset_entities(); setup_test_world();
    old = G_SetSLKRows("AbilityData", rows);
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    caster = impale_unit(0, 0, 0, TARG_GROUND);
    victim = impale_unit(80, 0, 1, TARG_GROUND);
    victim->unitinfo.FlyHeight = 30.0f;
    level.time = 1000; M_CheckGround(victim);
    base = victim->s.origin.z;
    T_ASSERT(impale_cast(caster, "AUim", (vec2_t){ 700, 0 }));
    level.time = 1100; G_RunEntities();
    T_ASSERT(unit_hasstatusstate(victim, WC3_STATUS_STATE_STUNNED));
    T_ASSERT(!S_UnitCanTranslate(victim));
    T_NOT_NULL(unit_findstatus(victim, MAKEFOURCC('B','U','i','m')));
    T_NOT_NULL(G_GetImpaleLift(victim));
    T_FEQ(victim->unitinfo.FlyHeight, 30.0f, 0.001f);
    level.time = 1300; G_RunEntities();
    T_ASSERT(victim->s.origin.z > base + 1.0f);
    T_ASSERT(unit_hasstatusstate(victim, WC3_STATUS_STATE_STUNNED));
    level.time = 1950; G_RunEntities();
    T_NULL(G_GetImpaleLift(victim));
    T_FEQ(victim->s.origin.z, base, 0.01f);
    T_ASSERT(unit_hasstatusstate(victim, WC3_STATUS_STATE_STUNNED));
    level.time = 3150; G_RunEntities();
    T_ASSERT(!unit_hasstatusstate(victim, WC3_STATUS_STATE_STUNNED));
    T_ASSERT(S_UnitCanTranslate(victim));
    T_FEQ(victim->unitinfo.FlyHeight, 30.0f, 0.001f);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, impale_lift_does_not_erase_other_flight_or_stun_contributors) {
    slkTestData_t *rows = parse_slk_string(impale_slk), *old;
    edict_t *caster, *victim;
    heroabilitystatus_t *other_stun;
    float base;
    reset_entities(); setup_test_world();
    old = G_SetSLKRows("AbilityData", rows);
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    caster = impale_unit(0, 0, 0, TARG_GROUND);
    victim = impale_unit(80, 0, 1, TARG_GROUND);
    level.time = 1000;
    other_stun = unit_applystatus(victim, &(status_application_t){
        .buff = "Bstu", .level = 1, .duration = 5.0f
    });
    T_NOT_NULL(other_stun);
    T_ASSERT(impale_cast(caster, "AUim", (vec2_t){ 700, 0 }));
    level.time = 1100; G_RunEntities();
    T_NOT_NULL(G_GetImpaleLift(victim));
    level.time = 1250; G_RunEntities();
    T_ASSERT(S_ImpaleLiftOffset(victim) > 0.0f);
    /* Cyclone/Ensnare/Raven adjust this same independent flight setting. */
    victim->unitinfo.FlyHeight = 85.0f;
    M_CheckGround(victim);
    base = victim->s.origin.z - S_ImpaleLiftOffset(victim);
    T_FEQ(victim->s.ground_offset, 85.0f + S_ImpaleLiftOffset(victim), 0.01f);
    level.time = 2100; G_RunEntities();
    T_NULL(G_GetImpaleLift(victim));
    T_FEQ(victim->s.origin.z, base, 0.01f);
    T_FEQ(victim->unitinfo.FlyHeight, 85.0f, 0.001f);
    T_ASSERT(unit_hasstatusstate(victim, WC3_STATUS_STATE_STUNNED));
    level.time = 3100; G_RunEntities(); /* BUim is gone; independent Bstu remains. */
    T_NULL(unit_findstatus(victim, MAKEFOURCC('B','U','i','m')));
    T_ASSERT(unit_hasstatusstate(victim, WC3_STATUS_STATE_STUNNED));
    level.time = 6100; G_RunEntities();
    T_ASSERT(!unit_hasstatusstate(victim, WC3_STATUS_STATE_STUNNED));
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, impale_second_wave_refreshes_lift_without_height_snap) {
    slkTestData_t *rows = parse_slk_string(impale_slk), *old;
    edict_t *caster, *victim;
    float previous;
    reset_entities(); setup_test_world();
    old = G_SetSLKRows("AbilityData", rows);
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    caster = impale_unit(0, 0, 0, TARG_GROUND);
    victim = impale_unit(80, 0, 1, TARG_GROUND);
    level.time = 1000;
    T_ASSERT(impale_cast(caster, "AUim", (vec2_t){ 700, 0 }));
    level.time = 1100; G_RunEntities();
    level.time = 1300; G_RunEntities();
    previous = S_ImpaleLiftOffset(victim);
    T_ASSERT(previous > 0.0f);
    T_ASSERT(impale_cast(caster, "AUim", (vec2_t){ 700, 0 }));
    level.time = 1400; G_RunEntities();
    T_NOT_NULL(G_GetImpaleLift(victim));
    T_ASSERT(S_ImpaleLiftOffset(victim) > 0.0f);
    T_ASSERT(unit_hasstatusstate(victim, WC3_STATUS_STATE_STUNNED));
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, impale_hero_stun_uses_hero_duration_and_lift_cleans_up_on_death) {
    static UnitBalance_t const hero_balance = { .strength = 18, .healthRegenType = "none" };
    slkTestData_t *rows = parse_slk_string(impale_slk), *old;
    edict_t *caster, *hero;
    float original_height;
    reset_entities(); setup_test_world();
    old = G_SetSLKRows("AbilityData", rows);
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    caster = impale_unit(0, 0, 0, TARG_GROUND);
    hero = impale_unit(80, 0, 1, TARG_GROUND);
    hero->data.UnitBalance = (UnitBalance_t *)&hero_balance;
    level.time = 1000;
    M_CheckGround(hero);
    original_height = hero->s.origin.z;
    T_ASSERT(G_UnitIsHero(hero));
    T_ASSERT(impale_cast(caster, "AUim", (vec2_t){ 700, 0 }));
    level.time = 1100; G_RunEntities();
    T_ASSERT(unit_hasstatusstate(hero, WC3_STATUS_STATE_STUNNED));
    T_NOT_NULL(G_GetImpaleLift(hero));
    level.time = 1300; G_RunEntities();
    T_ASSERT(hero->s.origin.z > original_height);
    G_SetHealth(hero, 0.0f);
    S_ImpaleLiftUpdate(hero);
    T_NULL(G_GetImpaleLift(hero));
    T_FEQ(hero->s.origin.z, original_height, 0.01f);
    /* The other case keeps the hero alive until authored HeroDur=1s. */
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, impale_hero_duration_is_distinct_from_normal_stun) {
    static UnitBalance_t const hero_balance = { .strength = 18, .healthRegenType = "none" };
    slkTestData_t *rows = parse_slk_string(impale_slk), *old;
    edict_t *caster, *hero, *normal;
    reset_entities(); setup_test_world();
    old = G_SetSLKRows("AbilityData", rows);
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    caster = impale_unit(0, 0, 0, TARG_GROUND);
    hero = impale_unit(80, 0, 1, TARG_GROUND);
    normal = impale_unit(90, 0, 1, TARG_GROUND);
    hero->data.UnitBalance = (UnitBalance_t *)&hero_balance;
    T_ASSERT(G_UnitIsHero(hero));
    level.time = 1000;
    T_ASSERT(impale_cast(caster, "AUim", (vec2_t){ 700, 0 }));
    level.time = 1100; G_RunEntities();
    T_ASSERT(unit_hasstatusstate(hero, WC3_STATUS_STATE_STUNNED));
    T_ASSERT(unit_hasstatusstate(normal, WC3_STATUS_STATE_STUNNED));
    level.time = 2150; G_RunEntities();
    T_ASSERT(!unit_hasstatusstate(hero, WC3_STATUS_STATE_STUNNED));
    T_ASSERT(unit_hasstatusstate(normal, WC3_STATUS_STATE_STUNNED));
    level.time = 3150; G_RunEntities();
    T_ASSERT(!unit_hasstatusstate(normal, WC3_STATUS_STATE_STUNNED));
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Tests use the real G_SpawnAbilityEffect* path and minimal generated MDX
 * fixtures in tests.mpq, not locally extracted retail models. The ability
 * and buff identities have different art: AUim's special/miss effect must
 * never be substituted for the BUim target/hit effect. */
static cstring_t const impale_visual_config =
    "[AUim]\n"
    "CasterArt=TestUI\\Models\\anim_oneshot.mdx\n"
    "SpecialArt=TestUI\\Models\\anim_projectile.mdx\n"
    "[ACmp]\n"
    "CasterArt=TestUI\\Models\\anim_oneshot.mdx\n"
    "SpecialArt=TestUI\\Models\\anim_projectile.mdx\n";

static cstring_t const impale_buff_visual_slk =
    "ID;PWXL;N;EBB;Y2;X3\n"
    "C;Y1;X1;K\"alias\"\n"
    "C;Y1;X2;K\"code\"\n"
    "C;Y1;X3;K\"TargetArt\"\n"
    "C;Y2;X1;K\"BUim\"\n"
    "C;Y2;X2;K\"BUim\"\n"
    "C;Y2;X3;K\"TestUI\\Models\\anim_pulse.mdx\"\nE\n";

static uint32_t impale_count_visuals(uint32_t model, edict_t const *target,
                                      float min_x, float max_x) {
    uint32_t count = 0;
    FILTER_EDICTS(effect, effect->s.model == model && effect->goalentity == target &&
                  effect->s.origin2.x >= min_x && effect->s.origin2.x <= max_x)
        count++;
    return count;
}

TEST(wc3_spell, impale_cast_hit_and_miss_models_follow_wave_timing) {
    slkTestData_t *rows = parse_slk_string(impale_slk), *old;
    slkTestData_t *buff_rows = parse_slk_string(impale_buff_visual_slk), *old_buff;
    edict_t *caster, *first, *second, *ally;
    uint32_t caster_model, miss_model, hit_model;
    reset_entities(); setup_test_world();
    old = G_SetSLKRows("AbilityData", rows);
    old_buff = G_SetSLKRows("AbilityBuffData", buff_rows);
    T_ASSERT(G_TestPushAbilityConfig(impale_visual_config));
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    caster = impale_unit(0, 0, 0, TARG_GROUND);
    first = impale_unit(80, 0, 1, TARG_GROUND);
    second = impale_unit(260, 0, 1, TARG_GROUND);
    ally = impale_unit(180, 0, 0, TARG_GROUND);
    caster_model = G_RegisterModel("TestUI\\Models\\anim_oneshot.mdx");
    miss_model = G_RegisterModel("TestUI\\Models\\anim_projectile.mdx");
    hit_model = G_RegisterModel("TestUI\\Models\\anim_pulse.mdx");
    T_ASSERT(caster_model && miss_model && hit_model);
    T_NE(miss_model, hit_model);
    T_STREQ(G_AbilityEffectArt(MAKEFOURCC('A','U','i','m'), WC3_EFFECT_SPECIAL, 0),
             "TestUI\\Models\\anim_projectile.mdx");
    T_STREQ(G_AbilityEffectArt(MAKEFOURCC('B','U','i','m'), WC3_EFFECT_TARGET, 0),
             "TestUI\\Models\\anim_pulse.mdx");

    level.time = 1000;
    T_ASSERT(impale_cast(caster, "AUim", (vec2_t){ 700, 0 }));
    T_EQ(impale_count_visuals(caster_model, caster, -1, 1), 1u);
    T_EQ(impale_count_visuals(hit_model, NULL, -1, 600), 0u);
    T_EQ(impale_count_visuals(miss_model, NULL, -1, 600), 0u);

    level.time = 1100; G_RunEntities(); /* first 100 world units */
    T_FEQ(first->health.value, 427.0f, 0.001f);
    T_FEQ(second->health.value, 500.0f, 0.001f);
    T_EQ(impale_count_visuals(hit_model, NULL, 79, 81), 1u);
    T_EQ(impale_count_visuals(miss_model, NULL, 63, 65), 0u); /* impact, not miss */

    level.time = 1200; G_RunEntities(); /* 100..200: samples at 128 and 192 */
    T_FEQ(second->health.value, 500.0f, 0.001f);
    T_EQ(impale_count_visuals(miss_model, NULL, 127, 129), 1u);
    T_EQ(impale_count_visuals(miss_model, NULL, 191, 193), 1u);
    T_FEQ(ally->health.value, 500.0f, 0.001f);

    level.time = 1300; G_RunEntities(); /* second hit, around sample 256 */
    T_FEQ(second->health.value, 427.0f, 0.001f);
    T_EQ(impale_count_visuals(hit_model, NULL, 259, 261), 1u);
    T_EQ(impale_count_visuals(miss_model, NULL, 255, 257), 0u);

    G_TestPopAbilityConfig();
    G_SetSLKRows("AbilityBuffData", old_buff);
    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows); free_slk_rows(buff_rows);
}

TEST(wc3_spell, impale_creep_alias_resolves_its_own_caster_art) {
    slkTestData_t *rows = parse_slk_string(impale_slk), *old;
    edict_t *caster, *victim;
    uint32_t caster_model;
    reset_entities(); setup_test_world();
    old = G_SetSLKRows("AbilityData", rows);
    T_ASSERT(G_TestPushAbilityConfig(impale_visual_config));
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    caster = impale_unit(0, 0, 0, TARG_GROUND);
    victim = impale_unit(150, 0, 1, TARG_GROUND);
    caster_model = G_RegisterModel("TestUI\\Models\\anim_oneshot.mdx");
    level.time = 1000;
    T_ASSERT(impale_cast(caster, "ACmp", (vec2_t){ 700, 0 }));
    T_EQ(impale_count_visuals(caster_model, caster, -1, 1), 1u);
    level.time = 1200; G_RunEntities();
    T_FEQ(victim->health.value, 463.0f, 0.001f);
    G_TestPopAbilityConfig();
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Art is optional, including when custom game data explicitly disables it.
 * The wave must still deal damage, apply BUim stun and schedule DataD lift. */
TEST(wc3_spell, impale_missing_art_does_not_block_damage_or_lift) {
    static cstring_t const disabled_art =
        "[AUim]\nCasterArt=-\nSpecialArt=-\n"
        "[BUim]\nTargetArt=-\n";
    slkTestData_t *rows = parse_slk_string(impale_slk), *old;
    edict_t *caster, *victim;
    reset_entities(); setup_test_world();
    old = G_SetSLKRows("AbilityData", rows);
    T_ASSERT(G_TestPushAbilityConfig(disabled_art));
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    caster = impale_unit(0, 0, 0, TARG_GROUND);
    victim = impale_unit(80, 0, 1, TARG_GROUND);
    level.time = 1000;
    T_ASSERT(impale_cast(caster, "AUim", (vec2_t){ 700, 0 }));
    level.time = 1100; G_RunEntities();
    T_FEQ(victim->health.value, 427.0f, 0.001f);
    T_ASSERT(unit_hasstatusstate(victim, WC3_STATUS_STATE_STUNNED));
    T_NOT_NULL(G_GetImpaleLift(victim));
    G_TestPopAbilityConfig();
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Legacy and modern damage policy is read from the concrete ability's DataF.
 * A victim is never offered as a new unit-targeted spell target while airborne.
 * The damage path must still preserve the victim's normal vulnerability after
 * landing, and keep its ordinary stun duration until Dur/HeroDur expires. */
TEST(wc3_spell, impale_legacy_airborne_blocks_attacks_damage_and_targeted_spells) {
    slkTestData_t *rows = parse_slk_string(impale_slk), *old;
    edict_t *caster, *victim;
    float health;
    reset_entities(); setup_test_world();
    old = G_SetSLKRows("AbilityData", rows);
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    caster = impale_unit(0, 0, 0, TARG_GROUND);
    victim = impale_unit(80, 0, 1, TARG_GROUND);
    caster->data.UnitWeapons = (UnitWeapons_t *)&impale_test_weapon;
    caster->unit_state->attack1.type = ATK_NORMAL;
    caster->unit_state->attack1.targetsAllowed = WC3_TARGET_FLAG_GROUND;
    T_ASSERT(S_AttackCanTarget(caster, victim));
    level.time = 1000;
    T_ASSERT(impale_cast(caster, "AUim", (vec2_t){ 700, 0 }));
    level.time = 1100; G_RunEntities();
    T_ASSERT(S_ImpaleIsAirborne(victim));
    T_ASSERT(!S_ImpaleAirborneVulnerable(victim));
    T_ASSERT(!S_AttackCanTarget(caster, victim));
    T_ASSERT(!S_SpellAllowsTarget(FS_SLKKey("AUim"), caster, victim));
    T_ASSERT(!S_SpellAllowsAreaTarget(FS_SLKKey("AUim"), caster, victim));
    health = victim->health.value;
    T_ASSERT(health > 0.0f);
    level.time = 2000; G_RunEntities();
    T_ASSERT(!S_ImpaleIsAirborne(victim));
    T_ASSERT(S_AttackCanTarget(caster, victim));
    T_ASSERT(S_SpellAllowsAreaTarget(FS_SLKKey("AUim"), caster, victim));
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, impale_modern_airborne_accepts_damage_but_not_new_unit_spells) {
    slkTestData_t *rows = parse_slk_string(impale_modern_slk), *old;
    edict_t *caster, *victim, *wave = NULL;
    float health;
    reset_entities(); setup_test_world();
    old = G_SetSLKRows("AbilityData", rows);
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    caster = impale_unit(0, 0, 0, TARG_GROUND);
    victim = impale_unit(80, 0, 1, TARG_GROUND);
    caster->data.UnitWeapons = (UnitWeapons_t *)&impale_test_weapon;
    caster->unit_state->attack1.type = ATK_NORMAL;
    caster->unit_state->attack1.targetsAllowed = WC3_TARGET_FLAG_GROUND;
    T_ASSERT(S_AttackCanTarget(caster, victim));
    level.time = 1000;
    T_ASSERT(impale_cast(caster, "AUim", (vec2_t){ 700, 0 }));
    FILTER_EDICTS(ent, ent->think == impale_wave_think) { wave = ent; break; }
    T_NOT_NULL(wave);
    if (wave) T_EQ(G_GetImpaleWave(wave)->uninterruptible, 1u);
    level.time = 1100; G_RunEntities();
    T_ASSERT(S_ImpaleIsAirborne(victim));
    T_ASSERT(S_ImpaleAirborneVulnerable(victim));
    T_ASSERT(S_AttackCanTarget(caster, victim));
    T_ASSERT(!S_SpellAllowsTarget(FS_SLKKey("AUim"), caster, victim));
    T_ASSERT(S_SpellAllowsAreaTarget(FS_SLKKey("AUim"), caster, victim));
    health = victim->health.value;
    T_Damage(victim, caster, 40);
    T_ASSERT(victim->health.value < health);
    T_ASSERT(unit_hasstatusstate(victim, WC3_STATUS_STATE_STUNNED));
    level.time = 2000; G_RunEntities();
    T_ASSERT(!S_ImpaleIsAirborne(victim));
    T_ASSERT(unit_hasstatusstate(victim, WC3_STATUS_STATE_STUNNED));
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, impale_authored_interruptibility_controls_midflight_movement) {
    slkTestData_t *rows = parse_slk_string(impale_slk), *old;
    edict_t *caster, *victim, *wave = NULL;
    reset_entities(); setup_test_world();
    old = G_SetSLKRows("AbilityData", rows);
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    caster = impale_unit(0, 0, 0, TARG_GROUND);
    victim = impale_unit(360, 0, 1, TARG_GROUND);
    level.time = 1000;
    T_ASSERT(impale_cast(caster, "AUim", (vec2_t){ 700, 0 }));
    FILTER_EDICTS(ent, ent->think == impale_wave_think) { wave = ent; break; }
    T_NOT_NULL(wave);
    if (wave) T_EQ(G_GetImpaleWave(wave)->uninterruptible, 0u);
    level.time = 1100; G_RunEntities();
    caster->s.origin2.y += 10.0f;
    caster->s.origin.y += 10.0f;
    level.time = 1200; G_RunEntities();
    if (wave) T_ASSERT(!wave->inuse);
    T_FEQ(victim->health.value, 500.0f, 0.001f);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);

    rows = parse_slk_string(impale_modern_slk);
    reset_entities(); setup_test_world();
    old = G_SetSLKRows("AbilityData", rows);
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    caster = impale_unit(0, 0, 0, TARG_GROUND);
    victim = impale_unit(360, 0, 1, TARG_GROUND);
    level.time = 1000;
    T_ASSERT(impale_cast(caster, "AUim", (vec2_t){ 700, 0 }));
    level.time = 1100; G_RunEntities();
    caster->s.origin2.y += 10.0f;
    caster->s.origin.y += 10.0f;
    level.time = 1400; G_RunEntities();
    T_FEQ(victim->health.value, 427.0f, 0.001f);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}


TEST(wc3_spell, impale_does_not_stun_or_lift_invulnerable_targets) {
    slkTestData_t *rows = parse_slk_string(impale_modern_slk), *old;
    edict_t *caster, *victim, *valid;
    reset_entities(); setup_test_world();
    old = G_SetSLKRows("AbilityData", rows);
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    caster = impale_unit(0, 0, 0, TARG_GROUND);
    victim = impale_unit(80, 0, 1, TARG_GROUND);
    valid = impale_unit(110, 0, 1, TARG_GROUND);
    G_SetUnitFlag(victim, UNIT_FLAG_INVULNERABLE, true);
    level.time = 1000;
    T_ASSERT(impale_cast(caster, "AUim", (vec2_t){ 700, 0 }));
    level.time = 1200; G_RunEntities();
    T_FEQ(victim->health.value, 500, 0.001f);
    T_ASSERT(!unit_hasstatusstate(victim, WC3_STATUS_STATE_STUNNED));
    T_NULL(G_GetImpaleLift(victim));
    T_ASSERT(valid->health.value < 500.0f); /* invulnerable target must not block the sweep */
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

#endif
