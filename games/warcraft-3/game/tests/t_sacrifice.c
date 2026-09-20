#ifdef BZ_TESTS
#include "test.h"
#include "../skills/s_skills.h"

#define ID_ASAC MAKEFOURCC('A','s','a','c')
#define ID_ALAM MAKEFOURCC('A','l','a','m')
#define ID_SHADE MAKEFOURCC('u','s','h','d')

LPEDICT alloc_test_unit(DWORD class_id, FLOAT x, FLOAT y);
void reset_entities(void);
void setup_test_world(void);
slkTestData_t *parse_slk_string(const char *text);
void free_slk_rows(slkTestData_t *rows);

#define SAC_SLK \
    "ID;PWXL;N;EBB;Y3;X7\n" \
    "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n" \
    "C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"Cost1\"\nC;Y1;X6;K\"Cool1\"\nC;Y1;X7;K\"Rng1\"\n" \
    "C;Y2;X1;K\"Asac\"\nC;Y2;X2;K\"Asac\"\nC;Y2;X3;K\"1\"\n" \
    "C;Y2;X4;K\"ground,player\"\nC;Y2;X5;K\"0\"\nC;Y2;X6;K\"0\"\nC;Y2;X7;K\"0\"\n" \
    "C;Y3;X1;K\"Alam\"\nC;Y3;X2;K\"Alam\"\nC;Y3;X3;K\"1\"\n" \
    "C;Y3;X4;K\"structure,player\"\nC;Y3;X5;K\"0\"\nC;Y3;X6;K\"0\"\nC;Y3;X7;K\"0\"\nE\n"

#define SAC_BALANCE_SLK \
    "ID;PWXL;N;EBB;Y3;X5\n" \
    "C;Y1;X1;K\"unitBalanceID\"\nC;Y1;X2;K\"realHP\"\n" \
    "C;Y1;X3;K\"bldtm\"\nC;Y1;X4;K\"fused\"\nC;Y1;X5;K\"isbldg\"\n" \
    "C;Y2;X1;K\"ushd\"\nC;Y2;X2;K\"250\"\n" \
    "C;Y2;X3;K\"2\"\nC;Y2;X4;K\"1\"\nC;Y2;X5;K\"0\"\n" \
    "C;Y3;X1;K\"usap\"\nC;Y3;X5;K\"1\"\nE\n"

typedef struct {
    slkTestData_t *rows, *old, *balance_rows, *old_balance;
    LPEDICT pit, acolyte, other;
    UnitBalance_t pit_balance, acolyte_balance, other_balance;
    LPGAMECLIENT client;
} SACFIX;

static void sac_setup(SACFIX *fix) {
    reset_entities(); setup_test_world(); level.time = 1000;
    fix->rows = parse_slk_string(SAC_SLK); fix->old = G_SetSLKRows("AbilityData", fix->rows);
    fix->balance_rows = parse_slk_string(SAC_BALANCE_SLK);
    fix->old_balance = G_SetSLKRows("UnitBalance", fix->balance_rows);
    fix->client = &game.clients[0];
    fix->pit_balance = MAKE(UnitBalance_t, .maxHealth = 900, .isBuilding = true);
    fix->acolyte_balance = MAKE(UnitBalance_t, .maxHealth = 220, .foodUsed = 1);
    fix->other_balance = MAKE(UnitBalance_t, .maxHealth = 400, .foodUsed = 1);
    fix->pit = alloc_test_unit(MAKEFOURCC('u','s','a','p'), 0, 0);
    fix->acolyte = alloc_test_unit(MAKEFOURCC('u','a','c','o'), 64, 0);
    fix->other = alloc_test_unit(MAKEFOURCC('u','g','h','o'), 96, 0);
    fix->pit->s.player = fix->acolyte->s.player = fix->other->s.player = 0;
    fix->pit->svflags = fix->acolyte->svflags = fix->other->svflags = SVF_MONSTER;
    fix->pit->data.UnitBalance = &fix->pit_balance;
    fix->acolyte->data.UnitBalance = &fix->acolyte_balance;
    fix->other->data.UnitBalance = &fix->other_balance;
    fix->pit->health.value = fix->pit->health.max_value = 900;
    fix->acolyte->health.value = fix->acolyte->health.max_value = 220;
    fix->other->health.value = fix->other->health.max_value = 400;
    fix->pit->targtype = TARG_STRUCTURE;
    fix->pit->collision = 192.0f;
    fix->pit->movetype = MOVETYPE_NONE;
    fix->acolyte->targtype = fix->other->targtype = TARG_GROUND;
    fix->pit->stand = fix->acolyte->stand = fix->other->stand = unit_stand;
    fix->pit->heroabilities[0] = MAKE(heroability_t, .code = ID_ASAC, .level = 1);
    fix->acolyte->heroabilities[0] = MAKE(heroability_t, .code = ID_ALAM, .level = 1);
    fix->client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 1000;
    fix->client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = 1000;
    fix->client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 10;
    G_SetUnitFoodUsed(fix->acolyte, 1);
}

static void sac_done(SACFIX *fix) {
    G_SetSLKRows("AbilityData", fix->old);
    G_SetSLKRows("UnitBalance", fix->old_balance);
    free_slk_rows(fix->rows);
    free_slk_rows(fix->balance_rows);
}

TEST(wc3_spell, sacrifice_registers_both_retail_endpoints) {
    abilityitem_t pit = S_AbilityItem(ID_ASAC), acolyte = S_AbilityItem(ID_ALAM);
    T_NOT_NULL(pit.ability); T_NOT_NULL(acolyte.ability);
    T_EQ(pit.ability->proc, CAbilitySacrifice);
    T_EQ(acolyte.ability->proc, CAbilitySacrifice);
    T_EQ(pit.ability->target_type, SPELL_TARGET_UNIT);
    T_EQ(acolyte.ability->target_type, SPELL_TARGET_UNIT);
}

TEST(wc3_spell, sacrifice_pit_target_hides_acolyte_and_queues_shade) {
    SACFIX fix;
    LPEDICT result;
    sac_setup(&fix);
    T_ASSERT(S_CastUnitTargetSpell(fix.pit, ID_ASAC, fix.acolyte));
    result = fix.pit->build;
    T_NOT_NULL(result);
    if (!result) { sac_done(&fix); return; }
    result->stand = unit_stand;
    result->collision = 16.0f;
    T_EQ(result->class_id, ID_SHADE);
    T_ASSERT(result->training);
    T_ASSERT(result->sacrifice.active);
    T_ASSERT(result->sacrifice.worker == fix.acolyte);
    T_ASSERT(fix.acolyte->s.renderfx & RF_HIDDEN);
    T_ASSERT(fix.acolyte->paused);
    /* Sacrifice does not reserve the Shade's food while the Acolyte still owns
     * its slot. */
    T_EQ(fix.client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_USED], 1);
    sac_done(&fix);
}

TEST(wc3_spell, sacrifice_uses_result_build_time_then_replaces_worker_without_extra_food) {
    SACFIX fix;
    LPEDICT result;
    UnitBalance_t result_balance;
    DWORD ticks;
    sac_setup(&fix);
    T_ASSERT(S_CastUnitTargetSpell(fix.acolyte, ID_ALAM, fix.pit));
    result = fix.pit->build;
    T_NOT_NULL(result);
    if (!result) { sac_done(&fix); return; }
    result->stand = unit_stand;
    result->collision = 16.0f;
    result_balance = *result->data.UnitBalance;
    result_balance.buildTime = 2; /* non-stock: prove queue cadence comes from result unit data */
    result_balance.foodUsed = 1;
    result->data.UnitBalance = &result_balance;
    ticks = (2000 + FRAMETIME - 1) / FRAMETIME + 5; /* allow queue setup and float accumulation */
    FOR_LOOP(i, ticks - 1) fix.pit->currentmove->think(fix.pit);
    T_ASSERT(fix.acolyte->inuse);
    T_ASSERT(result->training);
    fix.pit->currentmove->think(fix.pit);
    T_ASSERT(!fix.acolyte->inuse);
    T_ASSERT(result->inuse);
    T_ASSERT(!result->training);
    T_ASSERT(!(result->s.renderfx & RF_HIDDEN));
    T_EQ(fix.client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_USED], 1);
    sac_done(&fix);
}

TEST(wc3_spell, sacrifice_cancel_restores_acolyte_and_removes_queued_shade) {
    SACFIX fix;
    LPEDICT result;
    sac_setup(&fix);
    T_ASSERT(S_CastUnitTargetSpell(fix.pit, ID_ASAC, fix.acolyte));
    result = fix.pit->build;
    T_NOT_NULL(result);
    T_ASSERT(G_CancelTrainingQueueItem(fix.pit, 0, true));
    T_NULL(fix.pit->build);
    T_ASSERT(fix.acolyte->inuse);
    T_ASSERT(!(fix.acolyte->s.renderfx & RF_HIDDEN));
    T_ASSERT(!fix.acolyte->paused);
    T_ASSERT(!result->inuse);
    T_EQ(fix.client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_USED], 1);
    sac_done(&fix);
}

TEST(wc3_spell, sacrifice_requires_counterpart_ability_and_idle_pit) {
    SACFIX fix;
    LPEDICT blocker;
    sac_setup(&fix);
    T_ASSERT(!S_CastUnitTargetSpell(fix.pit, ID_ASAC, fix.other));
    blocker = alloc_test_unit(MAKEFOURCC('u','g','h','o'), 128, 0);
    blocker->training = true;
    fix.pit->build = blocker;
    T_ASSERT(!S_CastUnitTargetSpell(fix.pit, ID_ASAC, fix.acolyte));
    fix.pit->build = NULL;
    G_FreeEdict(blocker);
    sac_done(&fix);
}

#endif
