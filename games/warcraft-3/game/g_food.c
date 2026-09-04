#include "g_local.h"

static LPGAMECLIENT G_FoodClient(DWORD player) {
    LPGAMECLIENT client = G_GetPlayerClientByNumber(player);
    return client && client->ps.number == player ? client : NULL;
}

BOOL G_FoodLimitsEnabled(void) {
    LPCSTR value;

    value = gi.CvarString("wc3_food_limits", "1");
    return !value || atoi(value) != 0;
}

LONG G_GetEffectiveFoodCap(LPGAMECLIENT client) {
    LONG cap, ceiling;

    if (!client) return 0;
    cap = (LONG)client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP];
    ceiling = (LONG)client->ps.stats[PLAYERSTATE_FOOD_CAP_CEILING];
    if (ceiling > 0) cap = MIN(cap, ceiling);
    return MAX(0, cap);
}

DWORD G_GetPlayerUpkeepTier(LPGAMECLIENT client) {
    LONG food;
    DWORD count;

    if (!client) return 0;
    count = game.constants.upkeepUsageCount;
    if (!count) return 0;
    food = (LONG)client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_USED];
    FOR_LOOP(i, count) {
        if ((FLOAT)food <= game.constants.upkeepUsage[i]) return i;
    }
    /* Thresholds delimit tiers; food above the final threshold enters the
     * following tier instead of remaining at the last bounded tier. */
    return count;
}

static LONG G_UpkeepRate(LPCFLOAT taxes, DWORD count, DWORD tier) {
    FLOAT tax;

    if (!taxes || !count) return 100;
    tier = MIN(tier, count - 1);
    tax = MAX(0.0f, MIN(taxes[tier], 1.0f));
    return MAX(0, MIN(100, (LONG)(100.0f - tax * 100.0f + 0.5f)));
}

LONG G_GetUpkeepGoldRateForTier(DWORD tier) {
    return G_UpkeepRate(game.constants.upkeepGoldTax, game.constants.upkeepGoldTaxCount, tier);
}

LONG G_GetUpkeepLumberRateForTier(DWORD tier) {
    return G_UpkeepRate(game.constants.upkeepLumberTax, game.constants.upkeepLumberTaxCount, tier);
}

static void G_AdjustFoodStat(LPGAMECLIENT client, DWORD state, LONG delta) {
    LONG value;

    if (!client || !delta) return;
    value = (LONG)client->ps.stats[state] + delta;
    client->ps.stats[state] = (USHORT)MAX(0, MIN(value, USHRT_MAX));
    if (state == PLAYERSTATE_RESOURCE_FOOD_USED) {
        G_RecomputePlayerUpkeep(client);
    }
    G_InvalidateCommands(client);
}

void G_RecomputePlayerUpkeep(LPGAMECLIENT client) {
    DWORD tier;

    if (!client) return;
    tier = G_GetPlayerUpkeepTier(client);
    client->ps.stats[PLAYERSTATE_GOLD_UPKEEP_RATE] = (USHORT)G_GetUpkeepGoldRateForTier(tier);
    client->ps.stats[PLAYERSTATE_LUMBER_UPKEEP_RATE] = (USHORT)G_GetUpkeepLumberRateForTier(tier);
}

BOOL G_PlayerHasFoodFor(LPGAMECLIENT client, LONG food_cost) {
    LONG used, cap;

    if (!client) return false;
    if (food_cost <= 0 || !G_FoodLimitsEnabled()) return true;
    used = (LONG)client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_USED];
    cap = G_GetEffectiveFoodCap(client);
    return used + food_cost <= cap;
}

void G_SetUnitFoodUsed(LPEDICT unit, LONG amount) {
    LPGAMECLIENT client;
    LONG value, delta;

    if (!unit) return;
    value = MAX(0, amount);
    delta = value - unit->food.used;
    unit->food.used = value;
    client = G_FoodClient(unit->s.player);
    G_AdjustFoodStat(client, PLAYERSTATE_RESOURCE_FOOD_USED, delta);
}

void G_SetUnitFoodMade(LPEDICT unit, LONG amount) {
    LPGAMECLIENT client;
    LONG value, delta;

    if (!unit) return;
    value = MAX(0, amount);
    delta = value - unit->food.made;
    unit->food.made = value;
    client = G_FoodClient(unit->s.player);
    G_AdjustFoodStat(client, PLAYERSTATE_RESOURCE_FOOD_CAP, delta);
}

void G_ActivateUnitFood(LPEDICT unit) {
    if (!unit || !unit->data.UnitBalance || (unit->svflags & SVF_DEADMONSTER)) return;
    G_SetUnitFoodUsed(unit, unit->data.UnitBalance->foodUsed);
    G_SetUnitFoodMade(unit, unit->data.UnitBalance->foodMade);
}

void G_ClearUnitFood(LPEDICT unit) {
    if (!unit) return;
    G_SetUnitFoodMade(unit, 0);
    G_SetUnitFoodUsed(unit, 0);
}

void G_ClearTrainingQueueFood(LPEDICT producer) {
    LPEDICT queued;

    if (!producer) return;
    queued = producer->build;
    while (queued && queued->training) {
        G_SetUnitFoodUsed(queued, 0);
        queued = queued->build;
    }
}

void G_SetUnitPlayer(LPEDICT unit, DWORD player) {
    LPGAMECLIENT old_client, new_client;
    DWORD old_player;

    if (!unit || unit->s.player == player) return;
    G_InvalidateUnitShortcutsForUnit(unit);
    /* Queue charges belong to the original player. Cancel before ownership
     * changes so neither queued items nor refunds cross the transfer. */
    if (unit->revival.reviving) G_CancelHeroRevive(unit->revival.producer, unit);
    G_CancelHeroRevives(unit);
    G_CancelTrainingQueue(unit, true);
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
    G_InvalidateCommands(old_client);
    G_InvalidateCommands(new_client);
    G_InvalidateUnitInfoPanel(unit);
    G_InvalidateUnitShortcutsForUnit(unit);
}

BOOL G_ReserveTrainingFood(LPEDICT unit) {
    LPGAMECLIENT client;
    LONG cost;

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

LONG G_ApplyResourceIncome(LPPLAYER player, DWORD resource_state, LONG gross_amount) {
    LONG rate = 100;

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
LONG G_CreditResourceIncome(LPPLAYER player, LPEDICT source, DWORD resource_state, LONG gross_amount) {
    LONG const credited = G_ApplyResourceIncome(player, resource_state, gross_amount);

    if (!player || resource_state >= MAX_STATS || credited <= 0) return 0;
    player->stats[resource_state] += credited;
    G_ResourceGainEvent(source, resource_state, credited);
    return credited;
}
