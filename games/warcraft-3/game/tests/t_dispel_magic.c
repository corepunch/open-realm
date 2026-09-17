#ifdef BZ_TESTS
#include "test.h"
#include "../skills/s_skills.h"

#define BZ_ADIS MAKEFOURCC('A', 'd', 'i', 's') // rawcode; Priest Dispel Magic
#define BZ_ADCH MAKEFOURCC('A', 'd', 'c', 'h') // rawcode; TFT Disenchant(old)
#define BZ_ADVM MAKEFOURCC('A', 'd', 'v', 'm') // rawcode; Destroyer Devour Magic
#define BZ_AENS MAKEFOURCC('A', 'e', 'n', 's') // rawcode; Raider Ensnare
#define BZ_BSLO MAKEFOURCC('B', 's', 'l', 'o') // rawcode; Slow timed status probe
#define BZ_BINF MAKEFOURCC('B', 'i', 'n', 'f') // rawcode; Inner Fire timed status probe

LPEDICT alloc_test_unit(DWORD class_id, FLOAT x, FLOAT y);
void reset_entities(void);
void setup_test_world(void);
slkTestData_t *parse_slk_string(const char *text);
void free_slk_rows(slkTestData_t *rows);

/* Non-stock DataB/DataA/DataE so tests cannot pass on retail 200/50/75/180. */
static LPCSTR dispel_family_slk =
	"ID;PWXL;N;EBB;Y4;X12\n"
	"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
	"C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"Cost1\"\nC;Y1;X6;K\"Cool1\"\n"
	"C;Y1;X7;K\"Rng1\"\nC;Y1;X8;K\"Area1\"\nC;Y1;X9;K\"DataA1\"\n"
	"C;Y1;X10;K\"DataB1\"\nC;Y1;X11;K\"DataE1\"\nC;Y1;X12;K\"Dur1\"\n"
	"C;Y2;X1;K\"Adis\"\nC;Y2;X2;K\"Adis\"\nC;Y2;X3;K\"1\"\n"
	"C;Y2;X4;K\"air,ground,ward,invu,vuln\"\nC;Y2;X5;K\"75\"\nC;Y2;X6;K\"0\"\n"
	"C;Y2;X7;K\"500\"\nC;Y2;X8;K\"250\"\nC;Y2;X9;K\"0\"\n"
	"C;Y2;X10;K\"111\"\nC;Y2;X11;K\"0\"\nC;Y2;X12;K\"0\"\n"
	"C;Y3;X1;K\"Adch\"\nC;Y3;X2;K\"Adch\"\nC;Y3;X3;K\"1\"\n"
	"C;Y3;X4;K\"air,ground,ward,invu,vuln,enemy\"\nC;Y3;X5;K\"50\"\nC;Y3;X6;K\"0\"\n"
	"C;Y3;X7;K\"500\"\nC;Y3;X8;K\"250\"\nC;Y3;X9;K\"0\"\n"
	"C;Y3;X10;K\"222\"\nC;Y3;X11;K\"0\"\nC;Y3;X12;K\"0\"\n"
	"C;Y4;X1;K\"Advm\"\nC;Y4;X2;K\"Advm\"\nC;Y4;X3;K\"1\"\n"
	"C;Y4;X4;K\"air,ground,ward,invu,vuln,tree\"\nC;Y4;X5;K\"0\"\nC;Y4;X6;K\"0\"\n"
	"C;Y4;X7;K\"600\"\nC;Y4;X8;K\"250\"\nC;Y4;X9;K\"17\"\n"
	"C;Y4;X10;K\"23\"\nC;Y4;X11;K\"91\"\nC;Y4;X12;K\"0\"\nE\n";

typedef struct {
	slkTestData_t *rows, *old;
	LPEDICT caster, enemy, summon, far;
} DISPELFIX;

static DISPELFIX dispel_setup(DWORD code) {
	DISPELFIX fix;
	reset_entities(); setup_test_world(); level.time = 1000;
	((LPMAPINFO)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
	((LPMAPINFO)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
	memset(level.alliances, 0, sizeof(level.alliances));
	fix.rows = parse_slk_string(dispel_family_slk);
	fix.old = G_SetSLKRows("AbilityData", fix.rows);
	fix.caster = alloc_test_unit(MAKEFOURCC('u', 'n', 'e', 'c'), 0, 0);
	fix.enemy = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), 64, 0);
	fix.summon = alloc_test_unit(MAKEFOURCC('o', 'g', 'r', 'u'), 96, 0);
	fix.far = alloc_test_unit(MAKEFOURCC('o', 'g', 'r', 'u'), 800, 0);
	fix.caster->s.player = 0;
	fix.enemy->s.player = fix.summon->s.player = fix.far->s.player = 1;
	fix.caster->svflags |= SVF_MONSTER; fix.enemy->svflags |= SVF_MONSTER;
	fix.summon->svflags |= SVF_MONSTER; fix.far->svflags |= SVF_MONSTER;
	fix.caster->targtype = fix.enemy->targtype = fix.summon->targtype = fix.far->targtype = TARG_GROUND;
	fix.caster->heroabilities[0] = MAKE(heroability_t, .code = code, .level = 1);
	fix.caster->mana.value = fix.caster->mana.max_value = 200;
	fix.caster->health.value = fix.caster->health.max_value = 500;
	fix.enemy->health.value = fix.enemy->health.max_value = 500;
	fix.summon->health.value = fix.summon->health.max_value = 500;
	fix.far->health.value = fix.far->health.max_value = 500;
	/* S_SummonAt marks summons with owner; Purge/Dispel use the same predicate. */
	fix.summon->owner = fix.caster;
	return fix;
}

static void dispel_done(DISPELFIX fix) {
	G_SetSLKRows("AbilityData", fix.old); free_slk_rows(fix.rows);
}

TEST(wc3_spell, dispel_magic_aliases_share_procedure) {
	T_EQ(S_AbilityItem(BZ_ADIS).ability->proc, CAbilityDispelMagic);
	T_EQ(S_AbilityItem(BZ_ADCH).ability->proc, CAbilityDispelMagic);
	T_EQ(S_AbilityItem(BZ_ADVM).ability->proc, CAbilityDispelMagic);
}

/* Adis reads its own DataB for summoned damage; ordinary units are untouched. */
TEST(wc3_spell, dispel_adis_damages_summoned_with_datab) {
	DISPELFIX fix = dispel_setup(BZ_ADIS);
	VECTOR2 point = fix.summon->s.origin2;
	T_ASSERT(S_CastPointTargetSpell(fix.caster, BZ_ADIS, &point));
	T_FEQ(fix.summon->health.value, 389, 0.001f); /* 500 - 111 */
	T_FEQ(fix.enemy->health.value, 500, 0.001f);
	T_FEQ(fix.far->health.value, 500, 0.001f);
	dispel_done(fix);
}

/* Adch is not an Adis alias; it must still damage summons from its DataB row. */
TEST(wc3_spell, dispel_adch_damages_summoned_with_datab) {
	DISPELFIX fix = dispel_setup(BZ_ADCH);
	VECTOR2 point = fix.summon->s.origin2;
	T_ASSERT(S_CastPointTargetSpell(fix.caster, BZ_ADCH, &point));
	T_FEQ(fix.summon->health.value, 278, 0.001f); /* 500 - 222 */
	T_FEQ(fix.enemy->health.value, 500, 0.001f);
	dispel_done(fix);
}

TEST(wc3_spell, dispel_removes_timed_statuses_in_area) {
	DISPELFIX fix = dispel_setup(BZ_ADIS);
	VECTOR2 point = fix.enemy->s.origin2;
	unit_addtimedstatus(fix.enemy, "Bslo", 1, 30.0f);
	unit_addtimedstatus(fix.enemy, "Binf", 1, 30.0f);
	T_ASSERT(S_UnitHasStatus(fix.enemy, BZ_BSLO));
	T_ASSERT(S_UnitHasStatus(fix.enemy, BZ_BINF));
	T_ASSERT(S_CastPointTargetSpell(fix.caster, BZ_ADIS, &point));
	T_ASSERT(!S_UnitHasStatus(fix.enemy, BZ_BSLO));
	T_ASSERT(!S_UnitHasStatus(fix.enemy, BZ_BINF));
	T_FEQ(fix.enemy->health.value, 500, 0.001f);
	dispel_done(fix);
}

/* Advm heals DataA HP and DataB mana per buff removed (two buffs, non-stock values). */
TEST(wc3_spell, devour_magic_heals_per_buff_removed) {
	DISPELFIX fix = dispel_setup(BZ_ADVM);
	VECTOR2 point = fix.enemy->s.origin2;
	fix.caster->health.value = 100;
	fix.caster->mana.value = 10;
	unit_addtimedstatus(fix.enemy, "Bslo", 1, 30.0f);
	unit_addtimedstatus(fix.enemy, "Binf", 1, 30.0f);
	T_ASSERT(S_CastPointTargetSpell(fix.caster, BZ_ADVM, &point));
	T_ASSERT(!S_UnitHasStatus(fix.enemy, BZ_BSLO));
	T_ASSERT(!S_UnitHasStatus(fix.enemy, BZ_BINF));
	T_FEQ(fix.caster->health.value, 134, 0.001f); /* 100 + 17*2 */
	T_FEQ(fix.caster->mana.value, 56, 0.001f);    /* 10 + 23*2 */
	T_FEQ(fix.enemy->health.value, 500, 0.001f);
	dispel_done(fix);
}

/* Advm summoned damage must use DataE=91, not DataB=23. */
TEST(wc3_spell, devour_magic_summoned_damage_uses_datae) {
	DISPELFIX fix = dispel_setup(BZ_ADVM);
	VECTOR2 point = fix.summon->s.origin2;
	fix.caster->health.value = 100;
	fix.caster->mana.value = 10;
	T_ASSERT(S_CastPointTargetSpell(fix.caster, BZ_ADVM, &point));
	T_FEQ(fix.summon->health.value, 409, 0.001f); /* 500 - 91 */
	/* No buffs removed: heals stay at zero even though DataB is non-zero. */
	T_FEQ(fix.caster->health.value, 100, 0.001f);
	T_FEQ(fix.caster->mana.value, 10, 0.001f);
	dispel_done(fix);
}

/* Dispel must restore AI_FLYING; Ensnare expiry is not only a timed-status path. */
TEST(wc3_spell, dispel_restores_ensnared_flyer) {
	static UnitData_t flyer_data;
	const char slk[] =
		"ID;PWXL;N;EBB;Y3;X10\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
		"C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"Cost1\"\nC;Y1;X6;K\"Rng1\"\n"
		"C;Y1;X7;K\"Dur1\"\nC;Y1;X8;K\"HeroDur1\"\nC;Y1;X9;K\"Area1\"\nC;Y1;X10;K\"BuffID1\"\n"
		"C;Y2;X1;K\"Adis\"\nC;Y2;X2;K\"Adis\"\nC;Y2;X3;K\"1\"\n"
		"C;Y2;X4;K\"air,ground,ward,invu,vuln\"\nC;Y2;X5;K\"0\"\nC;Y2;X6;K\"500\"\n"
		"C;Y2;X7;K\"0\"\nC;Y2;X8;K\"0\"\nC;Y2;X9;K\"250\"\n"
		"C;Y3;X1;K\"Aens\"\nC;Y3;X2;K\"Aens\"\nC;Y3;X3;K\"1\"\n"
		"C;Y3;X4;K\"ground,air,enemy,neutral\"\nC;Y3;X5;K\"0\"\nC;Y3;X6;K\"500\"\n"
		"C;Y3;X7;K\"7\"\nC;Y3;X8;K\"3\"\nC;Y3;X10;K\"Bena,Beng\"\nE\n";
	slkTestData_t *rows, *old;
	LPEDICT priest, raider, flyer;
	VECTOR2 point;
	reset_entities(); setup_test_world(); level.time = 1000;
	((LPMAPINFO)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
	((LPMAPINFO)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
	memset(level.alliances, 0, sizeof(level.alliances));
	rows = parse_slk_string(slk); old = G_SetSLKRows("AbilityData", rows);
	priest = alloc_test_unit(MAKEFOURCC('h', 'p', 'r', 'i'), 0, 0);
	raider = alloc_test_unit(MAKEFOURCC('o', 'r', 'a', 'i'), 32, 0);
	flyer = alloc_test_unit(MAKEFOURCC('h', 'g', 'r', 'y'), 96, 0);
	priest->s.player = raider->s.player = 0; flyer->s.player = 1;
	priest->svflags |= SVF_MONSTER; raider->svflags |= SVF_MONSTER; flyer->svflags |= SVF_MONSTER;
	priest->targtype = raider->targtype = TARG_GROUND; flyer->targtype = TARG_AIR;
	priest->heroabilities[0] = MAKE(heroability_t, .code = BZ_ADIS, .level = 1);
	raider->heroabilities[0] = MAKE(heroability_t, .code = BZ_AENS, .level = 1);
	priest->mana.value = priest->mana.max_value = 100;
	raider->mana.value = raider->mana.max_value = 100;
	memset(&flyer_data, 0, sizeof(flyer_data));
	flyer_data.moveTypeName = "fly"; flyer_data.moveHeight = 180.0f;
	flyer->data.UnitData = &flyer_data;
	flyer->aiflags |= AI_FLYING; flyer->unitinfo.FlyHeight = 180.0f;
	T_ASSERT(S_CastUnitTargetSpell(raider, BZ_AENS, flyer));
	T_ASSERT(S_UnitIsEnsnared(flyer));
	T_ASSERT(!(flyer->aiflags & AI_FLYING));
	point = flyer->s.origin2;
	T_ASSERT(S_CastPointTargetSpell(priest, BZ_ADIS, &point));
	T_ASSERT(!S_UnitIsEnsnared(flyer));
	T_ASSERT(flyer->aiflags & AI_FLYING);
	T_FEQ(flyer->unitinfo.FlyHeight, 180, 0.001f);
	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, devour_magic_empty_area_heals_nothing) {
	DISPELFIX fix = dispel_setup(BZ_ADVM);
	VECTOR2 point = { 400, 400 };
	fix.caster->health.value = 100;
	fix.caster->mana.value = 10;
	T_ASSERT(S_CastPointTargetSpell(fix.caster, BZ_ADVM, &point));
	T_FEQ(fix.caster->health.value, 100, 0.001f);
	T_FEQ(fix.caster->mana.value, 10, 0.001f);
	T_FEQ(fix.enemy->health.value, 500, 0.001f);
	T_FEQ(fix.summon->health.value, 500, 0.001f);
	dispel_done(fix);
}

#endif
