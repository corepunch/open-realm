#ifdef BZ_TESTS
#include "test.h"
#include "../skills/s_skills.h"

#define BZ_AUCO MAKEFOURCC('A', 'u', 'c', 'o') // rawcode; Unstable Concoction

LPEDICT alloc_test_unit(DWORD class_id, FLOAT x, FLOAT y);
void reset_entities(void);
void setup_test_world(void);
slkTestData_t *parse_slk_string(const char *text);
void free_slk_rows(slkTestData_t *rows);

typedef struct {
	slkTestData_t *rows, *old;
	LPEDICT caster, primary, splash, ground, ally;
} UCFIX;

/* Non-stock DataB/DataC/DataD prove execute reads abilityitem_t.code, not retail 600/200/140. */
static char const uc_slk[] =
	"ID;PWXL;N;EBB;Y2;X14\n"
	"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\nC;Y1;X4;K\"targs\"\n"
	"C;Y1;X5;K\"Cost1\"\nC;Y1;X6;K\"Cool1\"\nC;Y1;X7;K\"Rng1\"\nC;Y1;X8;K\"Dur1\"\n"
	"C;Y1;X9;K\"HeroDur1\"\nC;Y1;X10;K\"DataA1\"\nC;Y1;X11;K\"DataB1\"\nC;Y1;X12;K\"DataC1\"\n"
	"C;Y1;X13;K\"DataD1\"\nC;Y1;X14;K\"DataE1\"\n"
	"C;Y2;X1;K\"Auco\"\nC;Y2;X2;K\"Auco\"\nC;Y2;X3;K\"1\"\n"
	"C;Y2;X4;K\"air,neutral,enemy\"\nC;Y2;X5;K\"0\"\nC;Y2;X6;K\"0\"\n"
	"C;Y2;X7;K\"400\"\nC;Y2;X8;K\"0\"\nC;Y2;X9;K\"0\"\n"
	"C;Y2;X10;K\"0\"\nC;Y2;X11;K\"77\"\nC;Y2;X12;K\"150\"\nC;Y2;X13;K\"33\"\n"
	"C;Y2;X14;K\"0\"\nE\n";

/* Fill in place: fixture stores no dangling UnitBalance pointers, but keep the void pattern. */
static void uc_setup(UCFIX *fix) {
	reset_entities(); setup_test_world(); level.time = 1000;
	((LPMAPINFO)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
	((LPMAPINFO)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
	memset(level.alliances, 0, sizeof(level.alliances));
	fix->rows = parse_slk_string(uc_slk); fix->old = G_SetSLKRows("AbilityData", fix->rows);
	fix->caster = alloc_test_unit(MAKEFOURCC('o', 't', 'b', 'r'), 0, 0);
	fix->primary = alloc_test_unit(MAKEFOURCC('h', 'g', 'r', 'y'), 64, 0);
	fix->splash = alloc_test_unit(MAKEFOURCC('h', 'g', 'r', 'y'), 100, 0);
	fix->ground = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), 80, 0);
	fix->ally = alloc_test_unit(MAKEFOURCC('h', 'g', 'r', 'y'), 90, 0);
	fix->caster->s.player = fix->ally->s.player = 0;
	fix->primary->s.player = fix->splash->s.player = fix->ground->s.player = 1;
	fix->caster->svflags |= SVF_MONSTER;
	fix->primary->svflags |= SVF_MONSTER; fix->splash->svflags |= SVF_MONSTER;
	fix->ground->svflags |= SVF_MONSTER; fix->ally->svflags |= SVF_MONSTER;
	fix->caster->targtype = TARG_AIR;
	fix->primary->targtype = fix->splash->targtype = fix->ally->targtype = TARG_AIR;
	fix->ground->targtype = TARG_GROUND;
	fix->caster->health.value = fix->caster->health.max_value = 200;
	fix->primary->health.value = fix->primary->health.max_value = 500;
	fix->splash->health.value = fix->splash->health.max_value = 500;
	fix->ground->health.value = fix->ground->health.max_value = 500;
	fix->ally->health.value = fix->ally->health.max_value = 500;
	fix->caster->armor_value = fix->primary->armor_value = fix->splash->armor_value = 0;
	fix->ground->armor_value = fix->ally->armor_value = 0;
	fix->caster->die = unit_die;
	fix->caster->heroabilities[0] = MAKE(heroability_t, .code = BZ_AUCO, .level = 1);
	fix->caster->mana.value = fix->caster->mana.max_value = 100;
}

static void uc_done(UCFIX *fix) {
	G_SetSLKRows("AbilityData", fix->old); free_slk_rows(fix->rows);
}

TEST(wc3_spell, unstable_concoction_procedure_registered) {
	T_EQ(S_AbilityItem(BZ_AUCO).ability->proc, CAbilityUnstableConcoction);
}

/* Primary air takes DataB; nearby air in DataC takes DataD; ground and allies untouched; caster dies. */
TEST(wc3_spell, unstable_concoction_explodes_on_air_and_kills_caster) {
	UCFIX fix;
	uc_setup(&fix);
	T_ASSERT(S_SpellAllowsTarget(BZ_AUCO, fix.caster, fix.primary));
	T_ASSERT(!S_SpellAllowsTarget(BZ_AUCO, fix.caster, fix.ground));
	T_ASSERT(!S_SpellAllowsTarget(BZ_AUCO, fix.caster, fix.ally));
	T_ASSERT(S_CastUnitTargetSpell(fix.caster, BZ_AUCO, fix.primary));
	T_ASSERT(M_IsDead(fix.caster));
	T_FEQ(fix.primary->health.value, 423, 0.001f);
	T_FEQ(fix.splash->health.value, 467, 0.001f);
	T_FEQ(fix.ground->health.value, 500, 0.001f);
	T_FEQ(fix.ally->health.value, 500, 0.001f);
	uc_done(&fix);
}

/* Air outside DataC is not splashed; primary still takes authored DataB. */
TEST(wc3_spell, unstable_concoction_splash_respects_datac_radius) {
	UCFIX fix;
	uc_setup(&fix);
	fix.splash->s.origin2.x = 300; fix.splash->s.origin.x = 300;
	T_ASSERT(S_CastUnitTargetSpell(fix.caster, BZ_AUCO, fix.primary));
	T_FEQ(fix.primary->health.value, 423, 0.001f);
	T_FEQ(fix.splash->health.value, 500, 0.001f);
	uc_done(&fix);
}

/* Ground primary is rejected without spending mana (cost is 0; still no cast). */
TEST(wc3_spell, unstable_concoction_rejects_ground_target) {
	UCFIX fix;
	uc_setup(&fix);
	T_ASSERT(!S_CastUnitTargetSpell(fix.caster, BZ_AUCO, fix.ground));
	T_ASSERT(!M_IsDead(fix.caster));
	T_FEQ(fix.ground->health.value, 500, 0.001f);
	T_FEQ(fix.primary->health.value, 500, 0.001f);
	uc_done(&fix);
}

#endif
