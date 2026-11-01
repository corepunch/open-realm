#ifdef BZ_TESTS
#include "test.h"
#include "../skills/s_skills.h"

#define BZ_ANSY MAKEFOURCC('A', 'N', 's', 'y') // rawcode; Pocket Factory (Tinker)
#define BZ_ANS1 MAKEFOURCC('A', 'N', 's', '1') // rawcode; Pocket Factory alias, faster DataA
#define BZ_ANS2 MAKEFOURCC('A', 'N', 's', '2') // rawcode; Pocket Factory alias L2 interval table
#define BZ_ANS3 MAKEFOURCC('A', 'N', 's', '3') // rawcode; Pocket Factory alias L3 interval table
#define BZ_BTLF MAKEFOURCC('B', 'T', 'L', 'F') // rawcode; timed-life status on factory and Clockwerks
#define BZ_ANEG MAKEFOURCC('A', 'N', 'e', 'g') // rawcode; Goblin Tinker Engineering
#define BZ_ASDG MAKEFOURCC('A', 's', 'd', 'g') // rawcode; Clockwerk Kaboom tier 1
#define BZ_ASD2 MAKEFOURCC('A', 's', 'd', '2') // rawcode; Clockwerk Kaboom tier 2
#define BZ_ASD3 MAKEFOURCC('A', 's', 'd', '3') // rawcode; Clockwerk Kaboom tier 3
#define BZ_HFOO MAKEFOURCC('h', 'f', 'o', 'o') // unitCode; fixture factory UnitID (non-stock)
#define BZ_OGRU MAKEFOURCC('o', 'g', 'r', 'u') // unitCode; fixture DataB Clockwerk (non-stock)

edict_t *alloc_test_unit(uint32_t class_id, float x, float y);
void reset_entities(void);
void setup_test_world(void);
slkTestData_t *parse_slk_string(char const *text);
void free_slk_rows(slkTestData_t *rows);

typedef struct { slkTestData_t *rows, *old; edict_t *caster; } pfFix_t;

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

/* Isolated expanded fixture for the rank-to-Kaboom integration test below. */
static char const pf_kaboom_slk[] =
    "ID;PWXL;N;EBB;Y8;X16\n"
    "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\nC;Y1;X4;K\"targs\"\n"
    "C;Y1;X5;K\"Cost1\"\nC;Y1;X6;K\"Cool1\"\nC;Y1;X7;K\"Rng1\"\nC;Y1;X8;K\"Dur1\"\n"
    "C;Y1;X9;K\"HeroDur1\"\nC;Y1;X10;K\"DataA1\"\nC;Y1;X11;K\"DataB1\"\nC;Y1;X12;K\"DataC1\"\n"
    "C;Y1;X13;K\"DataD1\"\nC;Y1;X14;K\"DataE1\"\nC;Y1;X15;K\"UnitID1\"\nC;Y1;X16;K\"DataF1\"\n"
    "C;Y2;X1;K\"ANsy\"\nC;Y2;X2;K\"ANsy\"\nC;Y2;X3;K\"1\"\nC;Y2;X4;K\"\"\n"
    "C;Y2;X5;K\"0\"\nC;Y2;X6;K\"0\"\nC;Y2;X7;K\"500\"\nC;Y2;X8;K\"25\"\n"
    "C;Y2;X9;K\"25\"\nC;Y2;X10;K\"2\"\nC;Y2;X11;K\"ogru\"\nC;Y2;X12;K\"8\"\n"
    "C;Y2;X13;K\"64\"\nC;Y2;X14;K\"200\"\nC;Y2;X15;K\"hfoo\"\n"
    "C;Y3;X1;K\"ANs1\"\nC;Y3;X2;K\"ANsy\"\nC;Y3;X3;K\"1\"\nC;Y3;X4;K\"\"\n"
    "C;Y3;X5;K\"0\"\nC;Y3;X6;K\"0\"\nC;Y3;X7;K\"500\"\nC;Y3;X8;K\"25\"\n"
    "C;Y3;X9;K\"25\"\nC;Y3;X10;K\"1\"\nC;Y3;X11;K\"ogru\"\nC;Y3;X12;K\"8\"\n"
    "C;Y3;X13;K\"64\"\nC;Y3;X14;K\"200\"\nC;Y3;X15;K\"hfoo\"\n"
    "C;Y4;X1;K\"ANs2\"\nC;Y4;X2;K\"ANsy\"\nC;Y4;X3;K\"1\"\nC;Y4;X4;K\"\"\n"
    "C;Y4;X5;K\"0\"\nC;Y4;X6;K\"0\"\nC;Y4;X7;K\"500\"\nC;Y4;X8;K\"25\"\n"
    "C;Y4;X9;K\"25\"\nC;Y4;X10;K\"1\"\nC;Y4;X11;K\"ogru\"\nC;Y4;X12;K\"8\"\n"
    "C;Y4;X13;K\"64\"\nC;Y4;X14;K\"200\"\nC;Y4;X15;K\"hfoo\"\n"
    "C;Y5;X1;K\"Asdg\"\nC;Y5;X2;K\"Asds\"\nC;Y5;X3;K\"1\"\nC;Y5;X4;K\"ground,structure,debris,enemy,neutral\"\n"
    "C;Y5;X10;K\"100\"\nC;Y5;X11;K\"30\"\nC;Y5;X12;K\"100\"\nC;Y5;X13;K\"30\"\nC;Y5;X14;K\"1\"\nC;Y5;X16;K\"1\"\n"
    "C;Y6;X1;K\"Asd2\"\nC;Y6;X2;K\"Asds\"\nC;Y6;X3;K\"1\"\nC;Y6;X4;K\"ground,structure,debris,enemy,neutral\"\n"
    "C;Y6;X10;K\"100\"\nC;Y6;X11;K\"60\"\nC;Y6;X12;K\"100\"\nC;Y6;X13;K\"60\"\nC;Y6;X14;K\"1\"\nC;Y6;X16;K\"1\"\n"
    "C;Y7;X1;K\"Asd3\"\nC;Y7;X2;K\"Asds\"\nC;Y7;X3;K\"1\"\nC;Y7;X4;K\"ground,structure,debris,enemy,neutral\"\n"
    "C;Y7;X10;K\"100\"\nC;Y7;X11;K\"80\"\nC;Y7;X12;K\"100\"\nC;Y7;X13;K\"80\"\nC;Y7;X14;K\"1\"\nC;Y7;X16;K\"1\"\n"
    "C;Y8;X1;K\"Asds\"\nC;Y8;X2;K\"Asds\"\nC;Y8;X3;K\"1\"\nE\n";

static pfFix_t pf_setup(uint32_t code) {
    pfFix_t fix;
    reset_entities(); setup_test_world(); level.time = 1000;
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    fix.rows = parse_slk_string(pf_slk); fix.old = G_SetSLKRows("AbilityData", fix.rows);
    fix.caster = alloc_test_unit(MAKEFOURCC('N', 't', 'i', 'n'), 0, 0);
    fix.caster->s.player = 0; fix.caster->svflags |= SVF_MONSTER; fix.caster->targtype = TARG_GROUND;
    fix.caster->mana.value = fix.caster->mana.max_value = 500;
    fix.caster->health.value = fix.caster->health.max_value = 1000;
    fix.caster->unit_state->ranks[0] = MAKE(heroability_t, .code = code, .level = 1);
    return fix;
}

static pfFix_t pf_setup_with_data(uint32_t code, cstring_t slk) {
    pfFix_t fix = pf_setup(code);
    G_SetSLKRows("AbilityData", fix.old);
    free_slk_rows(fix.rows);
    fix.rows = parse_slk_string(slk);
    fix.old = G_SetSLKRows("AbilityData", fix.rows);
    return fix;
}

static void pf_done(pfFix_t fix) { G_SetSLKRows("AbilityData", fix.old); free_slk_rows(fix.rows); }

static void pf_tick(uint32_t ms) { level.time += ms; G_RunEntities(); }

static edict_t *pf_find(uint32_t code) {
    FILTER_EDICTS(ent, ent->inuse && ent->class_id == code) return ent;
    return NULL;
}

static uint32_t pf_count(uint32_t code) {
    uint32_t n = 0;
    FILTER_EDICTS(ent, ent->inuse && ent->class_id == code) n++;
    return n;
}

static edict_t *pf_thinker(edict_t *factory) {
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
    pfFix_t fix = pf_setup(BZ_ANSY);
    vec2_t point = { 256, 192 };
    edict_t *factory, *thinker, *first;
    T_ASSERT(S_CastPointTargetSpell(fix.caster, BZ_ANSY, &point));
    factory = pf_find(BZ_HFOO); thinker = pf_thinker(factory);
    T_NOT_NULL(factory); T_NOT_NULL(thinker);
    T_EQ(factory->owner, fix.caster); T_EQ(factory->s.player, 0);
    T_EQ(factory->summon_ability, BZ_ANSY);
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

TEST(wc3_spell, pocket_factory_rally_orders_spawned_clockwerks) {
    static UnitProfile_t const factory_profile = { .trains = "" };
    static UnitAbilities_t const factory_abilities = { .abilList = "ARal" };
    pfFix_t fix = pf_setup(BZ_ANSY);
    vec2_t const factory_point = { 256.0f, 192.0f };
    vec2_t const rally_point = { 400.0f, 192.0f };
    edict_t *factory, *goblin;

    T_ASSERT(S_CastPointTargetSpell(fix.caster, BZ_ANSY, &factory_point));
    factory = pf_find(BZ_HFOO);
    T_NOT_NULL(factory);
    if (!factory) { pf_done(fix); return; }
    factory->data.UnitProfile = (UnitProfile_t *)&factory_profile;
    factory->data.UnitAbilities = (UnitAbilities_t *)&factory_abilities;
    T_ASSERT(G_UnitHasRally(factory));
    T_ASSERT(G_SetRallyPoint(factory, &rally_point));

    pf_tick(2000);
    goblin = pf_find(BZ_OGRU);
    T_NOT_NULL(goblin);
    if (goblin) {
        /* The synthetic ogru has no UnitBalance/UnitUI rows, so give the spawned
         * fixture the minimum live-mobile state that SP_SpawnUnit would provide. */
        goblin->svflags |= SVF_MONSTER;
        goblin->s.flags |= EF_UNIT;
        goblin->health.value = goblin->health.max_value = 100.0f;
        goblin->unitinfo.MoveSpeed = 270.0f;
        goblin->movetype = MOVETYPE_STEP;
        goblin->collision = 16.0f;
        T_ASSERT(G_ApplyRallyOrder(factory, goblin));
        T_NOT_NULL(goblin->goalentity);
        if (goblin->goalentity) {
            T_FEQ(goblin->goalentity->s.origin2.x, rally_point.x, 0.001f);
            T_FEQ(goblin->goalentity->s.origin2.y, rally_point.y, 0.001f);
        }
    }
    pf_done(fix);
}

/* DataC is each Clockwerk's timed life; expiry goes through unit_updatestatuses, not RunFrame. */
TEST(wc3_spell, pocket_factory_clockwerk_btlf_matches_datac) {
    pfFix_t fix = pf_setup(BZ_ANSY);
    vec2_t point = { 128, 128 };
    edict_t *goblin;
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
    pfFix_t fix = pf_setup(BZ_ANSY);
    vec2_t point = { 128, 128 };
    edict_t *factory, *thinker;
    T_ASSERT(S_CastPointTargetSpell(fix.caster, BZ_ANSY, &point));
    factory = pf_find(BZ_HFOO); thinker = pf_thinker(factory);
    T_NOT_NULL(factory); T_NOT_NULL(thinker);
    G_FreeEdict(factory);
    pf_tick(2000);
    T_EQ(pf_count(BZ_OGRU), 0); T_ASSERT(!thinker->inuse);
    pf_done(fix);
}

/* Reusing the factory edict generation must not let an older producer thinker attach to it. */
TEST(wc3_spell, pocket_factory_recycled_factory_slot_cancels_production) {
    pfFix_t fix = pf_setup(BZ_ANSY);
    vec2_t point = { 128, 128 };
    edict_t *factory, *thinker;
    T_ASSERT(S_CastPointTargetSpell(fix.caster, BZ_ANSY, &point));
    factory = pf_find(BZ_HFOO); thinker = pf_thinker(factory);
    T_NOT_NULL(factory); T_NOT_NULL(thinker);
    factory->spawn_time++;
    pocket_factory_think(thinker);
    T_ASSERT(!thinker->inuse);
    T_EQ(pf_count(BZ_OGRU), 0);
    pf_done(fix);
}

/* ANs1 shares CAbilityPocketFactory but reads its own DataA through abilityitem_t.code. */
TEST(wc3_spell, pocket_factory_ans1_uses_alias_dataa_interval) {
    pfFix_t fix = pf_setup(BZ_ANS1);
    vec2_t point = { 64, 64 };
    T_ASSERT(S_CastPointTargetSpell(fix.caster, BZ_ANS1, &point));
    T_NOT_NULL(pf_find(BZ_HFOO));
    pf_tick(999); T_EQ(pf_count(BZ_OGRU), 0);
    pf_tick(1); T_EQ(pf_count(BZ_OGRU), 1);
    pf_done(fix);
}

/* Thinker allocation failure must roll the factory back instead of leaving an inert summon. */
TEST(wc3_spell, pocket_factory_thinker_alloc_failure_rolls_back_factory) {
    pfFix_t fix = pf_setup(BZ_ANSY);
    vec2_t point = { 128, 128 };
    uint32_t max_edicts = globals.max_edicts;
    globals.max_edicts = globals.num_edicts + 1;
    T_ASSERT(S_CastPointTargetSpell(fix.caster, BZ_ANSY, &point));
    globals.max_edicts = max_edicts;
    T_NULL(pf_find(BZ_HFOO));
    pf_done(fix);
}

/* DataE leash: a factory-owned Clockwerk past the authored range is ordered home. */
TEST(wc3_spell, pocket_factory_datae_leash_returns_clockwerk) {
    pfFix_t fix = pf_setup(BZ_ANSY);
    vec2_t point = { 256, 192 };
    edict_t *factory, *goblin, *thinker;
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

/* Engineering selects the Factory variant, which creates the matching Clockwerk;
 * its normal death path must use that tier's authored Kaboom damage (30/60/80). */
TEST(wc3_spell, pocket_factory_engineering_levels_scale_clockwerk_kaboom_damage) {
    static uint32_t const factory_codes[] = { BZ_ANSY, BZ_ANS1, BZ_ANS2 };
    static uint32_t const kaboom_codes[] = { BZ_ASDG, BZ_ASD2, BZ_ASD3 };
    static float const expected_damage[] = { 30.0f, 60.0f, 80.0f };

    FOR_LOOP(rank, sizeof(factory_codes) / sizeof(factory_codes[0])) {
        pfFix_t fix = pf_setup_with_data(BZ_ANSY, pf_kaboom_slk);
        vec2_t const point = { 128.0f, 128.0f };
        edict_t *factory, *goblin, *enemy;
        uint32_t const factory_code = factory_codes[rank];
        ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
        memset(level.alliances, 0, sizeof(level.alliances));

        /* The fixture Clockwerk shell is shared; attach its tier's stock Kaboom alias below. */
        if (rank) fix.caster->unit_state->ranks[1] = MAKE(heroability_t,
            .code = BZ_ANEG, .level = rank);
        T_EQ(S_TinkerVariant(fix.caster, BZ_ANSY), factory_code);
        T_ASSERT(S_CastPointTargetSpell(fix.caster, BZ_ANSY, &point));
        factory = pf_find(BZ_HFOO);
        T_NOT_NULL(factory);
        if (!factory) { pf_done(fix); continue; }
        pf_tick((uint32_t)(S_SpellData(factory_code, 1, 1) * 1000.0f));
        goblin = pf_find(BZ_OGRU);
        T_NOT_NULL(goblin);
        if (!goblin) { pf_done(fix); continue; }

        enemy = alloc_test_unit(BZ_HFOO, goblin->s.origin2.x, goblin->s.origin2.y);
        enemy->s.player = 1;
        enemy->svflags |= SVF_MONSTER;
        enemy->targtype = TARG_GROUND;
        enemy->health.value = enemy->health.max_value = 200.0f;
        enemy->unit_state->armor_value = 0.0f;
        goblin->s.player = 0;
        goblin->health.value = goblin->health.max_value = 100.0f;
        goblin->unit_state->armor_value = 0.0f;
        goblin->die = unit_die;
        T_ASSERT(G_ActorAddSkill(goblin, kaboom_codes[rank]));
        unit_die(goblin, NULL);
        T_FEQ(enemy->health.value, 200.0f - expected_damage[rank], 0.001f);
        pf_done(fix);
    }
}

#endif
