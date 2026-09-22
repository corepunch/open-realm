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
#define BZ_ANHE MAKEFOURCC('A', 'n', 'h', 'e')
#define BZ_ANH1 MAKEFOURCC('A', 'n', 'h', '1')
#define BZ_ANH2 MAKEFOURCC('A', 'n', 'h', '2')
#define BZ_ACTC MAKEFOURCC('A', 'C', 't', 'c')
#define BZ_ACT2 MAKEFOURCC('A', 'C', 't', '2')
#define BZ_ACAD MAKEFOURCC('A', 'C', 'a', 'd')
#define BZ_ACRN MAKEFOURCC('A', 'C', 'r', 'n')
#define BZ_AASL MAKEFOURCC('A', 'a', 's', 'l')
#define BZ_AAKB MAKEFOURCC('A', 'a', 'k', 'b')
#define BZ_BTLF MAKEFOURCC('B', 'T', 'L', 'F')
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

/* Anhe/ACtc are the code= parents of the already-registered Anh1/Anh2/ACt2
 * rows; without these rows S_AbilityItem resolves to NULL through G_AbilityCode. */
TEST(wc3_spell, creep_code_parents_share_hero_procedures) {
    T_EQ(S_AbilityItem(BZ_ANHE).ability->proc, CAbilityHeal);
    T_EQ(S_AbilityItem(BZ_ANH1).ability->proc, CAbilityHeal);
    T_EQ(S_AbilityItem(BZ_ANH2).ability->proc, CAbilityHeal);
    T_EQ(S_AbilityItem(BZ_ACTC).ability->proc, CAbilityThunderClap);
    T_EQ(S_AbilityItem(BZ_ACT2).ability->proc, CAbilityThunderClap);
    T_EQ(S_AbilityItem(BZ_ACAD).ability->proc, CAbilityAnimateDead);
    T_EQ(S_AbilityItem(BZ_ACRN).ability->proc, CAbilityPassive);
    T_EQ(S_AbilityItem(BZ_AASL).ability->proc, CAbilityPassive);
    T_EQ(S_AbilityItem(BZ_AAKB).ability->proc, CAbilityPassive);
}

/* Anhe DataA=37 (not Ahea's 25), ACtc DataA=77 (not AHtc's 70) and
 * ACad DataA=2 (not AUan's 6) prove each alias reads its own row. */
TEST(wc3_spell, creep_heal_slam_animate_dead_use_alias_data) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y7;X9\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\n"
        "C;Y1;X7;K\"Area1\"\nC;Y1;X8;K\"Dur1\"\nC;Y1;X9;K\"DataA1\"\n"
        "C;Y2;X1;K\"Ahea\"\nC;Y2;X2;K\"Ahea\"\nC;Y2;X3;K\"air,ground,friend\"\n"
        "C;Y2;X4;K\"0\"\nC;Y2;X5;K\"0\"\nC;Y2;X6;K\"500\"\n"
        "C;Y2;X7;K\"0\"\nC;Y2;X8;K\"0\"\nC;Y2;X9;K\"25\"\n"
        "C;Y3;X1;K\"Anhe\"\nC;Y3;X2;K\"Anhe\"\nC;Y3;X3;K\"air,ground,friend\"\n"
        "C;Y3;X4;K\"0\"\nC;Y3;X5;K\"0\"\nC;Y3;X6;K\"500\"\n"
        "C;Y3;X7;K\"0\"\nC;Y3;X8;K\"0\"\nC;Y3;X9;K\"37\"\n"
        "C;Y4;X1;K\"AHtc\"\nC;Y4;X2;K\"AHtc\"\nC;Y4;X3;K\"ground,neutral\"\n"
        "C;Y4;X4;K\"0\"\nC;Y4;X5;K\"0\"\nC;Y4;X6;K\"0\"\n"
        "C;Y4;X7;K\"250\"\nC;Y4;X8;K\"4\"\nC;Y4;X9;K\"70\"\n"
        "C;Y5;X1;K\"ACtc\"\nC;Y5;X2;K\"ACtc\"\nC;Y5;X3;K\"ground,neutral\"\n"
        "C;Y5;X4;K\"0\"\nC;Y5;X5;K\"0\"\nC;Y5;X6;K\"0\"\n"
        "C;Y5;X7;K\"900\"\nC;Y5;X8;K\"4\"\nC;Y5;X9;K\"77\"\n"
        "C;Y6;X1;K\"AUan\"\nC;Y6;X2;K\"AUan\"\nC;Y6;X3;K\"air,ground,dead\"\n"
        "C;Y6;X4;K\"0\"\nC;Y6;X5;K\"0\"\nC;Y6;X6;K\"400\"\n"
        "C;Y6;X7;K\"900\"\nC;Y6;X8;K\"120\"\nC;Y6;X9;K\"6\"\n"
        "C;Y7;X1;K\"ACad\"\nC;Y7;X2;K\"ACad\"\nC;Y7;X3;K\"air,ground,dead\"\n"
        "C;Y7;X4;K\"0\"\nC;Y7;X5;K\"0\"\nC;Y7;X6;K\"400\"\n"
        "C;Y7;X7;K\"900\"\nC;Y7;X8;K\"120\"\nC;Y7;X9;K\"2\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "Anhe,ACtc,ACad" };
    UnitData_t corpse_data = { .deathType = 3 };
    slkTestData_t *rows = parse_slk_string(slk), *old;
    LPEDICT caster, ally, enemy, corpse;

    creep_alias_world();
    old = G_SetSLKRows("AbilityData", rows);
    caster = alloc_test_unit(MAKEFOURCC('n', 'd', 'r', 's'), 0, 0);
    ally = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), 64, 0);
    enemy = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), 0, 64);
    corpse = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), 128, 0);
    caster->data.UnitAbilities = &abilities; caster->s.player = 0;
    caster->mana.value = caster->mana.max_value = 500.0f;
    caster->svflags |= SVF_MONSTER;
    ally->s.player = 0; ally->svflags |= SVF_MONSTER; ally->targtype = TARG_GROUND;
    ally->health.value = 50.0f; ally->health.max_value = 100.0f;
    enemy->s.player = 1; enemy->svflags |= SVF_MONSTER; enemy->targtype = TARG_GROUND;
    enemy->health.value = enemy->health.max_value = 500.0f;
    corpse->data.UnitData = &corpse_data;
    corpse->health.value = 0.0f; corpse->health.max_value = 100.0f;
    corpse->svflags |= SVF_MONSTER | SVF_DEADMONSTER;
    T_ASSERT(S_CastUnitTargetSpell(caster, BZ_ANHE, ally));
    T_EQ((int)ally->health.value, 87);
    T_ASSERT(S_CastNoTargetSpell(caster, BZ_ACTC));
    T_EQ((int)enemy->health.value, 423);
    T_ASSERT(S_CastNoTargetSpell(caster, BZ_ACAD));
    T_ASSERT(!M_IsDead(corpse));
    T_EQ(corpse->s.player, 0);
    T_EQ(G_UnitStatusLevel(corpse, BZ_BTLF), 1);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}
#endif

#ifdef BZ_TESTS
/* Native ACes at authored 100% avoids the hit; base AEev is 0 so only the
 * alias row can produce the evade (repro from #476). */
TEST(wc3_spell, creep_evasion_alias_full_chance_avoids_hit) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y3;X3\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\n"
        "C;Y2;X1;K\"AEev\"\nC;Y2;X2;K\"AEev\"\nC;Y2;X3;K\"0\"\n"
        "C;Y3;X1;K\"ACes\"\nC;Y3;X2;K\"AEev\"\nC;Y3;X3;K\"1\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "ACes" };
    slkTestData_t *rows = parse_slk_string(slk), *old;
    LPEDICT attacker, target;
    creep_alias_world(); old = G_SetSLKRows("AbilityData", rows);
    attacker = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);
    attacker->s.player = 0; target->s.player = 1;
    target->data.UnitAbilities = &abilities;
    target->health.value = target->health.max_value = 100;
    T_EQ(G_UnitAbilityLevel(target, FS_SLKKey("ACes")), 1);
    T_FEQ(S_SpellData(FS_SLKKey("ACes"), 1, 1), 1, 0.001f);
    S_ResolveAttackHit(attacker, target, 10);
    T_FEQ(target->health.value, 100, 0.001f);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* A zero-chance alias takes the hit; the resolver must not evade by default. */
TEST(wc3_spell, creep_evasion_zero_chance_alias_takes_hit) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y3;X3\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\n"
        "C;Y2;X1;K\"AEev\"\nC;Y2;X2;K\"AEev\"\nC;Y2;X3;K\"0\"\n"
        "C;Y3;X1;K\"ACev\"\nC;Y3;X2;K\"AEev\"\nC;Y3;X3;K\"0\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "ACev" };
    slkTestData_t *rows = parse_slk_string(slk), *old;
    LPEDICT attacker, target;
    creep_alias_world(); old = G_SetSLKRows("AbilityData", rows);
    attacker = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);
    attacker->s.player = 0; target->s.player = 1;
    attacker->attack1.damageBase = 10;
    target->data.UnitAbilities = &abilities;
    target->health.value = target->health.max_value = 100;
    S_ResolveAttackHit(attacker, target, 10);
    T_ASSERT(target->health.value < 100);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Runtime-added aliases work; removal stops the effect. */
TEST(wc3_spell, creep_evasion_runtime_added_and_removed) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y3;X3\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\n"
        "C;Y2;X1;K\"AEev\"\nC;Y2;X2;K\"AEev\"\nC;Y2;X3;K\"0\"\n"
        "C;Y3;X1;K\"ACes\"\nC;Y3;X2;K\"AEev\"\nC;Y3;X3;K\"1\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "" };
    slkTestData_t *rows = parse_slk_string(slk), *old;
    LPEDICT attacker, target;
    creep_alias_world(); old = G_SetSLKRows("AbilityData", rows);
    attacker = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);
    attacker->s.player = 0; target->s.player = 1;
    attacker->attack1.damageBase = 10;
    target->data.UnitAbilities = &abilities;
    target->health.value = target->health.max_value = 100;
    target->abilities.added[0] = FS_SLKKey("ACes");
    ARRAY_COUNT(target->abilities.added) = 1;
    S_ResolveAttackHit(attacker, target, 10);
    T_FEQ(target->health.value, 100, 0.001f);
    target->abilities.removed[0] = FS_SLKKey("ACes");
    ARRAY_COUNT(target->abilities.removed) = 1;
    S_ResolveAttackHit(attacker, target, 10);
    T_ASSERT(target->health.value < 100);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* A ranked alias uses its actual rank and authored row: rank 1 (0%) takes
 * the hit while rank 2 (100%) avoids it. */
TEST(wc3_spell, creep_evasion_ranked_alias_uses_authored_row) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X5\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"DataA1\"\nC;Y1;X5;K\"DataA2\"\n"
        "C;Y2;X1;K\"ACes\"\nC;Y2;X2;K\"AEev\"\nC;Y2;X3;K\"2\"\n"
        "C;Y2;X4;K\"0\"\nC;Y2;X5;K\"1\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old;
    LPEDICT attacker, target;
    creep_alias_world(); old = G_SetSLKRows("AbilityData", rows);
    attacker = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);
    attacker->s.player = 0; target->s.player = 1;
    attacker->attack1.damageBase = 10;
    target->health.value = target->health.max_value = 100;
    target->heroabilities[0] = MAKE(heroability_t, .code = FS_SLKKey("ACes"), .level = 1);
    S_ResolveAttackHit(attacker, target, 10);
    T_ASSERT(target->health.value < 100);
    target->health.value = target->health.max_value = 100;
    target->heroabilities[0].level = 2;
    S_ResolveAttackHit(attacker, target, 10);
    T_FEQ(target->health.value, 100, 0.001f);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Base AEev and the Drunken Brawler path keep their existing behavior. */
TEST(wc3_spell, creep_evasion_base_and_brawler_retained) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y4;X3\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\n"
        "C;Y2;X1;K\"AEev\"\nC;Y2;X2;K\"AEev\"\nC;Y2;X3;K\"1\"\n"
        "C;Y3;X1;K\"ACes\"\nC;Y3;X2;K\"AEev\"\nC;Y3;X3;K\"0\"\n"
        "C;Y4;X1;K\"ANdb\"\nC;Y4;X2;K\"ANdb\"\nC;Y4;X3;K\"0\"\nE\n";
    UnitAbilities_t base_abils = { .abilList = "AEev" };
    UnitAbilities_t alias_abils = { .abilList = "ACes" };
    slkTestData_t *rows = parse_slk_string(slk), *old;
    LPEDICT attacker, base_target, alias_target;
    creep_alias_world(); old = G_SetSLKRows("AbilityData", rows);
    attacker = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    base_target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);
    alias_target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);
    attacker->s.player = 0; base_target->s.player = alias_target->s.player = 1;
    base_target->data.UnitAbilities = &base_abils;
    alias_target->data.UnitAbilities = &alias_abils;
    base_target->health.value = base_target->health.max_value = 100;
    alias_target->health.value = alias_target->health.max_value = 100;
    attacker->attack1.damageBase = 10;
    S_ResolveAttackHit(attacker, base_target, 10);
    T_FEQ(base_target->health.value, 100, 0.001f);
    S_ResolveAttackHit(attacker, alias_target, 10);
    T_ASSERT(alias_target->health.value < 100);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Creep Bash resolves through the owned alias: 100% ACbh stuns and adds its
 * authored bonus, while a 0% base AHbh does nothing. */
TEST(wc3_spell, creep_bash_alias_uses_authored_data) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y3;X5\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\n"
        "C;Y1;X4;K\"DataC1\"\nC;Y1;X5;K\"Dur1\"\n"
        "C;Y2;X1;K\"AHbh\"\nC;Y2;X2;K\"AHbh\"\nC;Y2;X3;K\"0\"\n"
        "C;Y2;X4;K\"0\"\nC;Y2;X5;K\"1\"\n"
        "C;Y3;X1;K\"ACbh\"\nC;Y3;X2;K\"AHbh\"\nC;Y3;X3;K\"100\"\n"
        "C;Y3;X4;K\"5\"\nC;Y3;X5;K\"2\"\nE\n";
    UnitAbilities_t base_abils = { .abilList = "AHbh" };
    UnitAbilities_t alias_abils = { .abilList = "ACbh" };
    slkTestData_t *rows = parse_slk_string(slk), *old;
    LPEDICT base_attacker, alias_attacker, target;
    creep_alias_world(); old = G_SetSLKRows("AbilityData", rows);
    base_attacker = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    alias_attacker = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 64);
    target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);
    base_attacker->s.player = alias_attacker->s.player = 0; target->s.player = 1;
    base_attacker->data.UnitAbilities = &base_abils;
    alias_attacker->data.UnitAbilities = &alias_abils;
    target->health.value = target->health.max_value = 500;
    S_ResolveAttackHit(base_attacker, target, 10);
    T_EQ(G_UnitStatusLevel(target, MAKEFOURCC('B','s','t','u')), 0);
    S_ResolveAttackHit(alias_attacker, target, 10);
    T_EQ(G_UnitStatusLevel(target, MAKEFOURCC('B','s','t','u')), 1);
    T_ASSERT(target->health.value < 500);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}
#endif
