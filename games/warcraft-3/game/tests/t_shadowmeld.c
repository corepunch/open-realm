#ifdef BZ_TESTS
#include "test.h"
#include "../skills/s_skills.h"

#define ID_ASHM MAKEFOURCC('A', 's', 'h', 'm')
#define ID_AHID MAKEFOURCC('A', 'h', 'i', 'd')

edict_t *alloc_test_unit(uint32_t class_id, float x, float y);
void reset_entities(void);
void setup_test_world(void);
slkTestData_t *parse_slk_string(char const *text);
void free_slk_rows(slkTestData_t *rows);

static char const shadowmeld_slk[] =
    "ID;PWXL;N;E8;Y3;X8\n"
    "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
    "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\n"
    "C;Y1;X7;K\"Dur1\"\nC;Y1;X8;K\"HeroDur1\"\n"
    "C;Y2;X1;K\"Ashm\"\nC;Y2;X2;K\"Ashm\"\nC;Y2;X3;K\"1\"\n"
    "C;Y2;X4;K\"0\"\nC;Y2;X5;K\"0\"\nC;Y2;X6;K\"0\"\nC;Y2;X7;K\"0\"\nC;Y2;X8;K\"0\"\n"
    "C;Y3;X1;K\"Ahid\"\nC;Y3;X2;K\"Ahid\"\nC;Y3;X3;K\"1\"\n"
    "C;Y3;X4;K\"0\"\nC;Y3;X5;K\"0\"\nC;Y3;X6;K\"0\"\nC;Y3;X7;K\"0\"\nC;Y3;X8;K\"0\"\nE\n";

typedef struct {
    slkTestData_t *rows, *old;
    edict_t *unit;
} shadowmeldFix_t;

static void shadowmeld_setup_as(shadowmeldFix_t *fix, uint32_t class_id) {
    reset_entities(); setup_test_world(); level.time = 1000;
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    memset(level.alliances, 0, sizeof(level.alliances));
    fix->rows = parse_slk_string(shadowmeld_slk);
    fix->old = G_SetSLKRows("AbilityData", fix->rows);
    fix->unit = alloc_test_unit(class_id, 0, 0);
    fix->unit->s.player = 0;
    fix->unit->svflags |= SVF_MONSTER;
    fix->unit->abilities.added[0] = ID_ASHM;
    ARRAY_COUNT(fix->unit->abilities.added) = 1;
    fix->unit->stand = unit_stand;
    unit_stand(fix->unit);
}

static void shadowmeld_setup(shadowmeldFix_t *fix) {
    shadowmeld_setup_as(fix, MAKEFOURCC('e', 'a', 'r', 'c'));
}

static void shadowmeld_done(shadowmeldFix_t *fix) {
    G_SetSLKRows("AbilityData", fix->old);
    free_slk_rows(fix->rows);
}

static void shadowmeld_tick(edict_t *unit, uint32_t ms) {
    level.time += ms;
    S_RunAbilityUpdates(unit);
}

TEST(wc3_shadowmeld, ability_classes_are_not_wind_walk) {
    abilityitem_t passive = S_AbilityItem(ID_ASHM);
    abilityitem_t hide = S_AbilityItem(ID_AHID);
    T_NOT_NULL(passive.ability); T_NOT_NULL(hide.ability);
    T_EQ(passive.ability->proc, CAbilityShadowMeld);
    T_EQ(hide.ability->proc, CAbilityShadowMeldAkama);
    T_ASSERT((passive.ability->flags & (AB_PASSIVE | AB_UPDATE | AB_SPELL)) ==
             (AB_PASSIVE | AB_UPDATE | AB_SPELL));
    T_ASSERT((hide.ability->flags & (AB_PASSIVE | AB_UPDATE | AB_SPELL)) ==
             (AB_PASSIVE | AB_UPDATE | AB_SPELL));
    T_EQ(G_OrderId("ambush"), 852131);
}

TEST(wc3_shadowmeld, akama_variant_is_distinct_and_suppresses_auto_acquire) {
    shadowmeldFix_t fix;
    shadowmeld_setup(&fix);
    fix.unit->abilities.added[0] = ID_AHID;
    ARRAY_COUNT(fix.unit->abilities.added) = 1;
    G_SetTimeOfDay(game.constants.duskTimeGameHours);
    G_UpdateTimeOfDay();

    T_ASSERT(S_UnitAbilityEvent(fix.unit, A_NO_ACQUIRE));
    S_RunAbilityUpdates(fix.unit);
    T_ASSERT(fix.unit->shadowmeld.fading);
    shadowmeld_tick(fix.unit, 1500);
    T_ASSERT(S_ShadowMeldActive(fix.unit));
    T_ASSERT(unit_issueimmediateorder(fix.unit, "ambush"));
    T_ASSERT(fix.unit->shadowmeld.hide_order_active);

    shadowmeld_done(&fix);
}

TEST(wc3_shadowmeld, passive_fades_after_stationary_night_interval) {
    shadowmeldFix_t fix;
    shadowmeld_setup(&fix);
    G_SetTimeOfDay(game.constants.duskTimeGameHours);
    G_UpdateTimeOfDay();

    S_RunAbilityUpdates(fix.unit);
    T_ASSERT(fix.unit->shadowmeld.fading);
    T_ASSERT(!S_ShadowMeldActive(fix.unit));
    shadowmeld_tick(fix.unit, 1499);
    T_ASSERT(!S_ShadowMeldActive(fix.unit));
    shadowmeld_tick(fix.unit, 1);
    T_ASSERT(S_ShadowMeldActive(fix.unit));
    T_ASSERT(S_UnitIsInvisibleToPlayer(fix.unit, 1));
    T_ASSERT(!S_UnitIsInvisibleToPlayer(fix.unit, 0));

    shadowmeld_done(&fix);
}

TEST(wc3_shadowmeld, daylight_cancels_fade_and_active_invisibility) {
    shadowmeldFix_t fix;
    shadowmeld_setup(&fix);
    G_SetTimeOfDay(game.constants.duskTimeGameHours);
    G_UpdateTimeOfDay();
    S_RunAbilityUpdates(fix.unit);
    shadowmeld_tick(fix.unit, 1500);
    T_ASSERT(S_ShadowMeldActive(fix.unit));

    G_SetTimeOfDay(12.0f);
    G_UpdateTimeOfDay();
    S_RunAbilityUpdates(fix.unit);
    T_ASSERT(!fix.unit->shadowmeld.fading);
    T_ASSERT(!S_ShadowMeldActive(fix.unit));
    T_ASSERT(!S_UnitIsInvisibleToPlayer(fix.unit, 1));

    shadowmeld_done(&fix);
}

TEST(wc3_shadowmeld, hide_ambush_suppresses_acquisition_and_uses_same_fade) {
    shadowmeldFix_t fix;
    shadowmeld_setup(&fix);
    G_SetTimeOfDay(game.constants.duskTimeGameHours);
    G_UpdateTimeOfDay();

    T_ASSERT(unit_issueimmediateorder(fix.unit, "ambush"));
    T_ASSERT(fix.unit->shadowmeld.hide_order_active);
    T_ASSERT(S_UnitAbilityEvent(fix.unit, A_NO_ACQUIRE));
    S_RunAbilityUpdates(fix.unit);
    shadowmeld_tick(fix.unit, 1500);
    T_ASSERT(S_ShadowMeldActive(fix.unit));

    S_UnitAbilityEvent(fix.unit, A_MOVE_LEAVE);
    T_ASSERT(!fix.unit->shadowmeld.hide_order_active);
    T_ASSERT(!S_ShadowMeldActive(fix.unit));

    shadowmeld_done(&fix);
}

TEST(wc3_shadowmeld, hide_button_preserves_already_active_shadowmeld) {
    shadowmeldFix_t fix;
    edict_t *clent = &g_edicts[0];
    gameClient_t *client = &game.clients[0];
    cstring_t button[] = { "button", "Ashm" };

    shadowmeld_setup(&fix);
    clent->client = client;
    fix.unit->s.player = client->ps.number;
    G_SelectEntity(client, fix.unit);
    G_SetTimeOfDay(game.constants.duskTimeGameHours);
    G_UpdateTimeOfDay();

    S_RunAbilityUpdates(fix.unit);
    shadowmeld_tick(fix.unit, 1500);
    T_ASSERT(S_ShadowMeldActive(fix.unit));

    G_ClientCommand(clent, 2, button);
    T_ASSERT(S_ShadowMeldActive(fix.unit));
    T_ASSERT(!fix.unit->shadowmeld.fading);
    T_ASSERT(fix.unit->shadowmeld.hide_order_active);

    shadowmeld_done(&fix);
}

TEST(wc3_shadowmeld, stop_retires_explicit_hide_then_allows_passive_refade) {
    shadowmeldFix_t fix;
    shadowmeld_setup(&fix);
    G_SetTimeOfDay(game.constants.duskTimeGameHours);
    G_UpdateTimeOfDay();

    T_ASSERT(unit_issueimmediateorder(fix.unit, "ambush"));
    S_RunAbilityUpdates(fix.unit);
    shadowmeld_tick(fix.unit, 1500);
    T_ASSERT(S_ShadowMeldActive(fix.unit));
    T_ASSERT(fix.unit->shadowmeld.hide_order_active);

    T_ASSERT(unit_issueimmediateorder(fix.unit, "stop"));
    T_ASSERT(!fix.unit->shadowmeld.hide_order_active);
    T_ASSERT(!S_ShadowMeldActive(fix.unit));
    S_RunAbilityUpdates(fix.unit);
    T_ASSERT(fix.unit->shadowmeld.fading);
    shadowmeld_tick(fix.unit, 1500);
    T_ASSERT(S_ShadowMeldActive(fix.unit));
    T_ASSERT(!fix.unit->shadowmeld.hide_order_active);

    shadowmeld_done(&fix);
}

TEST(wc3_shadowmeld, hold_position_allows_passive_shadowmeld_without_hide_hold_fire) {
    shadowmeldFix_t fix;
    shadowmeld_setup(&fix);
    G_SetTimeOfDay(game.constants.duskTimeGameHours);
    G_UpdateTimeOfDay();

    T_ASSERT(unit_issueimmediateorder(fix.unit, "holdposition"));
    T_ASSERT(fix.unit->movement.holding_position);
    T_ASSERT(!fix.unit->shadowmeld.hide_order_active);
    S_RunAbilityUpdates(fix.unit);
    T_ASSERT(fix.unit->shadowmeld.fading);
    shadowmeld_tick(fix.unit, 1500);
    T_ASSERT(S_ShadowMeldActive(fix.unit));
    T_ASSERT(fix.unit->movement.holding_position);
    T_ASSERT(!S_UnitAbilityEvent(fix.unit, A_NO_ACQUIRE));

    shadowmeld_done(&fix);
}

TEST(wc3_shadowmeld, attack_order_immediately_breaks_shadowmeld_and_hide) {
    static UnitWeapons_t const weapons = { .attacksEnabled = 1 };
    shadowmeldFix_t fix;
    edict_t *enemy;
    shadowmeld_setup(&fix);
    fix.unit->data.UnitWeapons = &weapons;
    fix.unit->attack1.type = ATK_NORMAL;
    fix.unit->attack1.targetsAllowed = WC3_TARGET_FLAG_GROUND;
    G_SetTimeOfDay(game.constants.duskTimeGameHours);
    G_UpdateTimeOfDay();

    enemy = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), 128, 0);
    enemy->s.player = 1;
    enemy->targtype = TARG_GROUND;
    enemy->svflags |= SVF_MONSTER;
    T_ASSERT(unit_issueimmediateorder(fix.unit, "ambush"));
    S_RunAbilityUpdates(fix.unit);
    shadowmeld_tick(fix.unit, 1500);
    T_ASSERT(S_ShadowMeldActive(fix.unit));
    T_ASSERT(fix.unit->shadowmeld.hide_order_active);

    order_attack(fix.unit, enemy);
    T_ASSERT(!S_ShadowMeldActive(fix.unit));
    T_ASSERT(!fix.unit->shadowmeld.fading);
    T_ASSERT(!fix.unit->shadowmeld.hide_order_active);
    T_EQ(fix.unit->goalentity, enemy);
    T_NOT_NULL(fix.unit->currentmove);
    T_EQ(fix.unit->currentmove->proc, CAbilityAttack);

    shadowmeld_done(&fix);
}

TEST(wc3_shadowmeld, explicit_hide_does_not_retaliate_when_hit_during_fade) {
    static UnitWeapons_t const weapons = { .attacksEnabled = 1 };
    shadowmeldFix_t fix;
    edict_t *enemy;
    shadowmeld_setup(&fix);
    fix.unit->health.value = fix.unit->health.max_value = 100.0f;
    fix.unit->data.UnitWeapons = &weapons;
    fix.unit->attack1.type = ATK_NORMAL;
    fix.unit->attack1.targetsAllowed = WC3_TARGET_FLAG_GROUND;
    G_SetTimeOfDay(game.constants.duskTimeGameHours);
    G_UpdateTimeOfDay();

    enemy = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), 128, 0);
    enemy->s.player = 1;
    enemy->targtype = TARG_GROUND;
    enemy->svflags |= SVF_MONSTER;
    T_ASSERT(unit_issueimmediateorder(fix.unit, "ambush"));
    S_RunAbilityUpdates(fix.unit);
    T_ASSERT(fix.unit->shadowmeld.fading);
    T_ASSERT(!S_ShadowMeldActive(fix.unit));
    T_ASSERT(fix.unit->shadowmeld.hide_order_active);

    T_Damage(fix.unit, enemy, 1);
    T_ASSERT(!S_ShadowMeldActive(fix.unit));
    T_ASSERT(fix.unit->shadowmeld.hide_order_active);
    T_ASSERT(fix.unit->shadowmeld.fading);
    T_ASSERT(fix.unit->goalentity != enemy);
    T_ASSERT(!fix.unit->currentmove || fix.unit->currentmove->proc != CAbilityAttack);

    shadowmeld_done(&fix);
}

TEST(wc3_shadowmeld, explicit_hide_blocks_idle_automatic_attack_acquisition) {
    static UnitWeapons_t const weapons = { .attacksEnabled = 1 };
    shadowmeldFix_t fix;
    edict_t *enemy;
    uint32_t i;
    shadowmeld_setup_as(&fix, MAKEFOURCC('E', 't', 'y', 'r'));
    fix.unit->data.UnitWeapons = &weapons;
    fix.unit->attack1.type = ATK_NORMAL;
    fix.unit->attack1.cooldown = 1.0f;
    fix.unit->attack1.damageBase = 10;
    fix.unit->attack1.range = 150.0f;
    fix.unit->attack1.targetsAllowed = WC3_TARGET_FLAG_GROUND;
    fix.unit->runtime.acquisition_range = 128.0f;
    enemy = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), 64, 0);
    enemy->s.player = 1;
    enemy->targtype = TARG_GROUND;
    enemy->svflags |= SVF_MONSTER;
    gi.LinkEntity(fix.unit); gi.LinkEntity(enemy);
    G_SetTimeOfDay(game.constants.duskTimeGameHours);
    G_UpdateTimeOfDay();

    T_ASSERT(unit_issueimmediateorder(fix.unit, "ambush"));
    T_ASSERT(fix.unit->shadowmeld.hide_order_active);
    T_ASSERT(S_UnitAbilityEvent(fix.unit, A_NO_ACQUIRE));
    T_ASSERT(!S_UnitAbilityEvent(fix.unit, A_IDLE));
    T_ASSERT(!(fix.unit->aiflags & AI_AUTOCAST_ACTIVE));
    T_FEQ(G_AcquisitionRange(fix.unit), 128.0f, 0.01f);
    T_ASSERT(G_FindNearestEnemy(fix.unit, 128.0f) == enemy);

    /* Exercise a frame on which ai_stand's staggered acquisition scan runs. */
    for (i = 0; i < 300; i++) {
        if (G_ShouldAcquireThisFrame(fix.unit)) break;
        level.time++;
    }
    T_ASSERT(i < 300);
    T_ASSERT(G_ShouldAcquireThisFrame(fix.unit));
    ai_stand(fix.unit);

    T_ASSERT(fix.unit->shadowmeld.hide_order_active);
    T_NULL(fix.unit->goalentity);
    T_ASSERT(!fix.unit->currentmove || fix.unit->currentmove->proc != CAbilityAttack);

    shadowmeld_done(&fix);
}

TEST(wc3_save, shadowmeld_state_round_trips) {
    cstring_t filename = "/tmp/openwarcraft3-shadowmeld-save.bin";
    shadowmeldFix_t fix;
    uint32_t unit_number;

    shadowmeld_setup(&fix);
    G_SetTimeOfDay(game.constants.duskTimeGameHours);
    G_UpdateTimeOfDay();
    T_ASSERT(unit_issueimmediateorder(fix.unit, "ambush"));
    S_RunAbilityUpdates(fix.unit);
    shadowmeld_tick(fix.unit, 1500);
    T_ASSERT(S_ShadowMeldActive(fix.unit));
    unit_number = fix.unit->s.number;

    T_ASSERT(WriteGame(filename));
    S_ShadowMeldBreak(fix.unit);
    T_ASSERT(ReadGame(filename));
    fix.unit = g_edicts + unit_number;
    T_ASSERT(fix.unit->shadowmeld.hide_order_active);
    T_ASSERT(S_ShadowMeldActive(fix.unit));
    T_ASSERT(S_UnitAbilityEvent(fix.unit, A_NO_ACQUIRE));

    remove(filename);
    shadowmeld_done(&fix);
}

TEST(wc3_shadowmeld, ambush_is_rejected_during_day) {
    shadowmeldFix_t fix;
    shadowmeld_setup(&fix);
    G_SetTimeOfDay(12.0f);
    G_UpdateTimeOfDay();

    T_ASSERT(!unit_issueimmediateorder(fix.unit, "ambush"));
    T_ASSERT(!fix.unit->shadowmeld.hide_order_active);

    shadowmeld_done(&fix);
}
#endif
