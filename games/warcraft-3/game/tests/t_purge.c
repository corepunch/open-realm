#ifdef BZ_TESTS
#include "test.h"
#include "../skills/s_skills.h"

#define BZ_APRG MAKEFOURCC('A', 'p', 'r', 'g') // rawcode; classic Purge
#define BZ_APG2 MAKEFOURCC('A', 'p', 'g', '2') // rawcode; TFT melee Purge with pause
#define BZ_AILP MAKEFOURCC('A', 'I', 'l', 'p') // rawcode; Item Purge alias
#define BZ_BPRG MAKEFOURCC('B', 'p', 'r', 'g') // rawcode; Purge slow/pause buff

LPEDICT alloc_test_unit(DWORD class_id, FLOAT x, FLOAT y);
void reset_entities(void);
void setup_test_world(void);
void unit_stand(LPEDICT self);
slkTestData_t *parse_slk_string(const char *text);
void free_slk_rows(slkTestData_t *rows);

/* Non-stock DataA/DataC/DataD/DataE/Dur so tests cannot pass on hardcoded retail constants. */
#define PURGE_APG2_SLK \
	"ID;PWXL;N;EBB;Y3;X14\n" \
	"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n" \
	"C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"Cost1\"\nC;Y1;X6;K\"Cool1\"\n" \
	"C;Y1;X7;K\"Rng1\"\nC;Y1;X8;K\"Dur1\"\nC;Y1;X9;K\"HeroDur1\"\n" \
	"C;Y1;X10;K\"BuffID1\"\nC;Y1;X11;K\"DataA1\"\nC;Y1;X12;K\"DataC1\"\n" \
	"C;Y1;X13;K\"DataD1\"\nC;Y1;X14;K\"DataE1\"\n" \
	"C;Y2;X1;K\"Apg2\"\nC;Y2;X2;K\"Aprg\"\nC;Y2;X3;K\"1\"\n" \
	"C;Y2;X4;K\"air,ground,ward,vuln,invu,tree\"\nC;Y2;X5;K\"75\"\nC;Y2;X6;K\"0\"\n" \
	"C;Y2;X7;K\"700\"\nC;Y2;X8;K\"12\"\nC;Y2;X9;K\"4\"\n" \
	"C;Y2;X10;K\"Bprg\"\nC;Y2;X11;K\"0.5\"\nC;Y2;X12;K\"180\"\n" \
	"C;Y2;X13;K\"2\"\nC;Y2;X14;K\"1\"\n" \
	"C;Y3;X1;K\"Aprg\"\nC;Y3;X2;K\"Aprg\"\nC;Y3;X3;K\"1\"\n" \
	"C;Y3;X4;K\"air,ground,ward,vuln,invu,tree\"\nC;Y3;X5;K\"75\"\nC;Y3;X6;K\"0\"\n" \
	"C;Y3;X7;K\"700\"\nC;Y3;X8;K\"12\"\nC;Y3;X9;K\"4\"\n" \
	"C;Y3;X10;K\"Bprg\"\nC;Y3;X11;K\"0.5\"\nC;Y3;X12;K\"180\"\n" \
	"C;Y3;X13;K\"0\"\nC;Y3;X14;K\"0\"\nE\n"

typedef struct {
	slkTestData_t *rows, *old;
	LPEDICT caster, enemy;
} PURGEFIX;

static PURGEFIX purge_setup(LPCSTR slk, DWORD code) {
	PURGEFIX fix;
	reset_entities(); setup_test_world(); level.time = 1000;
	((LPMAPINFO)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
	((LPMAPINFO)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
	memset(level.alliances, 0, sizeof(level.alliances));
	fix.rows = parse_slk_string(slk); fix.old = G_SetSLKRows("AbilityData", fix.rows);
	fix.caster = alloc_test_unit(MAKEFOURCC('o','s','h','m'), 0, 0);
	fix.enemy = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 96, 0);
	fix.caster->s.player = 0; fix.enemy->s.player = 1;
	fix.caster->svflags |= SVF_MONSTER; fix.enemy->svflags |= SVF_MONSTER;
	fix.caster->targtype = fix.enemy->targtype = TARG_GROUND;
	fix.caster->heroabilities[0] = MAKE(heroability_t, .code = code, .level = 1);
	fix.caster->mana.value = fix.caster->mana.max_value = 200;
	fix.enemy->health.value = fix.enemy->health.max_value = 500;
	fix.enemy->stand = unit_stand; unit_stand(fix.enemy);
	return fix;
}

static void purge_done(PURGEFIX fix) { G_SetSLKRows("AbilityData", fix.old); free_slk_rows(fix.rows); }

TEST(wc3_spell, purge_aliases_share_procedure) {
	T_EQ(S_AbilityItem(BZ_APRG).ability->proc, CAbilityPurge);
	T_EQ(S_AbilityItem(BZ_APG2).ability->proc, CAbilityPurge);
	T_EQ(S_AbilityItem(BZ_AILP).ability->proc, CAbilityPurge);
}

/* Aprg with DataD=0 slows by authored DataA and does not immobilize. */
TEST(wc3_spell, purge_aprg_slows_by_authored_dataa_without_immobilize) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X12\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
		"C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"Cost1\"\nC;Y1;X6;K\"Rng1\"\n"
		"C;Y1;X7;K\"Dur1\"\nC;Y1;X8;K\"HeroDur1\"\nC;Y1;X9;K\"BuffID1\"\n"
		"C;Y1;X10;K\"DataA1\"\nC;Y1;X11;K\"DataC1\"\nC;Y1;X12;K\"DataD1\"\n"
		"C;Y2;X1;K\"Aprg\"\nC;Y2;X2;K\"Aprg\"\nC;Y2;X3;K\"1\"\n"
		"C;Y2;X4;K\"air,ground,enemy\"\nC;Y2;X5;K\"75\"\nC;Y2;X6;K\"700\"\n"
		"C;Y2;X7;K\"12\"\nC;Y2;X8;K\"4\"\nC;Y2;X9;K\"Bprg\"\n"
		"C;Y2;X10;K\"0.5\"\nC;Y2;X11;K\"180\"\nC;Y2;X12;K\"0\"\nE\n";
	PURGEFIX fix = purge_setup(slk, BZ_APRG);
	LPEDICT wp;

	T_ASSERT(S_CastUnitTargetSpell(fix.caster, BZ_APRG, fix.enemy));
	T_EQ(G_UnitStatusLevel(fix.enemy, BZ_BPRG), 1);
	T_FEQ(S_PurgeMoveReduction(fix.enemy), 0.5f, 0.001f);
	T_ASSERT(!S_PurgeIsImmobilized(fix.enemy));
	wp = Waypoint_add(&(VECTOR2){200, 0});
	order_move(fix.enemy, wp);
	T_ASSERT(fix.enemy->goalentity == wp);
	purge_done(fix);
}

/* Apg2 DataD immobilizes ordinary units; after pause, DataA slow remains until Dur. */
TEST(wc3_spell, purge_apg2_immobilizes_for_datad_then_slows) {
	PURGEFIX fix = purge_setup(PURGE_APG2_SLK, BZ_APG2);
	LPEDICT wp;

	T_ASSERT(S_CastUnitTargetSpell(fix.caster, BZ_APG2, fix.enemy));
	T_EQ(G_UnitStatusLevel(fix.enemy, BZ_BPRG), 1);
	T_ASSERT(S_PurgeIsImmobilized(fix.enemy));
	T_FEQ(S_PurgeMoveReduction(fix.enemy), 1.0f, 0.001f);
	wp = Waypoint_add(&(VECTOR2){200, 0});
	fix.enemy->goalentity = NULL;
	order_move(fix.enemy, wp);
	T_ASSERT(fix.enemy->goalentity != wp);

	level.time += 2000; /* DataD=2s pause ends; Bprg slow remains */
	T_ASSERT(!S_PurgeIsImmobilized(fix.enemy));
	T_EQ(G_UnitStatusLevel(fix.enemy, BZ_BPRG), 1);
	T_FEQ(S_PurgeMoveReduction(fix.enemy), 0.5f, 0.001f);
	order_move(fix.enemy, wp);
	T_ASSERT(fix.enemy->goalentity == wp);

	level.time += 12000; unit_updatestatuses(fix.enemy);
	T_EQ(G_UnitStatusLevel(fix.enemy, BZ_BPRG), 0);
	T_FEQ(S_PurgeMoveReduction(fix.enemy), 0.0f, 0.001f);
	purge_done(fix);
}

/* Heroes use DataE pause length, not DataD. */
TEST(wc3_spell, purge_apg2_hero_uses_datae_pause) {
	static UnitBalance_t hero_bal = { .strength = 1 };
	PURGEFIX fix = purge_setup(PURGE_APG2_SLK, BZ_APG2);
	LPEDICT hero = alloc_test_unit(MAKEFOURCC('O','g','r','h'), 160, 0);
	LPEDICT wp;

	hero->s.player = 1; hero->svflags |= SVF_MONSTER; hero->targtype = TARG_GROUND;
	hero->data.UnitBalance = &hero_bal;
	hero->health.value = hero->health.max_value = 500;
	hero->stand = unit_stand; unit_stand(hero);
	T_ASSERT(G_UnitIsHero(hero));

	T_ASSERT(S_CastUnitTargetSpell(fix.caster, BZ_APG2, hero));
	T_ASSERT(S_PurgeIsImmobilized(hero));
	wp = Waypoint_add(&(VECTOR2){220, 0});
	order_move(hero, wp);
	T_ASSERT(hero->goalentity != wp);

	level.time += 1000; /* DataE=1s */
	T_ASSERT(!S_PurgeIsImmobilized(hero));
	T_FEQ(S_PurgeMoveReduction(hero), 0.5f, 0.001f);
	order_move(hero, wp);
	T_ASSERT(hero->goalentity == wp);
	purge_done(fix);
}

/* Apg2 still deals authored DataC to summoned units (owner set). */
TEST(wc3_spell, purge_apg2_damages_summoned_with_datac) {
	PURGEFIX fix = purge_setup(PURGE_APG2_SLK, BZ_APG2);
	LPEDICT summon = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 128, 0);
	summon->s.player = 1; summon->svflags |= SVF_MONSTER; summon->targtype = TARG_GROUND;
	summon->health.value = summon->health.max_value = 500;
	summon->owner = fix.caster;
	summon->stand = unit_stand; unit_stand(summon);
	T_ASSERT(S_CastUnitTargetSpell(fix.caster, BZ_APG2, summon));
	T_ASSERT(summon->health.value < 500);
	T_ASSERT(S_PurgeIsImmobilized(summon));
	purge_done(fix);
}

/* Zero DataA is a full stop after any pause window (fixture contract, not stock 5). */
TEST(wc3_spell, purge_apg2_zero_dataa_is_full_move_reduction_after_pause) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X14\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
		"C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"Cost1\"\nC;Y1;X6;K\"Rng1\"\n"
		"C;Y1;X7;K\"Dur1\"\nC;Y1;X8;K\"HeroDur1\"\nC;Y1;X9;K\"BuffID1\"\n"
		"C;Y1;X10;K\"DataA1\"\nC;Y1;X11;K\"DataC1\"\n"
		"C;Y1;X12;K\"DataD1\"\nC;Y1;X13;K\"DataE1\"\nC;Y1;X14;K\"Cool1\"\n"
		"C;Y2;X1;K\"Apg2\"\nC;Y2;X2;K\"Aprg\"\nC;Y2;X3;K\"1\"\n"
		"C;Y2;X4;K\"air,ground,enemy\"\nC;Y2;X5;K\"75\"\nC;Y2;X6;K\"700\"\n"
		"C;Y2;X7;K\"12\"\nC;Y2;X8;K\"4\"\nC;Y2;X9;K\"Bprg\"\n"
		"C;Y2;X10;K\"0\"\nC;Y2;X11;K\"180\"\nC;Y2;X12;K\"1\"\n"
		"C;Y2;X13;K\"1\"\nC;Y2;X14;K\"0\"\nE\n";
	PURGEFIX fix = purge_setup(slk, BZ_APG2);
	T_ASSERT(S_CastUnitTargetSpell(fix.caster, BZ_APG2, fix.enemy));
	T_FEQ(S_PurgeMoveReduction(fix.enemy), 1.0f, 0.001f);
	level.time += 1000;
	T_ASSERT(!S_PurgeIsImmobilized(fix.enemy));
	T_FEQ(S_PurgeMoveReduction(fix.enemy), 1.0f, 0.001f);
	purge_done(fix);
}

/* After pause, DataA slow is full at t=0 then weaker later in Dur (non-stock 0.4, not 5). */
TEST(wc3_spell, purge_gradual_recovery_weakens_after_pause) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X14\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
		"C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"Cost1\"\nC;Y1;X6;K\"Rng1\"\n"
		"C;Y1;X7;K\"Dur1\"\nC;Y1;X8;K\"HeroDur1\"\nC;Y1;X9;K\"BuffID1\"\n"
		"C;Y1;X10;K\"DataA1\"\nC;Y1;X11;K\"DataC1\"\n"
		"C;Y1;X12;K\"DataD1\"\nC;Y1;X13;K\"DataE1\"\nC;Y1;X14;K\"Cool1\"\n"
		"C;Y2;X1;K\"Apg2\"\nC;Y2;X2;K\"Aprg\"\nC;Y2;X3;K\"1\"\n"
		"C;Y2;X4;K\"air,ground,enemy\"\nC;Y2;X5;K\"75\"\nC;Y2;X6;K\"700\"\n"
		"C;Y2;X7;K\"10\"\nC;Y2;X8;K\"4\"\nC;Y2;X9;K\"Bprg\"\n"
		"C;Y2;X10;K\"0.4\"\nC;Y2;X11;K\"180\"\nC;Y2;X12;K\"2\"\n"
		"C;Y2;X13;K\"1\"\nC;Y2;X14;K\"0\"\nE\n";
	PURGEFIX fix = purge_setup(slk, BZ_APG2);
	FLOAT early, late;

	T_ASSERT(S_CastUnitTargetSpell(fix.caster, BZ_APG2, fix.enemy));
	T_ASSERT(S_PurgeIsImmobilized(fix.enemy));
	level.time += 2000; /* DataD pause ends; initial reduction = 1 - 0.4 = 0.6 */
	T_ASSERT(!S_PurgeIsImmobilized(fix.enemy));
	early = S_PurgeMoveReduction(fix.enemy);
	T_FEQ(early, 0.6f, 0.001f);

	level.time += 4000; /* halfway through remaining 8s slow window */
	late = S_PurgeMoveReduction(fix.enemy);
	T_ASSERT(late < early - 0.05f);
	T_ASSERT(late > 0.05f);
	purge_done(fix);
}

#endif
