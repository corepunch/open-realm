#ifdef BZ_TESTS
#include "test.h"
#include "../skills/s_skills.h"

#define BZ_ACYC MAKEFOURCC('A', 'c', 'y', 'c') // rawcode; stock Cyclone
#define BZ_ACCY MAKEFOURCC('A', 'C', 'c', 'y') // rawcode; creep Cyclone alias
#define BZ_SCC1 MAKEFOURCC('S', 'C', 'c', '1') // rawcode; Cenarius Cyclone alias
#define BZ_ACNY MAKEFOURCC('A', 'c', 'n', 'y') // rawcode; naga Cyclone alias
#define BZ_AICY MAKEFOURCC('A', 'I', 'c', 'y') // rawcode; item Cyclone alias
#define BZ_ADIS MAKEFOURCC('A', 'd', 'i', 's') // rawcode; Dispel Magic
#define BZ_APRG MAKEFOURCC('A', 'p', 'r', 'g') // rawcode; Purge
#define BZ_BCYC MAKEFOURCC('B', 'c', 'y', 'c') // rawcode; primary Cyclone buff
#define BZ_BCY2 MAKEFOURCC('B', 'c', 'y', '2') // rawcode; secondary Cyclone buff
#define BZ_BSLO MAKEFOURCC('B', 's', 'l', 'o') // rawcode; Slow probe buff
#define BZ_BPRG MAKEFOURCC('B', 'p', 'r', 'g') // rawcode; Purge slow buff

LPEDICT alloc_test_unit(DWORD class_id, FLOAT x, FLOAT y);
void reset_entities(void);
void setup_test_world(void);
slkTestData_t *parse_slk_string(const char *text);
void free_slk_rows(slkTestData_t *rows);

/* Non-stock Dur/HeroDur/Cost so tests cannot pass on hardcoded 20/6/150. */
#define CYC_SLK \
    "ID;PWXL;N;EBB;Y2;X11\n" \
    "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n" \
    "C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"Cost1\"\nC;Y1;X6;K\"Cool1\"\n" \
    "C;Y1;X7;K\"Rng1\"\nC;Y1;X8;K\"Dur1\"\nC;Y1;X9;K\"HeroDur1\"\n" \
    "C;Y1;X10;K\"BuffID1\"\nC;Y1;X11;K\"DataA1\"\n" \
    "C;Y2;X1;K\"Acyc\"\nC;Y2;X2;K\"Acyc\"\nC;Y2;X3;K\"1\"\n" \
    "C;Y2;X4;K\"ground,enemy,neutral,organic\"\nC;Y2;X5;K\"87\"\nC;Y2;X6;K\"0\"\n" \
    "C;Y2;X7;K\"600\"\nC;Y2;X8;K\"11\"\nC;Y2;X9;K\"4\"\n" \
    "C;Y2;X10;K\"Bcyc,Bcy2\"\nC;Y2;X11;K\"1\"\nE\n"

typedef struct {
    slkTestData_t *rows, *old;
    LPEDICT caster, ally, enemy, mech;
} CYCFIX;

static CYCFIX cyc_setup(LPCSTR slk, DWORD code) {
    CYCFIX fix;
    reset_entities(); setup_test_world(); level.time = 1000;
    ((LPMAPINFO)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((LPMAPINFO)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    memset(level.alliances, 0, sizeof(level.alliances));
    fix.rows = parse_slk_string(slk); fix.old = G_SetSLKRows("AbilityData", fix.rows);
    fix.caster = alloc_test_unit(MAKEFOURCC('e','d','o','t'), 0, 0);
    fix.ally = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 0);
    fix.enemy = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 96, 0);
    fix.mech = alloc_test_unit(MAKEFOURCC('h','m','t','t'), 128, 0);
    fix.caster->s.player = fix.ally->s.player = 0;
    fix.enemy->s.player = fix.mech->s.player = 1;
    fix.caster->svflags |= SVF_MONSTER; fix.ally->svflags |= SVF_MONSTER;
    fix.enemy->svflags |= SVF_MONSTER; fix.mech->svflags |= SVF_MONSTER;
    fix.caster->targtype = fix.ally->targtype = fix.enemy->targtype = TARG_GROUND;
    fix.mech->targtype = TARG_MECHANICAL;
    fix.caster->heroabilities[0] = MAKE(heroability_t, .code = code, .level = 1);
    fix.caster->mana.value = fix.caster->mana.max_value = 200;
    fix.enemy->health.value = fix.enemy->health.max_value = 500;
    fix.ally->health.value = fix.ally->health.max_value = 500;
    fix.mech->health.value = fix.mech->health.max_value = 500;
    fix.enemy->stand = unit_stand; unit_stand(fix.enemy);
    return fix;
}

static void cyc_done(CYCFIX fix) { G_SetSLKRows("AbilityData", fix.old); free_slk_rows(fix.rows); }

TEST(wc3_spell, cyclone_aliases_share_procedure) {
    T_EQ(S_AbilityItem(BZ_ACYC).ability->proc, CAbilityCyclone);
    T_EQ(S_AbilityItem(BZ_ACCY).ability->proc, CAbilityCyclone);
    T_EQ(S_AbilityItem(BZ_SCC1).ability->proc, CAbilityCyclone);
    T_EQ(S_AbilityItem(BZ_ACNY).ability->proc, CAbilityCyclone);
    T_EQ(S_AbilityItem(BZ_AICY).ability->proc, CAbilityCyclone);
}

/* Item AIcy reads its own Dur/DataA; status.data keeps AIcy for S_StatusIsUndispellable. */
TEST(wc3_spell, cyclone_item_aicy_applies_authored_duration_and_dispel) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y3;X12\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"Cost1\"\nC;Y1;X6;K\"Cool1\"\n"
        "C;Y1;X7;K\"Rng1\"\nC;Y1;X8;K\"Dur1\"\nC;Y1;X9;K\"HeroDur1\"\n"
        "C;Y1;X10;K\"BuffID1\"\nC;Y1;X11;K\"DataA1\"\nC;Y1;X12;K\"Area1\"\n"
        "C;Y2;X1;K\"AIcy\"\nC;Y2;X2;K\"Acyc\"\nC;Y2;X3;K\"1\"\n"
        "C;Y2;X4;K\"ground,enemy,neutral\"\nC;Y2;X5;K\"0\"\nC;Y2;X6;K\"0\"\n"
        "C;Y2;X7;K\"600\"\nC;Y2;X8;K\"13\"\nC;Y2;X9;K\"7\"\n"
        "C;Y2;X10;K\"Bcyc,Bcy2\"\nC;Y2;X11;K\"2\"\nC;Y2;X12;K\"0\"\n"
        "C;Y3;X1;K\"Adis\"\nC;Y3;X2;K\"Adis\"\nC;Y3;X3;K\"1\"\n"
        "C;Y3;X4;K\"air,ground,ward,invu,vuln\"\nC;Y3;X5;K\"61\"\nC;Y3;X6;K\"0\"\n"
        "C;Y3;X7;K\"500\"\nC;Y3;X8;K\"0\"\nC;Y3;X9;K\"0\"\n"
        "C;Y3;X10;K\"\"\nC;Y3;X11;K\"0\"\nC;Y3;X12;K\"250\"\nE\n";
    CYCFIX fix = cyc_setup(slk, BZ_AICY);
    LPEDICT priest = alloc_test_unit(MAKEFOURCC('h', 'p', 'r', 'i'), 32, 0);
    heroabilitystatus_t *slot = NULL;
    VECTOR2 point;

    T_EQ(S_AbilityItem(BZ_AICY).ability->proc, CAbilityCyclone);
    T_FEQ(S_SpellDuration(BZ_AICY, 1, false), 13, 0.001f);
    T_FEQ(S_SpellDuration(BZ_AICY, 1, true), 7, 0.001f);
    T_ASSERT(S_SpellAllowsTarget(BZ_AICY, fix.caster, fix.enemy));
    T_ASSERT(!S_SpellAllowsTarget(BZ_AICY, fix.caster, fix.ally));
    T_ASSERT(S_CastUnitTargetSpell(fix.caster, BZ_AICY, fix.enemy));
    T_ASSERT(S_UnitIsCycloned(fix.enemy));
    FOR_LOOP(i, MAX_UNIT_STATUSES)
        if (fix.enemy->abilstatus[i].level && fix.enemy->abilstatus[i].code == BZ_BCYC) {
            slot = fix.enemy->abilstatus + i; break;
        }
    T_NOT_NULL(slot);
    T_EQ(slot->data, BZ_AICY);
    T_ASSERT(!S_StatusIsUndispellable(slot));
    priest->s.player = 0; priest->svflags |= SVF_MONSTER; priest->targtype = TARG_GROUND;
    priest->heroabilities[0] = MAKE(heroability_t, .code = BZ_ADIS, .level = 1);
    priest->mana.value = priest->mana.max_value = 200;
    point = fix.enemy->s.origin2;
    T_ASSERT(S_CastPointTargetSpell(priest, BZ_ADIS, &point));
    T_ASSERT(!S_UnitIsCycloned(fix.enemy));
    cyc_done(fix);
}

/* TFT organic token rejects mechanical; organic enemy is accepted; allies are not. */
TEST(wc3_spell, cyclone_tft_organic_mechanical_and_ally_filters) {
    CYCFIX fix = cyc_setup(CYC_SLK, BZ_ACYC);
    T_ASSERT(S_SpellAllowsTarget(BZ_ACYC, fix.caster, fix.enemy));
    T_ASSERT(!S_SpellAllowsTarget(BZ_ACYC, fix.caster, fix.mech));
    T_ASSERT(!S_SpellAllowsTarget(BZ_ACYC, fix.caster, fix.ally));
    T_ASSERT(S_CastUnitTargetSpell(fix.caster, BZ_ACYC, fix.enemy));
    T_EQ(G_UnitStatusLevel(fix.enemy, BZ_BCYC), 1);
    T_EQ(G_UnitStatusLevel(fix.enemy, BZ_BCY2), 0);
    T_FEQ(fix.caster->mana.value, 113, 0.001f);
    T_ASSERT(!S_CastUnitTargetSpell(fix.caster, BZ_ACYC, fix.mech));
    T_ASSERT(!S_CastUnitTargetSpell(fix.caster, BZ_ACYC, fix.ally));
    cyc_done(fix);
}

/* Recast on an already-cycloned target fails without spending mana. */
TEST(wc3_spell, cyclone_rejects_already_cycloned_without_mana_spend) {
    CYCFIX fix = cyc_setup(CYC_SLK, BZ_ACYC);
    T_ASSERT(S_CastUnitTargetSpell(fix.caster, BZ_ACYC, fix.enemy));
    T_FEQ(fix.caster->mana.value, 113, 0.001f);
    T_ASSERT(S_UnitIsCycloned(fix.enemy));
    T_ASSERT(!S_CastUnitTargetSpell(fix.caster, BZ_ACYC, fix.enemy));
    T_FEQ(fix.caster->mana.value, 113, 0.001f);
    cyc_done(fix);
}

/* Cyclone locks move, attack both ways, spells on/from victim, and physical damage. */
TEST(wc3_spell, cyclone_locks_move_attack_spell_and_damage) {
    CYCFIX fix = cyc_setup(CYC_SLK, BZ_ACYC);
    LPEDICT wp;
    fix.enemy->heroabilities[0] = MAKE(heroability_t, .code = BZ_ACYC, .level = 1);
    fix.enemy->mana.value = fix.enemy->mana.max_value = 200;
    T_ASSERT(S_CastUnitTargetSpell(fix.caster, BZ_ACYC, fix.enemy));
    T_ASSERT(S_UnitIsCycloned(fix.enemy));

    wp = Waypoint_add(&(VECTOR2){200, 0});
    fix.enemy->goalentity = NULL;
    order_move(fix.enemy, wp);
    T_ASSERT(fix.enemy->goalentity != wp);

    T_ASSERT(!S_OrderAttack(fix.enemy, fix.caster));
    T_ASSERT(!S_OrderAttack(fix.caster, fix.enemy));
    T_ASSERT(!S_CastUnitTargetSpell(fix.caster, BZ_ACYC, fix.enemy));
    T_ASSERT(!S_CastUnitTargetSpell(fix.enemy, BZ_ACYC, fix.caster));

    fix.enemy->health.value = 500;
    S_ResolveAttackHit(fix.caster, fix.enemy, 50);
    T_FEQ(fix.enemy->health.value, 500, 0.001f);
    cyc_done(fix);
}

/* ROC AbilityData omits BuffID; still apply Bcyc from the documented fallback. */
TEST(wc3_spell, cyclone_roc_row_without_buffid_still_applies_bcyc) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X8\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"Cost1\"\nC;Y1;X6;K\"Rng1\"\n"
        "C;Y1;X7;K\"Dur1\"\nC;Y1;X8;K\"HeroDur1\"\n"
        "C;Y2;X1;K\"Acyc\"\nC;Y2;X2;K\"Acyc\"\nC;Y2;X3;K\"1\"\n"
        "C;Y2;X4;K\"ground,enemy,neutral\"\nC;Y2;X5;K\"87\"\nC;Y2;X6;K\"600\"\n"
        "C;Y2;X7;K\"11\"\nC;Y2;X8;K\"4\"\nE\n";
    CYCFIX fix = cyc_setup(slk, BZ_ACYC);
    T_ASSERT(S_CastUnitTargetSpell(fix.caster, BZ_ACYC, fix.enemy));
    T_EQ(G_UnitStatusLevel(fix.enemy, BZ_BCYC), 1);
    T_ASSERT(S_UnitIsCycloned(fix.enemy));
    cyc_done(fix);
}

/* Heroes use HeroDur; ordinary units use Dur. Expiry restores combat. */
TEST(wc3_spell, cyclone_hero_duration_and_expiry_restore) {
    static UnitBalance_t hero_bal = { .strength = 1 };
    CYCFIX fix = cyc_setup(CYC_SLK, BZ_ACYC);
    LPEDICT hero = alloc_test_unit(MAKEFOURCC('O','g','r','h'), 160, 0);
    LPEDICT wp;

    hero->s.player = 1; hero->svflags |= SVF_MONSTER; hero->targtype = TARG_GROUND;
    hero->data.UnitBalance = &hero_bal;
    hero->health.value = hero->health.max_value = 500;
    hero->stand = unit_stand; unit_stand(hero);
    T_ASSERT(G_UnitIsHero(hero));
    T_ASSERT(!G_UnitIsHero(fix.enemy));

    T_ASSERT(S_CastUnitTargetSpell(fix.caster, BZ_ACYC, hero));
    T_ASSERT(S_UnitIsCycloned(hero));
    level.time += 4000; unit_updatestatuses(hero);
    T_ASSERT(!S_UnitIsCycloned(hero));

    fix.caster->mana.value = 200;
    T_ASSERT(S_CastUnitTargetSpell(fix.caster, BZ_ACYC, fix.enemy));
    T_ASSERT(S_UnitIsCycloned(fix.enemy));
    level.time += 11000; unit_updatestatuses(fix.enemy);
    T_ASSERT(!S_UnitIsCycloned(fix.enemy));

    wp = Waypoint_add(&(VECTOR2){220, 0});
    order_move(fix.enemy, wp);
    T_ASSERT(fix.enemy->goalentity == wp);
    T_ASSERT(S_OrderAttack(fix.caster, fix.enemy));
    fix.enemy->health.value = 500;
    S_ResolveAttackHit(fix.caster, fix.enemy, 40);
    T_FEQ(fix.enemy->health.value, 460, 0.001f);

    fix.caster->mana.value = 200;
    T_ASSERT(S_CastUnitTargetSpell(fix.caster, BZ_ACYC, fix.enemy));
    T_ASSERT(S_UnitIsCycloned(fix.enemy));
    cyc_done(fix);
}

/* Non-stock DataA=2 (not retail 1) so dispel eligibility cannot pass on a hardcoded 1. */
#define CYC_DISPEL_SLK_DATAA(DATA_A) \
    "ID;PWXL;N;EBB;Y3;X12\n" \
    "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n" \
    "C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"Cost1\"\nC;Y1;X6;K\"Cool1\"\n" \
    "C;Y1;X7;K\"Rng1\"\nC;Y1;X8;K\"Dur1\"\nC;Y1;X9;K\"HeroDur1\"\n" \
    "C;Y1;X10;K\"BuffID1\"\nC;Y1;X11;K\"DataA1\"\nC;Y1;X12;K\"Area1\"\n" \
    "C;Y2;X1;K\"Acyc\"\nC;Y2;X2;K\"Acyc\"\nC;Y2;X3;K\"1\"\n" \
    "C;Y2;X4;K\"ground,enemy,neutral,organic\"\nC;Y2;X5;K\"87\"\nC;Y2;X6;K\"0\"\n" \
    "C;Y2;X7;K\"600\"\nC;Y2;X8;K\"11\"\nC;Y2;X9;K\"4\"\n" \
    "C;Y2;X10;K\"Bcyc,Bcy2\"\nC;Y2;X11;K\"" DATA_A "\"\nC;Y2;X12;K\"0\"\n" \
    "C;Y3;X1;K\"Adis\"\nC;Y3;X2;K\"Adis\"\nC;Y3;X3;K\"1\"\n" \
    "C;Y3;X4;K\"air,ground,ward,invu,vuln\"\nC;Y3;X5;K\"61\"\nC;Y3;X6;K\"0\"\n" \
    "C;Y3;X7;K\"500\"\nC;Y3;X8;K\"0\"\nC;Y3;X9;K\"0\"\n" \
    "C;Y3;X10;K\"\"\nC;Y3;X11;K\"0\"\nC;Y3;X12;K\"250\"\nE\n"

#define CYC_PURGE_SLK_DATAA(DATA_A) \
    "ID;PWXL;N;EBB;Y3;X12\n" \
    "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n" \
    "C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"Cost1\"\nC;Y1;X6;K\"Cool1\"\n" \
    "C;Y1;X7;K\"Rng1\"\nC;Y1;X8;K\"Dur1\"\nC;Y1;X9;K\"HeroDur1\"\n" \
    "C;Y1;X10;K\"BuffID1\"\nC;Y1;X11;K\"DataA1\"\nC;Y1;X12;K\"DataC1\"\n" \
    "C;Y2;X1;K\"Acyc\"\nC;Y2;X2;K\"Acyc\"\nC;Y2;X3;K\"1\"\n" \
    "C;Y2;X4;K\"ground,enemy,neutral,organic\"\nC;Y2;X5;K\"87\"\nC;Y2;X6;K\"0\"\n" \
    "C;Y2;X7;K\"600\"\nC;Y2;X8;K\"11\"\nC;Y2;X9;K\"4\"\n" \
    "C;Y2;X10;K\"Bcyc,Bcy2\"\nC;Y2;X11;K\"" DATA_A "\"\nC;Y2;X12;K\"0\"\n" \
    "C;Y3;X1;K\"Aprg\"\nC;Y3;X2;K\"Aprg\"\nC;Y3;X3;K\"1\"\n" \
    "C;Y3;X4;K\"air,ground,ward,vuln,invu\"\nC;Y3;X5;K\"75\"\nC;Y3;X6;K\"0\"\n" \
    "C;Y3;X7;K\"700\"\nC;Y3;X8;K\"12\"\nC;Y3;X9;K\"4\"\n" \
    "C;Y3;X10;K\"Bprg\"\nC;Y3;X11;K\"0.5\"\nC;Y3;X12;K\"180\"\nE\n"

/* DataA != 0: Adis removes Cyclone; Slow probe also clears; mana is spent. */
TEST(wc3_spell, cyclone_dataa_nonzero_removed_by_adis) {
    CYCFIX fix = cyc_setup(CYC_DISPEL_SLK_DATAA("2"), BZ_ACYC);
    LPEDICT priest = alloc_test_unit(MAKEFOURCC('h', 'p', 'r', 'i'), 32, 0);
    VECTOR2 point;

    priest->s.player = 0; priest->svflags |= SVF_MONSTER; priest->targtype = TARG_GROUND;
    priest->heroabilities[0] = MAKE(heroability_t, .code = BZ_ADIS, .level = 1);
    priest->mana.value = priest->mana.max_value = 200;
    T_ASSERT(S_CastUnitTargetSpell(fix.caster, BZ_ACYC, fix.enemy));
    T_ASSERT(S_UnitIsCycloned(fix.enemy));
    unit_addtimedstatus(fix.enemy, "Bslo", 1, 30.0f);
    T_ASSERT(S_UnitHasStatus(fix.enemy, BZ_BSLO));
    point = fix.enemy->s.origin2;
    T_ASSERT(S_CastPointTargetSpell(priest, BZ_ADIS, &point));
    T_FEQ(priest->mana.value, 139, 0.001f); /* 200 - 61 */
    T_ASSERT(!S_UnitIsCycloned(fix.enemy));
    T_ASSERT(!S_UnitHasStatus(fix.enemy, BZ_BSLO));
    cyc_done(fix);
}

/* DataA == 0 (ROC): Adis leaves Cyclone, still clears other timed buffs, still spends mana. */
TEST(wc3_spell, cyclone_dataa_zero_survives_adis) {
    CYCFIX fix = cyc_setup(CYC_DISPEL_SLK_DATAA("0"), BZ_ACYC);
    LPEDICT priest = alloc_test_unit(MAKEFOURCC('h', 'p', 'r', 'i'), 32, 0);
    VECTOR2 point;

    priest->s.player = 0; priest->svflags |= SVF_MONSTER; priest->targtype = TARG_GROUND;
    priest->heroabilities[0] = MAKE(heroability_t, .code = BZ_ADIS, .level = 1);
    priest->mana.value = priest->mana.max_value = 200;
    T_ASSERT(S_CastUnitTargetSpell(fix.caster, BZ_ACYC, fix.enemy));
    T_ASSERT(S_UnitIsCycloned(fix.enemy));
    unit_addtimedstatus(fix.enemy, "Bslo", 1, 30.0f);
    point = fix.enemy->s.origin2;
    T_ASSERT(S_CastPointTargetSpell(priest, BZ_ADIS, &point));
    T_FEQ(priest->mana.value, 139, 0.001f);
    T_ASSERT(S_UnitIsCycloned(fix.enemy));
    T_ASSERT(!S_UnitHasStatus(fix.enemy, BZ_BSLO));
    cyc_done(fix);
}

/* Purge shares the undispellable skip. Unit-target S_Cast cannot select cycloned units, so drive A_EXECUTE. */
TEST(wc3_spell, cyclone_dataa_zero_survives_purge) {
    CYCFIX fix = cyc_setup(CYC_PURGE_SLK_DATAA("0"), BZ_ACYC);
    LPEDICT shaman = alloc_test_unit(MAKEFOURCC('o', 's', 'h', 'm'), 32, 0);
    abilityitem_t item;
    spellTarget_t st;
    abilityCall_t call;

    shaman->s.player = 0; shaman->svflags |= SVF_MONSTER; shaman->targtype = TARG_GROUND;
    shaman->heroabilities[0] = MAKE(heroability_t, .code = BZ_APRG, .level = 1);
    shaman->mana.value = shaman->mana.max_value = 200;
    T_ASSERT(S_CastUnitTargetSpell(fix.caster, BZ_ACYC, fix.enemy));
    unit_addtimedstatus(fix.enemy, "Bslo", 1, 30.0f);
    item = S_AbilityItem(BZ_APRG);
    st = MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = fix.enemy);
    call = MAKE(abilityCall_t, .item = &item, .target = &st);
    T_ASSERT(S_AbilityMessage(shaman, A_EXECUTE, &call));
    T_ASSERT(S_UnitIsCycloned(fix.enemy));
    T_ASSERT(!S_UnitHasStatus(fix.enemy, BZ_BSLO));
    T_EQ(G_UnitStatusLevel(fix.enemy, BZ_BPRG), 1);
    cyc_done(fix);
}

TEST(wc3_spell, cyclone_dataa_nonzero_removed_by_purge) {
    CYCFIX fix = cyc_setup(CYC_PURGE_SLK_DATAA("2"), BZ_ACYC);
    LPEDICT shaman = alloc_test_unit(MAKEFOURCC('o', 's', 'h', 'm'), 32, 0);
    abilityitem_t item;
    spellTarget_t st;
    abilityCall_t call;

    shaman->s.player = 0; shaman->svflags |= SVF_MONSTER; shaman->targtype = TARG_GROUND;
    shaman->heroabilities[0] = MAKE(heroability_t, .code = BZ_APRG, .level = 1);
    T_ASSERT(S_CastUnitTargetSpell(fix.caster, BZ_ACYC, fix.enemy));
    item = S_AbilityItem(BZ_APRG);
    st = MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = fix.enemy);
    call = MAKE(abilityCall_t, .item = &item, .target = &st);
    T_ASSERT(S_AbilityMessage(shaman, A_EXECUTE, &call));
    T_ASSERT(!S_UnitIsCycloned(fix.enemy));
    T_EQ(G_UnitStatusLevel(fix.enemy, BZ_BPRG), 1);
    cyc_done(fix);
}

#endif
