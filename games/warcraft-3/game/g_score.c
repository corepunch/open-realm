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

/* RETAIL-PARITY GUESS: all five aggregate formulas below are provisional.
 * Do not mistake the weights or denominator for recovered Warcraft III data.
 * They are explicit, bounded and deterministic so maps can query a useful
 * first approximation; compare each value with stock WC3 before finalizing. */
static int32_t G_ScoreClamp(uint64_t value) {
    return (int32_t)MIN(value, (uint64_t)INT32_MAX);
}

static int32_t G_ScoreGuessTechPercent(gameClient_t const *client) {
    uint32_t observed = 0, researched = 0;
    FOR_LOOP(i, MAX_PLAYER_TECH_STATE) {
        playerTechState_t const *tech = &client->tech[i];
        if (!tech->id) continue;
        observed++;
        if (tech->researched > 0) researched++;
    }
    /* GUESS: known/observed technology rows are the denominator, not the
     * retail race/map technology tree. An empty set scores zero. */
    return observed ? (int32_t)((100ull * researched) / observed) : 0;
}

bool G_GetPlayerScoreValue(gameClient_t const *client, uint32_t score, int32_t *value) {
    uint64_t units, heroes, resources;
    uint32_t const *s;
    if (!client || !value || score >= WC3_PLAYER_SCORE_COUNT) return false;
    s = client->score.values;
    /* GUESS: raw activity points, intentionally excluding gained units/items
     * from hero score until hero-XP/usage telemetry exists. */
    units = (uint64_t)s[WC3_PLAYER_SCORE_UNITS_TRAINED] * 10u +
            (uint64_t)s[WC3_PLAYER_SCORE_UNITS_KILLED] * 10u +
            (uint64_t)s[WC3_PLAYER_SCORE_STRUCT_BUILT] * 20u +
            (uint64_t)s[WC3_PLAYER_SCORE_STRUCT_RAZED] * 20u;
    heroes = (uint64_t)s[WC3_PLAYER_SCORE_HEROES_KILLED] * 100u +
             (uint64_t)s[WC3_PLAYER_SCORE_ITEMS_GAINED] * 10u;
    /* GUESS: 10 gold or 5 lumber gathered = 1 score point, ignoring
     * transfers until their authoritative transaction callbacks exist. */
    resources = s[WC3_PLAYER_SCORE_GOLD_MINED_TOTAL] / 10u +
                s[WC3_PLAYER_SCORE_LUMBER_TOTAL] / 5u;
    switch (score) {
    case WC3_PLAYER_SCORE_TECH_PERCENT:
        *value = G_ScoreGuessTechPercent(client); break;
    case WC3_PLAYER_SCORE_UNIT_TOTAL:
        *value = G_ScoreClamp(units); break;
    case WC3_PLAYER_SCORE_HERO_TOTAL:
        *value = G_ScoreClamp(heroes); break;
    case WC3_PLAYER_SCORE_RESOURCE_TOTAL:
        *value = G_ScoreClamp(resources); break;
    case WC3_PLAYER_SCORE_TOTAL:
        /* GUESS: technology counts as 10 points per percentage point. */
        *value = G_ScoreClamp(units + heroes + resources +
                              (uint64_t)G_ScoreGuessTechPercent(client) * 10u);
        break;
    default:
        *value = (int32_t)s[score]; break;
    }
    return true;
}
