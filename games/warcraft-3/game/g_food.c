#include "g_local.h"

static gameClient_t * G_FoodClient(uint32_t player) {
    gameClient_t * client = G_GetPlayerClientByNumber(player);
    return client && client->ps.number == player ? client : NULL;
}

bool G_FoodLimitsEnabled(void) {
    cstring_t value;

    value = gi.CvarString("wc3_food_limits", "1");
    return !value || atoi(value) != 0;
}

int32_t G_GetEffectiveFoodCap(gameClient_t * client) {
    int32_t cap, ceiling;

    if (!client) return 0;
    cap = (int32_t)client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP];
    ceiling = (int32_t)client->ps.stats[PLAYERSTATE_FOOD_CAP_CEILING];
    if (ceiling > 0) cap = MIN(cap, ceiling);
    return MAX(0, cap);
}

uint32_t G_GetPlayerUpkeepTier(gameClient_t * client) {
    int32_t food;
    uint32_t count;

    if (!client) return 0;
    count = game.constants.upkeepUsageCount;
    if (!count) return 0;
    food = (int32_t)client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_USED];
    FOR_LOOP(i, count) {
        if ((float)food <= game.constants.upkeepUsage[i]) return i;
    }
    /* Thresholds delimit tiers; food above the final threshold enters the
     * following tier instead of remaining at the last bounded tier. */
    return count;
}

static int32_t G_UpkeepRate(float const * taxes, uint32_t count, uint32_t tier) {
    float tax;

    if (!taxes || !count) return 100;
    tier = MIN(tier, count - 1);
    tax = MAX(0.0f, MIN(taxes[tier], 1.0f));
    return MAX(0, MIN(100, (int32_t)(100.0f - tax * 100.0f + 0.5f)));
}

int32_t G_GetUpkeepGoldRateForTier(uint32_t tier) {
    return G_UpkeepRate(game.constants.upkeepGoldTax, game.constants.upkeepGoldTaxCount, tier);
}

int32_t G_GetUpkeepLumberRateForTier(uint32_t tier) {
    return G_UpkeepRate(game.constants.upkeepLumberTax, game.constants.upkeepLumberTaxCount, tier);
}

static void G_AdjustFoodStat(gameClient_t * client, uint32_t state, int32_t delta) {
    int32_t value;

    if (!client || !delta) return;
    value = (int32_t)client->ps.stats[state] + delta;
    client->ps.stats[state] = (uint16_t)MAX(0, MIN(value, USHRT_MAX));
    if (state == PLAYERSTATE_RESOURCE_FOOD_USED) {
        G_RecomputePlayerUpkeep(client);
    }
    G_InvalidateCommands(client);
}

void G_RecomputePlayerUpkeep(gameClient_t * client) {
    uint32_t tier;

    if (!client) return;
    tier = G_GetPlayerUpkeepTier(client);
    client->ps.stats[PLAYERSTATE_GOLD_UPKEEP_RATE] = (uint16_t)G_GetUpkeepGoldRateForTier(tier);
    client->ps.stats[PLAYERSTATE_LUMBER_UPKEEP_RATE] = (uint16_t)G_GetUpkeepLumberRateForTier(tier);
}

bool G_PlayerHasFoodFor(gameClient_t * client, int32_t food_cost) {
    int32_t used, cap;

    if (!client) return false;
    if (food_cost <= 0 || !G_FoodLimitsEnabled()) return true;
    used = (int32_t)client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_USED];
    cap = G_GetEffectiveFoodCap(client);
    return used + food_cost <= cap;
}

void G_SetUnitFoodUsed(edict_t * unit, int32_t amount) {
    gameClient_t * client;
    int32_t value, delta;

    if (!unit) return;
    value = MAX(0, amount);
    delta = value - unit->food.used;
    unit->food.used = value;
    client = G_FoodClient(unit->s.player);
    G_AdjustFoodStat(client, PLAYERSTATE_RESOURCE_FOOD_USED, delta);
}

void G_SetUnitFoodMade(edict_t * unit, int32_t amount) {
    gameClient_t * client;
    int32_t value, delta;

    if (!unit) return;
    value = MAX(0, amount);
    delta = value - unit->food.made;
    unit->food.made = value;
    client = G_FoodClient(unit->s.player);
    G_AdjustFoodStat(client, PLAYERSTATE_RESOURCE_FOOD_CAP, delta);
}

void G_ActivateUnitFood(edict_t * unit) {
    if (!unit || !unit->data.UnitBalance || (unit->svflags & SVF_DEADMONSTER)) return;
    G_SetUnitFoodUsed(unit, unit->data.UnitBalance->foodUsed);
    G_SetUnitFoodMade(unit, unit->data.UnitBalance->foodMade);
}

void G_ClearUnitFood(edict_t * unit) {
    if (!unit) return;
    G_SetUnitFoodMade(unit, 0);
    G_SetUnitFoodUsed(unit, 0);
}

void G_ClearTrainingQueueFood(edict_t * producer) {
    edict_t * queued;

    if (!producer) return;
    queued = producer->build;
    while (queued && queued->training) {
        G_SetUnitFoodUsed(queued, 0);
        queued = queued->build;
    }
}

void G_SetUnitPlayer(edict_t * unit, uint32_t player) {
    gameClient_t * old_client, *new_client;
    uint32_t old_player;

    if (!unit || unit->s.player == player) return;
    G_InvalidateUnitShortcutsForUnit(unit);
    /* Queue/upgrade charges belong to the original player. Cancel before
     * ownership changes so neither reservations nor refunds cross the transfer. */
    if (unit->revival.reviving) G_CancelHeroRevive(unit->revival.producer, unit);
    G_CancelHeroRevives(unit);
    G_CancelTrainingQueue(unit, true);
    if (G_BuildingUpgradeActive(unit)) G_CancelBuildingUpgrade(unit);
    old_player = unit->s.player;
    old_client = G_FoodClient(old_player);
    new_client = G_FoodClient(player);

    if (unit->food.used) {
        G_AdjustFoodStat(old_client, PLAYERSTATE_RESOURCE_FOOD_USED, -unit->food.used);
        G_AdjustFoodStat(new_client, PLAYERSTATE_RESOURCE_FOOD_USED, unit->food.used);
    }
    if (unit->food.made) {
        G_AdjustFoodStat(old_client, PLAYERSTATE_RESOURCE_FOOD_CAP, -unit->food.made);
        G_AdjustFoodStat(new_client, PLAYERSTATE_RESOURCE_FOOD_CAP, unit->food.made);
    }
    unit->s.player = player;
    G_PublishChangeOwnerEvents(unit, old_player);
    G_InvalidateCommands(old_client);
    G_InvalidateCommands(new_client);
    G_InvalidateUnitInfoPanel(unit);
    G_InvalidateUnitShortcutsForUnit(unit);
}

bool G_ReserveTrainingFood(edict_t * unit) {
    gameClient_t * client;
    int32_t cost;

    if (!unit || !unit->data.UnitBalance) return false;
    cost = MAX(0, unit->data.UnitBalance->foodUsed);
    if (unit->food.used == cost) return true;
    if (unit->food.used != 0) return false;
    if (cost == 0) return true;
    client = G_FoodClient(unit->s.player);
    if (!G_PlayerHasFoodFor(client, cost)) return false;
    G_SetUnitFoodUsed(unit, cost);
    return true;
}

int32_t G_ApplyResourceIncome(player_t * player, uint32_t resource_state, int32_t gross_amount) {
    int32_t rate = 100;

    if (!player || gross_amount <= 0) return 0;
    if (resource_state == PLAYERSTATE_RESOURCE_GOLD) {
        rate = player->stats[PLAYERSTATE_GOLD_UPKEEP_RATE];
    } else if (resource_state == PLAYERSTATE_RESOURCE_LUMBER) {
        rate = player->stats[PLAYERSTATE_LUMBER_UPKEEP_RATE];
    }
    rate = MAX(0, rate);
    return (gross_amount * rate) / 100;
}

/* Commit an income transaction before publishing its presentation event.
 * Callers use the returned net amount when they need the credited value; the
 * existing G_ApplyResourceIncome helper remains pure for previews/tests. */
int32_t G_CreditResourceIncome(player_t * player, edict_t * source, uint32_t resource_state, int32_t gross_amount) {
    int32_t const credited = G_ApplyResourceIncome(player, resource_state, gross_amount);

    if (!player || resource_state >= MAX_STATS || credited <= 0) return 0;
    player->stats[resource_state] += credited;
    G_ResourceGainEvent(source, resource_state, credited);
    return credited;
}
