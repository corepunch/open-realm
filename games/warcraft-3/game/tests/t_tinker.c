#ifdef BZ_TESTS
#include "shared/test.h"
#include "../g_local.h"
#include "../skills/s_skills.h"

edict_t *alloc_test_unit(uint32_t, float, float);
void reset_entities(void);
void setup_test_world(void);
slkTestData_t *parse_slk_string(char const *);
void free_slk_rows(slkTestData_t *);

static char const tinker_test_slk[] =
    "ID;PWXL;N;EBB;Y18;X26\n"
    "C;Y1;X1;K\"alias\"\n"
    "C;Y1;X2;K\"code\"\n"
    "C;Y1;X3;K\"levels\"\n"
    "C;Y1;X4;K\"targs\"\n"
    "C;Y1;X5;K\"DataA1\"\n"
    "C;Y1;X6;K\"DataB1\"\n"
    "C;Y1;X7;K\"DataC1\"\n"
    "C;Y1;X8;K\"DataD1\"\n"
    "C;Y1;X9;K\"DataE1\"\n"
    "C;Y1;X10;K\"DataF1\"\n"
    "C;Y1;X11;K\"Area1\"\n"
    "C;Y1;X12;K\"Dur1\"\n"
    "C;Y1;X13;K\"HeroDur1\"\n"
    "C;Y1;X14;K\"DataA2\"\n"
    "C;Y1;X15;K\"DataB2\"\n"
    "C;Y1;X16;K\"DataA3\"\n"
    "C;Y1;X17;K\"DataB3\"\n"
    "C;Y1;X18;K\"BuffID1\"\n"
    "C;Y1;X19;K\"DataC2\"\n"
    "C;Y1;X20;K\"DataD2\"\n"
    "C;Y1;X21;K\"DataE2\"\n"
    "C;Y1;X22;K\"DataF2\"\n"
    "C;Y1;X23;K\"DataC3\"\n"
    "C;Y1;X24;K\"DataD3\"\n"
    "C;Y1;X25;K\"DataE3\"\n"
    "C;Y1;X26;K\"DataF3\"\n"
    "C;Y2;X1;K\"ANeg\"\n"
    "C;Y2;X2;K\"ANeg\"\n"
    "C;Y2;X3;K\"3\"\n"
    "C;Y2;X5;K\".1\"\n"
    "C;Y2;X6;K\"2\"\n"
    "C;Y2;X14;K\".2\"\n"
    "C;Y2;X15;K\"4\"\n"
    "C;Y2;X16;K\".3\"\n"
    "C;Y2;X17;K\"6\"\n"
    "C;Y3;X1;K\"ANcs\"\n"
    "C;Y3;X2;K\"ANcs\"\n"
    "C;Y3;X3;K\"3\"\n"
    "C;Y3;X4;K\"air,ground,enemy,neutral,structure\"\n"
    "C;Y3;X5;K\"11.25\"\n"
    "C;Y3;X6;K\".25\"\n"
    "C;Y3;X7;K\"6\"\n"
    "C;Y3;X8;K\"105\"\n"
    "C;Y3;X9;K\"1\"\n"
    "C;Y3;X10;K\"1.01\"\n"
    "C;Y3;X11;K\"200\"\n"
    "C;Y3;X12;K\"1.01\"\n"
    "C;Y3;X13;K\"1.01\"\n"
    "C;Y3;X14;K\"18.75\"\n"
    "C;Y3;X15;K\".25\"\n"
    "C;Y3;X16;K\"27.5\"\n"
    "C;Y3;X17;K\".25\"\n"
    "C;Y3;X18;K\"BNcs\"\n"
    "C;Y3;X19;K\"12\"\n"
    "C;Y3;X20;K\"195\"\n"
    "C;Y3;X21;K\"1\"\n"
    "C;Y3;X22;K\"1.01\"\n"
    "C;Y3;X23;K\"18\"\n"
    "C;Y3;X24;K\"300\"\n"
    "C;Y3;X25;K\"1\"\n"
    "C;Y3;X26;K\"1.01\"\n"
    "C;Y4;X1;K\"ANrg\"\n"
    "C;Y4;X2;K\"ANrg\"\n"
    "C;Y4;X3;K\"1\"\n"
    "C;Y4;X9;K\"5\"\n"
    "C;Y4;X10;K\"1\"\n"
    "C;Y5;X1;K\"ANde\"\n"
    "C;Y5;X2;K\"ANde\"\n"
    "C;Y5;X3;K\"1\"\n"
    "C;Y5;X6;K\"2.0\"\n"
    "C;Y6;X1;K\"ANsy\"\n"
    "C;Y6;X2;K\"ANsy\"\n"
    "C;Y6;X3;K\"1\"\n"
    "C;Y6;X5;K\"5\"\n"
    "C;Y7;X1;K\"ANc1\"\n"
    "C;Y7;X2;K\"ANcs\"\n"
    "C;Y7;X3;K\"3\"\n"
    "C;Y7;X4;K\"air,ground,enemy,neutral,structure\"\n"
    "C;Y7;X5;K\"11.25\"\n"
    "C;Y7;X6;K\".25\"\n"
    "C;Y7;X7;K\"6\"\n"
    "C;Y7;X8;K\"105\"\n"
    "C;Y7;X9;K\"1\"\n"
    "C;Y7;X10;K\"1.01\"\n"
    "C;Y7;X11;K\"230\"\n"
    "C;Y7;X12;K\"1.01\"\n"
    "C;Y7;X13;K\"1.01\"\n"
    "C;Y7;X14;K\"18.75\"\n"
    "C;Y7;X15;K\".25\"\n"
    "C;Y7;X16;K\"27.5\"\n"
    "C;Y7;X17;K\".25\"\n"
    "C;Y7;X18;K\"BNcs\"\n"
    "C;Y7;X19;K\"12\"\n"
    "C;Y7;X20;K\"195\"\n"
    "C;Y7;X21;K\"1\"\n"
    "C;Y7;X22;K\"1.01\"\n"
    "C;Y7;X23;K\"18\"\n"
    "C;Y7;X24;K\"300\"\n"
    "C;Y7;X25;K\"1\"\n"
    "C;Y7;X26;K\"1.01\"\n"
    "C;Y8;X1;K\"ANg1\"\n"
    "C;Y8;X2;K\"ANrg\"\n"
    "C;Y8;X3;K\"1\"\n"
    "C;Y8;X9;K\"7\"\n"
    "C;Y8;X10;K\"2\"\n"
    "C;Y9;X1;K\"ANd1\"\n"
    "C;Y9;X2;K\"ANde\"\n"
    "C;Y9;X3;K\"1\"\n"
    "C;Y9;X6;K\"2.5\"\n"
    "C;Y10;X1;K\"ANs1\"\n"
    "C;Y10;X2;K\"ANsy\"\n"
    "C;Y10;X3;K\"1\"\n"
    "C;Y10;X5;K\"4\"\n"
    "C;Y11;X1;K\"ANc2\"\n"
    "C;Y11;X2;K\"ANcs\"\n"
    "C;Y11;X3;K\"3\"\n"
    "C;Y11;X4;K\"air,ground,enemy,neutral,structure\"\n"
    "C;Y11;X5;K\"11.25\"\n"
    "C;Y11;X6;K\".25\"\n"
    "C;Y11;X7;K\"6\"\n"
    "C;Y11;X8;K\"105\"\n"
    "C;Y11;X9;K\"1\"\n"
    "C;Y11;X10;K\"1.01\"\n"
    "C;Y11;X11;K\"260\"\n"
    "C;Y11;X12;K\"1.01\"\n"
    "C;Y11;X13;K\"1.01\"\n"
    "C;Y11;X14;K\"18.75\"\n"
    "C;Y11;X15;K\".25\"\n"
    "C;Y11;X16;K\"27.5\"\n"
    "C;Y11;X17;K\".25\"\n"
    "C;Y11;X18;K\"BNcs\"\n"
    "C;Y11;X19;K\"12\"\n"
    "C;Y11;X20;K\"195\"\n"
    "C;Y11;X21;K\"1\"\n"
    "C;Y11;X22;K\"1.01\"\n"
    "C;Y11;X23;K\"18\"\n"
    "C;Y11;X24;K\"300\"\n"
    "C;Y11;X25;K\"1\"\n"
    "C;Y11;X26;K\"1.01\"\n"
    "C;Y12;X1;K\"ANg2\"\n"
    "C;Y12;X2;K\"ANrg\"\n"
    "C;Y12;X3;K\"1\"\n"
    "C;Y12;X9;K\"9\"\n"
    "C;Y12;X10;K\"3\"\n"
    "C;Y13;X1;K\"ANd2\"\n"
    "C;Y13;X2;K\"ANde\"\n"
    "C;Y13;X3;K\"1\"\n"
    "C;Y13;X6;K\"3.0\"\n"
    "C;Y14;X1;K\"ANs2\"\n"
    "C;Y14;X2;K\"ANsy\"\n"
    "C;Y14;X3;K\"1\"\n"
    "C;Y14;X5;K\"3.2\"\n"
    "C;Y15;X1;K\"ANc3\"\n"
    "C;Y15;X2;K\"ANcs\"\n"
    "C;Y15;X3;K\"3\"\n"
    "C;Y15;X4;K\"air,ground,enemy,neutral,structure\"\n"
    "C;Y15;X5;K\"11.25\"\n"
    "C;Y15;X6;K\".25\"\n"
    "C;Y15;X7;K\"6\"\n"
    "C;Y15;X8;K\"105\"\n"
    "C;Y15;X9;K\"1\"\n"
    "C;Y15;X10;K\"1.01\"\n"
    "C;Y15;X11;K\"290\"\n"
    "C;Y15;X12;K\"1.01\"\n"
    "C;Y15;X13;K\"1.01\"\n"
    "C;Y15;X14;K\"18.75\"\n"
    "C;Y15;X15;K\".25\"\n"
    "C;Y15;X16;K\"27.5\"\n"
    "C;Y15;X17;K\".25\"\n"
    "C;Y15;X18;K\"BNcs\"\n"
    "C;Y15;X19;K\"12\"\n"
    "C;Y15;X20;K\"195\"\n"
    "C;Y15;X21;K\"1\"\n"
    "C;Y15;X22;K\"1.01\"\n"
    "C;Y15;X23;K\"18\"\n"
    "C;Y15;X24;K\"300\"\n"
    "C;Y15;X25;K\"1\"\n"
    "C;Y15;X26;K\"1.01\"\n"
    "C;Y16;X1;K\"ANg3\"\n"
    "C;Y16;X2;K\"ANrg\"\n"
    "C;Y16;X3;K\"1\"\n"
    "C;Y16;X9;K\"11\"\n"
    "C;Y16;X10;K\"4\"\n"
    "C;Y17;X1;K\"ANd3\"\n"
    "C;Y17;X2;K\"ANde\"\n"
    "C;Y17;X3;K\"1\"\n"
    "C;Y17;X6;K\"3.5\"\n"
    "C;Y18;X1;K\"ANs3\"\n"
    "C;Y18;X2;K\"ANsy\"\n"
    "C;Y18;X3;K\"1\"\n"
    "C;Y18;X5;K\"2.56\"\n"
    "E\n";

static UnitAbilities_t const tinker_unit_abilities = {
    .abilList = "ANcs,ANrg,ANeg,ANde,ANsy"
};

static edict_t *tinker_test_unit(uint32_t owner, float x) {
    edict_t *unit = alloc_test_unit(MAKEFOURCC('h','f','o','o'), x, 0);
    unit->svflags |= SVF_MONSTER;
    unit->s.player = owner;
    unit->targtype = TARG_GROUND;
    unit->spawn_time = G_Time();
    unit->health.max_value = unit->health.value = 1000.0f;
    unit->mana.max_value = unit->mana.value = 1000.0f;
    unit->stand = unit_stand;
    unit_stand(unit);
    return unit;
}

TEST(wc3_tinker, engineering_rank_selects_authored_variants_and_demolish) {
    slkTestData_t *rows = parse_slk_string(tinker_test_slk);
    slkTestData_t *previous = G_SetSLKRows("AbilityData", rows);
    edict_t *tinker, *building;
    reset_entities(); setup_test_world(); level.time = 1000;
    tinker = tinker_test_unit(0, 0);
    building = tinker_test_unit(1, 100);
    building->s.flags |= EF_BUILDING;
    tinker->data.UnitAbilities = &tinker_unit_abilities;
    tinker->class_id = MAKEFOURCC('N','r','o','b');
    tinker->unit_state->ranks[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','N','e','g'), .level = 1);
    T_EQ(S_TinkerVariant(tinker, MAKEFOURCC('A','N','c','s')), MAKEFOURCC('A','N','c','1'));
    T_EQ(S_TinkerVariant(tinker, MAKEFOURCC('A','N','s','y')), MAKEFOURCC('A','N','s','1'));
    T_EQ(S_TinkerVariant(tinker, MAKEFOURCC('A','N','r','g')), MAKEFOURCC('A','N','g','1'));
    T_FEQ(S_TinkerEngineeringMoveBonus(tinker), .1f, .001f);
    T_FEQ(S_TinkerEngineeringDamageBonus(tinker), 2.0f, .001f);
    T_FEQ(S_TinkerRoboArmor(tinker), 2.0f, .001f);
    T_FEQ(S_TinkerDemolishMultiplier(tinker, building), 2.5f, .001f);
    tinker->unit_state->ranks[0].level = 2;
    T_EQ(S_TinkerVariant(tinker, MAKEFOURCC('A','N','c','s')), MAKEFOURCC('A','N','c','2'));
    T_FEQ(S_TinkerEngineeringDamageBonus(tinker), 4.0f, .001f);
    T_FEQ(S_TinkerDemolishMultiplier(tinker, building), 3.0f, .001f);
    tinker->unit_state->ranks[0].level = 3;
    T_FEQ(S_TinkerEngineeringMoveBonus(tinker), .3f, .001f);
    T_FEQ(S_TinkerRoboArmor(tinker), 4.0f, .001f);
    T_FEQ(S_TinkerDemolishMultiplier(tinker, building), 3.5f, .001f);
    building->s.flags &= ~EF_BUILDING;
    T_FEQ(S_TinkerDemolishMultiplier(tinker, building), 1.0f, .001f);
    G_SetSLKRows("AbilityData", previous); free_slk_rows(rows);
}

/* Engineering's +2 attack damage must reach Robo-Goblin's building hit after
 * the authored Demolish multiplier is applied in the shared attack pipeline. */
TEST(wc3_tinker, engineering_adds_two_damage_to_robo_goblin_demolish_attack) {
    static UnitBalance_t const tinker_balance = {
        .maxHealth = 1000.0f,
        .strength = 18,
        .primaryAttribute = "STR"
    };
    static UnitWeapons_t const tinker_weapons = {
        .attacksEnabled = 1,
        .attack1 = { .damageBase = 10 }
    };
    slkTestData_t *rows = parse_slk_string(tinker_test_slk);
    slkTestData_t *previous = G_SetSLKRows("AbilityData", rows);
    edict_t *tinker, *building;
    uint32_t base_attack_without_engineering, base_attack_with_engineering;
    int32_t attack_damage_without_engineering, attack_damage_with_engineering;
    float effective_multiplier;
    int32_t expected_engineering_damage;

    reset_entities(); setup_test_world();
    tinker = tinker_test_unit(0, 0);
    building = tinker_test_unit(1, 100);
    tinker->class_id = MAKEFOURCC('N','r','o','b');
    tinker->data.UnitBalance = &tinker_balance;
    tinker->data.UnitWeapons = &tinker_weapons;
    tinker->data.UnitAbilities = &tinker_unit_abilities;
    tinker->unit_state->hero.str = tinker_balance.strength;
    tinker->unit_state->attack1.type = ATK_NORMAL;
    tinker->unit_state->attack1.targetsAllowed = UINT32_MAX;
    building->s.flags |= EF_BUILDING;
    building->unit_state->armor_value = 0.0f;
    building->unit_state->defense_type = 0;

    G_RecomputeHeroStats(tinker);
    base_attack_without_engineering = tinker->unit_state->attack1.damageBase;
    tinker->unit_state->ranks[0] = MAKE(heroability_t,
        .code = MAKEFOURCC('A','N','e','g'), .level = 1);
    G_RecomputeHeroStats(tinker);
    base_attack_with_engineering = tinker->unit_state->attack1.damageBase;
    T_EQ(base_attack_with_engineering, base_attack_without_engineering + 2);
    T_FEQ(S_TinkerDemolishMultiplier(tinker, building), 2.5f, .001f);

    attack_damage_without_engineering = G_AttackDamage(
        tinker, building, (int32_t)base_attack_with_engineering - 2);
    attack_damage_with_engineering = G_AttackDamage(
        tinker, building, (int32_t)base_attack_with_engineering);
    effective_multiplier = S_TinkerDemolishMultiplier(tinker, building) *
        G_AttackTypeDamageMultiplier(ATK_NORMAL, building->unit_state->defense_type);
    expected_engineering_damage = (int32_t)((float)base_attack_with_engineering * effective_multiplier) -
        (int32_t)(((float)base_attack_with_engineering - 2.0f) * effective_multiplier);
    T_EQ(attack_damage_with_engineering - attack_damage_without_engineering,
         expected_engineering_damage);
    T_ASSERT(attack_damage_with_engineering > attack_damage_without_engineering);

    G_SetSLKRows("AbilityData", previous); free_slk_rows(rows);
}

TEST(wc3_tinker, cluster_rockets_four_independent_damage_intervals) {
    slkTestData_t *rows = parse_slk_string(tinker_test_slk);
    slkTestData_t *previous = G_SetSLKRows("AbilityData", rows);
    edict_t *caster, *enemy, *outsider, *thinker = NULL;
    abilityitem_t ability = S_AbilityItem(MAKEFOURCC('A','N','c','s'));
    spellTarget_t point = { .type = SPELL_TARGET_POINT, .point = {100.0f, 0.0f} };
    abilityCall_t call = MAKE(abilityCall_t, .item = &ability, .target = &point);
    reset_entities(); setup_test_world(); level.time = 1000;
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    caster = tinker_test_unit(0, 0);
    caster->data.UnitAbilities = &tinker_unit_abilities;
    enemy = tinker_test_unit(1, 100);
    outsider = tinker_test_unit(1, 500);
    T_ASSERT(CAbilityClusterRockets(caster, A_VALIDATE, &call));
    T_ASSERT(CAbilityClusterRockets(caster, A_EXECUTE, &call));
    FILTER_EDICTS(candidate, candidate->owner == caster && candidate->think == tinker_rockets_think) {
        thinker = candidate; break;
    }
    T_NOT_NULL(thinker);
    if (thinker) {
        T_EQ(thinker->resources, 4u);
        FOR_LOOP(i, 4) {
            level.time = 1250 + 250 * i;
            tinker_rockets_think(thinker);
        }
        T_ASSERT(!thinker->inuse);
    }
    /* 11.25 damage every quarter second totals 45, not four rounded 11s. */
    T_FEQ(enemy->health.value, 955.0f, .001f);
    T_FEQ(outsider->health.value, 1000.0f, .001f);
    G_SetSLKRows("AbilityData", previous); free_slk_rows(rows);
}

TEST(wc3_tinker, cluster_rockets_thinker_round_trips_mid_salvo) {
    cstring_t path = Test_TempPath("wc3-tinker-rockets.bin");
    slkTestData_t *rows = parse_slk_string(tinker_test_slk);
    slkTestData_t *previous = G_SetSLKRows("AbilityData", rows);
    edict_t *caster, *thinker = NULL;
    abilityitem_t ability = S_AbilityItem(MAKEFOURCC('A','N','c','s'));
    spellTarget_t point = { .type = SPELL_TARGET_POINT, .point = {100.0f, 0.0f} };
    abilityCall_t call = MAKE(abilityCall_t, .item = &ability, .target = &point);
    reset_entities(); setup_test_world(); level.time = 1000;
    caster = tinker_test_unit(0, 0);
    caster->data.UnitAbilities = &tinker_unit_abilities;
    T_ASSERT(CAbilityClusterRockets(caster, A_EXECUTE, &call));
    FILTER_EDICTS(candidate, candidate->owner == caster && candidate->think == tinker_rockets_think) {
        thinker = candidate; break;
    }
    T_NOT_NULL(thinker);
    if (thinker) {
        level.time = 1250;
        tinker_rockets_think(thinker);
        T_EQ(thinker->resources, 3u);
        T_ASSERT(WriteGame(path));
        thinker->think = NULL;
        thinker->resources = 0;
        T_ASSERT(ReadGame(path));
        T_ASSERT(thinker->think == tinker_rockets_think);
        T_EQ(thinker->resources, 3u);
        T_ASSERT(S_SpellChannelOwner(thinker) == caster);
    }
    remove(path);
    G_SetSLKRows("AbilityData", previous); free_slk_rows(rows);
}
#endif /* BZ_TESTS */
