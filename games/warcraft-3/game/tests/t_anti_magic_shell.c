#ifdef BZ_TESTS
#include "test.h"
#include "../skills/s_skills.h"

#define BZ_ANTI_MAGIC_SHELL MAKEFOURCC('A', 'a', 'm', 's') // rawcode; stock duration-based Anti-Magic Shell
#define BZ_ANTI_MAGIC_SHELL_BUFF MAKEFOURCC('B', 'a', 'm', 's') // rawcode; recipient immunity buff

LPEDICT alloc_test_unit(DWORD class_id, FLOAT x, FLOAT y);
void reset_entities(void);
void setup_test_world(void);
slkTestData_t *parse_slk_string(const char *text);
void free_slk_rows(slkTestData_t *rows);

/* The active ROC/TFT rows both describe a timed spell barrier, not a damage pool. */
TEST(wc3_anti_magic_shell, friendly_target_blocks_spells_until_expiry_and_recasts) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X10\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"Cost1\"\nC;Y1;X6;K\"Cool1\"\n"
        "C;Y1;X7;K\"Rng1\"\nC;Y1;X8;K\"Dur1\"\nC;Y1;X9;K\"HeroDur1\"\nC;Y1;X10;K\"BuffID1\"\n"
        "C;Y2;X1;K\"Aams\"\nC;Y2;X2;K\"Aams\"\nC;Y2;X3;K\"1\"\n"
        "C;Y2;X4;K\"air,ground\"\nC;Y2;X5;K\"75\"\nC;Y2;X6;K\"0\"\n"
        "C;Y2;X7;K\"500\"\nC;Y2;X8;K\"90\"\nC;Y2;X9;K\"90\"\nC;Y2;X10;K\"Bams,Bam2\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    LPEDICT caster, ally, enemy;

    reset_entities(); setup_test_world(); level.time = 1000;
    caster = alloc_test_unit(MAKEFOURCC('u','n','e','c'), 0, 0);
    ally = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 0);
    enemy = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 96, 0);
    caster->s.player = ally->s.player = 0; enemy->s.player = 1;
    caster->svflags |= SVF_MONSTER; ally->svflags |= SVF_MONSTER; enemy->svflags |= SVF_MONSTER;
    caster->targtype = ally->targtype = enemy->targtype = TARG_GROUND;
    caster->heroabilities[0] = MAKE(heroability_t, .code = BZ_ANTI_MAGIC_SHELL, .level = 1);
    caster->mana.value = caster->mana.max_value = 100;
    ally->health.value = ally->health.max_value = 500;

    T_EQ(S_AbilityItem(BZ_ANTI_MAGIC_SHELL).ability->proc, CAbilityAntiMagicShell);
    T_ASSERT(S_SpellAllowsTarget(BZ_ANTI_MAGIC_SHELL, caster, enemy));
    T_ASSERT(S_CastUnitTargetSpell(caster, BZ_ANTI_MAGIC_SHELL, ally));
    T_FEQ(caster->mana.value, 25, 0.001f);
    T_EQ(G_UnitStatusLevel(ally, BZ_ANTI_MAGIC_SHELL_BUFF), 1);
    T_ASSERT(S_UnitSpellImmune(ally));
    T_ASSERT(!S_SpellAllowsTarget(MAKEFOURCC('A','H','t','b'), enemy, ally));
    T_ASSERT(!S_SpellDamage(ally, enemy, 100)); T_FEQ(ally->health.value, 500, 0.001f);
    S_ResolveAttackHit(enemy, ally, 50); T_FEQ(ally->health.value, 450, 0.001f);
    T_ASSERT(!S_CastUnitTargetSpell(caster, BZ_ANTI_MAGIC_SHELL, ally));

    level.time += 90000; unit_updatestatuses(ally);
    T_ASSERT(!S_UnitSpellImmune(ally));
    T_ASSERT(S_SpellDamage(ally, enemy, 100));
    caster->mana.value = 100;
    T_ASSERT(S_CastUnitTargetSpell(caster, BZ_ANTI_MAGIC_SHELL, ally));
    T_ASSERT(S_UnitSpellImmune(ally));

    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

#endif