#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
#include "../game/skills/s_skills.h"

LPEDICT alloc_test_unit(DWORD code, FLOAT x, FLOAT y);
void reset_entities(void);
void setup_test_world(void);
slkTestData_t *parse_slk_string(const char *text);
void free_slk_rows(slkTestData_t *rows);

static UnitAbilities_t review_abilities = { .abilList = "AHfs,AHbz,AHdr,ANdr,AEtq,AHtb,AHre,AUfn,AHwe,Acan" };
static char const review_slk[] =
    "ID;PWXL;N;EBB;Y11;X15\n"
    "C;Y1;X1;K\"alias\"\n"
    "C;Y1;X2;K\"code\"\n"
    "C;Y1;X3;K\"targs\"\n"
    "C;Y1;X4;K\"Cost1\"\n"
    "C;Y1;X5;K\"Rng1\"\n"
    "C;Y1;X6;K\"Dur1\"\n"
    "C;Y1;X7;K\"HeroDur1\"\n"
    "C;Y1;X8;K\"Area1\"\n"
    "C;Y1;X9;K\"DataA1\"\n"
    "C;Y1;X10;K\"DataB1\"\n"
    "C;Y1;X11;K\"DataC1\"\n"
    "C;Y1;X12;K\"DataD1\"\n"
    "C;Y1;X13;K\"DataE1\"\n"
    "C;Y1;X14;K\"BuffID1\"\n"
    "C;Y2;X1;K\"AHfs\"\n"
    "C;Y2;X2;K\"AHfs\"\n"
    "C;Y2;X3;K\"ground,enemy\"\n"
    "C;Y2;X4;K\"0\"\n"
    "C;Y2;X5;K\"800\"\n"
    "C;Y2;X6;K\"9\"\n"
    "C;Y2;X7;K\"2.67\"\n"
    "C;Y2;X8;K\"200\"\n"
    "C;Y2;X9;K\"15\"\n"
    "C;Y2;X10;K\"0.33\"\n"
    "C;Y2;X11;K\"4\"\n"
    "C;Y2;X12;K\"1\"\n"
    "C;Y2;X13;K\"0.75\"\n"
    "C;Y2;X14;K\"BHfs\"\n"
    "C;Y3;X1;K\"AHbz\"\n"
    "C;Y3;X2;K\"AHbz\"\n"
    "C;Y3;X3;K\"ground,enemy\"\n"
    "C;Y3;X4;K\"0\"\n"
    "C;Y3;X5;K\"800\"\n"
    "C;Y3;X6;K\"0\"\n"
    "C;Y3;X7;K\"0\"\n"
    "C;Y3;X8;K\"200\"\n"
    "C;Y3;X9;K\"6\"\n"
    "C;Y3;X10;K\"30\"\n"
    "C;Y3;X11;K\"6\"\n"
    "C;Y3;X12;K\"0.5\"\n"
    "C;Y3;X13;K\"0\"\n"
    "C;Y3;X14;K\"BHbd,BHbz\"\n"
    "C;Y4;X1;K\"AHdr\"\n"
    "C;Y4;X2;K\"AHdr\"\n"
    "C;Y4;X3;K\"air,ground,enemy,friend\"\n"
    "C;Y4;X4;K\"0\"\n"
    "C;Y4;X5;K\"600\"\n"
    "C;Y4;X6;K\"6\"\n"
    "C;Y4;X7;K\"6\"\n"
    "C;Y4;X8;K\"800\"\n"
    "C;Y4;X9;K\"0\"\n"
    "C;Y4;X10;K\"15\"\n"
    "C;Y4;X11;K\"1\"\n"
    "C;Y4;X12;K\"0\"\n"
    "C;Y4;X13;K\"30\"\n"
    "C;Y4;X14;K\"\"\n"
    "C;Y5;X1;K\"ANdr\"\n"
    "C;Y5;X2;K\"AHdr\"\n"
    "C;Y5;X3;K\"air,ground,enemy\"\n"
    "C;Y5;X4;K\"0\"\n"
    "C;Y5;X5;K\"500\"\n"
    "C;Y5;X6;K\"8\"\n"
    "C;Y5;X7;K\"8\"\n"
    "C;Y5;X8;K\"800\"\n"
    "C;Y5;X9;K\"30\"\n"
    "C;Y5;X10;K\"0\"\n"
    "C;Y5;X11;K\"1\"\n"
    "C;Y5;X12;K\"0\"\n"
    "C;Y5;X13;K\"0\"\n"
    "C;Y5;X14;K\"\"\n"
    "C;Y6;X1;K\"AEtq\"\n"
    "C;Y6;X2;K\"AEtq\"\n"
    "C;Y6;X3;K\"air,ground,friend\"\n"
    "C;Y6;X4;K\"0\"\n"
    "C;Y6;X5;K\"0\"\n"
    "C;Y6;X6;K\"10\"\n"
    "C;Y6;X7;K\"10\"\n"
    "C;Y6;X8;K\"500\"\n"
    "C;Y6;X9;K\"20\"\n"
    "C;Y6;X10;K\"1\"\n"
    "C;Y6;X11;K\"0\"\n"
    "C;Y6;X12;K\"0\"\n"
    "C;Y6;X13;K\"0\"\n"
    "C;Y6;X14;K\"\"\n"
    "C;Y7;X1;K\"AHtb\"\n"
    "C;Y7;X2;K\"AHtb\"\n"
    "C;Y7;X3;K\"air,ground,enemy\"\n"
    "C;Y7;X4;K\"0\"\n"
    "C;Y7;X5;K\"600\"\n"
    "C;Y7;X6;K\"5\"\n"
    "C;Y7;X7;K\"3\"\n"
    "C;Y7;X8;K\"0\"\n"
    "C;Y7;X9;K\"100\"\n"
    "C;Y7;X10;K\"0\"\n"
    "C;Y7;X11;K\"0\"\n"
    "C;Y7;X12;K\"0\"\n"
    "C;Y7;X13;K\"0\"\n"
    "C;Y7;X14;K\"BPSE\"\n"
    "C;Y8;X1;K\"AHre\"\n"
    "C;Y8;X2;K\"AHre\"\n"
    "C;Y8;X3;K\"ground,friend,dead\"\n"
    "C;Y8;X4;K\"0\"\n"
    "C;Y8;X5;K\"400\"\n"
    "C;Y8;X6;K\"0\"\n"
    "C;Y8;X7;K\"0\"\n"
    "C;Y8;X8;K\"900\"\n"
    "C;Y8;X9;K\"6\"\n"
    "C;Y8;X10;K\"0\"\n"
    "C;Y8;X11;K\"0\"\n"
    "C;Y8;X12;K\"0\"\n"
    "C;Y8;X13;K\"0\"\n"
    "C;Y8;X14;K\"\"\n"
    "C;Y9;X1;K\"AUfn\"\n"
    "C;Y9;X2;K\"AUfn\"\n"
    "C;Y9;X3;K\"air,ground,enemy\"\n"
    "C;Y9;X4;K\"0\"\n"
    "C;Y9;X5;K\"800\"\n"
    "C;Y9;X6;K\"4\"\n"
    "C;Y9;X7;K\"2\"\n"
    "C;Y9;X8;K\"200\"\n"
    "C;Y9;X9;K\"50\"\n"
    "C;Y9;X10;K\"100\"\n"
    "C;Y9;X11;K\"0\"\n"
    "C;Y9;X12;K\"0\"\n"
    "C;Y9;X13;K\"0\"\n"
    "C;Y9;X14;K\"Bfro\"\n"
    "C;Y10;X1;K\"AHwe\"\n"
    "C;Y10;X2;K\"AHwe\"\n"
    "C;Y10;X3;K\"\"\n"
    "C;Y10;X4;K\"0\"\n"
    "C;Y10;X5;K\"0\"\n"
    "C;Y10;X6;K\"30\"\n"
    "C;Y10;X7;K\"30\"\n"
    "C;Y10;X8;K\"200\"\n"
    "C;Y10;X9;K\"1\"\n"
    "C;Y10;X10;K\"0\"\n"
    "C;Y10;X11;K\"0\"\n"
    "C;Y10;X12;K\"0\"\n"
    "C;Y10;X13;K\"0\"\n"
    "C;Y10;X14;K\"\"\n"
    "C;Y11;X1;K\"Acan\"\n"
    "C;Y11;X2;K\"Acan\"\n"
    "C;Y11;X3;K\"ground,dead,organic\"\n"
    "C;Y11;X4;K\"0\"\n"
    "C;Y11;X5;K\"50\"\n"
    "C;Y11;X6;K\"33\"\n"
    "C;Y11;X7;K\"33\"\n"
    "C;Y11;X8;K\"0\"\n"
    "C;Y11;X9;K\"10\"\n"
    "C;Y11;X10;K\"800\"\n"
    "C;Y11;X11;K\"0\"\n"
    "C;Y11;X12;K\"0\"\n"
    "C;Y11;X13;K\"0\"\n"
    "C;Y11;X14;K\"\"\n"
    "C;Y1;X15;K\"Cast1\"\n"
    "C;Y2;X15;K1.33\n"
    "C;Y3;X15;K1\n"
    "E\n"
;

/* Independent minimal units drive ordinary casting and the production thinker scheduler. */
static LPEDICT review_unit(DWORD owner, FLOAT x) {
    LPEDICT ent = alloc_test_unit(MAKEFOURCC('h','f','o','o'), x, 0);
    ent->spawn_time = G_Time(); ent->svflags |= SVF_MONSTER; ent->s.player = owner; ent->targtype = TARG_GROUND;
    ent->health.value = ent->health.max_value = 1000;
    ent->mana.value = 100; ent->mana.max_value = 1000;
    ent->stand = unit_stand; unit_stand(ent);
    return ent;
}

static LPEDICT review_setup(void) {
    reset_entities(); setup_test_world(); level.time = 1000;
    ((LPMAPINFO)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((LPMAPINFO)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    LPEDICT caster = review_unit(0, 0);
    caster->data.UnitAbilities = &review_abilities;
    return caster;
}

static LPEDICT review_thinker(LPEDICT caster) {
    FILTER_EDICTS(ent, ent->owner == caster && ent->think) return ent;
    return NULL;
}

/* Stock AHfs starts burning promptly; DataA is damage per full-damage pulse, not a 15-second delay. */
TEST(wc3_ability_lifecycle, flame_strike_stock_data_burns_within_two_seconds) {
    LPEDICT caster = review_setup(), enemy = review_unit(1, 100);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    BOOL cast = S_CastPointTargetSpell(caster, FS_SLKKey("AHfs"), &enemy->s.origin2);
    LPEDICT thinker = review_thinker(caster);
    level.time += 2000;
    if (thinker) G_RunEntity(thinker);
    FLOAT hp = enemy->health.value;
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
    T_ASSERT(cast); T_NOT_NULL(thinker); T_ASSERT(hp < 1000);
}

/* Once a flame pulse runs, another server frame must not count as another full burn tick. */
TEST(wc3_ability_lifecycle, flame_strike_does_not_burn_every_server_frame) {
    LPEDICT caster = review_setup(), enemy = review_unit(1, 100);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    BOOL cast = S_CastPointTargetSpell(caster, FS_SLKKey("AHfs"), &enemy->s.origin2);
    LPEDICT thinker = review_thinker(caster);
    FLOAT hp = 0;
    if (thinker) {
        level.time = thinker->freetime; G_RunEntity(thinker); hp = enemy->health.value;
        level.time += FRAMETIME; G_RunEntity(thinker);
    }
    FLOAT after = enemy->health.value;
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
    T_ASSERT(cast); T_NOT_NULL(thinker); T_FEQ(after, hp, .001f);
}

/* Stock Blizzard authors six waves and a zero Dur; zero must not truncate the cast to one second. */
TEST(wc3_ability_lifecycle, blizzard_stock_zero_duration_keeps_all_six_waves) {
    LPEDICT caster = review_setup(), enemy = review_unit(1, 100);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    BOOL cast = S_CastPointTargetSpell(caster, FS_SLKKey("AHbz"), &enemy->s.origin2);
    LPEDICT thinker = review_thinker(caster);
    FOR_LOOP(i, 5) { level.time += 1000; if (thinker && thinker->inuse) G_RunEntity(thinker); }
    FLOAT hp = enemy->health.value;
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
    T_ASSERT(cast); T_FEQ(hp, 820, .001f);
}

/* Cancelling the caster's channel must retire its pending resource transfer too. */
TEST(wc3_ability_lifecycle, siphon_mana_stops_after_movement_cancel) {
    LPEDICT caster = review_setup(), enemy = review_unit(1, 100);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    BOOL cast = S_CastUnitTargetSpell(caster, FS_SLKKey("AHdr"), enemy);
    LPEDICT thinker = review_thinker(caster);
    caster->s.origin2.x += 10; spell_run_frame(caster);
    DWORD channel = caster->channel.code;
    level.time += 1000;
    if (thinker) G_RunEntity(thinker);
    FLOAT mana = enemy->mana.value;
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
    T_ASSERT(cast); T_EQ(channel, 0); T_FEQ(mana, 100, .001f);
}

/* ANdr's authored DataA drains life even when the target has no mana pool. */
TEST(wc3_ability_lifecycle, life_drain_transfers_health_from_manaless_target) {
    LPEDICT caster = review_setup(), enemy = review_unit(1, 100);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    caster->health.value = 500; enemy->mana.value = enemy->mana.max_value = 0;
    BOOL cast = S_CastUnitTargetSpell(caster, FS_SLKKey("ANdr"), enemy);
    LPEDICT thinker = review_thinker(caster);
    level.time += 1000;
    if (thinker) G_RunEntity(thinker);
    FLOAT hp = enemy->health.value, healed = caster->health.value;
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
    T_ASSERT(cast); T_FEQ(hp, 970, .001f); T_FEQ(healed, 530, .001f);
}

/* Tranquility is AB_CHANNEL even though it needs no target-selection click. */
TEST(wc3_ability_lifecycle, no_target_tranquility_establishes_channel) {
    LPEDICT caster = review_setup();
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    BOOL cast = S_CastNoTargetSpell(caster, FS_SLKKey("AEtq"));
    DWORD channel = caster->channel.code;
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
    T_ASSERT(cast); T_EQ(channel, FS_SLKKey("AEtq"));
}

/* Cannibalize picks the nearest eligible corpse, consumes it at channel start, and heals on one-second pulses. */
TEST(wc3_ability_lifecycle, cannibalize_consumes_nearest_organic_corpse_and_caps_healing) {
    LPEDICT caster = review_setup(), mechanical = review_unit(1, 10), near = review_unit(1, 30), far = review_unit(1, 40);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    mechanical->health.value = near->health.value = far->health.value = 0;
    mechanical->svflags |= SVF_DEADMONSTER; near->svflags |= SVF_DEADMONSTER; far->svflags |= SVF_DEADMONSTER;
    mechanical->targtype = TARG_MECHANICAL; caster->health.value = 995;
    T_ASSERT(S_CastNoTargetSpell(caster, FS_SLKKey("Acan")));
    LPEDICT thinker = review_thinker(caster);
    T_ASSERT(mechanical->inuse); T_ASSERT(!near->inuse); T_ASSERT(far->inuse); T_NOT_NULL(thinker);
    T_FEQ(caster->health.value, 995, .001f); level.time += 1000; G_RunEntities();
    T_FEQ(caster->health.value, 1000, .001f); T_EQ(caster->channel.code, FS_SLKKey("Acan"));
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Dur=33 and DataA=10 are authoritative in both ROC and TFT; the production frame scheduler owns every pulse. */
TEST(wc3_ability_lifecycle, cannibalize_heals_for_authored_duration_through_entity_scheduler) {
    LPEDICT caster = review_setup(), corpse = review_unit(1, 40);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    caster->health.value = 500; corpse->health.value = 0; corpse->svflags |= SVF_DEADMONSTER;
    T_ASSERT(S_CastNoTargetSpell(caster, FS_SLKKey("Acan")));
    FOR_LOOP(i, 33) { level.time += 1000; G_RunEntities(); }
    T_FEQ(caster->health.value, 830, .001f); T_EQ(caster->channel.code, 0);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Live, mechanical, Hero-lifecycle and out-of-search-range units cannot fund a cast; movement interrupts an active feast. */
TEST(wc3_ability_lifecycle, cannibalize_rejects_invalid_corpses_and_stops_when_caster_moves) {
    LPEDICT caster = review_setup(), live = review_unit(1, 20), mechanical = review_unit(1, 30), far = review_unit(1, 801);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    mechanical->health.value = far->health.value = 0;
    mechanical->svflags |= SVF_DEADMONSTER; far->svflags |= SVF_DEADMONSTER; mechanical->targtype = TARG_MECHANICAL;
    T_ASSERT(!S_CastNoTargetSpell(caster, FS_SLKKey("Acan")));
    T_ASSERT(live->inuse); T_ASSERT(mechanical->inuse); T_ASSERT(far->inuse);
    far->s.origin2.x = far->s.origin.x = 40; caster->health.value = 500;
    T_ASSERT(S_CastNoTargetSpell(caster, FS_SLKKey("Acan")));
    caster->s.origin2.x += 10; caster->s.origin.x += 10; level.time += 1000; G_RunEntities();
    T_FEQ(caster->health.value, 500, .001f); T_EQ(caster->channel.code, 0);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* A launched bolt must not stun a target which becomes spell immune before impact. */
TEST(wc3_ability_lifecycle, avatar_blocks_storm_bolt_stun_at_impact) {
    LPEDICT caster = review_setup(), enemy = review_unit(1, 100);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    BOOL cast = S_CastUnitTargetSpell(caster, FS_SLKKey("AHtb"), enemy);
    LPEDICT missile = NULL;
    FILTER_EDICTS(ent, ent->owner == caster && ent->movetype == MOVETYPE_FLYMISSILE) { missile = ent; break; }
    unit_addtimedstatus(enemy, "BHav", 1, 10);
    if (missile) missile->currentmove->endfunc(missile);
    DWORD stun = G_UnitStatusLevel(enemy, FS_SLKKey("Bstu"));
    FLOAT hp = enemy->health.value;
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
    T_ASSERT(cast); T_NOT_NULL(missile); T_FEQ(hp, 1000, .001f); T_EQ(stun, 0);
}

/* Resurrection's stock tooltip promises ordinary friendly corpses, not exclusively Heroes. */
TEST(wc3_ability_lifecycle, resurrection_revives_friendly_footman) {
    LPEDICT caster = review_setup(), ally = review_unit(0, 100);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    ally->health.value = 0; ally->svflags |= SVF_DEADMONSTER;
    BOOL cast = S_CastNoTargetSpell(caster, FS_SLKKey("AHre"));
    FLOAT hp = ally->health.value;
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
    T_ASSERT(cast); T_ASSERT(hp > 0);
}

/* Frost Nova's range and target damage require a unit-target order. */
TEST(wc3_ability_lifecycle, frost_nova_accepts_enemy_unit_target) {
    LPEDICT caster = review_setup(), enemy = review_unit(1, 500);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    BOOL cast = S_CastUnitTargetSpell(caster, FS_SLKKey("AUfn"), enemy);
    FLOAT hp = enemy->health.value;
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
    T_ASSERT(cast); T_FEQ(hp, 850, .001f);
}

/* Serial ownership separates two casts of the same spell even when both thinkers still exist. */
TEST(wc3_ability_lifecycle, recast_retires_old_thinker_without_cancelling_new_channel) {
    LPEDICT caster = review_setup(), enemy = review_unit(1, 100);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("AHdr"), enemy));
    LPEDICT first = review_thinker(caster);
    DWORD serial = caster->channel.serial;
    T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("AHdr"), enemy));
    T_NE(caster->channel.serial, serial);
    level.time += 1000; G_RunEntity(first);
    T_ASSERT(!first->inuse); T_EQ(caster->channel.code, FS_SLKKey("AHdr")); T_FEQ(enemy->mana.value, 100, .001f);
    LPEDICT next = review_thinker(caster);
    T_NOT_NULL(next);
    if (next) G_RunEntity(next);
    T_FEQ(enemy->mana.value, 85, .001f);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Stun can happen after cast commitment but before the caster's next own frame. */
TEST(wc3_ability_lifecycle, stun_interrupts_blizzard_before_next_wave) {
    LPEDICT caster = review_setup(), enemy = review_unit(1, 100);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    T_ASSERT(S_CastPointTargetSpell(caster, FS_SLKKey("AHbz"), &enemy->s.origin2));
    LPEDICT thinker = review_thinker(caster);
    unit_addtimedstatus(caster, "Bstu", 1, 5); level.time += 1000; G_RunEntity(thinker);
    T_FEQ(enemy->health.value, 970, .001f); T_ASSERT(!thinker->inuse); T_EQ(caster->channel.code, 0);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* The retired caster slot must not donate its old drain to a newly spawned unit. */
TEST(wc3_ability_lifecycle, removed_caster_slot_cannot_own_old_drain) {
    LPEDICT caster = review_setup(), enemy = review_unit(1, 100);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("AHdr"), enemy));
    LPEDICT thinker = review_thinker(caster);
    G_FreeEdict(caster); level.time += 2000;
    LPEDICT fresh = review_unit(0, 0);
    T_ASSERT(fresh == caster);
    fresh->channel.code = FS_SLKKey("AHdr"); fresh->channel.serial = thinker->channel.serial;
    G_RunEntity(thinker);
    T_ASSERT(!thinker->inuse); T_FEQ(enemy->mana.value, 100, .001f);
    T_EQ(fresh->channel.code, FS_SLKKey("AHdr"));
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* The target's edict identity matters independently of the still-active caster and cast token. */
TEST(wc3_ability_lifecycle, removed_target_slot_cannot_receive_old_drain) {
    LPEDICT caster = review_setup(), enemy = review_unit(1, 100);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("AHdr"), enemy));
    LPEDICT thinker = review_thinker(caster);
    G_FreeEdict(enemy); level.time += 2000;
    LPEDICT fresh = review_unit(1, 100);
    T_ASSERT(fresh == enemy); G_RunEntity(thinker);
    T_ASSERT(!thinker->inuse); T_FEQ(fresh->mana.value, 100, .001f); T_EQ(caster->channel.code, 0);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* AHdr's authored friendly transfer rate and direction differ from enemy siphoning. */
TEST(wc3_ability_lifecycle, siphon_sends_mana_to_allies_and_expires) {
    LPEDICT caster = review_setup(), ally = review_unit(0, 100);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    caster->mana.value = 500; ally->mana.value = 0;
    T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("AHdr"), ally));
    LPEDICT thinker = review_thinker(caster);
    FOR_LOOP(i, 6) { level.time += 1000; G_RunEntity(thinker); }
    T_FEQ(caster->mana.value, 320, .001f); T_FEQ(ally->mana.value, 180, .001f);
    T_ASSERT(!thinker->inuse); T_EQ(caster->channel.code, 0);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Correcting the schema must preserve delayed startup, the reduced-damage phase, and final cleanup. */
TEST(wc3_ability_lifecycle, flame_strike_obeys_authored_phase_intervals_and_expiry) {
    LPEDICT caster = review_setup(), enemy = review_unit(1, 100);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    T_ASSERT(S_CastPointTargetSpell(caster, FS_SLKKey("AHfs"), &enemy->s.origin2));
    LPEDICT thinker = review_thinker(caster);
    level.time = 2000; G_RunEntity(thinker); T_FEQ(enemy->health.value, 1000, .001f);
    FOR_LOOP(i, 9) { level.time = 2330 + i * 330; G_RunEntity(thinker); }
    T_FEQ(enemy->health.value, 865, .001f);
    level.time = 5999; G_RunEntity(thinker); T_FEQ(enemy->health.value, 865, .001f);
    level.time = 6000; G_RunEntity(thinker); T_FEQ(enemy->health.value, 861, .001f);
    level.time = 11331; G_RunEntity(thinker); T_ASSERT(!thinker->inuse);
    T_FEQ(enemy->health.value, 861, .001f);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* A targeted nova damages around its victim; nearby units do not receive the direct-target bonus. */
TEST(wc3_ability_lifecycle, frost_nova_uses_victim_center_and_separate_direct_damage) {
    LPEDICT caster = review_setup(), enemy = review_unit(1, 500), near = review_unit(1, 550);
    LPEDICT far = review_unit(1, 50), ally = review_unit(0, 550);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("AUfn"), enemy));
    T_FEQ(enemy->health.value, 850, .001f); T_FEQ(near->health.value, 950, .001f);
    T_FEQ(far->health.value, 1000, .001f); T_FEQ(ally->health.value, 1000, .001f);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Ordinary corpses restore selection and the idle move instead of retaining their decay callback. */
TEST(wc3_ability_lifecycle, resurrection_retires_death_state_and_rejects_empty_cast) {
    LPEDICT caster = review_setup(), ally = review_unit(0, 100), enemy = review_unit(1, 100);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    T_ASSERT(!S_CastNoTargetSpell(caster, FS_SLKKey("AHre")));
    unit_die(ally, enemy); ally->aiflags |= AI_HOLD_FRAME;
    T_ASSERT(S_CastNoTargetSpell(caster, FS_SLKKey("AHre")));
    T_FEQ(ally->health.value, ally->health.max_value, .001f);
    T_ASSERT(!(ally->svflags & SVF_DEADMONSTER)); T_ASSERT(!(ally->s.flags & EF_NOT_SELECTABLE));
    T_ASSERT(!(ally->aiflags & AI_HOLD_FRAME));
    T_ASSERT(!S_CastNoTargetSpell(caster, FS_SLKKey("AHre")));
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Saving a live drain preserves its owner/target pointers, callback, deadline and cast identity together. */
TEST(wc3_ability_lifecycle, live_drain_continues_once_after_save_load) {
    LPCSTR path = "/tmp/openwarcraft3-skill-drain-save.bin";
    LPEDICT caster = review_setup(), enemy = review_unit(1, 100);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("AHdr"), enemy));
    LPEDICT thinker = review_thinker(caster);
    T_ASSERT(WriteGame(path)); caster->channel.code = 0; thinker->think = NULL;
    T_ASSERT(ReadGame(path));
    level.time += 1000; G_RunEntity(thinker);
    T_FEQ(enemy->mana.value, 85, .001f); T_EQ(caster->channel.code, FS_SLKKey("AHdr"));
    G_RunEntity(thinker); T_FEQ(enemy->mana.value, 85, .001f);
    remove(path); G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* The ROC Data11/Data12 spell schema and TFT DataA1/DataB1 schema must produce the same stock six waves. */
TEST(wc3_ability_lifecycle, blizzard_roc_data_columns_keep_all_authored_waves) {
    char roc[sizeof(review_slk)];
    memcpy(roc, review_slk, sizeof(roc));
    FOR_LOOP(i, 5) {
        char field[] = "DataA1"; field[4] += i;
        char *at = strstr(roc, field);
        T_NOT_NULL(at);
        if (at) { at[4] = '1'; at[5] = '1' + i; }
    }
    LPEDICT caster = review_setup(), enemy = review_unit(1, 100);
    slkTestData_t *rows = parse_slk_string(roc), *old = G_SetSLKRows("AbilityData", rows);
    T_FEQ(S_SpellData(FS_SLKKey("AHbz"), 1, 1), 6, .001f);
    T_ASSERT(S_CastPointTargetSpell(caster, FS_SLKKey("AHbz"), &enemy->s.origin2));
    LPEDICT thinker = review_thinker(caster);
    FOR_LOOP(i, 5) { level.time += 1000; G_RunEntity(thinker); }
    T_FEQ(enemy->health.value, 820, .001f); T_ASSERT(!thinker->inuse);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* UnitBalance creep level is unrelated to Hero identity when selecting Storm Bolt's duration. */
TEST(wc3_ability_lifecycle, storm_bolt_uses_hero_identity_for_stun_duration) {
    LPEDICT caster = review_setup(), hero = review_unit(1, 100), creep = review_unit(1, 150);
    UnitBalance_t balance = { .level = 8 }, hero_row = { .level = 1, .strength = 20 };
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    hero->class_id = MAKEFOURCC('H','p','a','l'); hero->data.UnitBalance = &hero_row; creep->data.UnitBalance = &balance;
    T_ASSERT(G_UnitIsHero(hero)); T_ASSERT(!G_UnitIsHero(creep));
    T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("AHtb"), hero));
    FILTER_EDICTS(ent, ent->owner == caster && ent->movetype == MOVETYPE_FLYMISSILE) {
        T_FEQ(ent->wait, 3, .001f); ent->currentmove->endfunc(ent);
    }
    T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("AHtb"), creep));
    FILTER_EDICTS(ent, ent->owner == caster && ent->movetype == MOVETYPE_FLYMISSILE) T_FEQ(ent->wait, 5, .001f);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Fractional authored intervals accumulate on scheduled deadlines instead of rounding each tick up to FRAMETIME. */
TEST(wc3_ability_lifecycle, flame_strike_fractional_interval_keeps_cadence_on_server_frames) {
    LPEDICT caster = review_setup(), enemy = review_unit(1, 100);
    slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
    T_ASSERT(S_CastPointTargetSpell(caster, FS_SLKKey("AHfs"), &enemy->s.origin2));
    LPEDICT thinker = review_thinker(caster);
    while (level.time < 5000) { level.time += FRAMETIME; G_RunEntity(thinker); }
    T_FEQ(enemy->health.value, 865, .001f); T_EQ(thinker->freetime, 6000);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Replacement orders interrupt at dispatch, even before a Move changes coordinates or an Attack swings. */
TEST(wc3_ability_lifecycle, stop_move_and_attack_retire_channel_before_motion) {
    LPCSTR orders[] = { "stop", "move", "attack" };
    FOR_LOOP(i, 3) {
        LPEDICT caster = review_setup(), enemy = review_unit(1, 100);
        slkTestData_t *rows = parse_slk_string(review_slk), *old = G_SetSLKRows("AbilityData", rows);
        caster->unitinfo.MoveSpeed = 300; caster->movetype = MOVETYPE_STEP;
        caster->attack1.range = 600; caster->attack1.damageBase = 10; caster->attack1.cooldown = 1;
        T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("AHdr"), enemy));
        LPEDICT thinker = review_thinker(caster);
        if (i == 0) T_ASSERT(unit_issueimmediateorder(caster, orders[i]));
        else if (i == 1) T_ASSERT(unit_issueorder(caster, orders[i], &MAKE(VECTOR2, .x = 300)));
        else T_ASSERT(unit_issuetargetorder(caster, orders[i], enemy));
        umove_t const *move = caster->currentmove;
        T_EQ(caster->channel.code, 0); T_FEQ(caster->s.origin2.x, 0, .001f);
        level.time += 1000; G_RunEntity(thinker);
        T_ASSERT(!thinker->inuse); T_FEQ(enemy->mana.value, 100, .001f); T_ASSERT(caster->currentmove == move);
        G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
    }
}

/* Multiple authored pulses can fit in one server frame; the patch preserves them instead of clamping the interval. */
TEST(wc3_ability_lifecycle, flame_strike_custom_interval_can_tick_twice_per_frame) {
    char slk[sizeof(review_slk)];
    memcpy(slk, review_slk, sizeof(slk));
    char *interval = strstr(slk, "0.33");
    T_NOT_NULL(interval);
    if (interval) memcpy(interval, "0.05", 4);
    LPEDICT caster = review_setup(), enemy = review_unit(1, 100);
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    T_ASSERT(S_CastPointTargetSpell(caster, FS_SLKKey("AHfs"), &enemy->s.origin2));
    LPEDICT thinker = review_thinker(caster);
    level.time = 2300; G_RunEntity(thinker); T_FEQ(enemy->health.value, 1000, .001f);
    level.time = 2400; G_RunEntity(thinker); T_FEQ(enemy->health.value, 970, .001f);
    level.time = 2500; G_RunEntity(thinker); T_FEQ(enemy->health.value, 940, .001f);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}
#endif
