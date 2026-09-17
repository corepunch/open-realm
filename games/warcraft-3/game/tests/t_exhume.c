#ifdef BZ_TESTS
#include "test.h"
#include "../skills/s_skills.h"

#define BZ_AEXH MAKEFOURCC('A', 'e', 'x', 'h') // rawcode; Exhume Corpses (TFT Meat Wagon)
#define BZ_HFOO MAKEFOURCC('h', 'f', 'o', 'o') // unitCode; non-stock fixture corpse UnitID

LPEDICT alloc_test_unit(DWORD class_id, FLOAT x, FLOAT y);
void reset_entities(void);
void setup_test_world(void);
slkTestData_t *parse_slk_string(const char *text);
void free_slk_rows(slkTestData_t *rows);

typedef struct { slkTestData_t *rows, *old; LPEDICT wagon; } EXHFIX;

/* Non-stock Dur=2 / DataA=2 / UnitID=hfoo prove the update path is data-driven. */
static char const exh_slk[] =
	"ID;PWXL;N;EBB;Y2;X10\n"
	"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\nC;Y1;X4;K\"targs\"\n"
	"C;Y1;X5;K\"Cost1\"\nC;Y1;X6;K\"Cool1\"\nC;Y1;X7;K\"Rng1\"\nC;Y1;X8;K\"Dur1\"\n"
	"C;Y1;X9;K\"DataA1\"\nC;Y1;X10;K\"UnitID1\"\n"
	"C;Y2;X1;K\"Aexh\"\nC;Y2;X2;K\"Aexh\"\nC;Y2;X3;K\"1\"\nC;Y2;X4;K\"_\"\n"
	"C;Y2;X5;K\"0\"\nC;Y2;X6;K\"0\"\nC;Y2;X7;K\"0\"\nC;Y2;X8;K\"2\"\n"
	"C;Y2;X9;K\"2\"\nC;Y2;X10;K\"hfoo\"\nE\n";

/* Fill in place: fixture must not return-by-value when pointing at local SLK state. */
static void exh_setup(EXHFIX *fix) {
	reset_entities(); setup_test_world(); level.time = 1000;
	((LPMAPINFO)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
	fix->rows = parse_slk_string(exh_slk); fix->old = G_SetSLKRows("AbilityData", fix->rows);
	fix->wagon = alloc_test_unit(MAKEFOURCC('u', 'm', 't', 'w'), 100, 100);
	fix->wagon->s.player = 0; fix->wagon->svflags |= SVF_MONSTER; fix->wagon->targtype = TARG_GROUND;
	fix->wagon->health.value = fix->wagon->health.max_value = 500;
	fix->wagon->heroabilities[0] = MAKE(heroability_t, .code = BZ_AEXH, .level = 1);
	fix->wagon->think = monster_think;
}

static void exh_done(EXHFIX *fix) { G_SetSLKRows("AbilityData", fix->old); free_slk_rows(fix->rows); }

static void exh_tick(DWORD ms) { level.time += ms; G_RunEntities(); }

static DWORD exh_corpse_count(LPEDICT wagon) {
	DWORD n = 0;
	FILTER_EDICTS(ent, ent->inuse && ent->owner == wagon && ent->class_id == BZ_HFOO && M_IsDead(ent)) n++;
	return n;
}

static LPEDICT exh_thinker(LPEDICT wagon) {
	FILTER_EDICTS(ent, ent->inuse && ent->owner == wagon && ent->think && !ent->class_id) return ent;
	return NULL;
}

TEST(wc3_spell, exhume_registers_passive_update_procedure) {
	abilityitem_t item = S_AbilityItem(BZ_AEXH);
	T_NOT_NULL(item.ability);
	T_EQ(item.ability->proc, CAbilityExhumeCorpses);
	T_ASSERT(item.ability->flags & AB_PASSIVE);
	T_ASSERT(item.ability->flags & AB_UPDATE);
}

/* First pulse waits a full Dur; then one authored UnitID corpse appears near the wagon. */
TEST(wc3_spell, exhume_spawns_corpse_after_dur_interval) {
	EXHFIX fix; LPEDICT corpse;
	exh_setup(&fix);
	S_RunAbilityUpdates(fix.wagon);
	T_NOT_NULL(exh_thinker(fix.wagon));
	T_EQ(exh_corpse_count(fix.wagon), 0);
	exh_tick(1999); T_EQ(exh_corpse_count(fix.wagon), 0);
	exh_tick(1); T_EQ(exh_corpse_count(fix.wagon), 1);
	corpse = NULL;
	FILTER_EDICTS(ent, ent->inuse && ent->owner == fix.wagon && ent->class_id == BZ_HFOO && M_IsDead(ent))
		if (!corpse) corpse = ent;
	T_NOT_NULL(corpse);
	T_FEQ(corpse->s.origin2.x, fix.wagon->s.origin2.x, 0.001f);
	T_FEQ(corpse->s.origin2.y, fix.wagon->s.origin2.y, 0.001f);
	exh_done(&fix);
}

/* DataA caps how many owned corpses the wagon keeps; further pulses wait until under cap. */
TEST(wc3_spell, exhume_respects_dataa_corpse_cap) {
	EXHFIX fix;
	exh_setup(&fix);
	S_RunAbilityUpdates(fix.wagon);
	exh_tick(2000); T_EQ(exh_corpse_count(fix.wagon), 1);
	exh_tick(2000); T_EQ(exh_corpse_count(fix.wagon), 2);
	exh_tick(2000); T_EQ(exh_corpse_count(fix.wagon), 2);
	exh_done(&fix);
}

/* Freeing the wagon cancels its thinker so a later pulse cannot spawn. */
TEST(wc3_spell, exhume_wagon_removal_cancels_production) {
	EXHFIX fix; LPEDICT thinker;
	exh_setup(&fix);
	S_RunAbilityUpdates(fix.wagon);
	thinker = exh_thinker(fix.wagon); T_NOT_NULL(thinker);
	G_FreeEdict(fix.wagon);
	exh_tick(2000);
	T_ASSERT(!thinker->inuse);
	T_EQ(exh_corpse_count(fix.wagon), 0);
	exh_done(&fix);
}

#endif
