#ifdef BZ_TESTS
#include "test.h"
#include "../skills/s_skills.h"

#define BZ_ANTM MAKEFOURCC('A', 'N', 't', 'm') // rawcode; TFT Alchemist Transmute

LPEDICT alloc_test_unit(DWORD class_id, FLOAT x, FLOAT y);
void reset_entities(void);
void setup_test_world(void);
slkTestData_t *parse_slk_string(const char *text);
void free_slk_rows(slkTestData_t *rows);
void unit_die(LPEDICT self, LPEDICT attacker);

/* Non-stock Cost/DataA/DataC so tests cannot pass on retail 150/0.8/5. */
static LPCSTR transmute_slk =
    "ID;PWXL;N;EBB;Y2;X12\n"
    "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
    "C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"Cost1\"\nC;Y1;X6;K\"Cool1\"\n"
    "C;Y1;X7;K\"Rng1\"\nC;Y1;X8;K\"DataA1\"\nC;Y1;X9;K\"DataB1\"\n"
    "C;Y1;X10;K\"DataC1\"\nC;Y1;X11;K\"DataD1\"\nC;Y1;X12;K\"BuffID1\"\n"
    "C;Y2;X1;K\"ANtm\"\nC;Y2;X2;K\"ANtm\"\nC;Y2;X3;K\"1\"\n"
    "C;Y2;X4;K\"air,ground,enemy,neutral,nonhero\"\n"
    "C;Y2;X5;K\"40\"\nC;Y2;X6;K\"0\"\nC;Y2;X7;K\"650\"\n"
    "C;Y2;X8;K\"0.5\"\nC;Y2;X9;K\"0\"\nC;Y2;X10;K\"3\"\n"
    "C;Y2;X11;K\"1\"\nC;Y2;X12;K\"BNtm\"\nE\n";

typedef struct {
    slkTestData_t *rows, *old;
    LPEDICT caster, enemy, ally, hero;
    UnitBalance_t enemy_bal, ally_bal, hero_bal;
} TMFIX;

/* Fill caller in place: edict UnitBalance pointers must not dangle. */
static void tm_setup(TMFIX *fix) {
    reset_entities(); setup_test_world(); level.time = 1000;
    ((LPMAPINFO)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((LPMAPINFO)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    memset(level.alliances, 0, sizeof(level.alliances));
    fix->rows = parse_slk_string(transmute_slk);
    fix->old = G_SetSLKRows("AbilityData", fix->rows);
    fix->enemy_bal = MAKE(UnitBalance_t, .maxHealth = 500, .level = 2, .goldCost = 200, .lumberCost = 40);
    fix->ally_bal = MAKE(UnitBalance_t, .maxHealth = 500, .level = 2, .goldCost = 200);
    fix->hero_bal = MAKE(UnitBalance_t, .maxHealth = 500, .strength = 20, .level = 1, .goldCost = 425);
    fix->caster = alloc_test_unit(MAKEFOURCC('N', 'a', 'l', 'c'), 0, 0);
    fix->enemy = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), 64, 0);
    fix->ally = alloc_test_unit(MAKEFOURCC('o', 'g', 'r', 'u'), 96, 0);
    fix->hero = alloc_test_unit(MAKEFOURCC('H', 'p', 'a', 'l'), 128, 0);
    fix->caster->s.player = fix->ally->s.player = 0;
    fix->enemy->s.player = fix->hero->s.player = 1;
    fix->caster->svflags |= SVF_MONSTER; fix->enemy->svflags |= SVF_MONSTER;
    fix->ally->svflags |= SVF_MONSTER; fix->hero->svflags |= SVF_MONSTER;
    fix->caster->targtype = fix->enemy->targtype = fix->ally->targtype = fix->hero->targtype = TARG_GROUND;
    fix->enemy->data.UnitBalance = &fix->enemy_bal;
    fix->ally->data.UnitBalance = &fix->ally_bal;
    fix->hero->data.UnitBalance = &fix->hero_bal;
    fix->caster->heroabilities[0] = MAKE(heroability_t, .code = BZ_ANTM, .level = 1);
    fix->caster->mana.value = fix->caster->mana.max_value = 200;
    fix->enemy->health.value = fix->enemy->health.max_value = 500;
    fix->ally->health.value = fix->ally->health.max_value = 500;
    fix->hero->health.value = fix->hero->health.max_value = 500;
    fix->enemy->die = unit_die; fix->ally->die = unit_die; fix->hero->die = unit_die;
    game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 100;
    game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = 50;
    game.clients[0].ps.stats[PLAYERSTATE_GOLD_UPKEEP_RATE] = 100;
    game.clients[0].ps.stats[PLAYERSTATE_LUMBER_UPKEEP_RATE] = 100;
}

static void tm_done(TMFIX *fix) { G_SetSLKRows("AbilityData", fix->old); free_slk_rows(fix->rows); }

TEST(wc3_spell, transmute_procedure_is_unit_spell) {
    abilityitem_t item = S_AbilityItem(BZ_ANTM);
    T_NOT_NULL(item.ability);
    T_EQ(item.ability->proc, CAbilityTransmute);
    T_ASSERT(!(item.ability->flags & AB_CHANNEL));
    T_EQ(item.ability->target_type, SPELL_TARGET_UNIT);
}

/* Kills the target and credits goldCost * DataA (200 * 0.5 = 100). */
TEST(wc3_spell, transmute_kills_and_credits_gold_factor) {
    TMFIX fix; tm_setup(&fix);
    T_ASSERT(S_CastUnitTargetSpell(fix.caster, BZ_ANTM, fix.enemy));
    T_FEQ(fix.caster->mana.value, 160, 0.001f);
    T_ASSERT(M_IsDead(fix.enemy));
    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD], 200);
    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_LUMBER], 50);
    tm_done(&fix);
}

/* DataB lumberCostFactor credits lumber when authored non-zero. */
TEST(wc3_spell, transmute_credits_lumber_when_datab_set) {
    TMFIX fix; tm_setup(&fix);
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X12\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"Cost1\"\nC;Y1;X6;K\"Cool1\"\n"
        "C;Y1;X7;K\"Rng1\"\nC;Y1;X8;K\"DataA1\"\nC;Y1;X9;K\"DataB1\"\n"
        "C;Y1;X10;K\"DataC1\"\nC;Y1;X11;K\"DataD1\"\nC;Y1;X12;K\"BuffID1\"\n"
        "C;Y2;X1;K\"ANtm\"\nC;Y2;X2;K\"ANtm\"\nC;Y2;X3;K\"1\"\n"
        "C;Y2;X4;K\"air,ground,enemy,neutral,nonhero\"\n"
        "C;Y2;X5;K\"40\"\nC;Y2;X6;K\"0\"\nC;Y2;X7;K\"650\"\n"
        "C;Y2;X8;K\"0.5\"\nC;Y2;X9;K\"0.5\"\nC;Y2;X10;K\"3\"\n"
        "C;Y2;X11;K\"1\"\nC;Y2;X12;K\"BNtm\"\nE\n";
    G_SetSLKRows("AbilityData", fix.old); free_slk_rows(fix.rows);
    fix.rows = parse_slk_string(slk); fix.old = G_SetSLKRows("AbilityData", fix.rows);
    T_ASSERT(S_CastUnitTargetSpell(fix.caster, BZ_ANTM, fix.enemy));
    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD], 200);
    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_LUMBER], 70); /* 50 + 40*0.5 */
    tm_done(&fix);
}

/* Rejects ally/dead/hero/over-level without spending mana. */
TEST(wc3_spell, transmute_rejects_invalid_targets_without_mana_spend) {
    TMFIX fix; tm_setup(&fix);
    FLOAT mana = fix.caster->mana.value;

    T_ASSERT(!S_CastUnitTargetSpell(fix.caster, BZ_ANTM, fix.ally));
    T_FEQ(fix.caster->mana.value, mana, 0.001f);

    fix.enemy->health.value = 0;
    T_ASSERT(!S_CastUnitTargetSpell(fix.caster, BZ_ANTM, fix.enemy));
    T_FEQ(fix.caster->mana.value, mana, 0.001f);
    fix.enemy->health.value = 500;

    T_ASSERT(G_UnitIsHero(fix.hero));
    T_ASSERT(!S_CastUnitTargetSpell(fix.caster, BZ_ANTM, fix.hero));
    T_FEQ(fix.caster->mana.value, mana, 0.001f);

    fix.enemy_bal.level = 4;
    T_ASSERT(!S_CastUnitTargetSpell(fix.caster, BZ_ANTM, fix.enemy));
    T_FEQ(fix.caster->mana.value, mana, 0.001f);
    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD], 100);
    tm_done(&fix);
}

#endif
