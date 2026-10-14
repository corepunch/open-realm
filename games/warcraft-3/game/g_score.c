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
static uint32_t G_ScoreUnitValue(edict_t const *unit) {
    if (!unit || !unit->data.UnitBalance) return 0;
    return (uint32_t)MAX(0, unit->data.UnitBalance->goldCost) +
           (uint32_t)MAX(0, unit->data.UnitBalance->lumberCost);
}

static void G_ScoreAddValue(uint32_t *target, uint32_t amount) {
    if (!target) return;
    *target = (uint32_t)MIN((uint64_t)*target + amount, (uint64_t)INT32_MAX);
}

void G_PlayerScoreUnitTrained(edict_t *unit) {
    gameClient_t *client;

    if (!unit || G_UnitIsStructure(unit)) return;
    client = G_ScoreClient(unit->s.player);
    G_PlayerScoreAdd(client, WC3_PLAYER_SCORE_UNITS_TRAINED, 1);
    if (client) G_ScoreAddValue(&client->score.unit_value_produced, G_ScoreUnitValue(unit));
    /* Heroes Used is captured from current entities in the outgoing snapshot. */
}

/* Capture retail Heroes Used as current hero portraits, not historical
 * creation events. A transfer moves the hero to its new owner; hiding and
 * death retain it, while RemoveUnit (including deferred free) excludes it.
 * Capture identities and art before the outgoing level is unloaded. */
void G_PlayerScoreSnapshotHeroes(wc3ScorePlayerSnapshot_t *player) {
    if (!player) return;
    player->hero_count = 0;
    memset(player->heroes, 0, sizeof(player->heroes));
    FILTER_EDICTS(hero, hero->inuse && hero->s.player == player->number &&
                  G_UnitIsHero(hero) && !hero->training && !G_IsDeferredFree(hero)) {
        uint32_t const index = player->hero_count++;
        wc3ScoreHeroSnapshot_t *entry;
        cstring_t art;
        if (index >= WC3_SCORE_HERO_PORTRAITS) continue;
        entry = &player->heroes[index];
        entry->entity = hero->s.number;
        entry->rawcode = hero->class_id;
        entry->level = MAX(1u, hero->hero.level);
        art = hero->data.UnitProfile ? hero->data.UnitProfile->art : NULL;
        if (art) strlcpy(entry->art, art, sizeof(entry->art));
    }
}

void G_PlayerScoreExperienceGained(edict_t *hero, uint32_t amount) {
    gameClient_t *client;
    if (!hero || !amount || !G_UnitIsHero(hero)) return;
    client = G_ScoreClient(hero->s.player);
    if (!client) return;
    client->score.experience_gained = (uint32_t)MIN(
        (uint64_t)client->score.experience_gained + amount, (uint64_t)INT32_MAX);
}

/* Scripted level awards follow the hero's *current* owner, not its
 * temporary creation owner. Campaign setup frequently creates a hero for
 * Player(0), levels it, then transfers it to neutral. Only the retained
 * player's heroes contribute that scripted starting XP. */
uint32_t G_PlayerScoreScriptedXP(uint32_t player) {
    uint64_t total = 0;
    FILTER_EDICTS(ent, ent->inuse && G_UnitIsHero(ent) && ent->s.player == player) {
        total += ent->score_level_xp;
    }
    return (uint32_t)MIN(total, (uint64_t)INT32_MAX);
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
    if (client) G_ScoreAddValue(&client->score.structure_value_built, G_ScoreUnitValue(building));
}

void G_PlayerScoreDeath(edict_t *victim, edict_t *attacker) {
    gameClient_t *client;

    if (!victim) return;
    if (!attacker || attacker == victim || attacker->s.player == victim->s.player) return;
    client = G_ScoreClient(attacker->s.player);
    if (!client) return;

    if (G_UnitIsStructure(victim)) {
        G_PlayerScoreAdd(client, WC3_PLAYER_SCORE_STRUCT_RAZED, 1);
        G_ScoreAddValue(&client->score.structure_value_razed, G_ScoreUnitValue(victim));
    } else if (G_UnitIsHero(victim)) {
        G_PlayerScoreAdd(client, WC3_PLAYER_SCORE_HEROES_KILLED, 1);
    } else {
        G_PlayerScoreAdd(client, WC3_PLAYER_SCORE_UNITS_KILLED, 1);
        G_ScoreAddValue(&client->score.unit_value_killed, G_ScoreUnitValue(victim));
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

/* Candidate retail scoring rule: researched levels across the full, authored,
 * race-specific upgrade set (including untouched upgrades in the denominator).
 * JASS technology availability restrictions never count as research. */
static int32_t G_ScoreGuessTechPercent(gameClient_t const *client) {
    uint32_t eligible_levels = 0, achieved_levels = 0;
    unitRace_t const race = (unitRace_t)client->ps.race;


    FOR_LOOP(i, g_UpgradeDataCount) {
        UpgradeData_t const *upgrade = &g_UpgradeData[i];
        uint32_t achieved = 0;
        if (!upgrade->id || !upgrade->used || upgrade->maxLevel <= 0 ||
            WC3_RaceFromString(upgrade->race) != race) continue;

        FOR_LOOP(j, MAX_PLAYER_TECH_STATE) {
            playerTechState_t const *tech = &client->tech[j];
            if (tech->id != upgrade->id) continue;
            achieved = (uint32_t)MAX(0, tech->researched);
            break;
        }
        achieved = MIN(achieved, (uint32_t)upgrade->maxLevel);
        eligible_levels += (uint32_t)upgrade->maxLevel;
        achieved_levels += achieved;
    }
    WC3_SCORE_LOG("score tech summary player=%u achieved=%u eligible=%u",
        client->ps.number, achieved_levels, eligible_levels);
    return eligible_levels ? (int32_t)((100ull * achieved_levels) / eligible_levels) : 0;
}

bool G_GetPlayerScoreValue(gameClient_t const *client, uint32_t score, int32_t *value) {
    uint64_t units, heroes, resources;
    uint32_t const *s;
    if (!client || !value || score >= WC3_PLAYER_SCORE_COUNT) return false;
    s = client->score.values;
    /* Cost-based approximation. Retail's treatment of hero/building weights,
     * kills, and special map units remains unverified. */
    units = (uint64_t)client->score.unit_value_produced +
            client->score.unit_value_killed + client->score.structure_value_built +
            client->score.structure_value_razed;
    /* GUESS: the retail reference's 500 XP -> 5000 Hero Score supports a
     * 10x experience contribution. Other Hero weights remain unverified. */
    heroes = (uint64_t)s[WC3_PLAYER_SCORE_HEROES_KILLED] * 100u +
             (uint64_t)s[WC3_PLAYER_SCORE_ITEMS_GAINED] * 10u +
             (uint64_t)(client->score.experience_gained +
                 (uint64_t)G_PlayerScoreScriptedXP(client->ps.number)) * 10u;
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
