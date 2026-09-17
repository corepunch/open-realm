#ifdef BZ_TESTS
#include "test.h"
#include "../skills/s_skills.h"

#define BZ_ASDG MAKEFOURCC('A', 's', 'd', 'g') // rawcode; Clockwerk Self Destruct L1
#define BZ_ASD2 MAKEFOURCC('A', 's', 'd', '2') // rawcode; Clockwerk Self Destruct L2
#define BZ_ASD3 MAKEFOURCC('A', 's', 'd', '3') // rawcode; Clockwerk Self Destruct L3
#define BZ_ASDS MAKEFOURCC('A', 's', 'd', 's') // rawcode; shared Self Destruct code=
#define BZ_BTLF MAKEFOURCC('B', 'T', 'L', 'F') // rawcode; timed life
#define BZ_ANSY MAKEFOURCC('A', 'N', 's', 'y') // rawcode; Pocket Factory
#define BZ_OGRU MAKEFOURCC('o', 'g', 'r', 'u') // unitCode; fixture Clockwerk

LPEDICT alloc_test_unit(DWORD class_id, FLOAT x, FLOAT y);
void reset_entities(void);
void setup_test_world(void);
slkTestData_t *parse_slk_string(const char *text);
void free_slk_rows(slkTestData_t *rows);

typedef struct {
	slkTestData_t *rows, *old;
	LPEDICT goblin, near_enemy, far_enemy;
} SDFIX;

/* Non-stock DataA/B/C/D/E/F prove death blast reads abilityitem_t.code, not hardcoded Clockwerk values. */
static char const sd_slk[] =
	"ID;PWXL;N;EBB;Y4;X14\n"
	"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\nC;Y1;X4;K\"targs\"\n"
	"C;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"DataB1\"\nC;Y1;X7;K\"DataC1\"\nC;Y1;X8;K\"DataD1\"\n"
	"C;Y1;X9;K\"DataE1\"\nC;Y1;X10;K\"DataF1\"\nC;Y1;X11;K\"Dur1\"\nC;Y1;X12;K\"HeroDur1\"\n"
	"C;Y1;X13;K\"Cost1\"\nC;Y1;X14;K\"Area1\"\n"
	"C;Y2;X1;K\"Asdg\"\nC;Y2;X2;K\"Asds\"\nC;Y2;X3;K\"1\"\n"
	"C;Y2;X4;K\"ground,structure,debris,enemy,neutral\"\n"
	"C;Y2;X5;K\"80\"\nC;Y2;X6;K\"40\"\nC;Y2;X7;K\"160\"\nC;Y2;X8;K\"15\"\n"
	"C;Y2;X9;K\"1\"\nC;Y2;X10;K\"1\"\nC;Y2;X11;K\"0.1\"\nC;Y2;X12;K\"0.1\"\n"
	"C;Y2;X13;K\"0\"\nC;Y2;X14;K\"0\"\n"
	"C;Y3;X1;K\"Asd2\"\nC;Y3;X2;K\"Asds\"\nC;Y3;X3;K\"1\"\n"
	"C;Y3;X4;K\"ground,structure,debris,enemy,neutral\"\n"
	"C;Y3;X5;K\"80\"\nC;Y3;X6;K\"55\"\nC;Y3;X7;K\"160\"\nC;Y3;X8;K\"20\"\n"
	"C;Y3;X9;K\"1\"\nC;Y3;X10;K\"1\"\nC;Y3;X11;K\"0.1\"\nC;Y3;X12;K\"0.1\"\n"
	"C;Y3;X13;K\"0\"\nC;Y3;X14;K\"0\"\n"
	"C;Y4;X1;K\"Asds\"\nC;Y4;X2;K\"Asds\"\nC;Y4;X3;K\"1\"\n"
	"C;Y4;X4;K\"ground,structure,debris,tree,ward\"\n"
	"C;Y4;X5;K\"100\"\nC;Y4;X6;K\"250\"\nC;Y4;X7;K\"250\"\nC;Y4;X8;K\"100\"\n"
	"C;Y4;X9;K\"3\"\nC;Y4;X10;K\"0\"\nC;Y4;X11;K\"0.1\"\nC;Y4;X12;K\"0.1\"\n"
	"C;Y4;X13;K\"0\"\nC;Y4;X14;K\"0\"\nE\n";

static SDFIX sd_setup(DWORD code) {
	SDFIX fix;
	reset_entities(); setup_test_world(); level.time = 1000;
	((LPMAPINFO)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
	((LPMAPINFO)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
	memset(level.alliances, 0, sizeof(level.alliances));
	fix.rows = parse_slk_string(sd_slk); fix.old = G_SetSLKRows("AbilityData", fix.rows);
	fix.goblin = alloc_test_unit(MAKEFOURCC('n', 'c', 'g', 'b'), 0, 0);
	fix.near_enemy = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), 40, 0);
	fix.far_enemy = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), 300, 0);
	fix.goblin->s.player = 0; fix.near_enemy->s.player = fix.far_enemy->s.player = 1;
	fix.goblin->svflags |= SVF_MONSTER; fix.near_enemy->svflags |= SVF_MONSTER; fix.far_enemy->svflags |= SVF_MONSTER;
	fix.goblin->targtype = fix.near_enemy->targtype = fix.far_enemy->targtype = TARG_GROUND;
	fix.goblin->health.value = fix.goblin->health.max_value = 100;
	fix.near_enemy->health.value = fix.near_enemy->health.max_value = 500;
	fix.far_enemy->health.value = fix.far_enemy->health.max_value = 500;
	fix.goblin->armor_value = 0; fix.near_enemy->armor_value = 0; fix.far_enemy->armor_value = 0;
	fix.goblin->die = unit_die;
	T_ASSERT(G_ActorAddSkill(fix.goblin, code));
	return fix;
}

static void sd_done(SDFIX fix) { G_SetSLKRows("AbilityData", fix.old); free_slk_rows(fix.rows); }

TEST(wc3_spell, self_destruct_aliases_share_procedure) {
	T_EQ(S_AbilityItem(BZ_ASDG).ability->proc, CAbilitySelfDestruct);
	T_EQ(S_AbilityItem(BZ_ASD2).ability->proc, CAbilitySelfDestruct);
	T_EQ(S_AbilityItem(BZ_ASD3).ability->proc, CAbilitySelfDestruct);
	T_EQ(S_AbilityItem(BZ_ASDS).ability->proc, CAbilitySelfDestruct);
}

/* Death of a unit that has Asdg deals authored full-radius DataB; units outside DataC are untouched. */
TEST(wc3_spell, self_destruct_death_deals_authored_area_damage) {
	SDFIX fix = sd_setup(BZ_ASDG);
	unit_die(fix.goblin, NULL);
	T_ASSERT(M_IsDead(fix.goblin));
	T_FEQ(fix.near_enemy->health.value, 460, 0.001f);
	T_FEQ(fix.far_enemy->health.value, 500, 0.001f);
	sd_done(fix);
}

/* Asd2 shares the procedure but reads its own DataB through abilityitem_t.code. */
TEST(wc3_spell, self_destruct_asd2_uses_alias_datab) {
	SDFIX fix = sd_setup(BZ_ASD2);
	unit_die(fix.goblin, NULL);
	T_FEQ(fix.near_enemy->health.value, 445, 0.001f);
	T_FEQ(fix.far_enemy->health.value, 500, 0.001f);
	sd_done(fix);
}

/* Partial ring uses DataD when distance is between DataA and DataC. */
TEST(wc3_spell, self_destruct_partial_radius_uses_datad) {
	SDFIX fix = sd_setup(BZ_ASDG);
	fix.near_enemy->s.origin2.x = 100; fix.near_enemy->s.origin.x = 100;
	unit_die(fix.goblin, NULL);
	T_FEQ(fix.near_enemy->health.value, 485, 0.001f);
	sd_done(fix);
}

/* Asds with DataF=0 must not detonate on death (Goblin Sapper contract). */
TEST(wc3_spell, self_destruct_asds_without_dataf_does_not_explode_on_death) {
	SDFIX fix = sd_setup(BZ_ASDS);
	unit_die(fix.goblin, NULL);
	T_FEQ(fix.near_enemy->health.value, 500, 0.001f);
	sd_done(fix);
}

/* BTLF expiry that kills the goblin still detonates when DataF is set. */
TEST(wc3_spell, self_destruct_btlf_expiry_detonates) {
	SDFIX fix = sd_setup(BZ_ASDG);
	unit_addtimedstatus(fix.goblin, "BTLF", 1, 2.0f);
	T_EQ(G_UnitStatusLevel(fix.goblin, BZ_BTLF), 1);
	level.time += 1999; unit_updatestatuses(fix.goblin);
	T_ASSERT(fix.goblin->health.value > 0);
	level.time += 1; unit_updatestatuses(fix.goblin);
	T_ASSERT(M_IsDead(fix.goblin));
	T_FEQ(fix.near_enemy->health.value, 460, 0.001f);
	sd_done(fix);
}

/* Pocket Factory still spawns Clockwerks; Asdg on the goblin detonates when BTLF expires. */
TEST(wc3_spell, self_destruct_pocket_factory_goblin_btlf_detonates) {
	static char const both[] =
		"ID;PWXL;N;EBB;Y3;X16\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\nC;Y1;X4;K\"targs\"\n"
		"C;Y1;X5;K\"Cost1\"\nC;Y1;X6;K\"Cool1\"\nC;Y1;X7;K\"Rng1\"\nC;Y1;X8;K\"Dur1\"\n"
		"C;Y1;X9;K\"HeroDur1\"\nC;Y1;X10;K\"DataA1\"\nC;Y1;X11;K\"DataB1\"\nC;Y1;X12;K\"DataC1\"\n"
		"C;Y1;X13;K\"DataD1\"\nC;Y1;X14;K\"DataE1\"\nC;Y1;X15;K\"DataF1\"\nC;Y1;X16;K\"UnitID1\"\n"
		"C;Y2;X1;K\"ANsy\"\nC;Y2;X2;K\"ANsy\"\nC;Y2;X3;K\"1\"\nC;Y2;X4;K\"\"\n"
		"C;Y2;X5;K\"0\"\nC;Y2;X6;K\"0\"\nC;Y2;X7;K\"500\"\nC;Y2;X8;K\"25\"\n"
		"C;Y2;X9;K\"25\"\nC;Y2;X10;K\"2\"\nC;Y2;X11;K\"ogru\"\nC;Y2;X12;K\"8\"\n"
		"C;Y2;X13;K\"64\"\nC;Y2;X14;K\"1100\"\nC;Y2;X15;K\"0\"\nC;Y2;X16;K\"hfoo\"\n"
		"C;Y3;X1;K\"Asdg\"\nC;Y3;X2;K\"Asds\"\nC;Y3;X3;K\"1\"\n"
		"C;Y3;X4;K\"ground,structure,debris,enemy,neutral\"\n"
		"C;Y3;X5;K\"0\"\nC;Y3;X6;K\"0\"\nC;Y3;X7;K\"0\"\nC;Y3;X8;K\"0.1\"\n"
		"C;Y3;X9;K\"0.1\"\nC;Y3;X10;K\"80\"\nC;Y3;X11;K\"40\"\nC;Y3;X12;K\"160\"\n"
		"C;Y3;X13;K\"15\"\nC;Y3;X14;K\"1\"\nC;Y3;X15;K\"1\"\nC;Y3;X16;K\"\"\nE\n";
	slkTestData_t *rows = parse_slk_string(both), *old;
	LPEDICT caster, goblin = NULL, enemy;
	VECTOR2 point = { 128, 128 };
	reset_entities(); setup_test_world(); level.time = 1000;
	((LPMAPINFO)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
	((LPMAPINFO)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
	memset(level.alliances, 0, sizeof(level.alliances));
	old = G_SetSLKRows("AbilityData", rows);
	caster = alloc_test_unit(MAKEFOURCC('N', 't', 'i', 'n'), 0, 0);
	caster->s.player = 0; caster->svflags |= SVF_MONSTER; caster->targtype = TARG_GROUND;
	caster->mana.value = caster->mana.max_value = 500;
	caster->heroabilities[0] = MAKE(heroability_t, .code = BZ_ANSY, .level = 1);
	T_ASSERT(S_CastPointTargetSpell(caster, BZ_ANSY, &point));
	level.time += 2000; G_RunEntities();
	FILTER_EDICTS(ent, ent->inuse && ent->class_id == BZ_OGRU) goblin = ent;
	T_NOT_NULL(goblin);
	/* Test fixtures may omit UnitUI.modelFile, so SP_CallSpawn skips SP_monster_unit. */
	goblin->die = unit_die;
	T_ASSERT(G_ActorAddSkill(goblin, BZ_ASDG));
	goblin->health.value = goblin->health.max_value = 100; goblin->armor_value = 0;
	/* Place the probe after spawn so factory DataD offset cannot miss the full-radius ring. */
	enemy = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), goblin->s.origin2.x + 40, goblin->s.origin2.y);
	enemy->s.player = 1; enemy->svflags |= SVF_MONSTER; enemy->targtype = TARG_GROUND;
	enemy->health.value = enemy->health.max_value = 500; enemy->armor_value = 0;
	level.time += 7999; unit_updatestatuses(goblin);
	T_ASSERT(goblin->health.value > 0);
	level.time += 1; unit_updatestatuses(goblin);
	T_ASSERT(M_IsDead(goblin));
	T_FEQ(enemy->health.value, 460, 0.001f);
	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

#endif
