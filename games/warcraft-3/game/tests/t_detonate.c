#ifdef BZ_TESTS
#include "test.h"
#include "../skills/s_skills.h"

#define DETONATE MAKEFOURCC('A','d','t','n')
#define DETONATE_ALIAS MAKEFOURCC('A','0','d','t')
#define RESISTANT_SKIN MAKEFOURCC('A','C','r','k')
#define BUFF_MAGIC_IMMUNE "Biyy"
#define BUFF_SLOW MAKEFOURCC('B','s','l','o')
#define BUFF_TIMED_LIFE MAKEFOURCC('B','T','L','F')

edict_t *alloc_test_unit(uint32_t class_id, float x, float y);
void reset_entities(void);
void setup_test_world(void);
slkTestData_t *parse_slk_string(char const *text);
void free_slk_rows(slkTestData_t *rows);

/* Non-stock figures prove DataA/DataB/Area/Rng are read from the concrete
 * AbilityData alias, not the historical Wisp balance or Sapper Kaboom fields. */
static cstring_t const detonate_slk =
    "ID;PWXL;N;EBB;Y3;X10\n"
    "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
    "C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"Cost1\"\nC;Y1;X6;K\"Cool1\"\n"
    "C;Y1;X7;K\"Rng1\"\nC;Y1;X8;K\"Area1\"\nC;Y1;X9;K\"DataA1\"\nC;Y1;X10;K\"DataB1\"\n"
    "C;Y2;X1;K\"Adtn\"\nC;Y2;X2;K\"Adtn\"\nC;Y2;X3;K\"1\"\n"
    "C;Y2;X4;K\"air,ground,ward,invu,vuln,tree\"\nC;Y2;X5;K\"0\"\nC;Y2;X6;K\"0\"\n"
    "C;Y2;X7;K\"100\"\nC;Y2;X8;K\"247\"\nC;Y2;X9;K\"17\"\nC;Y2;X10;K\"123\"\n"
    "C;Y3;X1;K\"A0dt\"\nC;Y3;X2;K\"Adtn\"\nC;Y3;X3;K\"1\"\n"
    "C;Y3;X4;K\"air,ground,ward,invu,vuln,tree\"\nC;Y3;X5;K\"0\"\nC;Y3;X6;K\"0\"\n"
    "C;Y3;X7;K\"100\"\nC;Y3;X8;K\"247\"\nC;Y3;X9;K\"29\"\nC;Y3;X10;K\"131\"\nE\n";

typedef struct {
    slkTestData_t *rows, *old;
    edict_t *wisp, *enemy, *summon, *friend, *far;
} detonateFixture_t;

static detonateFixture_t detonate_setup(uint32_t code) {
    detonateFixture_t f;
    reset_entities(); setup_test_world(); level.time = 1000;
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    memset(level.alliances, 0, sizeof(level.alliances));
    f.rows = parse_slk_string(detonate_slk);
    f.old = G_SetSLKRows("AbilityData", f.rows);
    f.wisp = alloc_test_unit(MAKEFOURCC('e','w','s','p'), 0, 0);
    f.enemy = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 48, 0);
    f.summon = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 96, 0);
    f.friend = alloc_test_unit(MAKEFOURCC('h','p','r','i'), 120, 0);
    f.far = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 450, 0);
    f.wisp->s.player = f.friend->s.player = 0;
    f.enemy->s.player = f.summon->s.player = f.far->s.player = 1;
    edict_t *units[5] = { f.wisp, f.enemy, f.summon, f.friend, f.far };
    globals.num_edicts = MAX(globals.num_edicts, (uint32_t)(f.far - globals.edicts + 1));
    FOR_LOOP(i, 5) {
        edict_t *u = units[i];
        u->svflags |= SVF_MONSTER;
        u->targtype = TARG_GROUND;
        u->health.value = u->health.max_value = 500.0f;
        u->mana.value = u->mana.max_value = 200.0f;
    }
    f.wisp->die = unit_die;
    /* An owner and enemy relationship make this an ordinary dispellable summon. */
    f.summon->owner = f.wisp;
    T_ASSERT(G_ActorAddSkill(f.wisp, code));
    return f;
}

static void detonate_done(detonateFixture_t f) {
    G_SetSLKRows("AbilityData", f.old);
    free_slk_rows(f.rows);
}

TEST(wc3_spell, detonate_is_point_spell_not_kaboom) {
    T_EQ(G_OrderId("detonate"), 852145);
    T_STREQ(G_OrderId2String(852145), "detonate");
    T_EQ(S_AbilityItem(DETONATE).ability->proc, CAbilityDetonate);
    T_EQ(S_AbilityItem(DETONATE).ability->target_type, SPELL_TARGET_POINT);
    T_EQ(S_AbilityItem(MAKEFOURCC('A','s','d','s')).ability->proc, CAbilitySelfDestruct);
}

TEST(wc3_spell, detonate_affects_mana_and_hostile_summons_only) {
    detonateFixture_t f = detonate_setup(DETONATE);
    vec2_t point = { 0.0f, 0.0f };
    unit_addtimedstatus(f.enemy, "Bslo", 1, 30.0f);
    unit_addtimedstatus(f.friend, "Bslo", 1, 30.0f);
    unit_addtimedstatus(f.friend, "BTLF", 1, 30.0f);
    T_ASSERT(S_CastPointTargetSpell(f.wisp, DETONATE, &point));
    T_ASSERT(M_IsDead(f.wisp));
    T_ASSERT(f.wisp->aiflags & AI_SCRIPT_EXPLODED);
    T_FEQ(f.enemy->mana.value, 183.0f, 0.001f);
    T_FEQ(f.friend->mana.value, 183.0f, 0.001f);
    T_FEQ(f.summon->mana.value, 183.0f, 0.001f);
    T_FEQ(f.far->mana.value, 200.0f, 0.001f);
    T_FEQ(f.enemy->health.value, 500.0f, 0.001f);
    T_FEQ(f.friend->health.value, 500.0f, 0.001f);
    T_FEQ(f.summon->health.value, 377.0f, 0.001f);
    T_ASSERT(!S_UnitHasStatus(f.enemy, BUFF_SLOW));
    T_ASSERT(!S_UnitHasStatus(f.friend, BUFF_SLOW));
    T_ASSERT(S_UnitHasStatus(f.friend, BUFF_TIMED_LIFE));
    detonate_done(f);
}

TEST(wc3_spell, detonate_invulnerability_dispels_but_spares_mana) {
    detonateFixture_t f = detonate_setup(DETONATE);
    vec2_t point = { 0.0f, 0.0f };
    f.enemy->invulnerable = true;
    f.summon->invulnerable = true;
    unit_addtimedstatus(f.enemy, "Bslo", 1, 30.0f);
    T_ASSERT(S_CastPointTargetSpell(f.wisp, DETONATE, &point));
    T_FEQ(f.enemy->mana.value, 200.0f, 0.001f);
    T_FEQ(f.summon->health.value, 500.0f, 0.001f);
    T_ASSERT(!S_UnitHasStatus(f.enemy, BUFF_SLOW));
    detonate_done(f);
}

TEST(wc3_spell, detonate_preserves_immune_and_resistant_summons) {
    detonateFixture_t f = detonate_setup(DETONATE);
    vec2_t point = { 0.0f, 0.0f };
    f.summon->abilities.added[0] = RESISTANT_SKIN;
    f.summon->abilities.added_count = 1;
    T_NOT_NULL(unit_applystatus(f.enemy, &(status_application_t){
        .buff = BUFF_MAGIC_IMMUNE, .level = 1, .duration = 10.0f,
        .state_mask = WC3_STATUS_STATE_MAGIC_IMMUNE
    }));
    unit_addtimedstatus(f.enemy, "Bslo", 1, 30.0f);
    T_ASSERT(S_CastPointTargetSpell(f.wisp, DETONATE, &point));
    T_FEQ(f.summon->health.value, 500.0f, 0.001f);
    T_FEQ(f.enemy->mana.value, 200.0f, 0.001f);
    T_ASSERT(S_UnitHasStatus(f.enemy, BUFF_SLOW));
    detonate_done(f);
}

TEST(wc3_spell, detonate_custom_alias_uses_own_data) {
    detonateFixture_t f = detonate_setup(DETONATE_ALIAS);
    vec2_t point = { 0.0f, 0.0f };
    T_EQ(S_AbilityItem(DETONATE_ALIAS).ability->proc, CAbilityDetonate);
    T_ASSERT(S_CastPointTargetSpell(f.wisp, DETONATE_ALIAS, &point));
    T_FEQ(f.enemy->mana.value, 171.0f, 0.001f);
    T_FEQ(f.summon->health.value, 369.0f, 0.001f);
    detonate_done(f);
}

TEST(wc3_spell, detonate_does_not_trigger_when_wisp_is_killed) {
    detonateFixture_t f = detonate_setup(DETONATE);
    unit_die(f.wisp, f.enemy);
    T_FEQ(f.enemy->mana.value, 200.0f, 0.001f);
    T_FEQ(f.summon->health.value, 500.0f, 0.001f);
    T_ASSERT(!(f.wisp->aiflags & AI_SCRIPT_EXPLODED));
    detonate_done(f);
}

TEST(wc3_spell, detonate_banished_wisp_can_cast) {
    detonateFixture_t f = detonate_setup(DETONATE);
    vec2_t point = { 0.0f, 0.0f };
    heroabilitystatus_t *status = unit_applystatus(f.wisp, &(status_application_t){
        .buff = "Bban", .level = 1, .duration = 10.0f,
        .state_mask = WC3_STATUS_STATE_ETHEREAL,
        .buff_flags = WC3_STATUS_BUFF_MAGICAL
    });
    T_NOT_NULL(status);
    T_ASSERT(S_CastPointTargetSpell(f.wisp, DETONATE, &point));
    T_ASSERT(M_IsDead(f.wisp));
    detonate_done(f);
}

TEST(wc3_spell, detonate_empty_area_still_consumes_caster) {
    detonateFixture_t f = detonate_setup(DETONATE);
    vec2_t point = { 0.0f, 0.0f };
    G_FreeEdict(f.enemy);
    G_FreeEdict(f.summon);
    G_FreeEdict(f.friend);
    G_FreeEdict(f.far);
    T_ASSERT(S_CastPointTargetSpell(f.wisp, DETONATE, &point));
    T_ASSERT(M_IsDead(f.wisp));
    detonate_done(f);
}
/* Blight raster dispel must not erase corners being maintained by a
 * living growth source. The synthetic test uses the fixture's positive DataA
 * as a source flag so both protection and inverse can run without retail data. */
TEST(wc3_spell, detonate_blight_dispel_protects_live_growth) {
    detonateFixture_t f = detonate_setup(DETONATE);
    vec2_t point = { 32.0f, 32.0f };
    blightGrowth_t *growth;
    G_SetBlightPoint(&point, true);
    T_ASSERT(G_IsPointBlighted(&point));
    growth = (f.wisp->blight_growth ? f.wisp->blight_growth : (f.wisp->blight_growth=G_AllocBlightGrowth()));
    T_NOT_NULL(growth);
    growth->ability = DETONATE;
    growth->radius = 150.0f;
    G_DispelBlightRadius(&point, 247.0f);
    T_ASSERT(G_IsPointBlighted(&point));
    G_FreeBlightGrowth(f.wisp);
    G_DispelBlightRadius(&point, 247.0f);
    T_ASSERT(!G_IsPointBlighted(&point));
    detonate_done(f);
}

TEST(wc3_spell, detonate_far_point_is_pending_not_instant) {
    detonateFixture_t f = detonate_setup(DETONATE);
    vec2_t point = { 550.0f, 0.0f };
    T_ASSERT(unit_issueorder(f.wisp, "detonate", &point));
    T_ASSERT(!M_IsDead(f.wisp));
    T_EQ(G_GetIssuedOrderId(f.wisp), 852145);
    detonate_done(f);
}
#endif
