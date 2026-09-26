#ifdef BZ_TESTS
#include "test.h"
#include "../skills/s_skills.h"

#define ID_ASAC MAKEFOURCC('A','s','a','c')
#define ID_ALAM MAKEFOURCC('A','l','a','m')
#define ID_SHADE MAKEFOURCC('u','s','h','d')

edict_t * alloc_test_unit(uint32_t class_id, float x, float y);
void reset_entities(void);
void setup_test_world(void);
void G_RunEntity(edict_t *);
void CM_SetupTestPathmap(uint32_t width, uint32_t height, uint8_t const *cells);
void CM_SetupTestWorldBounds(box2_t const * bounds);
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
    edict_t * pit, *acolyte, *other;
    UnitBalance_t pit_balance, acolyte_balance, other_balance;
    gameClient_t * client;
    pathTex_t *pathtex;
} sacFix_t;

static void sac_setup(sacFix_t *fix) {
    enum { CELLS = 64, FOOT_W = 16, FOOT_H = 16 };
    uint8_t pathmap[CELLS * CELLS] = {0};
    size_t const pathtex_size = sizeof(pathTex_t) + FOOT_W * FOOT_H * sizeof(color32_t);
    reset_entities(); setup_test_world(); level.time = 1000;
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t, .min = {-1024.0f, -1024.0f}, .max = {1024.0f, 1024.0f}));
    fix->pathtex = gi.MemAlloc(pathtex_size);
    memset(fix->pathtex, 0, pathtex_size);
    fix->pathtex->width = FOOT_W; fix->pathtex->height = FOOT_H;
    FOR_LOOP(i, FOOT_W * FOOT_H) fix->pathtex->map[i].b = 0xff;
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
    fix->pit->pathtex = fix->pathtex;
    fix->pit->think = monster_think;
    fix->acolyte->targtype = fix->other->targtype = TARG_GROUND;
    fix->pit->stand = fix->acolyte->stand = fix->other->stand = unit_stand;
    fix->pit->heroabilities[0] = MAKE(heroability_t, .code = ID_ASAC, .level = 1);
    fix->acolyte->heroabilities[0] = MAKE(heroability_t, .code = ID_ALAM, .level = 1);
    fix->client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 1000;
    fix->client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = 1000;
    fix->client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 10;
    G_SetUnitFoodUsed(fix->acolyte, 1);
    gi.LinkEntity(fix->pit);
    gi.LinkEntity(fix->acolyte);
}

static void sac_done(sacFix_t *fix) {
    if (fix->pathtex) gi.MemFree(fix->pathtex);
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
    sacFix_t fix;
    edict_t * result;
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
    sacFix_t fix;
    edict_t * result;
    UnitBalance_t result_balance;
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
    FOR_LOOP(i, 40) {
        level.time += FRAMETIME; G_RunEntity(fix.pit);
        if (!result->inuse || !result->training) break;
    }
    T_ASSERT(!fix.acolyte->inuse);
    T_ASSERT(result->inuse);
    T_ASSERT(!result->training);
    T_ASSERT(!(result->s.renderfx & RF_HIDDEN));
    T_EQ(fix.client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_USED], 1);
    sac_done(&fix);
}

TEST(wc3_spell, sacrifice_cancel_restores_acolyte_and_removes_queued_shade) {
    sacFix_t fix;
    edict_t * result;
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
    sacFix_t fix;
    edict_t * blocker;
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

/* Worker death invalidates the queue through the scheduler; the dead worker
 * is not consumed and the queue clears. */
TEST(wc3_spell, sacrifice_worker_death_cancels_queue) {
    sacFix_t fix;
    edict_t * result;
    sac_setup(&fix);
    T_ASSERT(S_CastUnitTargetSpell(fix.pit, ID_ASAC, fix.acolyte));
    result = fix.pit->build;
    T_NOT_NULL(result);
    G_SetHealth(fix.acolyte, 0);
    T_ASSERT(M_IsDead(fix.acolyte));
    level.time += FRAMETIME; G_RunEntity(fix.pit);
    T_NULL(fix.pit->build);
    T_ASSERT(!result->inuse);
    T_ASSERT(M_IsDead(fix.acolyte));
    sac_done(&fix);
}

/* Ownership change invalidates the queue; the former worker survives. */
TEST(wc3_spell, sacrifice_worker_ownership_change_cancels_queue) {
    sacFix_t fix;
    edict_t * result;
    sac_setup(&fix);
    T_ASSERT(S_CastUnitTargetSpell(fix.pit, ID_ASAC, fix.acolyte));
    result = fix.pit->build;
    T_NOT_NULL(result);
    fix.acolyte->s.player = 1;
    level.time += FRAMETIME; G_RunEntity(fix.pit);
    T_NULL(fix.pit->build);
    T_ASSERT(!result->inuse);
    T_ASSERT(fix.acolyte->inuse);
    T_ASSERT(!(fix.acolyte->s.renderfx & RF_HIDDEN));
    sac_done(&fix);
}

/* A freed worker slot reused by another unit is never consumed: the stale
 * queue cancels and the new occupant survives. */
TEST(wc3_spell, sacrifice_reused_slot_not_consumed) {
    sacFix_t fix;
    edict_t * result, *occupant;
    uint32_t slot;
    sac_setup(&fix);
    T_ASSERT(S_CastUnitTargetSpell(fix.pit, ID_ASAC, fix.acolyte));
    result = fix.pit->build;
    T_NOT_NULL(result);
    slot = (uint32_t)(fix.acolyte - g_edicts);
    G_FreeEdict(fix.acolyte);
    occupant = alloc_test_unit(MAKEFOURCC('u','g','h','o'), 64, 0);
    occupant->s.player = 0;
    level.time += FRAMETIME; G_RunEntity(fix.pit);
    T_NULL(fix.pit->build);
    T_ASSERT(!result->inuse);
    T_ASSERT(occupant->inuse);
    (void)slot;
    sac_done(&fix);
}

/* Blocked result placement defers completion without consuming the worker;
 * clearing space lets the retained order finish. */
TEST(wc3_spell, sacrifice_blocked_placement_preserves_worker) {
    sacFix_t fix;
    edict_t * result;
    UnitBalance_t result_balance;
    sac_setup(&fix);
    T_ASSERT(S_CastUnitTargetSpell(fix.pit, ID_ASAC, fix.acolyte));
    result = fix.pit->build;
    T_NOT_NULL(result);
    if (!result) { sac_done(&fix); return; }
    result->stand = unit_stand;
    result_balance = *result->data.UnitBalance;
    result_balance.buildTime = 1;
    result_balance.foodUsed = 1;
    result->data.UnitBalance = &result_balance;
    result->collision = 4000.0f; /* no exit fits: placement must fail */
    FOR_LOOP(i, 20) {
        level.time += FRAMETIME; G_RunEntity(fix.pit);
        if (!result->inuse || !result->training) break;
    }
    T_ASSERT(result->training);
    T_ASSERT(fix.acolyte->inuse);
    T_ASSERT(fix.acolyte->s.renderfx & RF_HIDDEN);
    result->collision = 16.0f;
    FOR_LOOP(i, 40) {
        level.time += FRAMETIME; G_RunEntity(fix.pit);
        if (!result->inuse || !result->training) break;
    }
    T_ASSERT(!fix.acolyte->inuse);
    T_ASSERT(result->inuse && !result->training);
    sac_done(&fix);
}

/* Save/load resumes the queued sacrifice: worker linkage survives and the
 * scheduler still completes after load. */
TEST(wc3_save, sacrifice_queue_round_trips_then_completes) {
    cstring_t filename = "/tmp/openwarcraft3-sacrifice-queue.bin";
    sacFix_t fix;
    edict_t * result;
    UnitBalance_t result_balance;
    sac_setup(&fix);
    T_ASSERT(S_CastUnitTargetSpell(fix.pit, ID_ASAC, fix.acolyte));
    result = fix.pit->build;
    T_NOT_NULL(result);
    if (!result) { sac_done(&fix); return; }
    result->stand = unit_stand;
    result->collision = 16.0f;
    T_ASSERT(WriteGame(filename));
    T_ASSERT(ReadGame(filename));
    T_ASSERT(fix.pit->build == result);
    T_ASSERT(result->sacrifice.active);
    T_ASSERT(result->sacrifice.worker == fix.acolyte);
    result_balance = *result->data.UnitBalance;
    result_balance.buildTime = 1;
    result_balance.foodUsed = 1;
    result->data.UnitBalance = &result_balance;
    FOR_LOOP(i, 40) {
        level.time += FRAMETIME; G_RunEntity(fix.pit);
        if (!result->inuse || !result->training) break;
    }
    T_ASSERT(!fix.acolyte->inuse);
    T_ASSERT(result->inuse && !result->training);
    remove(filename);
    sac_done(&fix);
}

/* Save/load preserves cancellation: the restored worker is released. */
TEST(wc3_save, sacrifice_queue_round_trips_then_cancels) {
    cstring_t filename = "/tmp/openwarcraft3-sacrifice-cancel.bin";
    sacFix_t fix;
    edict_t * result;
    sac_setup(&fix);
    T_ASSERT(S_CastUnitTargetSpell(fix.pit, ID_ASAC, fix.acolyte));
    result = fix.pit->build;
    T_NOT_NULL(result);
    T_ASSERT(WriteGame(filename));
    T_ASSERT(ReadGame(filename));
    T_ASSERT(G_CancelTrainingQueueItem(fix.pit, 0, true));
    T_NULL(fix.pit->build);
    T_ASSERT(fix.acolyte->inuse);
    T_ASSERT(!(fix.acolyte->s.renderfx & RF_HIDDEN));
    T_ASSERT(!fix.acolyte->paused);
    remove(filename);
    sac_done(&fix);
}

#endif
