#include "g_local.h"

static gameClient_t *G_ScoreClient(uint32_t player) {
    gameClient_t *client;

    if (player >= game.max_clients) return NULL;
    client = G_GetPlayerClientByNumber(player);
    return client && client->ps.number == player ? client : NULL;
}

static void G_PlayerScoreAdd(gameClient_t *client, uint32_t score, uint32_t amount) {
    uint64_t value;

    if (!client || score >= WC3_PLAYER_SCORE_COUNT || !amount) return;
    value = (uint64_t)client->score.values[score] + amount;
    client->score.values[score] = (uint32_t)MIN(value, (uint64_t)INT32_MAX);
}

static void G_PlayerScoreAddGatheredState(player_t *player, uint32_t state, uint32_t amount) {
    uint32_t value;

    if (!player || state >= MAX_STATS || !amount) return;
    value = (uint32_t)player->stats[state] + amount;
    player->stats[state] = (uint16_t)MIN(value, (uint32_t)USHRT_MAX);
}

void G_PlayerScoreObserveFood(gameClient_t *client) {
    uint32_t produced, used;

    if (!client) return;
    produced = client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP];
    used = client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_USED];
    client->score.values[WC3_PLAYER_SCORE_FOOD_MAXPROD] =
        MAX(client->score.values[WC3_PLAYER_SCORE_FOOD_MAXPROD], produced);
    client->score.values[WC3_PLAYER_SCORE_FOOD_MAXUSED] =
        MAX(client->score.values[WC3_PLAYER_SCORE_FOOD_MAXUSED], used);
}

void G_PlayerScoreSetGathered(gameClient_t *client, uint32_t state) {
    if (!client) return;
    if (state == PLAYERSTATE_GOLD_GATHERED) {
        client->score.values[WC3_PLAYER_SCORE_GOLD_MINED_TOTAL] = client->ps.stats[state];
    } else if (state == PLAYERSTATE_LUMBER_GATHERED) {
        client->score.values[WC3_PLAYER_SCORE_LUMBER_TOTAL] = client->ps.stats[state];
    }
}

void G_PlayerScoreUnitTrained(edict_t *unit) {
    gameClient_t *client;

    if (!unit) return;
    client = G_ScoreClient(unit->s.player);
    G_PlayerScoreAdd(client, WC3_PLAYER_SCORE_UNITS_TRAINED, 1);
}

void G_PlayerScoreStructureBuilt(edict_t *building) {
    gameClient_t *client;

    if (!building) return;
    client = G_ScoreClient(building->s.player);
    G_PlayerScoreAdd(client, WC3_PLAYER_SCORE_STRUCT_BUILT, 1);
}

void G_PlayerScoreDeath(edict_t *victim, edict_t *attacker) {
    gameClient_t *client;

    if (!victim || !attacker || attacker == victim || attacker->s.player == victim->s.player) return;
    client = G_ScoreClient(attacker->s.player);
    if (!client) return;

    if (G_UnitIsStructure(victim)) {
        G_PlayerScoreAdd(client, WC3_PLAYER_SCORE_STRUCT_RAZED, 1);
    } else if (G_UnitIsHero(victim)) {
        G_PlayerScoreAdd(client, WC3_PLAYER_SCORE_HEROES_KILLED, 1);
    } else {
        G_PlayerScoreAdd(client, WC3_PLAYER_SCORE_UNITS_KILLED, 1);
    }
}

void G_PlayerScoreItemGained(edict_t *unit) {
    gameClient_t *client;

    if (!unit) return;
    client = G_ScoreClient(unit->s.player);
    G_PlayerScoreAdd(client, WC3_PLAYER_SCORE_ITEMS_GAINED, 1);
}

void G_PlayerScoreMercenaryHired(gameClient_t *client) {
    G_PlayerScoreAdd(client, WC3_PLAYER_SCORE_MERCS_HIRED, 1);
}

void G_PlayerScoreResourceIncome(player_t *player, uint32_t resource_state,
                                 int32_t gross_amount, int32_t credited_amount) {
    gameClient_t *client;
    uint32_t gross, credited, lost;

    if (!player || gross_amount <= 0) return;
    client = G_ScoreClient(PLAYER_NUM(player));
    if (!client) return;
    gross = (uint32_t)gross_amount;
    credited = (uint32_t)MAX(0, credited_amount);
    lost = credited < gross ? gross - credited : 0;

    if (resource_state == PLAYERSTATE_RESOURCE_GOLD) {
        G_PlayerScoreAdd(client, WC3_PLAYER_SCORE_GOLD_MINED_TOTAL, gross);
        G_PlayerScoreAdd(client, WC3_PLAYER_SCORE_GOLD_MINED_UPKEEP, credited);
        G_PlayerScoreAdd(client, WC3_PLAYER_SCORE_GOLD_LOST_UPKEEP, lost);
        G_PlayerScoreAddGatheredState(player, PLAYERSTATE_GOLD_GATHERED, gross);
    } else if (resource_state == PLAYERSTATE_RESOURCE_LUMBER) {
        G_PlayerScoreAdd(client, WC3_PLAYER_SCORE_LUMBER_TOTAL, gross);
        G_PlayerScoreAdd(client, WC3_PLAYER_SCORE_LUMBER_LOST_UPKEEP, lost);
        G_PlayerScoreAddGatheredState(player, PLAYERSTATE_LUMBER_GATHERED, gross);
    }
}

bool G_GetPlayerScoreValue(gameClient_t const *client, uint32_t score, int32_t *value) {
    if (!client || !value || score >= WC3_PLAYER_SCORE_COUNT) return false;

    /* These five values need retail data/formulas that OpenRealm does not yet
     * own. Do not manufacture aggregate scores from the raw counters. */
    if (score == WC3_PLAYER_SCORE_TECH_PERCENT ||
        score == WC3_PLAYER_SCORE_UNIT_TOTAL ||
        score == WC3_PLAYER_SCORE_HERO_TOTAL ||
        score == WC3_PLAYER_SCORE_RESOURCE_TOTAL ||
        score == WC3_PLAYER_SCORE_TOTAL) {
        return false;
    }

    *value = (int32_t)client->score.values[score];
    return true;
}
