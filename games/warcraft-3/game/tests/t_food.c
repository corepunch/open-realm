#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"

LPEDICT alloc_test_unit(DWORD class_id, FLOAT x, FLOAT y);
void setup_test_world(void);
void unit_die(LPEDICT self, LPEDICT attacker);
void ai_train_build(LPEDICT ent);
void unit_build(LPEDICT ent, DWORD class_id);
BOOL run_test_jass(LPCSTR src);

static LPCSTR food_limits_off_cvar(LPCSTR name, LPCSTR fallback) {
    return !strcmp(name, "wc3_food_limits") ? "0" : fallback;
}

TEST(wc3_food, unit_food_accounting_is_delta_based_and_death_releases_it) {
    LPGAMECLIENT client = &game.clients[0];
    LPEDICT unit = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0.0f, 0.0f);
    UnitBalance_t balance = *unit->UnitBalance;

    balance.foodUsed = 3;
    balance.foodMade = 6;
    unit->UnitBalance = &balance;
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
    LPGAMECLIENT client = &game.clients[0];
    LPEDICT unit = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0.0f, 0.0f);
    UnitBalance_t balance = *unit->UnitBalance;

    balance.foodUsed = 2;
    balance.foodMade = 6;
    unit->UnitBalance = &balance;
    unit->s.player = client->ps.number;
    G_ActivateUnitFood(unit);

    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_USED], 2);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP], 6);

    G_FreeEdict(unit);

    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_USED], 0);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP], 0);
}

TEST(wc3_food, owner_change_transfers_accounted_food) {
    LPGAMECLIENT old_client = &game.clients[0];
    LPGAMECLIENT new_client = &game.clients[1];
    LPEDICT unit = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0.0f, 0.0f);
    UnitBalance_t balance = *unit->UnitBalance;

    balance.foodUsed = 3;
    balance.foodMade = 6;
    unit->UnitBalance = &balance;
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
    LPGAMECLIENT client = &game.clients[0];

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
    LPGAMECLIENT client = &game.clients[0];
    LPEDICT unit = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0.0f, 0.0f);
    UnitBalance_t balance = *unit->UnitBalance;
    LPCSTR (*saved_cvar)(LPCSTR, LPCSTR) = gi.CvarString;

    balance.foodUsed = 3;
    unit->UnitBalance = &balance;
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

TEST(wc3_food, upkeep_rates_follow_food_used_thresholds) {
    LPGAMECLIENT client = &game.clients[0];
    LPEDICT unit = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0.0f, 0.0f);

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
    LPPLAYER player = &game.clients[0].ps;

    player->stats[PLAYERSTATE_GOLD_UPKEEP_RATE] = 70;
    player->stats[PLAYERSTATE_LUMBER_UPKEEP_RATE] = 100;
    T_EQ(G_ApplyResourceIncome(player, PLAYERSTATE_RESOURCE_GOLD, 10), 7);
    T_EQ(G_ApplyResourceIncome(player, PLAYERSTATE_RESOURCE_LUMBER, 10), 10);

    player->stats[PLAYERSTATE_GOLD_UPKEEP_RATE] = 40;
    T_EQ(G_ApplyResourceIncome(player, PLAYERSTATE_RESOURCE_GOLD, 10), 4);
}

TEST(wc3_food, active_training_waits_for_food_and_only_head_reserves) {
    LPGAMECLIENT client = &game.clients[0];
    LPEDICT producer = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 0.0f, 0.0f);
    LPEDICT first = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0.0f, 0.0f);
    LPEDICT second = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0.0f, 0.0f);
    UnitBalance_t balance = { .buildTime = 10, .foodUsed = 3 };

    setup_test_world();
    producer->s.player = first->s.player = second->s.player = client->ps.number;
    producer->build = first;
    first->build = second;
    first->training = second->training = true;
    first->UnitBalance = second->UnitBalance = &balance;
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
    LPGAMECLIENT client = &game.clients[0];
    LPEDICT producer;
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
    LPGAMECLIENT client = &game.clients[0];
    LPEDICT producer = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 0.0f, 0.0f);
    LPEDICT queued = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0.0f, 0.0f);
    UnitBalance_t balance = { .buildTime = 10, .foodUsed = 3 };
    gameQueueItem_t queue[2];

    producer->s.player = queued->s.player = client->ps.number;
    producer->build = queued;
    queued->training = true;
    queued->UnitBalance = &balance;
    queued->health.max_value = 100.0f;
    queued->health.value = 0.0f;
    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 2;

    ai_train_build(producer);

    T_EQ(G_GetBuildQueue(producer, queue, 2), 1);
    T_EQ(queue[0].starttime, 0);
    T_EQ(queue[0].endtime, 0);
}

TEST(wc3_food, cancelling_unreserved_head_does_not_release_unowned_food) {
    LPGAMECLIENT client = &game.clients[0];
    LPEDICT producer = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 0.0f, 0.0f);
    LPEDICT queued = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0.0f, 0.0f);
    UnitBalance_t balance = { .goldCost = 100, .lumberCost = 20, .foodUsed = 3 };

    producer->s.player = queued->s.player = client->ps.number;
    producer->build = queued;
    queued->training = true;
    queued->UnitBalance = &balance;
    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_USED] = 5;
    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 5;
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 0;
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = 0;

    T_ASSERT(G_CancelTrainingQueueItem(producer, 0, true));

    T_NULL(producer->build);
    T_ASSERT(!queued->inuse);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_USED], 5);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_GOLD], 100);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER], 20);
}

TEST(wc3_food, cancelling_waiting_item_refunds_cost_without_touching_head_reservation) {
    LPGAMECLIENT client = &game.clients[0];
    LPEDICT producer = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 0.0f, 0.0f);
    LPEDICT first = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0.0f, 0.0f);
    LPEDICT second = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0.0f, 0.0f);
    UnitBalance_t balance = { .goldCost = 100, .lumberCost = 20, .foodUsed = 3 };

    producer->s.player = first->s.player = second->s.player = client->ps.number;
    producer->build = first;
    first->build = second;
    first->training = second->training = true;
    first->UnitBalance = second->UnitBalance = &balance;
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
    LPGAMECLIENT client = &game.clients[0];
    LPEDICT producer = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 0.0f, 0.0f);
    LPEDICT queued = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0.0f, 0.0f);
    UnitBalance_t balance = { .goldCost = 100, .lumberCost = 20, .foodUsed = 3 };

    producer->s.player = queued->s.player = client->ps.number;
    producer->build = queued;
    queued->training = true;
    queued->UnitBalance = &balance;
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
