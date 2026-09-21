#ifdef BZ_TESTS
#include "test.h"
#include "../skills/s_skills.h"

#define BZ_AEXH MAKEFOURCC('A', 'e', 'x', 'h') // rawcode; Exhume Corpses (TFT Meat Wagon)
#define BZ_AMEL MAKEFOURCC('A', 'm', 'e', 'l')
#define BZ_AMTC MAKEFOURCC('A', 'm', 't', 'c')
#define BZ_SCH2 MAKEFOURCC('S', 'c', 'h', '2')
#define BZ_HFOO MAKEFOURCC('h', 'f', 'o', 'o') // unitCode; non-stock fixture corpse UnitID

LPEDICT alloc_test_unit(DWORD class_id, FLOAT x, FLOAT y);
void reset_entities(void);
void setup_test_world(void);
slkTestData_t *parse_slk_string(const char *text);
void free_slk_rows(slkTestData_t *rows);

typedef struct { slkTestData_t *rows, *old, *unit_rows, *old_units; LPEDICT wagon; } EXHFIX;

/* Non-stock Dur=2 / DataA=2 / UnitID=hfoo prove the update path is data-driven. */
static char const exh_slk[] =
	"ID;PWXL;N;EBB;Y2;X10\n"
	"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\nC;Y1;X4;K\"targs\"\n"
	"C;Y1;X5;K\"Cost1\"\nC;Y1;X6;K\"Cool1\"\nC;Y1;X7;K\"Rng1\"\nC;Y1;X8;K\"Dur1\"\n"
	"C;Y1;X9;K\"DataA1\"\nC;Y1;X10;K\"UnitID1\"\n"
	"C;Y2;X1;K\"Aexh\"\nC;Y2;X2;K\"Aexh\"\nC;Y2;X3;K\"1\"\nC;Y2;X4;K\"_\"\n"
	"C;Y2;X5;K\"0\"\nC;Y2;X6;K\"0\"\nC;Y2;X7;K\"0\"\nC;Y2;X8;K\"2\"\n"
	"C;Y2;X9;K\"2\"\nC;Y2;X10;K\"hfoo\"\n"
	"C;Y3;X1;K\"Amel\"\nC;Y3;X2;K\"Amel\"\nC;Y3;X3;K\"1\"\nC;Y3;X4;K\"ground,dead,nonhero\"\n"
	"C;Y3;X5;K\"0\"\nC;Y3;X6;K\"0\"\nC;Y3;X7;K\"100\"\nC;Y3;X8;K\"0\"\nC;Y3;X9;K\"0\"\n"
	"C;Y4;X1;K\"Sch2\"\nC;Y4;X2;K\"Amtc\"\nC;Y4;X3;K\"1\"\nC;Y4;X4;K\"dead\"\n"
	"C;Y4;X5;K\"0\"\nC;Y4;X6;K\"0\"\nC;Y4;X7;K\"160\"\nC;Y4;X8;K\"0\"\nC;Y4;X9;K\"8\"\nE\n";

static char const exh_unit_slk[] =
	"ID;PWXL;N;EBB;Y2;X3\n"
	"C;Y1;X1;K\"unitID\"\nC;Y1;X2;K\"deathType\"\nC;Y1;X3;K\"targType\"\n"
	"C;Y2;X1;K\"hfoo\"\nC;Y2;X2;K3\nC;Y2;X3;K\"ground\"\nE\n";

/* Fill in place: fixture must not return-by-value when pointing at local SLK state. */
static void exh_setup(EXHFIX *fix) {
	reset_entities(); setup_test_world(); level.time = 1000;
	((LPMAPINFO)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
	fix->rows = parse_slk_string(exh_slk); fix->old = G_SetSLKRows("AbilityData", fix->rows);
	fix->unit_rows = parse_slk_string(exh_unit_slk); fix->old_units = G_SetSLKRows("UnitData", fix->unit_rows);
	fix->wagon = alloc_test_unit(MAKEFOURCC('u', 'm', 't', 'w'), 100, 100);
	fix->wagon->s.player = 0; fix->wagon->svflags |= SVF_MONSTER; fix->wagon->targtype = TARG_GROUND;
	fix->wagon->health.value = fix->wagon->health.max_value = 500;
	fix->wagon->heroabilities[0] = MAKE(heroability_t, .code = BZ_AEXH, .level = 1);
	fix->wagon->heroabilities[1] = MAKE(heroability_t, .code = BZ_AMEL, .level = 1);
	fix->wagon->heroabilities[2] = MAKE(heroability_t, .code = BZ_SCH2, .level = 1);
	G_ActorAddSkill(fix->wagon, BZ_AMEL);
	G_ActorAddSkill(fix->wagon, BZ_SCH2);
	fix->wagon->think = monster_think;
}

static void exh_done(EXHFIX *fix) {
	G_SetSLKRows("AbilityData", fix->old); G_SetSLKRows("UnitData", fix->old_units);
	free_slk_rows(fix->rows); free_slk_rows(fix->unit_rows);
}

static void exh_tick(DWORD ms) { level.time += ms; G_RunEntities(); }

static DWORD exh_corpse_count(LPEDICT wagon) {
	DWORD n = 0;
	FOR_LOOP(i, wagon->cargo.count) {
		LPEDICT ent = S_CargoUnitAt(wagon, i);
		if (ent && ent->class_id == BZ_HFOO && S_CorpseCargoIsStored(ent)) n++;
	}
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
	FOR_LOOP(i, fix.wagon->cargo.count) {
		LPEDICT ent = S_CargoUnitAt(fix.wagon, i);
		if (ent && ent->class_id == BZ_HFOO && S_CorpseCargoIsStored(ent) && !corpse) corpse = ent;
	}
	T_NOT_NULL(corpse);
	T_FEQ(corpse->s.origin2.x, fix.wagon->s.origin2.x, 0.001f);
	T_FEQ(corpse->s.origin2.y, fix.wagon->s.origin2.y, 0.001f);
	T_ASSERT(S_CorpseCargoIsStored(corpse)); T_ASSERT(corpse->paused);
	T_EQ((int)S_CargoCapacity(fix.wagon), 8);
	T_ASSERT(S_CargoUnloadAt(fix.wagon, 0)); T_EQ(fix.wagon->cargo.count, 0);
	exh_done(&fix);
}

TEST(wc3_spell, corpse_cargo_effective_position_tracks_moving_holder) {
    EXHFIX fix; LPEDICT corpse = NULL; VECTOR2 effective;
    exh_setup(&fix);
    S_RunAbilityUpdates(fix.wagon);
    exh_tick(2000);
    FOR_LOOP(i, fix.wagon->cargo.count) {
        LPEDICT ent = S_CargoUnitAt(fix.wagon, i);
        if (ent && S_CorpseCargoIsStored(ent)) { corpse = ent; break; }
    }
    T_NOT_NULL(corpse);
    if (corpse) {
        fix.wagon->s.origin2 = (VECTOR2){ 420.0f, 315.0f };
        fix.wagon->s.origin.x = 420.0f; fix.wagon->s.origin.y = 315.0f;
        T_ASSERT(S_CorpseCargoPosition(corpse, &effective));
        T_FEQ(effective.x, 420.0f, 0.001f); T_FEQ(effective.y, 315.0f, 0.001f);
    }
    exh_done(&fix);
}

TEST(wc3_spell, unloading_bone_phase_corpse_restarts_bone_decay_time) {
    EXHFIX fix; LPEDICT corpse;
    exh_setup(&fix);
    game.constants.decayTime = (FLOAT)FRAMETIME / 1000.0f;
    game.constants.boneDecayTime = 2.75f;
    corpse = alloc_test_unit(BZ_HFOO, fix.wagon->s.origin2.x, fix.wagon->s.origin2.y);
    corpse->s.player = fix.wagon->s.player; corpse->svflags |= SVF_MONSTER | SVF_DEADMONSTER;
    corpse->targtype = TARG_GROUND;
    corpse->health.value = 0.0f;
    unit_begin_decay(corpse);
    T_NOT_NULL(corpse->currentmove);
    if (corpse->currentmove && corpse->currentmove->think) corpse->currentmove->think(corpse);
    T_FEQ(corpse->wait, 2.75f, 0.001f);
    corpse->wait = 0.25f;
    T_ASSERT(S_CorpseCargoTryLoad(fix.wagon, corpse));
    T_ASSERT(S_CargoUnloadAt(fix.wagon, 0));
    T_FEQ(corpse->wait, 2.75f, 0.001f);
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

TEST(wc3_spell, graveyard_uses_cool_dataa_datab_datac_unitid) {
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
    {
        LPEDICT first = NULL;
        FILTER_EDICTS(ent, ent->inuse && ent->class_id == BZ_HFOO && M_IsDead(ent)) { first = ent; break; }
        T_NOT_NULL(first);
        if (first) {
            /* Gyd2/DataB=64 owns placement while Gyd3/DataC=128 owns the
             * nearby-corpse cap.  Move the first corpse outside Gyd2 but keep
             * it inside Gyd3; it must still count against DataA. */
            T_FEQ(Vector2_distance(&first->s.origin2, &graveyard->s.origin2), 64.0f, 0.01f);
            first->s.origin2 = (VECTOR2){ graveyard->s.origin2.x + 100.0f, graveyard->s.origin2.y };
            first->s.origin.x = first->s.origin2.x; first->s.origin.y = first->s.origin2.y;
        }
    }
    level.time += 1000; graveyard_think(thinker); T_EQ(graveyard_test_corpse_count(graveyard), 2);
    level.time += 1000; graveyard_think(thinker); T_EQ(graveyard_test_corpse_count(graveyard), 2);

    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

#endif
