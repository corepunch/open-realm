#ifdef BZ_TESTS
#include "test.h"
#include "../skills/s_skills.h"

#define BZ_ANSY MAKEFOURCC('A', 'N', 's', 'y') // rawcode; Pocket Factory (Tinker)
#define BZ_ANS1 MAKEFOURCC('A', 'N', 's', '1') // rawcode; Pocket Factory alias, faster DataA
#define BZ_ANS2 MAKEFOURCC('A', 'N', 's', '2') // rawcode; Pocket Factory alias L2 interval table
#define BZ_ANS3 MAKEFOURCC('A', 'N', 's', '3') // rawcode; Pocket Factory alias L3 interval table
#define BZ_BTLF MAKEFOURCC('B', 'T', 'L', 'F') // rawcode; timed-life status on factory and Clockwerks
#define BZ_HFOO MAKEFOURCC('h', 'f', 'o', 'o') // unitCode; fixture factory UnitID (non-stock)
#define BZ_OGRU MAKEFOURCC('o', 'g', 'r', 'u') // unitCode; fixture DataB Clockwerk (non-stock)

LPEDICT alloc_test_unit(DWORD class_id, FLOAT x, FLOAT y);
void reset_entities(void);
void setup_test_world(void);
slkTestData_t *parse_slk_string(const char *text);
void free_slk_rows(slkTestData_t *rows);

typedef struct { slkTestData_t *rows, *old; LPEDICT caster; } PFFIX;

/* Non-stock DataA/Dur/DataC/DataE and hfoo/ogru UnitID/DataB prove the execute path is data-driven. */
static char const pf_slk[] =
    "ID;PWXL;N;EBB;Y3;X15\n"
    "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\nC;Y1;X4;K\"targs\"\n"
    "C;Y1;X5;K\"Cost1\"\nC;Y1;X6;K\"Cool1\"\nC;Y1;X7;K\"Rng1\"\nC;Y1;X8;K\"Dur1\"\n"
    "C;Y1;X9;K\"HeroDur1\"\nC;Y1;X10;K\"DataA1\"\nC;Y1;X11;K\"DataB1\"\nC;Y1;X12;K\"DataC1\"\n"
    "C;Y1;X13;K\"DataD1\"\nC;Y1;X14;K\"DataE1\"\nC;Y1;X15;K\"UnitID1\"\n"
    "C;Y2;X1;K\"ANsy\"\nC;Y2;X2;K\"ANsy\"\nC;Y2;X3;K\"1\"\nC;Y2;X4;K\"\"\n"
    "C;Y2;X5;K\"0\"\nC;Y2;X6;K\"0\"\nC;Y2;X7;K\"500\"\nC;Y2;X8;K\"25\"\n"
    "C;Y2;X9;K\"25\"\nC;Y2;X10;K\"2\"\nC;Y2;X11;K\"ogru\"\nC;Y2;X12;K\"8\"\n"
    "C;Y2;X13;K\"64\"\nC;Y2;X14;K\"200\"\nC;Y2;X15;K\"hfoo\"\n"
    "C;Y3;X1;K\"ANs1\"\nC;Y3;X2;K\"ANsy\"\nC;Y3;X3;K\"1\"\nC;Y3;X4;K\"\"\n"
    "C;Y3;X5;K\"0\"\nC;Y3;X6;K\"0\"\nC;Y3;X7;K\"500\"\nC;Y3;X8;K\"25\"\n"
    "C;Y3;X9;K\"25\"\nC;Y3;X10;K\"1\"\nC;Y3;X11;K\"ogru\"\nC;Y3;X12;K\"8\"\n"
    "C;Y3;X13;K\"64\"\nC;Y3;X14;K\"200\"\nC;Y3;X15;K\"hfoo\"\nE\n";

static PFFIX pf_setup(DWORD code) {
    PFFIX fix;
    reset_entities(); setup_test_world(); level.time = 1000;
    ((LPMAPINFO)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    fix.rows = parse_slk_string(pf_slk); fix.old = G_SetSLKRows("AbilityData", fix.rows);
    fix.caster = alloc_test_unit(MAKEFOURCC('N', 't', 'i', 'n'), 0, 0);
    fix.caster->s.player = 0; fix.caster->svflags |= SVF_MONSTER; fix.caster->targtype = TARG_GROUND;
    fix.caster->mana.value = fix.caster->mana.max_value = 500;
    fix.caster->health.value = fix.caster->health.max_value = 1000;
    fix.caster->heroabilities[0] = MAKE(heroability_t, .code = code, .level = 1);
    return fix;
}

static void pf_done(PFFIX fix) { G_SetSLKRows("AbilityData", fix.old); free_slk_rows(fix.rows); }

static void pf_tick(DWORD ms) { level.time += ms; G_RunEntities(); }

static LPEDICT pf_find(DWORD code) {
    FILTER_EDICTS(ent, ent->inuse && ent->class_id == code) return ent;
    return NULL;
}

static DWORD pf_count(DWORD code) {
    DWORD n = 0;
    FILTER_EDICTS(ent, ent->inuse && ent->class_id == code) n++;
    return n;
}

static LPEDICT pf_thinker(LPEDICT factory) {
    FILTER_EDICTS(ent, ent->inuse && ent->owner == factory && ent->think && !ent->class_id) return ent;
    return NULL;
}

TEST(wc3_spell, pocket_factory_aliases_share_procedure) {
    T_EQ(S_AbilityItem(BZ_ANSY).ability->proc, CAbilityPocketFactory);
    T_EQ(S_AbilityItem(BZ_ANS1).ability->proc, CAbilityPocketFactory);
    T_EQ(S_AbilityItem(BZ_ANS2).ability->proc, CAbilityPocketFactory);
    T_EQ(S_AbilityItem(BZ_ANS3).ability->proc, CAbilityPocketFactory);
}

/* Point cast summons the authored UnitID at the point with Dur as BTLF; goblins wait for DataA. */
TEST(wc3_spell, pocket_factory_cast_creates_owned_factory_and_spawns_on_interval) {
    PFFIX fix = pf_setup(BZ_ANSY);
    VECTOR2 point = { 256, 192 };
    LPEDICT factory, thinker, first;
    T_ASSERT(S_CastPointTargetSpell(fix.caster, BZ_ANSY, &point));
    factory = pf_find(BZ_HFOO); thinker = pf_thinker(factory);
    T_NOT_NULL(factory); T_NOT_NULL(thinker);
    T_EQ(factory->owner, fix.caster); T_EQ(factory->s.player, 0);
    T_FEQ(factory->s.origin2.x, point.x, .001f); T_FEQ(factory->s.origin2.y, point.y, .001f);
    T_EQ(G_UnitStatusLevel(factory, BZ_BTLF), 1);
    T_EQ(pf_count(BZ_OGRU), 0);
    pf_tick(1999); T_EQ(pf_count(BZ_OGRU), 0);
    pf_tick(1); first = pf_find(BZ_OGRU);
    T_NOT_NULL(first); T_EQ(first->owner, factory); T_EQ(first->s.player, 0);
    T_EQ(G_UnitStatusLevel(first, BZ_BTLF), 1);
    pf_tick(2000); T_EQ(pf_count(BZ_OGRU), 2);
    pf_done(fix);
}

/* DataC is each Clockwerk's timed life; expiry goes through unit_updatestatuses, not RunFrame. */
TEST(wc3_spell, pocket_factory_clockwerk_btlf_matches_datac) {
    PFFIX fix = pf_setup(BZ_ANSY);
    VECTOR2 point = { 128, 128 };
    LPEDICT goblin;
    T_ASSERT(S_CastPointTargetSpell(fix.caster, BZ_ANSY, &point));
    pf_tick(2000); goblin = pf_find(BZ_OGRU); T_NOT_NULL(goblin);
    if (goblin) goblin->health.value = goblin->health.max_value = 100;
    level.time += 7999; unit_updatestatuses(goblin);
    T_ASSERT(goblin->health.value > 0);
    level.time += 1; unit_updatestatuses(goblin);
    T_FEQ(goblin->health.value, 0, .001f);
    pf_done(fix);
}

/* Freeing the factory invalidates its classless thinker so a later DataA tick cannot spawn. */
TEST(wc3_spell, pocket_factory_factory_removal_cancels_production) {
    PFFIX fix = pf_setup(BZ_ANSY);
    VECTOR2 point = { 128, 128 };
    LPEDICT factory, thinker;
    T_ASSERT(S_CastPointTargetSpell(fix.caster, BZ_ANSY, &point));
    factory = pf_find(BZ_HFOO); thinker = pf_thinker(factory);
    T_NOT_NULL(factory); T_NOT_NULL(thinker);
    G_FreeEdict(factory);
    pf_tick(2000);
    T_EQ(pf_count(BZ_OGRU), 0); T_ASSERT(!thinker->inuse);
    pf_done(fix);
}

/* ANs1 shares CAbilityPocketFactory but reads its own DataA through abilityitem_t.code. */
TEST(wc3_spell, pocket_factory_ans1_uses_alias_dataa_interval) {
    PFFIX fix = pf_setup(BZ_ANS1);
    VECTOR2 point = { 64, 64 };
    T_ASSERT(S_CastPointTargetSpell(fix.caster, BZ_ANS1, &point));
    T_NOT_NULL(pf_find(BZ_HFOO));
    pf_tick(999); T_EQ(pf_count(BZ_OGRU), 0);
    pf_tick(1); T_EQ(pf_count(BZ_OGRU), 1);
    pf_done(fix);
}

/* Thinker allocation failure must roll the factory back instead of leaving an inert summon. */
TEST(wc3_spell, pocket_factory_thinker_alloc_failure_rolls_back_factory) {
    PFFIX fix = pf_setup(BZ_ANSY);
    VECTOR2 point = { 128, 128 };
    DWORD max_edicts = globals.max_edicts;
    globals.max_edicts = globals.num_edicts + 1;
    T_ASSERT(S_CastPointTargetSpell(fix.caster, BZ_ANSY, &point));
    globals.max_edicts = max_edicts;
    T_NULL(pf_find(BZ_HFOO));
    pf_done(fix);
}

/* DataE leash: a factory-owned Clockwerk past the authored range is ordered home. */
TEST(wc3_spell, pocket_factory_datae_leash_returns_clockwerk) {
    PFFIX fix = pf_setup(BZ_ANSY);
    VECTOR2 point = { 256, 192 };
    LPEDICT factory, goblin, thinker;
    T_ASSERT(S_CastPointTargetSpell(fix.caster, BZ_ANSY, &point));
    factory = pf_find(BZ_HFOO); thinker = pf_thinker(factory);
    T_NOT_NULL(factory); T_NOT_NULL(thinker);
    T_FEQ(thinker->velocity, 200.0f, .001f);
    pf_tick(2000); goblin = pf_find(BZ_OGRU); T_NOT_NULL(goblin);
    /* Fixture ogru has no UnitBalance row; give life so M_IsDead does not skip the leash. */
    goblin->health.value = goblin->health.max_value = 100;
    goblin->s.origin2.x = point.x + 400.0f; goblin->s.origin2.y = point.y;
    goblin->s.origin.x = goblin->s.origin2.x; goblin->s.origin.y = goblin->s.origin2.y;
    goblin->goalentity = NULL;
    pf_tick(1);
    T_NOT_NULL(goblin->goalentity);
    if (!goblin->goalentity) { pf_done(fix); return; }
    T_FEQ(goblin->goalentity->s.origin2.x, factory->s.origin2.x, .001f);
    T_FEQ(goblin->goalentity->s.origin2.y, factory->s.origin2.y, .001f);
    pf_done(fix);
}

#endif
