#ifdef BZ_TESTS
#include "test.h"
#include "../skills/s_skills.h"

#define BZ_ANSO MAKEFOURCC('A', 'N', 's', 'o') // rawcode; Soul Burn
#define BZ_BNSO MAKEFOURCC('B', 'N', 's', 'o') // rawcode; Soul Burn silence/drain buff
#define BZ_BNSI MAKEFOURCC('B', 'N', 's', 'i') // rawcode; Silence ability buff
#define BZ_AHTB MAKEFOURCC('A', 'H', 't', 'b') // rawcode; Storm Bolt unit-target probe
#define BZ_ATAU MAKEFOURCC('A', 't', 'a', 'u') // rawcode; Taunt no-target probe

LPEDICT alloc_test_unit(DWORD class_id, FLOAT x, FLOAT y);
void reset_entities(void);
void setup_test_world(void);
slkTestData_t *parse_slk_string(const char *text);
void free_slk_rows(slkTestData_t *rows);

/* Non-stock Cost/Dur/DataA/DataC so tests cannot pass on hardcoded retail values. */
#define NSO_SLK \
    "ID;PWXL;N;EBB;Y4;X12\n" \
    "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n" \
    "C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"Cost1\"\nC;Y1;X6;K\"Cool1\"\n" \
    "C;Y1;X7;K\"Rng1\"\nC;Y1;X8;K\"Dur1\"\nC;Y1;X9;K\"HeroDur1\"\n" \
    "C;Y1;X10;K\"BuffID1\"\nC;Y1;X11;K\"DataA1\"\nC;Y1;X12;K\"DataC1\"\n" \
    "C;Y2;X1;K\"ANso\"\nC;Y2;X2;K\"ANso\"\nC;Y2;X3;K\"1\"\n" \
    "C;Y2;X4;K\"air,ground,enemy,neutral,organic\"\nC;Y2;X5;K\"40\"\nC;Y2;X6;K\"0\"\n" \
    "C;Y2;X7;K\"700\"\nC;Y2;X8;K\"9\"\nC;Y2;X9;K\"9\"\n" \
    "C;Y2;X10;K\"BNso\"\nC;Y2;X11;K\"4.5\"\nC;Y2;X12;K\"0.35\"\n" \
    "C;Y3;X1;K\"AHtb\"\nC;Y3;X2;K\"AHtb\"\nC;Y3;X3;K\"1\"\n" \
    "C;Y3;X4;K\"air,ground,enemy\"\nC;Y3;X5;K\"50\"\nC;Y3;X6;K\"0\"\n" \
    "C;Y3;X7;K\"600\"\nC;Y3;X8;K\"5\"\nC;Y3;X9;K\"3\"\n" \
    "C;Y3;X10;K\"BHtb\"\nC;Y3;X11;K\"100\"\nC;Y3;X12;K\"0\"\n" \
    "C;Y4;X1;K\"Atau\"\nC;Y4;X2;K\"Atau\"\nC;Y4;X3;K\"1\"\n" \
    "C;Y4;X4;K\"ground,enemy\"\nC;Y4;X5;K\"25\"\nC;Y4;X6;K\"0\"\n" \
    "C;Y4;X7;K\"0\"\nC;Y4;X8;K\"0\"\nC;Y4;X9;K\"0\"\n" \
    "C;Y4;X10;K\"\"\nC;Y4;X11;K\"0\"\nC;Y4;X12;K\"0\"\nE\n"

typedef struct {
    slkTestData_t *rows, *old;
    LPEDICT caster, enemy, ally;
} NSOFIX;

static NSOFIX nso_setup(void) {
    NSOFIX fix;
    reset_entities(); setup_test_world(); level.time = 1000;
    ((LPMAPINFO)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((LPMAPINFO)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    memset(level.alliances, 0, sizeof(level.alliances));
    fix.rows = parse_slk_string(NSO_SLK); fix.old = G_SetSLKRows("AbilityData", fix.rows);
    fix.caster = alloc_test_unit(MAKEFOURCC('N', 'f', 'i', 'r'), 0, 0);
    fix.enemy = alloc_test_unit(MAKEFOURCC('o', 'g', 'r', 'u'), 96, 0);
    fix.ally = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), 64, 0);
    fix.caster->s.player = fix.ally->s.player = 0; fix.enemy->s.player = 1;
    fix.caster->svflags |= SVF_MONSTER; fix.enemy->svflags |= SVF_MONSTER;
    fix.ally->svflags |= SVF_MONSTER;
    fix.caster->targtype = fix.enemy->targtype = fix.ally->targtype = TARG_GROUND;
    fix.caster->heroabilities[0] = MAKE(heroability_t, .code = BZ_ANSO, .level = 1);
    fix.caster->mana.value = fix.caster->mana.max_value = 200;
    fix.enemy->health.value = fix.enemy->health.max_value = 500;
    fix.enemy->mana.value = fix.enemy->mana.max_value = 200;
    fix.enemy->heroabilities[0] = MAKE(heroability_t, .code = BZ_AHTB, .level = 1);
    fix.enemy->heroabilities[1] = MAKE(heroability_t, .code = BZ_ATAU, .level = 1);
    return fix;
}

static void nso_done(NSOFIX fix) { G_SetSLKRows("AbilityData", fix.old); free_slk_rows(fix.rows); }

TEST(wc3_spell, soul_burn_procedure_lookup) {
    T_EQ(S_AbilityItem(BZ_ANSO).ability->proc, CAbilitySoulBurn);
}

TEST(wc3_spell, soul_burn_applies_authored_drain_and_reduction) {
    NSOFIX fix = nso_setup();
    T_ASSERT(S_SpellAllowsTarget(BZ_ANSO, fix.caster, fix.enemy));
    T_ASSERT(!S_SpellAllowsTarget(BZ_ANSO, fix.caster, fix.ally));
    T_ASSERT(S_CastUnitTargetSpell(fix.caster, BZ_ANSO, fix.enemy));
    T_FEQ(fix.caster->mana.value, 160, 0.001f);
    T_EQ(G_UnitStatusLevel(fix.enemy, BZ_BNSO), 1);
    T_ASSERT(S_UnitIsSilenced(fix.enemy));
    T_FEQ(S_SoulBurnDamageRate(fix.enemy), 4.5f, 0.001f);
    T_FEQ(S_SoulBurnDamageReduction(fix.enemy), 0.35f, 0.001f);
    T_ASSERT(!S_CastUnitTargetSpell(fix.caster, BZ_ANSO, fix.ally));
    T_FEQ(fix.caster->mana.value, 160, 0.001f);
    nso_done(fix);
}

/* BNso blocks unit-target and no-target casts without spending the victim's mana. */
TEST(wc3_spell, soul_burn_bnso_blocks_casts_without_mana_spend) {
    NSOFIX fix = nso_setup();
    T_ASSERT(S_CastUnitTargetSpell(fix.caster, BZ_ANSO, fix.enemy));
    T_ASSERT(S_UnitIsSilenced(fix.enemy));
    T_ASSERT(!S_CastUnitTargetSpell(fix.enemy, BZ_AHTB, fix.caster));
    T_FEQ(fix.enemy->mana.value, 200, 0.001f);
    T_ASSERT(!S_CastNoTargetSpell(fix.enemy, BZ_ATAU));
    T_FEQ(fix.enemy->mana.value, 200, 0.001f);
    nso_done(fix);
}

/* Existing Silence buff BNsi must keep rejecting casts through the shared helper. */
TEST(wc3_spell, soul_burn_bnsi_still_silences) {
    NSOFIX fix = nso_setup();
    unit_addtimedstatus(fix.enemy, "BNsi", 1, 10);
    T_EQ(G_UnitStatusLevel(fix.enemy, BZ_BNSI), 1);
    T_ASSERT(S_UnitIsSilenced(fix.enemy));
    T_ASSERT(!S_UnitHasStatus(fix.enemy, BZ_BNSO));
    T_ASSERT(!S_CastUnitTargetSpell(fix.enemy, BZ_AHTB, fix.caster));
    T_FEQ(fix.enemy->mana.value, 200, 0.001f);
    T_ASSERT(!S_CastNoTargetSpell(fix.enemy, BZ_ATAU));
    T_FEQ(fix.enemy->mana.value, 200, 0.001f);
    nso_done(fix);
}

/* Expiry clears silence; casting works again; recast re-applies BNso. */
TEST(wc3_spell, soul_burn_expiry_restores_casting_and_recast) {
    NSOFIX fix = nso_setup();
    T_ASSERT(S_CastUnitTargetSpell(fix.caster, BZ_ANSO, fix.enemy));
    T_ASSERT(S_UnitIsSilenced(fix.enemy));
    level.time += 9000; unit_updatestatuses(fix.enemy);
    T_ASSERT(!S_UnitIsSilenced(fix.enemy));
    T_EQ(G_UnitStatusLevel(fix.enemy, BZ_BNSO), 0);
    T_FEQ(S_SoulBurnDamageRate(fix.enemy), 0.0f, 0.001f);
    T_ASSERT(S_CastUnitTargetSpell(fix.enemy, BZ_AHTB, fix.caster));
    T_FEQ(fix.enemy->mana.value, 150, 0.001f);

    fix.caster->mana.value = 200;
    T_ASSERT(S_CastUnitTargetSpell(fix.caster, BZ_ANSO, fix.enemy));
    T_ASSERT(S_UnitIsSilenced(fix.enemy));
    T_ASSERT(!S_CastNoTargetSpell(fix.enemy, BZ_ATAU));
    nso_done(fix);
}

#endif
