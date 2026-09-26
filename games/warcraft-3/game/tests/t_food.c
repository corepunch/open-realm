#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"

edict_t * alloc_test_unit(uint32_t class_id, float x, float y);
void setup_test_world(void);
void unit_die(edict_t * self, edict_t * attacker);
void ai_train_build(edict_t * ent);
void unit_build(edict_t * ent, uint32_t class_id);
bool run_test_jass(cstring_t src);

bool UI_TestUpkeepBodyHasTierRanges(cstring_t text);
void UI_TestFormatUpkeepLegend(string_t out, uint32_t out_size, cstring_t info, bool use_wood_info);

static cstring_t food_limits_off_cvar(cstring_t name, cstring_t fallback) {
    return !strcmp(name, "wc3_food_limits") ? "0" : fallback;
}

typedef struct {
    pfWriteType_t types[16];
    int32_t integral[16];
    float real[16];
    vector3_t position;
    char text[32];
    uint32_t count;
    uint32_t font_size;
    char font_name[MAX_PATHLEN];
    uint32_t multicast_count;
    multicast_t multicast_to;
    vector3_t multicast_origin;
} resourceGainCapture_t;

static resourceGainCapture_t resource_gain_capture;

static void resource_gain_test_write(pfWriteType_t type, void const *value) {
    uint32_t const slot = resource_gain_capture.count++;

    uint32_t const capacity = sizeof(resource_gain_capture.types) / sizeof(resource_gain_capture.types[0]);

    if (slot < capacity) resource_gain_capture.types[slot] = type;
    if (!value || slot >= capacity) return;
    switch (type) {
        case PF_BYTE:
        case PF_SHORT:
        case PF_LONG:
            resource_gain_capture.integral[slot] = *(int32_t const *)value;
            break;
        case PF_FLOAT:
            resource_gain_capture.real[slot] = *(float const *)value;
            break;
        case PF_POSITION:
            resource_gain_capture.position = *(vector3_t const *)value;
            break;
        case PF_STRING:
            strlcpy(resource_gain_capture.text, value, sizeof(resource_gain_capture.text));
            break;
        default:
            break;
    }
}

static int resource_gain_test_font(cstring_t name, uint32_t size) {
    strlcpy(resource_gain_capture.font_name, name ? name : "", sizeof(resource_gain_capture.font_name));
    resource_gain_capture.font_size = size;
    return 17;
}

static void resource_gain_test_multicast(vector3_t const * origin, multicast_t to) {
    resource_gain_capture.multicast_count++;
    resource_gain_capture.multicast_to = to;
    if (origin) resource_gain_capture.multicast_origin = *origin;
}

TEST(wc3_food, unit_food_accounting_is_delta_based_and_death_releases_it) {
    gameClient_t * client = &game.clients[0];
    edict_t * unit = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0.0f, 0.0f);
    UnitBalance_t balance = *unit->data.UnitBalance;

    balance.foodUsed = 3;
    balance.foodMade = 6;
    unit->data.UnitBalance = &balance;
    unit->s.player = client->ps.number;

    G_ActivateUnitFood(unit);
    G_ActivateUnitFood(unit);

    T_EQ(unit->food.used, 3);
    T_EQ(unit->food.made, 6);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_USED], 3);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP], 6);

    unit_die(unit, NULL);

    T_EQ(unit->food.used, 0);
    T_EQ(unit->food.made, 0);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_USED], 0);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP], 0);
}

TEST(wc3_food, explicit_remove_releases_used_and_made_food) {
    gameClient_t * client = &game.clients[0];
    edict_t * unit = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0.0f, 0.0f);
    UnitBalance_t balance = *unit->data.UnitBalance;

    balance.foodUsed = 2;
    balance.foodMade = 6;
    unit->data.UnitBalance = &balance;
    unit->s.player = client->ps.number;
    G_ActivateUnitFood(unit);

    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_USED], 2);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP], 6);

    G_FreeEdict(unit);

    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_USED], 0);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP], 0);
}

TEST(wc3_food, owner_change_transfers_accounted_food) {
    gameClient_t * old_client = &game.clients[0];
    gameClient_t * new_client = &game.clients[1];
    edict_t * unit = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0.0f, 0.0f);
    UnitBalance_t balance = *unit->data.UnitBalance;

    balance.foodUsed = 3;
    balance.foodMade = 6;
    unit->data.UnitBalance = &balance;
    unit->s.player = old_client->ps.number;
    G_ActivateUnitFood(unit);

    G_SetUnitPlayer(unit, new_client->ps.number);

    T_EQ(unit->s.player, new_client->ps.number);
    T_EQ(old_client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_USED], 0);
    T_EQ(old_client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP], 0);
    T_EQ(new_client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_USED], 3);
    T_EQ(new_client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP], 6);
}


TEST(wc3_food, food_cap_ceiling_limits_effective_supply_without_losing_raw_cap) {
    gameClient_t * client = &game.clients[0];

    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 120;
    client->ps.stats[PLAYERSTATE_FOOD_CAP_CEILING] = 100;
    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_USED] = 99;

    T_EQ(G_GetEffectiveFoodCap(client), 100);
    T_ASSERT(G_PlayerHasFoodFor(client, 1));
    T_ASSERT(!G_PlayerHasFoodFor(client, 2));
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP], 120);

    client->ps.stats[PLAYERSTATE_FOOD_CAP_CEILING] = 150;
    T_EQ(G_GetEffectiveFoodCap(client), 120);
}

TEST(wc3_food, food_limits_cvar_allows_training_over_cap_but_keeps_accounting) {
    gameClient_t * client = &game.clients[0];
    edict_t * unit = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0.0f, 0.0f);
    UnitBalance_t balance = *unit->data.UnitBalance;
    cstring_t (*saved_cvar)(cstring_t, cstring_t) = gi.CvarString;

    balance.foodUsed = 3;
    unit->data.UnitBalance = &balance;
    unit->s.player = client->ps.number;
    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 0;
    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_USED] = 50;
    gi.CvarString = food_limits_off_cvar;

    T_ASSERT(G_PlayerHasFoodFor(client, 3));
    T_ASSERT(G_ReserveTrainingFood(unit));
    T_EQ(unit->food.used, 3);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_USED], 53);
    T_EQ(client->ps.stats[PLAYERSTATE_GOLD_UPKEEP_RATE], 70);

    gi.CvarString = saved_cvar;
}

TEST(wc3_food, upkeep_tier_rate_helpers_match_gameplay_tax_data) {
    T_EQ(G_GetUpkeepGoldRateForTier(0), 100);
    T_EQ(G_GetUpkeepGoldRateForTier(1), 70);
    T_EQ(G_GetUpkeepGoldRateForTier(2), 40);
    T_EQ(G_GetUpkeepLumberRateForTier(0), 100);
    T_EQ(G_GetUpkeepLumberRateForTier(1), 100);
    T_EQ(G_GetUpkeepLumberRateForTier(2), 100);
}

TEST(wc3_food, upkeep_tooltip_detects_embedded_legend_ranges) {
    T_ASSERT(UI_TestUpkeepBodyHasTierRanges(
        "0-50 Food: No Upkeep|n51-80 Food: Low Upkeep|n81-100 Food: High Upkeep"));
    T_ASSERT(!UI_TestUpkeepBodyHasTierRanges(
        "Upkeep is determined by the amount of Food your forces are using."));
}

TEST(wc3_food, upkeep_tooltip_fallback_legend_uses_gameplay_thresholds_and_rates) {
    char text[1024];

    UI_TestFormatUpkeepLegend(text, sizeof(text),
                              "|n%d-%d Food: %s|R (%d%% income)", false);
    T_ASSERT(strstr(text, "0-50 Food:"));
    T_ASSERT(strstr(text, "51-80 Food:"));
    T_ASSERT(strstr(text, "81-100 Food:"));
    T_ASSERT(strstr(text, "(100% income)"));
    T_ASSERT(strstr(text, "(70% income)"));
    T_ASSERT(strstr(text, "(40% income)"));
}

TEST(wc3_food, upkeep_rates_follow_food_used_thresholds) {
    gameClient_t * client = &game.clients[0];
    edict_t * unit = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0.0f, 0.0f);

    unit->s.player = client->ps.number;

    G_SetUnitFoodUsed(unit, 50);
    T_EQ(client->ps.stats[PLAYERSTATE_GOLD_UPKEEP_RATE], 100);
    T_EQ(client->ps.stats[PLAYERSTATE_LUMBER_UPKEEP_RATE], 100);

    G_SetUnitFoodUsed(unit, 51);
    T_EQ(client->ps.stats[PLAYERSTATE_GOLD_UPKEEP_RATE], 70);

    G_SetUnitFoodUsed(unit, 81);
    T_EQ(client->ps.stats[PLAYERSTATE_GOLD_UPKEEP_RATE], 40);

    G_SetUnitFoodUsed(unit, 50);
    T_EQ(client->ps.stats[PLAYERSTATE_GOLD_UPKEEP_RATE], 100);
}

TEST(wc3_food, resource_income_applies_upkeep_rate_only_to_selected_resource) {
    player_t * player = &game.clients[0].ps;

    player->stats[PLAYERSTATE_GOLD_UPKEEP_RATE] = 70;
    player->stats[PLAYERSTATE_LUMBER_UPKEEP_RATE] = 100;
    T_EQ(G_ApplyResourceIncome(player, PLAYERSTATE_RESOURCE_GOLD, 10), 7);
    T_EQ(G_ApplyResourceIncome(player, PLAYERSTATE_RESOURCE_LUMBER, 10), 10);

    player->stats[PLAYERSTATE_GOLD_UPKEEP_RATE] = 40;
    T_EQ(G_ApplyResourceIncome(player, PLAYERSTATE_RESOURCE_GOLD, 10), 4);
}

TEST(wc3_food, credited_gold_emits_net_resource_gain_world_text) {
    player_t * player = &game.clients[0].ps;
    edict_t * source = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 100.0f, 200.0f);
    void (*saved_write)(pfWriteType_t, void const *) = gi.Write;
    void (*saved_multicast)(vector3_t const *, multicast_t) = gi.multicast;
    int (*saved_font)(cstring_t, uint32_t) = gi.FontIndex;

    memset(&resource_gain_capture, 0, sizeof(resource_gain_capture));
    source->s.origin = MAKE(vector3_t, 100.0f, 200.0f, 3.0f);
    player->stats[PLAYERSTATE_RESOURCE_GOLD] = 500;
    player->stats[PLAYERSTATE_GOLD_UPKEEP_RATE] = 70;
    gi.Write = resource_gain_test_write;
    gi.multicast = resource_gain_test_multicast;
    gi.FontIndex = resource_gain_test_font;

    T_EQ(G_CreditResourceIncome(player, source, PLAYERSTATE_RESOURCE_GOLD, 10), 7);
    T_EQ(player->stats[PLAYERSTATE_RESOURCE_GOLD], 507);
    T_EQ(resource_gain_capture.count, 10);
    T_EQ(resource_gain_capture.types[0], PF_BYTE);
    T_EQ(resource_gain_capture.integral[0], svc_temp_entity);
    T_EQ(resource_gain_capture.types[1], PF_BYTE);
    T_EQ(resource_gain_capture.integral[1], TE_FLOATING_TEXT);
    T_EQ(resource_gain_capture.types[2], PF_POSITION);
    T_FEQ(resource_gain_capture.position.x, 100.0f, 0.001f);
    T_FEQ(resource_gain_capture.position.y, 200.0f, 0.001f);
    T_FEQ(resource_gain_capture.position.z, 13.0f, 0.001f);
    T_EQ(resource_gain_capture.types[3], PF_STRING);
    T_STREQ(resource_gain_capture.text, "+7");
    T_EQ(resource_gain_capture.types[4], PF_LONG);
    T_EQ((uint32_t)resource_gain_capture.integral[4], 0xff00dcffu);
    T_EQ(resource_gain_capture.types[5], PF_SHORT);
    T_EQ(resource_gain_capture.integral[5], 17);
    T_EQ(resource_gain_capture.types[6], PF_LONG);
    T_EQ(resource_gain_capture.integral[6], 2000);
    T_EQ(resource_gain_capture.types[7], PF_LONG);
    T_EQ(resource_gain_capture.integral[7], 1000);
    T_EQ(resource_gain_capture.types[8], PF_FLOAT);
    T_FEQ(resource_gain_capture.real[8], 0.0f, 0.001f);
    T_EQ(resource_gain_capture.types[9], PF_FLOAT);
    T_FEQ(resource_gain_capture.real[9], 60.0f, 0.001f);
    T_STREQ(resource_gain_capture.font_name, "Fonts\\FRIZQT__.TTF");
    T_EQ(resource_gain_capture.font_size, 12);
    T_EQ(resource_gain_capture.multicast_count, 1);
    T_EQ(resource_gain_capture.multicast_to, MULTICAST_ALL);
    T_FEQ(resource_gain_capture.multicast_origin.z, 13.0f, 0.001f);

    gi.Write = saved_write;
    gi.multicast = saved_multicast;
    gi.FontIndex = saved_font;
}

TEST(wc3_food, active_training_waits_for_food_and_only_head_reserves) {
    gameClient_t * client = &game.clients[0];
    edict_t * producer = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 0.0f, 0.0f);
    edict_t * first = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0.0f, 0.0f);
    edict_t * second = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0.0f, 0.0f);
    UnitBalance_t balance = { .buildTime = 10, .foodUsed = 3 };

    setup_test_world();
    producer->s.player = first->s.player = second->s.player = client->ps.number;
    producer->build = first;
    first->build = second;
    first->training = second->training = true;
    first->data.UnitBalance = second->data.UnitBalance = &balance;
    first->health.max_value = second->health.max_value = 100.0f;
    first->health.value = second->health.value = 0.0f;
    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 2;

    ai_train_build(producer);

    T_FEQ(first->health.value, 0.0f, 0.001f);
    T_EQ(first->food.used, 0);
    T_EQ(second->food.used, 0);
    T_ASSERT(first->training_food_wait_notified);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_USED], 0);

    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 10;
    ai_train_build(producer);

    T_ASSERT(first->health.value > 0.0f);
    T_EQ(first->food.used, 3);
    T_EQ(second->food.used, 0);
    T_ASSERT(!first->training_food_wait_notified);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_USED], 3);
}

TEST(wc3_food, first_queued_unit_reserves_food_immediately) {
    gameClient_t * client = &game.clients[0];
    edict_t * producer;
    UnitBalance_t const *balance = G_UnitBalance(MAKEFOURCC('h','f','o','o'));

    setup_test_world();
    producer = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 0.0f, 0.0f);
    producer->s.player = client->ps.number;
    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 100;
    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_USED] = 0;

    unit_build(producer, MAKEFOURCC('h','f','o','o'));

    T_NOT_NULL(producer->build);
    T_ASSERT(producer->build->training);
    T_EQ(producer->build->food.used, MAX(0, balance->foodUsed));
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_USED], MAX(0, balance->foodUsed));
}

TEST(wc3_food, food_blocked_queue_uses_paused_timer_sentinel) {
    gameClient_t * client = &game.clients[0];
    edict_t * producer = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 0.0f, 0.0f);
    edict_t * queued = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0.0f, 0.0f);
    UnitBalance_t balance = { .buildTime = 10, .foodUsed = 3 };
    gameQueueItem_t queue[2];

    producer->s.player = queued->s.player = client->ps.number;
    producer->build = queued;
    queued->training = true;
    queued->data.UnitBalance = &balance;
    queued->health.max_value = 100.0f;
    queued->health.value = 0.0f;
    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 2;

    ai_train_build(producer);

    T_EQ(G_GetBuildQueue(producer, queue, 2), 1);
    T_EQ(queue[0].starttime, 0);
    T_EQ(queue[0].endtime, 0);
}

TEST(wc3_food, cancelling_unreserved_head_does_not_release_unowned_food) {
    gameClient_t * client = &game.clients[0];
    edict_t * producer = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 0.0f, 0.0f);
    edict_t * queued = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0.0f, 0.0f);
    UnitBalance_t balance = { .goldCost = 100, .lumberCost = 20, .foodUsed = 3 };

    producer->s.player = queued->s.player = client->ps.number;
    producer->build = queued;
    queued->training = true;
    queued->data.UnitBalance = &balance;
    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_USED] = 5;
    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 5;
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 0;
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = 0;
    level.events.read = level.events.write = 0;

    T_ASSERT(G_CancelTrainingQueueItem(producer, 0, true));

    T_NULL(producer->build);
    T_ASSERT(!queued->inuse);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_USED], 5);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_GOLD], 100);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER], 20);
    T_EQ(level.events.write, 2);
    T_EQ(level.events.queue[0].type, EVENT_PLAYER_UNIT_TRAIN_CANCEL);
    T_EQ(level.events.queue[1].type, EVENT_UNIT_TRAIN_CANCEL);
}

TEST(wc3_food, cancelling_waiting_item_refunds_cost_without_touching_head_reservation) {
    gameClient_t * client = &game.clients[0];
    edict_t * producer = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 0.0f, 0.0f);
    edict_t * first = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0.0f, 0.0f);
    edict_t * second = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0.0f, 0.0f);
    UnitBalance_t balance = { .goldCost = 100, .lumberCost = 20, .foodUsed = 3 };

    producer->s.player = first->s.player = second->s.player = client->ps.number;
    producer->build = first;
    first->build = second;
    first->training = second->training = true;
    first->data.UnitBalance = second->data.UnitBalance = &balance;
    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 10;
    T_ASSERT(G_ReserveTrainingFood(first));
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 0;
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = 0;

    T_ASSERT(G_CancelTrainingQueueItem(producer, 1, true));

    T_ASSERT(producer->build == first);
    T_NULL(first->build);
    T_ASSERT(!second->inuse);
    T_EQ(first->food.used, 3);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_USED], 3);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_GOLD], 100);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER], 20);
}

TEST(wc3_food, producer_death_cancels_queue_refunds_costs_and_releases_food) {
    gameClient_t * client = &game.clients[0];
    edict_t * producer = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 0.0f, 0.0f);
    edict_t * queued = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0.0f, 0.0f);
    UnitBalance_t balance = { .goldCost = 100, .lumberCost = 20, .foodUsed = 3 };

    producer->s.player = queued->s.player = client->ps.number;
    producer->build = queued;
    queued->training = true;
    queued->data.UnitBalance = &balance;
    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 10;
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 0;
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = 0;
    T_ASSERT(G_ReserveTrainingFood(queued));
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_USED], 3);

    unit_die(producer, NULL);

    T_NULL(producer->build);
    T_ASSERT(!queued->inuse);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_USED], 0);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_GOLD], 100);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER], 20);
}

TEST(wc3_food, rawcode_food_natives_read_unit_object_data) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "    call BJassAssert(GetFoodUsed('hpea') > 0, \"peasant food used\")\n"
        "    call BJassAssert(GetFoodMade('hhou') > 0, \"farm food made\")\n"
        "endfunction\n"));
}

#endif /* BZ_TESTS */
