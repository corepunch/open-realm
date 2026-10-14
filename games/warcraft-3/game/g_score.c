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
    if (used > client->score.values[WC3_PLAYER_SCORE_FOOD_MAXUSED])
        WC3_SCORE_LOG("score food new peak player=%u old=%u new=%u cap=%u",
            client->ps.number, client->score.values[WC3_PLAYER_SCORE_FOOD_MAXUSED], used, produced);
    client->score.values[WC3_PLAYER_SCORE_FOOD_MAXUSED] =
        MAX(client->score.values[WC3_PLAYER_SCORE_FOOD_MAXUSED], used);
}

/* Audit the first playable food level after scripted startup removals.
 * Retail Largest Army retains the historical peak reached during map setup,
 * even when RemoveUnit reduces current food before the first playable frame.
 * This audit must not overwrite the peak or alter gameplay food accounting. */
void G_PlayerScoreSetGameplayFoodBaseline(void) {
    FOR_LOOP(i, MAX_PLAYERS) {
        gameClient_t *client = G_ScoreClient(i);
        uint32_t live_food = 0, survivors = 0;
        uint64_t live_cost = 0;
        if (!client || !client->mapplayer || !client->mapplayer->used) continue;
        FILTER_EDICTS(ent, ent->inuse && ent->s.player == i &&
                      ent->data.UnitBalance && (ent->s.flags & EF_UNIT) &&
                      !(ent->svflags & SVF_DEADMONSTER)) {
            uint32_t food = ent->food ? (uint32_t)MAX(ent->food->used, 0) : 0;
            ++survivors;
            live_food += food;
            live_cost += (uint32_t)MAX(ent->data.UnitBalance->goldCost, 0) +
                         (uint32_t)MAX(ent->data.UnitBalance->lumberCost, 0);
            WC3_SCORE_LOG("score baseline survivor player=%u rawcode=%08x entity=%u food=%u cost=%u",
                i, ent->class_id, ent->s.number, food,
                (uint32_t)MAX(ent->data.UnitBalance->goldCost, 0) +
                (uint32_t)MAX(ent->data.UnitBalance->lumberCost, 0));
        }
        uint32_t previous_peak = client->score.values[WC3_PLAYER_SCORE_FOOD_MAXUSED];
        uint32_t used = client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_USED];
        WC3_SCORE_LOG("score food gameplay baseline player=%u previous_peak=%u current_food=%u surviving_food=%u surviving_units=%u surviving_cost=%llu produced_value=%u",
            i, previous_peak, used, live_food, survivors,
            (unsigned long long)live_cost, client->score.unit_value_produced);
    }
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

/* Score investigation only: one consistent identity/cost/food snapshot across
 * creation, training, scripted removal, death, freeing, and ownership transfer.
 * Never writes a counter or alters gameplay food; disabled unless the existing
 * result-debug cvar is enabled. No additional persisted fields are required. */
void G_PlayerScoreTraceLifecycle(edict_t const *unit, cstring_t event) {
    gameClient_t const *client;
    uint32_t gold, lumber;

    if (!G_GameResultDebugEnabled() || !unit || !unit->inuse ||
        !(unit->s.flags & EF_UNIT) || !unit->data.UnitBalance) return;
    client = G_ScoreClient(unit->s.player);
    gold = (uint32_t)MAX(0, unit->data.UnitBalance->goldCost);
    lumber = (uint32_t)MAX(0, unit->data.UnitBalance->lumberCost);
    WC3_SCORE_LOG("score lifecycle event=%s frame=%u time=%u owner=%u rawcode=%08x entity=%u "
        "gold=%u lumber=%u cost=%u authored_food=%d tracked_food=%d player_food=%u "
        "peak_food=%u produced=%u produced_value=%u hero=%u structure=%u training=%u dead=%u hidden=%u deferred=%u",
        event ? event : "unknown", (unsigned)level.framenum, (unsigned)level.time,
        unit->s.player, unit->class_id, unit->s.number, gold, lumber, gold + lumber,
        unit->data.UnitBalance->foodUsed, unit->food ? unit->food->used : 0,
        client ? client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_USED] : 0,
        client ? client->score.values[WC3_PLAYER_SCORE_FOOD_MAXUSED] : 0,
        client ? client->score.values[WC3_PLAYER_SCORE_UNITS_TRAINED] : 0,
        client ? client->score.unit_value_produced : 0,
        (unsigned)G_UnitIsHero(unit), (unsigned)G_UnitIsStructure(unit),
        (unsigned)unit->training,
        (unsigned)((unit->svflags & SVF_DEADMONSTER) != 0),
        (unsigned)((unit->s.renderfx & RF_HIDDEN) != 0),
        (unsigned)G_IsDeferredFree(unit));
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
    G_PlayerScoreTraceLifecycle(unit, "score_unit_credited");
    WC3_SCORE_LOG("score unit created owner=%u rawcode=%08x hero=%u cost=%u food=%d produced=%u value=%u",
        unit->s.player, unit->class_id, (unsigned)G_UnitIsHero(unit), G_ScoreUnitValue(unit),
        unit->data.UnitBalance ? unit->data.UnitBalance->foodUsed : 0,
        client ? client->score.values[WC3_PLAYER_SCORE_UNITS_TRAINED] : 0,
        client ? client->score.unit_value_produced : 0);
    if (G_UnitIsHero(unit)) WC3_SCORE_LOG("score hero lifecycle event=create owner=%u rawcode=%08x entity=%u",
        unit->s.player, unit->class_id, unit->s.number);
    if (G_UnitIsHero(unit) && client && client->score.heroes_used < INT32_MAX) {
        client->score.heroes_used++;
        WC3_SCORE_LOG("score hero seen owner=%u rawcode=%08x entity=%u heroes_used=%u xp=%u level=%u",
            unit->s.player, unit->class_id, unit->s.number, client->score.heroes_used,
            unit->hero.xp, unit->hero.level);
    }
}

void G_PlayerScoreExperienceGained(edict_t *hero, uint32_t amount) {
    gameClient_t *client;
    if (!hero || !amount || !G_UnitIsHero(hero)) return;
    client = G_ScoreClient(hero->s.player);
    if (!client) return;
    WC3_SCORE_LOG("score hero xp owner=%u rawcode=%08x entity=%u credited=%u before=%u level=%u",
        hero->s.player, hero->class_id, hero->s.number, amount,
        client->score.experience_gained, hero->hero.level);
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
    WC3_SCORE_LOG("score death attribution entity=%u rawcode=%08x victim_owner=%u attacker_entity=%u attacker_owner=%u cost=%u eligible=%u",
        victim->s.number, victim->class_id, victim->s.player,
        attacker ? attacker->s.number : 0, attacker ? attacker->s.player : MAX_PLAYERS,
        G_ScoreUnitValue(victim),
        (unsigned)(attacker && attacker != victim && attacker->s.player != victim->s.player));
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
    WC3_SCORE_LOG("score death credit attacker_owner=%u victim_entity=%u hero=%u structure=%u units_killed=%u heroes_killed=%u buildings_razed=%u killed_value=%u razed_value=%u",
        attacker->s.player, victim->s.number, (unsigned)G_UnitIsHero(victim),
        (unsigned)G_UnitIsStructure(victim),
        client->score.values[WC3_PLAYER_SCORE_UNITS_KILLED],
        client->score.values[WC3_PLAYER_SCORE_HEROES_KILLED],
        client->score.values[WC3_PLAYER_SCORE_STRUCT_RAZED],
        client->score.unit_value_killed, client->score.structure_value_razed);
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
#ifdef WC3_DEBUG_SCORE
    uint32_t observed = 0, researched = 0;
#endif
    uint32_t eligible_rows = 0, eligible_levels = 0, achieved_levels = 0;
    unitRace_t const race = (unitRace_t)client->ps.race;

#ifdef WC3_DEBUG_SCORE
    /* Keep the old observed-entry diagnostic for comparison with earlier runs. */
    FOR_LOOP(i, MAX_PLAYER_TECH_STATE) {
        playerTechState_t const *tech = &client->tech[i];
        if (!tech->id) continue;
        observed++;
        if (tech->researched > 0) researched++;
    }
#endif

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
        eligible_rows++;
        eligible_levels += (uint32_t)upgrade->maxLevel;
        achieved_levels += achieved;
#ifdef WC3_DEBUG_SCORE
        WC3_SCORE_LOG("score tech eligible player=%u id=%08x race=%s researched=%u authored_max=%d",
            client->ps.number, upgrade->id, upgrade->race, achieved, upgrade->maxLevel);
#endif
    }
#ifdef WC3_DEBUG_SCORE
    WC3_SCORE_LOG("score tech player=%u race=%u observed=%u researched=%u previous_pct=%u eligible_rows=%u achieved_levels=%u authored_levels=%u race_pct=%u",
        client->ps.number, (unsigned)race, observed, researched,
        observed ? (unsigned)((100ull * researched) / observed) : 0,
        eligible_rows, achieved_levels, eligible_levels,
        eligible_levels ? (unsigned)((100ull * achieved_levels) / eligible_levels) : 0);
#endif
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
