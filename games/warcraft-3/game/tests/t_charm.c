#ifdef BZ_TESTS
#include "test.h"
#include "../skills/s_skills.h"

#define BZ_ANCH MAKEFOURCC('A', 'N', 'c', 'h')
#define BZ_ACSK MAKEFOURCC('A', 'C', 's', 'k')

edict_t *alloc_test_unit(uint32_t class_id, float x, float y);
void reset_entities(void);
void setup_test_world(void);
slkTestData_t *parse_slk_string(cstring_t text);
void free_slk_rows(slkTestData_t *rows);

/* Non-stock mana, range, and level limit keep validation data-driven. */
static cstring_t const charm_slk =
    "ID;PWXL;N;EBB;Y2;X8\n"
    "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
    "C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"Cost1\"\nC;Y1;X6;K\"Cool1\"\n"
    "C;Y1;X7;K\"Rng1\"\nC;Y1;X8;K\"DataA1\"\n"
    "C;Y2;X1;K\"ANch\"\nC;Y2;X2;K\"ANch\"\nC;Y2;X3;K\"1\"\n"
    "C;Y2;X4;K\"air,ground,enemy,neutral,nonhero\"\n"
    "C;Y2;X5;K\"37\"\nC;Y2;X6;K\"0\"\nC;Y2;X7;K\"700\"\n"
    "C;Y2;X8;K\"5\"\nE\n";

typedef struct {
    slkTestData_t *rows, *old;
    edict_t *caster, *target;
    UnitBalance_t target_balance;
} charmFix_t;

static void charm_setup(charmFix_t *fix, uint32_t target_type) {
    reset_entities(); setup_test_world(); level.time = 1000;
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    memset(level.alliances, 0, sizeof(level.alliances));
    fix->rows = parse_slk_string(charm_slk);
    fix->old = G_SetSLKRows("AbilityData", fix->rows);
    fix->target_balance = MAKE(UnitBalance_t, .maxHealth = 500, .level = 3);
    fix->caster = alloc_test_unit(MAKEFOURCC('H','b','l','m'), 0, 0);
    fix->target = alloc_test_unit(target_type, 96, 0);
    fix->caster->s.player = 0; fix->target->s.player = 1;
    fix->caster->svflags |= SVF_MONSTER; fix->target->svflags |= SVF_MONSTER;
    fix->caster->targtype = fix->target->targtype = TARG_GROUND;
    fix->caster->heroabilities[0] = MAKE(heroability_t, .code = BZ_ANCH, .level = 1);
    fix->caster->mana.value = fix->caster->mana.max_value = 200;
    fix->target->health.value = fix->target->health.max_value = 500;
    fix->target->data.UnitBalance = &fix->target_balance;
}

static void charm_done(charmFix_t *fix) {
    G_SetSLKRows("AbilityData", fix->old);
    free_slk_rows(fix->rows);
}

TEST(wc3_spell, charm_rejects_dead_hero_resistant_and_unbalanced_targets) {
    charmFix_t fix;
    float mana;

    charm_setup(&fix, MAKEFOURCC('n','f','t','t'));
    mana = fix.caster->mana.value;
    fix.target->health.value = 0;
    T_ASSERT(!S_CastUnitTargetSpell(fix.caster, BZ_ANCH, fix.target));
    T_FEQ(fix.caster->mana.value, mana, 0.001f);
    charm_done(&fix);

    charm_setup(&fix, MAKEFOURCC('H','p','a','l'));
    mana = fix.caster->mana.value;
    fix.target_balance.strength = 1;
    T_ASSERT(G_UnitIsHero(fix.target));
    T_ASSERT(!S_CastUnitTargetSpell(fix.caster, BZ_ANCH, fix.target));
    T_FEQ(fix.caster->mana.value, mana, 0.001f);
    charm_done(&fix);

    charm_setup(&fix, MAKEFOURCC('n','f','t','t'));
    fix.target_balance.level = 6;
    mana = fix.caster->mana.value;
    T_ASSERT(!S_CastUnitTargetSpell(fix.caster, BZ_ANCH, fix.target));
    T_FEQ(fix.caster->mana.value, mana, 0.001f);
    charm_done(&fix);

    charm_setup(&fix, MAKEFOURCC('n','f','t','t'));
    fix.target->heroabilities[0] = MAKE(heroability_t, .code = BZ_ACSK, .level = 1);
    mana = fix.caster->mana.value;
    T_ASSERT(S_UnitIsResistant(fix.target));
    T_ASSERT(!S_CastUnitTargetSpell(fix.caster, BZ_ANCH, fix.target));
    T_FEQ(fix.caster->mana.value, mana, 0.001f);
    charm_done(&fix);

    charm_setup(&fix, MAKEFOURCC('n','f','t','t'));
    fix.target->data.UnitBalance = NULL;
    mana = fix.caster->mana.value;
    T_ASSERT(!S_CastUnitTargetSpell(fix.caster, BZ_ANCH, fix.target));
    T_FEQ(fix.caster->mana.value, mana, 0.001f);
    charm_done(&fix);
}

#endif
