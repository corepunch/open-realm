#ifdef BZ_TESTS
#include "test.h"
#include "../skills/s_skills.h"

#define BZ_ACCB MAKEFOURCC('A', 'C', 'c', 'b')
#define BZ_ACDE MAKEFOURCC('A', 'C', 'd', 'e')
#define BZ_ACCA MAKEFOURCC('A', 'C', 'c', 'a')
#define BZ_ACCV MAKEFOURCC('A', 'C', 'c', 'v')
#define BZ_ACC2 MAKEFOURCC('A', 'C', 'c', '2')
#define BZ_ACC3 MAKEFOURCC('A', 'C', 'c', '3')
#define BZ_AENR MAKEFOURCC('A', 'e', 'n', 'r')
#define BZ_AENW MAKEFOURCC('A', 'e', 'n', 'w')
#define BZ_ACMF MAKEFOURCC('A', 'C', 'm', 'f')
#define BZ_ACSA MAKEFOURCC('A', 'C', 's', 'a')
#define BZ_ACPY MAKEFOURCC('A', 'C', 'p', 'y')
#define BZ_ACSL MAKEFOURCC('A', 'C', 's', 'l')
#define BZ_ANE2 MAKEFOURCC('A', 'n', 'e', '2')
#define BZ_AHFA MAKEFOURCC('A', 'H', 'f', 'a')
#define BZ_ANEU MAKEFOURCC('A', 'n', 'e', 'u')
#define BZ_BNMS MAKEFOURCC('B', 'N', 'm', 's')
#define BZ_BPLY MAKEFOURCC('B', 'p', 'l', 'y')
#define BZ_BUSL MAKEFOURCC('B', 'U', 's', 'l')
#define BZ_BSLP MAKEFOURCC('B', 's', 'l', 'p')

LPEDICT alloc_test_unit(DWORD class_id, FLOAT x, FLOAT y);
void reset_entities(void);
void setup_test_world(void);
slkTestData_t *parse_slk_string(const char *text);
void free_slk_rows(slkTestData_t *rows);

static void creep_alias_world(void) {
    reset_entities(); setup_test_world(); level.time = 1000;
    ((LPMAPINFO)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((LPMAPINFO)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    memset(level.alliances, 0, sizeof(level.alliances));
}

/* True code= aliases share the parent procedure; wrong-class rows stay unregistered. */
TEST(wc3_spell, creep_aliases_share_parent_procedures) {
    T_EQ(S_AbilityItem(BZ_ACCB).ability->proc, CAbilityThunderBolt);
    T_EQ(S_AbilityItem(BZ_ACDE).ability->proc, CAbilityDispelMagic);
    T_EQ(S_AbilityItem(BZ_ACCA).ability->proc, CAbilityCarrionSwarm);
    T_EQ(S_AbilityItem(BZ_ACCV).ability->proc, CAbilityCarrionSwarm);
    T_EQ(S_AbilityItem(BZ_ACC2).ability->proc, CAbilityCarrionSwarm);
    T_EQ(S_AbilityItem(BZ_ACC3).ability->proc, CAbilityCarrionSwarm);
    T_EQ(S_AbilityItem(BZ_AENR).ability->proc, CAbilityEntanglingRoots);
    T_EQ(S_AbilityItem(BZ_AENW).ability->proc, CAbilityEntanglingRoots);
    T_EQ(S_AbilityItem(BZ_ACMF).ability->proc, CAbilityManaShield);
    T_EQ(FindAbilityByClassname("ACmf")->orders, FindAbilityByClassname("ANms")->orders);
    T_EQ(S_AbilityItem(BZ_ACSA).ability->proc, CAbilityFlamingArrows);
    T_EQ(S_AbilityItem(BZ_ACPY).ability->proc, CAbilityPolymorph);
    T_EQ(S_AbilityItem(BZ_ACSL).ability->proc, CAbilitySleep);
    T_EQ(FindAbilityByClassname("Ane2")->proc, CAbilityPassive);
    T_EQ(S_AbilityItem(BZ_ANE2).ability->proc, CAbilityPassive);
    T_EQ(S_AbilityItem(BZ_ANEU).ability->proc, CAbilityPassive);
    T_NULL(FindAbilityByClassname("ACmo"));
    T_NULL(FindAbilityByClassname("ACf3"));
    T_NULL(FindAbilityByClassname("ACfd"));
    T_NULL(FindAbilityByClassname("ACwb"));
    T_NULL(FindAbilityByClassname("AHta"));
    T_NULL(FindAbilityByClassname("Ache"));
}

/* ACmf DataA=2 is not ANms L1's 1; damage must read the owner's alias. */
TEST(wc3_spell, creep_mana_shield_uses_alias_data_and_orders) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y3;X6\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\nC;Y1;X4;K\"DataB1\"\n"
        "C;Y1;X5;K\"BuffID1\"\nC;Y1;X6;K\"Area1\"\n"
        "C;Y2;X1;K\"ANms\"\nC;Y2;X2;K\"ANms\"\nC;Y2;X3;K\"1\"\nC;Y2;X4;K\"1\"\n"
        "C;Y2;X5;K\"BNms\"\nC;Y2;X6;K\"128\"\n"
        "C;Y3;X1;K\"ACmf\"\nC;Y3;X2;K\"ANms\"\nC;Y3;X3;K\"2\"\nC;Y3;X4;K\"1\"\n"
        "C;Y3;X5;K\"BNms\"\nC;Y3;X6;K\"128\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "ACmf" };
    slkTestData_t *rows = parse_slk_string(slk), *old;
    LPEDICT attacker, caster;

    creep_alias_world();
    old = G_SetSLKRows("AbilityData", rows);
    attacker = alloc_test_unit(MAKEFOURCC('o', 'g', 'r', 'u'), 50, 0);
    caster = alloc_test_unit(MAKEFOURCC('n', 'b', 'd', 'r'), 0, 0);
    caster->data.UnitAbilities = &abilities;
    caster->health.value = caster->health.max_value = 100.0f;
    caster->mana.value = caster->mana.max_value = 3.0f;
    caster->svflags |= SVF_MONSTER;
    T_ASSERT(S_CastNoTargetSpell(caster, BZ_ACMF));
    T_ASSERT(S_UnitHasStatus(caster, BZ_BNMS));
    T_EQ((int)S_SpellData(BZ_ACMF, 1, 1), 2);
    T_Damage(caster, attacker, 8);
    T_EQ((int)caster->health.value, 98);
    T_EQ((int)caster->mana.value, 0);
    T_ASSERT(!S_UnitHasStatus(caster, BZ_BNMS));
    caster->mana.value = 3.0f;
    T_ASSERT(unit_issueimmediateorder(caster, "manashieldon"));
    T_ASSERT(S_UnitHasStatus(caster, BZ_BNMS));
    T_ASSERT(unit_issueimmediateorder(caster, "manashieldon"));
    T_ASSERT(S_UnitHasStatus(caster, BZ_BNMS));
    T_ASSERT(unit_issueimmediateorder(caster, "manashieldoff"));
    T_ASSERT(!S_UnitHasStatus(caster, BZ_BNMS));
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Toggle stores ACsa; bonus damage must read that alias, not AHfa. */
TEST(wc3_spell, creep_searing_arrows_uses_alias_bonus_damage) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y3;X3\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\n"
        "C;Y2;X1;K\"AHfa\"\nC;Y2;X2;K\"AHfa\"\nC;Y2;X3;K\"10\"\n"
        "C;Y3;X1;K\"ACsa\"\nC;Y3;X2;K\"AHfa\"\nC;Y3;X3;K\"13\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "ACsa" };
    slkTestData_t *rows = parse_slk_string(slk), *old;
    LPEDICT attacker;

    creep_alias_world();
    old = G_SetSLKRows("AbilityData", rows);
    attacker = alloc_test_unit(MAKEFOURCC('n', 's', 'k', 'e'), 0, 0);
    attacker->data.UnitAbilities = &abilities;
    attacker->attack1.weapon = WPN_MISSILE;
    attacker->svflags |= SVF_MONSTER;
    T_ASSERT(S_CastNoTargetSpell(attacker, BZ_ACSA));
    T_EQ(G_UnitStatusLevel(attacker, BZ_ACSA), 1);
    T_EQ(G_UnitStatusLevel(attacker, BZ_AHFA), 0);
    T_EQ(S_SearingArrowDamage(attacker, 20), 33);
    attacker->attack1.weapon = WPN_NORMAL;
    T_EQ(S_SearingArrowDamage(attacker, 20), 20);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* ROC ACpy omits BuffID; human_buff must apply Bply, not a melee-table miss. */
TEST(wc3_spell, creep_polymorph_roc_empty_buffid_applies_bply) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X13\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\n"
        "C;Y1;X7;K\"Dur1\"\nC;Y1;X8;K\"HeroDur1\"\nC;Y1;X9;K\"DataA1\"\n"
        "C;Y1;X10;K\"DataB1\"\nC;Y1;X11;K\"DataC1\"\nC;Y1;X12;K\"DataD1\"\n"
        "C;Y1;X13;K\"DataE1\"\n"
        "C;Y2;X1;K\"ACpy\"\nC;Y2;X2;K\"Aply\"\nC;Y2;X3;K\"air,ground,enemy\"\n"
        "C;Y2;X4;K\"0\"\nC;Y2;X5;K\"0\"\nC;Y2;X6;K\"500\"\n"
        "C;Y2;X7;K\"60\"\nC;Y2;X8;K\"60\"\nC;Y2;X9;K\"5\"\n"
        "C;Y2;X10;K\"opeo\"\nC;Y2;X11;K\"opeo\"\nC;Y2;X12;K\"opeo\"\n"
        "C;Y2;X13;K\"opeo\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "ACpy" };
    UnitData_t ground = { .moveTypeName = "foot" };
    UnitBalance_t creep = { .level = 5 };
    slkTestData_t *rows = parse_slk_string(slk), *old;
    LPEDICT caster, target;

    creep_alias_world();
    old = G_SetSLKRows("AbilityData", rows);
    caster = alloc_test_unit(MAKEFOURCC('n', 'd', 'r', 's'), 0, 0);
    target = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), 64, 0);
    caster->data.UnitAbilities = &abilities; caster->s.player = 0;
    caster->mana.value = caster->mana.max_value = 500.0f;
    caster->svflags |= SVF_MONSTER;
    target->data.UnitData = &ground; target->data.UnitBalance = &creep;
    target->s.player = 1; target->svflags |= SVF_MONSTER; target->targtype = TARG_GROUND;
    T_ASSERT(S_CastUnitTargetSpell(caster, BZ_ACPY, target));
    T_EQ(G_UnitStatusLevel(target, BZ_BPLY), 1);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* ROC ACsl omits BuffID; fallback is BUsl, not Bslp. */
TEST(wc3_spell, creep_sleep_roc_empty_buffid_applies_busl) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X9\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\n"
        "C;Y1;X7;K\"Dur1\"\nC;Y1;X8;K\"HeroDur1\"\nC;Y1;X9;K\"DataA1\"\n"
        "C;Y2;X1;K\"ACsl\"\nC;Y2;X2;K\"AUsl\"\nC;Y2;X3;K\"air,ground,enemy,organic,neutral\"\n"
        "C;Y2;X4;K\"0\"\nC;Y2;X5;K\"0\"\nC;Y2;X6;K\"600\"\n"
        "C;Y2;X7;K\"20\"\nC;Y2;X8;K\"10\"\nC;Y2;X9;K\"2\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "ACsl" };
    slkTestData_t *rows = parse_slk_string(slk), *old;
    LPEDICT caster, target;

    creep_alias_world();
    old = G_SetSLKRows("AbilityData", rows);
    caster = alloc_test_unit(MAKEFOURCC('n', 'd', 'r', 's'), 0, 0);
    target = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), 64, 0);
    caster->data.UnitAbilities = &abilities; caster->s.player = 0;
    caster->mana.value = caster->mana.max_value = 200.0f;
    caster->svflags |= SVF_MONSTER;
    target->s.player = 1; target->svflags |= SVF_MONSTER; target->targtype = TARG_GROUND;
    T_ASSERT(S_CastUnitTargetSpell(caster, BZ_ACSL, target));
    T_EQ(G_UnitStatusLevel(target, BZ_BUSL), 1);
    T_EQ(G_UnitStatusLevel(target, BZ_BSLP), 0);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}
#endif
