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

/* Score units only when they enter play. Callers own the lifecycle:
 * preplaced map units, immediate JASS CreateUnit, or finished training.
 * Never call at generic SP_SpawnUnit time: transformations reuse it and
 * queued trainees are created long before they can enter the world. */
void G_PlayerScoreUnitTrained(edict_t *unit) {
    gameClient_t *client;

    if (!unit || G_UnitIsStructure(unit)) return;
    client = G_ScoreClient(unit->s.player);
    G_PlayerScoreAdd(client, WC3_PLAYER_SCORE_UNITS_TRAINED, 1);
    if (G_UnitIsHero(unit) && client && client->score.heroes_used < INT32_MAX)
        client->score.heroes_used++;
}

void G_PlayerScoreExperienceGained(edict_t *hero, uint32_t amount) {
    gameClient_t *client;
    if (!hero || !amount || !G_UnitIsHero(hero)) return;
    client = G_ScoreClient(hero->s.player);
    if (!client) return;
    client->score.experience_gained = (uint32_t)MIN(
        (uint64_t)client->score.experience_gained + amount, (uint64_t)INT32_MAX);
}

/* Honor explicit SetPlayerOnScoreScreen(false) without requiring another
 * player to explicitly opt in. Retail campaigns use negative overrides.
 * Without an override, standard human/computer participants are eligible;
 * neutral and script/rescue slots are not. Defeated players remain eligible. */
bool G_ScorePlayerEligible(gameClient_t const *client) {
    mapPlayer_t const *mapplayer;
    if (!client || client->ps.number >= MAX_PLAYERS) return false;
    if (client->jass.on_score_screen_set)
        return client->jass.on_score_screen;
    mapplayer = client->mapplayer;
    /* Save/load drops runtime mapplayer pointers, but the map's slot roster
     * remains available. Never lose AI participants after loading a save. */
    if (!mapplayer && level.mapinfo)
        mapplayer = &level.mapinfo->players[client->ps.number];
    if (mapplayer && mapplayer->used)
        return mapplayer->playerType == kPlayerTypeHuman ||
               mapplayer->playerType == kPlayerTypeComputer;
    /* Synthetic/hand-constructed sessions may not have W3I slot metadata. */
    return client->connected;
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

/* RETAIL-PARITY GUESS: the four remaining weights and denominators are provisional.
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
    /* GUESS: activity-based Unit Score weights, until Warcraft's exact
     * unit-cost and kill contribution rules are recovered. */
    units = (uint64_t)s[WC3_PLAYER_SCORE_UNITS_TRAINED] * 10u +
            (uint64_t)s[WC3_PLAYER_SCORE_UNITS_KILLED] * 10u +
            (uint64_t)s[WC3_PLAYER_SCORE_STRUCT_BUILT] * 20u +
            (uint64_t)s[WC3_PLAYER_SCORE_STRUCT_RAZED] * 20u;
    /* GUESS: the retail reference's 500 XP -> 5000 Hero Score supports a
     * 10x experience contribution. Other Hero weights remain unverified. */
    heroes = (uint64_t)s[WC3_PLAYER_SCORE_HEROES_KILLED] * 100u +
             (uint64_t)s[WC3_PLAYER_SCORE_ITEMS_GAINED] * 10u +
             (uint64_t)client->score.experience_gained * 10u;
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
        /* Retail Overview's Total Score equals its three displayed scores.
         * Unlike the earlier GUESS, Tech Percentage is informational only. */
        *value = G_ScoreClamp((uint64_t)G_ScoreClamp(units) +
                              G_ScoreClamp(heroes) + G_ScoreClamp(resources));
        break;
    default:
        *value = (int32_t)s[score]; break;
    }
    return true;
}
