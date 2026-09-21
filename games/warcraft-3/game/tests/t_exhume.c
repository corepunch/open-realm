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

#define BZ_AGYD MAKEFOURCC('A', 'g', 'y', 'd') // rawcode; Graveyard Create Corpse

static char const graveyard_slk[] =
    "ID;PWXL;N;EBB;Y2;X9\n"
    "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
    "C;Y1;X4;K\"Cool1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"DataB1\"\n"
    "C;Y1;X7;K\"DataC1\"\nC;Y1;X8;K\"UnitID1\"\nC;Y1;X9;K\"targs\"\n"
    "C;Y2;X1;K\"Agyd\"\nC;Y2;X2;K\"Agyd\"\nC;Y2;X3;K\"1\"\n"
    "C;Y2;X4;K\"1\"\nC;Y2;X5;K\"2\"\nC;Y2;X6;K\"64\"\n"
    "C;Y2;X7;K\"128\"\nC;Y2;X8;K\"hfoo\"\nC;Y2;X9;K\"_\"\nE\n";

static LPEDICT graveyard_test_thinker(LPEDICT graveyard) {
    FILTER_EDICTS(ent, ent->inuse && ent->owner == graveyard && ent->think == graveyard_think) return ent;
    return NULL;
}

static DWORD graveyard_test_corpse_count(LPEDICT graveyard) {
    DWORD count = 0;
    FILTER_EDICTS(ent, ent->inuse && ent->class_id == BZ_HFOO && M_IsDead(ent) &&
                  Vector2_distance(&ent->s.origin2, &graveyard->s.origin2) <= 128.0f) count++;
    return count;
}

TEST(wc3_spell, graveyard_registers_and_uses_cool_dataa_datac_unitid) {
    slkTestData_t *rows, *old;
    LPEDICT graveyard, thinker;
    abilityitem_t item = S_AbilityItem(BZ_AGYD);

    reset_entities(); setup_test_world(); level.time = 1000;
    rows = parse_slk_string(graveyard_slk); old = G_SetSLKRows("AbilityData", rows);
    graveyard = alloc_test_unit(MAKEFOURCC('u','g','r','v'), 100, 100);
    graveyard->s.player = 0; graveyard->svflags |= SVF_MONSTER;
    graveyard->health.value = graveyard->health.max_value = 900;
    graveyard->heroabilities[0] = MAKE(heroability_t, .code = BZ_AGYD, .level = 1);

    T_NOT_NULL(item.ability); T_EQ(item.ability->proc, CAbilityGraveyard);
    T_ASSERT(item.ability->flags & AB_PASSIVE); T_ASSERT(item.ability->flags & AB_UPDATE);
    S_RunAbilityUpdates(graveyard);
    thinker = graveyard_test_thinker(graveyard); T_NOT_NULL(thinker);
    T_EQ(graveyard_test_corpse_count(graveyard), 0);
    level.time += 999; graveyard_think(thinker); T_EQ(graveyard_test_corpse_count(graveyard), 0);
    level.time += 1; graveyard_think(thinker); T_EQ(graveyard_test_corpse_count(graveyard), 1);
    level.time += 1000; graveyard_think(thinker); T_EQ(graveyard_test_corpse_count(graveyard), 2);
    level.time += 1000; graveyard_think(thinker); T_EQ(graveyard_test_corpse_count(graveyard), 2);

    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

#endif
