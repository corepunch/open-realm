#ifdef BZ_TESTS
#include "test.h"
#include "../skills/s_skills.h"

#define BZ_ASPL MAKEFOURCC('A', 's', 'p', 'l') // rawcode; Spirit Link
#define BZ_BSPL MAKEFOURCC('B', 's', 'p', 'l') // rawcode; Spirit Link buff

LPEDICT alloc_test_unit(DWORD class_id, FLOAT x, FLOAT y);
void reset_entities(void);
void setup_test_world(void);
slkTestData_t *parse_slk_string(const char *text);
void free_slk_rows(slkTestData_t *rows);

/* Non-stock DataA=0.25 / DataB=3 / Cost=40 so tests cannot pass on retail 0.5/4/75. */
#define SPL_SLK \
	"ID;PWXL;N;EBB;Y2;X14\n" \
	"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n" \
	"C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"Cost1\"\nC;Y1;X6;K\"Cool1\"\n" \
	"C;Y1;X7;K\"Rng1\"\nC;Y1;X8;K\"Dur1\"\nC;Y1;X9;K\"HeroDur1\"\n" \
	"C;Y1;X10;K\"Area1\"\nC;Y1;X11;K\"DataA1\"\nC;Y1;X12;K\"DataB1\"\n" \
	"C;Y1;X13;K\"BuffID1\"\nC;Y1;X14;K\"DataC1\"\n" \
	"C;Y2;X1;K\"Aspl\"\nC;Y2;X2;K\"Aspl\"\nC;Y2;X3;K\"1\"\n" \
	"C;Y2;X4;K\"air,ground,friend,self,organic\"\n" \
	"C;Y2;X5;K\"40\"\nC;Y2;X6;K\"0\"\nC;Y2;X7;K\"750\"\n" \
	"C;Y2;X8;K\"20\"\nC;Y2;X9;K\"20\"\nC;Y2;X10;K\"300\"\n" \
	"C;Y2;X11;K\"0.25\"\nC;Y2;X12;K\"3\"\nC;Y2;X13;K\"Bspl\"\nC;Y2;X14;K\"0\"\nE\n"

typedef struct {
	slkTestData_t *rows, *old;
	LPEDICT caster, a, b, c, d, enemy;
} SPLFIX;

/* Fill caller in place; no UnitBalance pointers required. */
static void spl_setup(SPLFIX *fix) {
	reset_entities(); setup_test_world(); level.time = 1000;
	((LPMAPINFO)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
	((LPMAPINFO)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
	memset(level.alliances, 0, sizeof(level.alliances));
	fix->rows = parse_slk_string(SPL_SLK); fix->old = G_SetSLKRows("AbilityData", fix->rows);
	/* Caster within Rng=750 but outside Area=300 of A so DataB picks A/B/C, not the walker. */
	fix->caster = alloc_test_unit(MAKEFOURCC('o', 's', 'h', 'm'), -500, 0);
	fix->a = alloc_test_unit(MAKEFOURCC('o', 'g', 'r', 'u'), 0, 0);
	fix->b = alloc_test_unit(MAKEFOURCC('o', 'g', 'r', 'u'), 32, 0);
	fix->c = alloc_test_unit(MAKEFOURCC('o', 'g', 'r', 'u'), 64, 0);
	fix->d = alloc_test_unit(MAKEFOURCC('o', 'g', 'r', 'u'), 400, 0); /* outside Area=300 from a */
	fix->enemy = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), 48, 0);
	fix->caster->s.player = fix->a->s.player = fix->b->s.player = fix->c->s.player = fix->d->s.player = 0;
	fix->enemy->s.player = 1;
	fix->caster->svflags |= SVF_MONSTER; fix->a->svflags |= SVF_MONSTER; fix->b->svflags |= SVF_MONSTER;
	fix->c->svflags |= SVF_MONSTER; fix->d->svflags |= SVF_MONSTER; fix->enemy->svflags |= SVF_MONSTER;
	fix->caster->targtype = fix->a->targtype = fix->b->targtype = fix->c->targtype =
		fix->d->targtype = fix->enemy->targtype = TARG_GROUND;
	fix->caster->heroabilities[0] = MAKE(heroability_t, .code = BZ_ASPL, .level = 1);
	fix->caster->mana.value = fix->caster->mana.max_value = 100;
	fix->a->health.value = fix->a->health.max_value = 500;
	fix->b->health.value = fix->b->health.max_value = 500;
	fix->c->health.value = fix->c->health.max_value = 500;
	fix->d->health.value = fix->d->health.max_value = 500;
	fix->enemy->health.value = fix->enemy->health.max_value = 500;
}

static void spl_done(SPLFIX *fix) { G_SetSLKRows("AbilityData", fix->old); free_slk_rows(fix->rows); }

TEST(wc3_spell, spirit_link_procedure_lookup) {
	T_EQ(S_AbilityItem(BZ_ASPL).ability->proc, CAbilitySpiritLink);
}

/* Cast on A links nearest DataB=3 allies in Area (A,B,C); D outside area stays unlinked. */
TEST(wc3_spell, spirit_link_links_datab_nearest_in_area) {
	SPLFIX fix; spl_setup(&fix);
	T_ASSERT(S_SpellAllowsTarget(BZ_ASPL, fix.caster, fix.a));
	T_ASSERT(!S_SpellAllowsTarget(BZ_ASPL, fix.caster, fix.enemy));
	T_ASSERT(S_CastUnitTargetSpell(fix.caster, BZ_ASPL, fix.a));
	T_FEQ(fix.caster->mana.value, 60, 0.001f);
	T_EQ(G_UnitStatusLevel(fix.a, BZ_BSPL), 1);
	T_EQ(G_UnitStatusLevel(fix.b, BZ_BSPL), 1);
	T_EQ(G_UnitStatusLevel(fix.c, BZ_BSPL), 1);
	T_EQ(G_UnitStatusLevel(fix.d, BZ_BSPL), 0);
	T_EQ(G_UnitStatusLevel(fix.enemy, BZ_BSPL), 0);
	spl_done(&fix);
}

/* DataA=0.25 with 3 linked: 100 dmg -> shared 25 / 3 each; primary keeps 75+portion. */
TEST(wc3_spell, spirit_link_splits_authored_damage_among_linked) {
	SPLFIX fix; spl_setup(&fix);
	int portion, primary;
	T_ASSERT(S_CastUnitTargetSpell(fix.caster, BZ_ASPL, fix.a));
	T_Damage(fix.a, fix.enemy, 100);
	portion = 25 / 3;
	primary = 75 + portion;
	T_FEQ(fix.a->health.value, 500 - primary, 0.001f);
	T_FEQ(fix.b->health.value, 500 - portion, 0.001f);
	T_FEQ(fix.c->health.value, 500 - portion, 0.001f);
	T_FEQ(fix.d->health.value, 500, 0.001f);
	T_FEQ(fix.enemy->health.value, 500, 0.001f);
	spl_done(&fix);
}

/* Unlinked unit takes full T_Damage; linked group is untouched. */
TEST(wc3_spell, spirit_link_unlinked_unit_takes_full_damage) {
	SPLFIX fix; spl_setup(&fix);
	T_ASSERT(S_CastUnitTargetSpell(fix.caster, BZ_ASPL, fix.a));
	T_Damage(fix.d, fix.enemy, 40);
	T_FEQ(fix.d->health.value, 460, 0.001f);
	T_FEQ(fix.a->health.value, 500, 0.001f);
	T_FEQ(fix.b->health.value, 500, 0.001f);
	spl_done(&fix);
}

/* After Dur expiry, damage no longer redirects. */
TEST(wc3_spell, spirit_link_expires_and_stops_redirect) {
	SPLFIX fix; spl_setup(&fix);
	T_ASSERT(S_CastUnitTargetSpell(fix.caster, BZ_ASPL, fix.a));
	level.time += 20000; unit_updatestatuses(fix.a); unit_updatestatuses(fix.b); unit_updatestatuses(fix.c);
	T_EQ(G_UnitStatusLevel(fix.a, BZ_BSPL), 0);
	T_Damage(fix.a, fix.enemy, 50);
	T_FEQ(fix.a->health.value, 450, 0.001f);
	T_FEQ(fix.b->health.value, 500, 0.001f);
	spl_done(&fix);
}

/* Recast refreshes Bspl duration on the newly gathered group. */
TEST(wc3_spell, spirit_link_recast_refreshes_group) {
	SPLFIX fix; spl_setup(&fix);
	T_ASSERT(S_CastUnitTargetSpell(fix.caster, BZ_ASPL, fix.a));
	level.time += 10000; unit_updatestatuses(fix.a);
	T_ASSERT(G_UnitStatusLevel(fix.a, BZ_BSPL) == 1);
	fix.caster->mana.value = 100;
	T_ASSERT(S_CastUnitTargetSpell(fix.caster, BZ_ASPL, fix.a));
	level.time += 10000; unit_updatestatuses(fix.a); unit_updatestatuses(fix.b);
	T_EQ(G_UnitStatusLevel(fix.a, BZ_BSPL), 1);
	T_EQ(G_UnitStatusLevel(fix.b, BZ_BSPL), 1);
	level.time += 20000; unit_updatestatuses(fix.a); unit_updatestatuses(fix.b);
	T_EQ(G_UnitStatusLevel(fix.a, BZ_BSPL), 0);
	spl_done(&fix);
}

/* Redirected share that would kill clamps to 1 HP and clears Bspl. */
TEST(wc3_spell, spirit_link_redirect_never_fatal) {
	SPLFIX fix; spl_setup(&fix);
	T_ASSERT(S_CastUnitTargetSpell(fix.caster, BZ_ASPL, fix.a));
	fix.b->health.value = 5;
	T_Damage(fix.a, fix.enemy, 100);
	T_ASSERT(fix.b->health.value >= 1.0f);
	T_ASSERT(!M_IsDead(fix.b));
	T_EQ(G_UnitStatusLevel(fix.b, BZ_BSPL), 0);
	spl_done(&fix);
}

#endif
