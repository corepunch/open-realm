#ifndef api_ai_h
#define api_ai_h

#include "../skills/s_skills.h"

/* Blizzard AI traces use %d substitution, but the script string must never become a C format string. */
static void BotDisplayFormat(string_t dst, size_t size, cstring_t format, int32_t const *values, uint32_t count) {
    uint32_t value = 0;
    size_t pos = 0;
    while (*format && pos + 1 < size) {
        if (format[0] == '\\' && format[1] == 'n') dst[pos++] = '\n', format += 2;
        else if (format[0] == '%' && format[1] == '%' && pos + 1 < size) dst[pos++] = '%', format += 2;
        else if (format[0] == '%' && format[1] == 'd' && value < count) {
            int written = snprintf(dst + pos, size - pos, "%d", values[value++]);
            if (written < 0) break;
            pos += (size_t)written < size - pos ? (size_t)written : size - pos - 1;
            format += 2;
        } else dst[pos++] = *format++;
    }
    dst[pos] = 0;
}

static uint32_t BotDisplayText(jass_t *j, uint32_t count) {
    int32_t player = jass_checkinteger(j, 1), values[3] = {0};
    cstring_t format = jass_checkstring(j, 2);
    char message[1024];
    FOR_LOOP(i, count) values[i] = jass_checkinteger(j, 3 + i);
    BotDisplayFormat(message, sizeof(message), format, values, count);
#ifdef WC3_TRACE_AI
    /* Retail common.ai enables these poll messages by default; they drown out diagnostics. */
    if (strstr(message, "waiting for a signal") || strstr(message, "signal received") ||
        strstr(message, "trying to gather forces") || strstr(message, "forming group") ||
        strstr(message, "waiting for attack wave") || strstr(message, "waiting for suicide") ||
        strstr(message, "waiting for timeout") || strstr(message, "waiting for attack wave to die"))
        return 0;
    if (strstr(message, "preparing suicide attack wave") || strstr(message, "exit form group") ||
        strstr(message, "done - all units are dead") || strstr(message, "done - captain has entered combat") ||
        strstr(message, "done - timeout") || strstr(message, "ABORT")) {
        player_t *player = jass_getcontext(j)->playerState;
        G_BOT_TRACE(player ? PLAYER_NUM(player) : MAX_PLAYERS, j, "script_ai_status", "message=\"%s\"", message);
    }
#endif
#ifdef WC3_DEBUG_AI
    {
        cstring_t trace = gi.CvarString ? gi.CvarString("wc3_ai_trace", "0") : "0";
        if (trace && atoi(trace) != 0) {
            bot_t *bot = player >= 0 && player < MAX_PLAYERS ? level.bots + player : NULL;
            char callchain[512];
            jass_formatcallchain(j, callchain, sizeof(callchain));
            fprintf(stderr,
                "WC3_AI_TRACE time=%u player=%d script=\"%s\" function=\"%s\" callchain=\"%s\"\n",
                (unsigned)G_Time(), player, bot ? bot->script : "(unknown)",
                jass_currentfunctionname(j) ? jass_currentfunctionname(j) : "(unknown)",
                callchain[0] ? callchain : "(empty)");
        }
    }
#endif
    fprintf(stderr, "WC3 AI[%d]: %s", player, message);
    return 0;
}

uint32_t DisplayText(jass_t *j) { return BotDisplayText(j, 0); }
uint32_t DisplayTextI(jass_t *j) { return BotDisplayText(j, 1); }
uint32_t DisplayTextII(jass_t *j) { return BotDisplayText(j, 2); }
uint32_t DisplayTextIII(jass_t *j) { return BotDisplayText(j, 3); }

/* common.ai counts queued and constructing units toward desired totals; Done excludes both incomplete states. */
static int32_t BotUnitCount(player_t *player, uint32_t unitid, bool done) {
    int32_t count = 0;
    if (!player || !unitid) return 0;
    FILTER_EDICTS(ent, ent->inuse && (ent->svflags & SVF_MONSTER) && ent->class_id == unitid &&
                         ent->s.player == PLAYER_NUM(player) && !(ent->svflags & SVF_DEADMONSTER)) {
        if (!done || (!ent->construction && !ent->training)) count++;
    }
    if (!done) FILTER_EDICTS(builder, G_BotUnitAlive(builder) && builder->s.player == PLAYER_NUM(player) &&
                                      builder->build_project == unitid) count++;
    return count;
}

#ifdef WC3_TRACE_AI
typedef struct {
    uint32_t unitid;
    int32_t owned, complete, training, constructing, producing, assigned;
    bool seen;
} botAssaultSupplyTrace_t;

enum { BOT_ASSAULT_TRACE_TYPES = 16 };
static botAssaultSupplyTrace_t bot_assault_supply_trace[MAX_PLAYERS][BOT_ASSAULT_TRACE_TYPES];

/* Emit a supply snapshot once per wave and again only when production or
 * captain assignment changes. This keeps formation polling from flooding logs. */
static void BotTraceAssaultSupply(jass_t *j, player_t *player, uint32_t unitid,
                                  int32_t requested, uint32_t group_size, uint32_t desired) {
    botAssaultSupplyTrace_t *state = NULL;
    bot_t *bot;
    int32_t owned = 0, complete = 0, training = 0, constructing = 0, producing = 0, assigned = 0;
    uint32_t playernum;
    if (!player || !unitid) return;
    playernum = PLAYER_NUM(player);
    if (playernum >= MAX_PLAYERS) return;
    FOR_LOOP(i, BOT_ASSAULT_TRACE_TYPES) {
        if (bot_assault_supply_trace[playernum][i].unitid == unitid) {
            state = bot_assault_supply_trace[playernum] + i;
            break;
        }
        if (!state && !bot_assault_supply_trace[playernum][i].unitid)
            state = bot_assault_supply_trace[playernum] + i;
    }
    if (!state) return;
    bot = &level.bots[playernum];
    FILTER_EDICTS(ent, G_BotUnitAlive(ent) && ent->s.player == playernum && ent->class_id == unitid) {
        owned++;
        if (ent->construction) constructing++;
        else if (ent->training) training++;
        else {
            complete++;
            bool in_captain = false;
            if (bot) FOR_LOOP(c, BOT_CAPTAIN_COUNT) {
                FOR_EACH_ARRAY(edict_t *, member, bot->captains[c].units)
                    if (*member == ent) { in_captain = true; break; }
                if (in_captain) break;
            }
            if (in_captain) assigned++;
        }
    }
    FILTER_EDICTS(builder, G_BotUnitAlive(builder) && builder->s.player == playernum &&
                          builder->build_project == unitid) producing++;
    if (state->seen && state->unitid == unitid && state->owned == owned &&
        state->complete == complete && state->training == training &&
        state->constructing == constructing && state->producing == producing &&
        state->assigned == assigned) return;
    state->unitid = unitid;
    state->owned = owned;
    state->complete = complete;
    state->training = training;
    state->constructing = constructing;
    state->producing = producing;
    state->assigned = assigned;
    state->seen = true;
    G_BOT_TRACE(playernum, j, "wave_unit_supply",
               "unit=%.4s owned=%d complete=%d training=%d constructing=%d producer_projects=%d assigned=%d available=%d requested=%d group_size=%u desired=%u",
               (cstring_t)&unitid, owned, complete, training, constructing, producing,
               assigned, complete - assigned, requested, group_size, desired);
}

#endif

uint32_t GetAiPlayer(jass_t *j) {
    player_t *player = jass_getcontext(j)->playerState;
    return jass_pushinteger(j, player ? (int32_t)PLAYER_NUM(player) : -1);
}

uint32_t MeleeDifficulty(jass_t *j) {
    /* common.ai uses MELEE_NEWBIE/NORMAL/INSANE = 1/2/3, distinct from aidifficulty handles = 0/1/2. */
    return jass_pushinteger(j, 2); /* Lobby slots currently expose WC3's normal AI difficulty only. */
}

uint32_t GetAIDifficulty(jass_t *j) {
    player_t *player = jass_checkhandle(j, 1, "player");
    uint32_t *difficulty = jass_newhandle(j, sizeof(*difficulty), "aidifficulty");
    /* The lobby does not expose per-player AI difficulty; do not conflate it with map difficulty. */
    *difficulty = player ? 1 : 0;
    return 1;
}

uint32_t GetUnitCount(jass_t *j) {
    player_t *player = jass_getcontext(j)->playerState;
    uint32_t class_id = jass_checkinteger(j, 1);
    int32_t count = BotUnitCount(player, class_id, false);
#ifdef WC3_DEBUG_AI
    if (class_id == MAKEFOURCC('h','p','e','a'))
        fprintf(stderr, "WC3_DEBUG_AI count id=%.4s value=%d gold=%d lumber=%d\n", (cstring_t)&class_id, count,
            player->stats[PLAYERSTATE_RESOURCE_GOLD], player->stats[PLAYERSTATE_RESOURCE_LUMBER]);
#endif
    return jass_pushinteger(j, count);
}

uint32_t GetPlayerUnitTypeCount(jass_t *j) {
    player_t *player = jass_checkhandle(j, 1, "player");
    return jass_pushinteger(j, BotUnitCount(player, jass_checkinteger(j, 2), false));
}

uint32_t GetUnitCountDone(jass_t *j) {
    return jass_pushinteger(j, BotUnitCount(jass_getcontext(j)->playerState, jass_checkinteger(j, 1), true));
}

uint32_t GetTownUnitCount(jass_t *j) {
    return jass_pushinteger(j, G_BotTownUnitCount(jass_getcontext(j)->playerState,
        jass_checkinteger(j, 1), jass_checkinteger(j, 2), jass_checkboolean(j, 3)));
}

uint32_t GetMinesOwned(jass_t *j) { return jass_pushinteger(j, G_BotMinesOwned(jass_getcontext(j)->playerState)); }
uint32_t GetGoldOwned(jass_t *j) { return jass_pushinteger(j, G_BotGoldOwned(jass_getcontext(j)->playerState)); }
uint32_t TownWithMine(jass_t *j) { return jass_pushinteger(j, G_BotTownWithMine(jass_getcontext(j)->playerState)); }
uint32_t TownHasMine(jass_t *j) {
    return jass_pushboolean(j, G_BotTownMine(jass_getcontext(j)->playerState, jass_checkinteger(j, 1)) != NULL);
}
uint32_t TownHasHall(jass_t *j) {
    return jass_pushboolean(j, G_BotUnitAlive(G_BotTown(jass_getcontext(j)->playerState, jass_checkinteger(j, 1))));
}

uint32_t SetAllianceTarget(jass_t *j) {
    G_BotSetAllianceTarget(jass_getcontext(j)->playerState, jass_checkhandle(j, 1, "unit"));
    return 0;
}

uint32_t GetAllianceTarget(jass_t *j) {
    edict_t *target = G_BotGetAllianceTarget(jass_getcontext(j)->playerState);
    return target ? jass_pushlighthandle(j, target, "unit") : jass_pushnullhandle(j, "unit");
}

uint32_t GetNextExpansion(jass_t *j) {
    return jass_pushinteger(j, G_BotNextExpansion(jass_getcontext(j)->playerState));
}

uint32_t GetExpansionFoe(jass_t *j) {
    edict_t *unit = G_BotExpansionFoe(jass_getcontext(j)->playerState);
    return unit ? jass_pushlighthandle(j, unit, "unit") : jass_pushnullhandle(j, "unit");
}

uint32_t GetExpansionPeon(jass_t *j) {
    edict_t *unit = G_BotExpansionPeon(jass_getcontext(j)->playerState);
    return unit ? jass_pushlighthandle(j, unit, "unit") : jass_pushnullhandle(j, "unit");
}

uint32_t GetExpansionX(jass_t *j) {
    vec2_t position = G_BotExpansionPosition(jass_getcontext(j)->playerState);
    return jass_pushnumber(j, position.x);
}

uint32_t GetExpansionY(jass_t *j) {
    vec2_t position = G_BotExpansionPosition(jass_getcontext(j)->playerState);
    return jass_pushnumber(j, position.y);
}

uint32_t SetExpansion(jass_t *j) {
    edict_t *worker = jass_checkhandle(j, 1, "unit");
    return jass_pushboolean(j, G_BotSetExpansion(jass_getcontext(j)->playerState, worker,
                                                  (uint32_t)jass_checkinteger(j, 2)));
}

uint32_t SetProduce(jass_t *j) {
    player_t *player = jass_getcontext(j)->playerState;
    int32_t qty = jass_checkinteger(j, 1), town = jass_checkinteger(j, 3);
    uint32_t class_id = (uint32_t)jass_checkinteger(j, 2);
    bool accepted = G_BotProduce(player, qty, class_id, town);
    if (accepted)
        G_BOT_TRACE(player ? PLAYER_NUM(player) : MAX_PLAYERS, j, "production_requested",
                   "qty=%d unit=%.4s town=%d accepted=1", qty, (cstring_t)&class_id, town);
    return jass_pushboolean(j, accepted);
}

uint32_t SetUpgrade(jass_t *j) {
    player_t *player = jass_getcontext(j)->playerState;
    uint32_t upgrade = (uint32_t)jass_checkinteger(j, 1);
    bool accepted = G_BotUpgrade(player, upgrade);
    if (accepted)
        G_BOT_TRACE(player ? PLAYER_NUM(player) : MAX_PLAYERS, j, "upgrade_requested",
                   "upgrade=%.4s accepted=1", (cstring_t)&upgrade);
    return jass_pushboolean(j, accepted);
}

uint32_t GetUnitGoldCost(jass_t *j) {
    UnitBalance_t const *balance = G_UnitBalance((uint32_t)jass_checkinteger(j, 1));
    return jass_pushinteger(j, balance ? MAX(0, balance->goldCost) : 0);
}

uint32_t GetUnitWoodCost(jass_t *j) {
    UnitBalance_t const *balance = G_UnitBalance((uint32_t)jass_checkinteger(j, 1));
    return jass_pushinteger(j, balance ? MAX(0, balance->lumberCost) : 0);
}

uint32_t GetUnitBuildTime(jass_t *j) {
    UnitBalance_t const *balance = G_UnitBalance((uint32_t)jass_checkinteger(j, 1));
    return jass_pushinteger(j, balance ? MAX(0, balance->buildTime) : 0);
}

uint32_t GetUpgradeLevel(jass_t *j) {
    player_t *player = jass_getcontext(j)->playerState;
    return jass_pushinteger(j, player ? G_GetPlayerTechResearchedLevel(PLAYER_CLIENT(player), jass_checkinteger(j, 1)) : 0);
}

uint32_t UnitAlive(jass_t *j) {
    edict_t *unit = jass_checkhandle(j, 1, "unit");
    return jass_pushboolean(j, G_BotUnitAlive(unit));
}

uint32_t TownThreatened(jass_t *j) {
    return jass_pushboolean(j, G_BotTownThreatened(jass_getcontext(j)->playerState));
}

uint32_t IsTowered(jass_t *j) {
    return jass_pushboolean(j, G_BotIsTowered(jass_getcontext(j)->playerState, jass_checkhandle(j, 1, "unit")));
}

uint32_t StartGetEnemyBase(jass_t *j) {
    G_BotStartGetEnemyBase(jass_getcontext(j)->playerState);
    return 0;
}

uint32_t WaitGetEnemyBase(jass_t *j) {
    return jass_pushboolean(j, G_BotWaitGetEnemyBase(jass_getcontext(j)->playerState));
}

uint32_t GetEnemyBase(jass_t *j) {
    edict_t *target = G_BotGetEnemyBase(jass_getcontext(j)->playerState);
    return target ? jass_pushlighthandle(j, target, "unit") : jass_pushnullhandle(j, "unit");
}

uint32_t GetEnemyExpansion(jass_t *j) {
    edict_t *target = G_BotGetEnemyExpansion(jass_getcontext(j)->playerState);
    return target ? jass_pushlighthandle(j, target, "unit") : jass_pushnullhandle(j, "unit");
}

uint32_t GetMegaTarget(jass_t *j) {
    edict_t *target = G_BotGetMegaTarget(jass_getcontext(j)->playerState);
    return target ? jass_pushlighthandle(j, target, "unit") : jass_pushnullhandle(j, "unit");
}

uint32_t GetCreepCamp(jass_t *j) {
    edict_t *target = G_BotGetCreepCamp(jass_getcontext(j)->playerState,
        jass_checkinteger(j, 1), jass_checkinteger(j, 2), jass_checkboolean(j, 3));
    return target ? jass_pushlighthandle(j, target, "unit") : jass_pushnullhandle(j, "unit");
}

uint32_t PurchaseZeppelin(jass_t *j) {
    G_BotPurchaseZeppelin(jass_getcontext(j)->playerState);
    return 0;
}

uint32_t RemoveSiege(jass_t *j) {
    G_BotRemoveSiege(jass_getcontext(j)->playerState);
    return 0;
}

/* common.ai separates intrinsic invisibility from player-relative detection. */
uint32_t UnitInvis(jass_t *j) {
    edict_t *unit = jass_checkhandle(j, 1, "unit");
    return jass_pushboolean(j, unit && unit->inuse && !M_IsDead(unit) && S_UnitHasInvisibilityState(unit));
}

static bot_t *BotState(jass_t *j) {
    player_t *player = jass_getcontext(j)->playerState;
    return player ? level.bots + PLAYER_NUM(player) : NULL;
}

uint32_t GetHeroId(jass_t *j) { bot_t *bot = BotState(j); return jass_pushinteger(j, bot ? (int32_t)bot->hero_id : 0); }
uint32_t GetHeroLevelAI(jass_t *j) { bot_t *bot = BotState(j); return jass_pushinteger(j, bot ? (int32_t)bot->hero_level : 0); }

static uint32_t BotSetFlag(jass_t *j, botFlag_t flag) {
    bot_t *bot = BotState(j);
    bool set = jass_checkboolean(j, 1);
    if (bot) {
        bool changed = ((bot->flags & flag) != 0) != set;
        bot->flags = set ? bot->flags | flag : bot->flags & ~flag;
        if (changed && flag == BOT_PEONS_REPAIR) G_BotRefreshPeonsRepair(bot->player);
    }
    return 0;
}

uint32_t SetCampaignAI(jass_t *j) { bot_t *bot = BotState(j); if (bot) bot->mode = BOT_CAMPAIGN; return 0; }
uint32_t SetMeleeAI(jass_t *j) { bot_t *bot = BotState(j); if (bot) bot->mode = BOT_MELEE; return 0; }
uint32_t SetHeroLevels(jass_t *j) { bot_t *bot = BotState(j); if (bot) bot->hero_levels = jass_checkcode(j, 1); return 0; }
uint32_t SetTargetHeroes(jass_t *j) { return BotSetFlag(j, BOT_TARGET_HEROES); }
uint32_t SetPeonsRepair(jass_t *j) { return BotSetFlag(j, BOT_PEONS_REPAIR); }
uint32_t SetHeroesFlee(jass_t *j) { return BotSetFlag(j, BOT_HEROES_FLEE); }
uint32_t SetWatchMegaTargets(jass_t *j) { return BotSetFlag(j, BOT_WATCH_MEGA); }
uint32_t SetIgnoreInjured(jass_t *j) { return BotSetFlag(j, BOT_IGNORE_INJURED); }
uint32_t SetHeroesTakeItems(jass_t *j) { return BotSetFlag(j, BOT_HEROES_TAKE_ITEM); }
uint32_t SetUnitsFlee(jass_t *j) { return BotSetFlag(j, BOT_UNITS_FLEE); }
uint32_t SetGroupsFlee(jass_t *j) { return BotSetFlag(j, BOT_GROUPS_FLEE); }
uint32_t SetSlowChopping(jass_t *j) { return BotSetFlag(j, BOT_SLOW_CHOPPING); }
uint32_t SetCaptainChanges(jass_t *j) { return BotSetFlag(j, BOT_CAPTAIN_CHANGES); }
uint32_t SetSmartArtillery(jass_t *j) { return BotSetFlag(j, BOT_SMART_ARTILLERY); }
uint32_t GroupTimedLife(jass_t *j) { return BotSetFlag(j, BOT_GROUP_TIMED_LIFE); }
uint32_t SetNewHeroes(jass_t *j) { return BotSetFlag(j, BOT_NEW_HEROES); }
uint32_t SetRandomPaths(jass_t *j) { return BotSetFlag(j, BOT_RANDOM_PATHS); }
/* BZ_COMPAT_GUESS: Naga AI uses this as a routing policy. Physical movement
 * remains governed by each unit's authored pathing type (land/float/amphibious).
 * Campaign groups therefore retain their real amphibious access without
 * accidentally granting water travel to ground-only units. */
uint32_t DisablePathing(jass_t *j) {
    bot_t *bot = BotState(j);
    if (bot) bot->flags |= BOT_DISABLE_PATHING;
    return 0;
}
uint32_t SetAmphibious(jass_t *j) {
    bot_t *bot = BotState(j);
    if (bot) bot->flags |= BOT_AMPHIBIOUS;
    return 0;
}
uint32_t SetDefendPlayer(jass_t *j) { return BotSetFlag(j, BOT_DEFEND_PLAYER); }
uint32_t SetHeroesBuyItems(jass_t *j) { return BotSetFlag(j, BOT_HEROES_BUY_ITEMS); }

uint32_t SetReplacementCount(jass_t *j) {
    bot_t *bot = BotState(j);
    if (bot) bot->replacement_count = MAX(0, jass_checkinteger(j, 1));
    return 0;
}

uint32_t RemoveInjuries(jass_t *j) {
    G_BotRemoveInjuries(jass_getcontext(j)->playerState);
    return 0;
}

uint32_t StopGathering(jass_t *j) {
    G_BotStopGathering(jass_getcontext(j)->playerState);
    return 0;
}

uint32_t ClearHarvestAI(jass_t *j) { G_BotClearHarvest(jass_getcontext(j)->playerState); return 0; }
uint32_t HarvestGold(jass_t *j) {
    G_BotHarvest(jass_getcontext(j)->playerState, jass_checkinteger(j, 1), jass_checkinteger(j, 2), true);
    return 0;
}
uint32_t HarvestWood(jass_t *j) {
    G_BotHarvest(jass_getcontext(j)->playerState, jass_checkinteger(j, 1), jass_checkinteger(j, 2), false);
    return 0;
}

uint32_t CreateCaptains(jass_t *j) {
    G_BotCreateCaptains(jass_getcontext(j)->playerState);
    return 0;
}

uint32_t IgnoredUnits(jass_t *j) {
    return jass_pushinteger(j, G_BotIgnoredUnits(jass_getcontext(j)->playerState, jass_checkinteger(j, 1)));
}

uint32_t CaptainInCombat(jass_t *j) {
    return jass_pushboolean(j, G_BotCaptainInCombat(jass_getcontext(j)->playerState, jass_checkboolean(j, 1)));
}

uint32_t AttackMoveKill(jass_t *j) {
    player_t *player = jass_getcontext(j)->playerState;
    edict_t *target = jass_checkhandle(j, 1, "unit");
    G_BotAttackMoveKill(player, target);
    if (target)
        G_BOT_TRACE(player ? PLAYER_NUM(player) : MAX_PLAYERS, j, "attack_target",
                   "kind=unit unit=%ld id=%.4s target_player=%u point=(%.1f,%.1f)",
                   (long)(target - g_edicts), (cstring_t)&target->class_id,
                   (unsigned)target->s.player, target->s.origin2.x, target->s.origin2.y);
    return 0;
}

uint32_t InitAssault(jass_t *j) {
    player_t *player = jass_getcontext(j)->playerState;
    G_BotInitAssault(player);
    G_BOT_TRACE(player ? PLAYER_NUM(player) : MAX_PLAYERS, j, "wave_form_started", "");
    return 0;
}
uint32_t AddAssault(jass_t *j) {
    player_t *player = jass_getcontext(j)->playerState;
    bot_t *bot = BotState(j);
    (void)bot; // Retained for WC3_TRACE_AI diagnostics when tracing is enabled.
    int32_t qty = jass_checkinteger(j, 1);
    uint32_t class_id = (uint32_t)jass_checkinteger(j, 2);
    bool ready = G_BotAddAssault(player, qty, class_id);
    G_BOT_TRACE(player ? PLAYER_NUM(player) : MAX_PLAYERS, j, "wave_form_progress",
               "qty=%d unit=%.4s ready=%d group_size=%u desired=%u", qty,
               (cstring_t)&class_id, (int)ready, G_BotCaptainGroupSize(player),
               bot ? bot->captains[BOT_CAPTAIN_ATTACK].desired : 0);
#ifdef WC3_TRACE_AI
    BotTraceAssaultSupply(j, player, class_id, qty,
                          G_BotCaptainGroupSize(player),
                          bot ? bot->captains[BOT_CAPTAIN_ATTACK].desired : 0);
#endif
    return jass_pushboolean(j, ready);
}
uint32_t CaptainGroupSize(jass_t *j) { return jass_pushinteger(j, G_BotCaptainGroupSize(jass_getcontext(j)->playerState)); }
uint32_t CaptainIsFull(jass_t *j) { return jass_pushboolean(j, G_BotCaptainIsFull(jass_getcontext(j)->playerState)); }
uint32_t CaptainIsEmpty(jass_t *j) { return jass_pushboolean(j, !G_BotCaptainGroupSize(jass_getcontext(j)->playerState)); }
uint32_t CaptainRetreating(jass_t *j) { return jass_pushboolean(j, G_BotCaptainRetreating(jass_getcontext(j)->playerState)); }
uint32_t CaptainReadiness(jass_t *j) { return jass_pushinteger(j, G_BotCaptainReadiness(jass_getcontext(j)->playerState, false)); }
uint32_t CaptainReadinessHP(jass_t *j) { return jass_pushinteger(j, G_BotCaptainReadiness(jass_getcontext(j)->playerState, false)); }
uint32_t CaptainReadinessMa(jass_t *j) { return jass_pushinteger(j, G_BotCaptainReadiness(jass_getcontext(j)->playerState, true)); }

uint32_t AddDefenders(jass_t *j) {
    return jass_pushboolean(j, G_BotAddDefenders(jass_getcontext(j)->playerState, jass_checkinteger(j, 1), jass_checkinteger(j, 2)));
}

uint32_t AddGuardPost(jass_t *j) {
    G_BotAddGuardPost(jass_getcontext(j)->playerState, jass_checkinteger(j, 1), jass_checknumber(j, 2), jass_checknumber(j, 3));
    return 0;
}
uint32_t FillGuardPosts(jass_t *j) { G_BotFillGuardPosts(jass_getcontext(j)->playerState); return 0; }
uint32_t ReturnGuardPosts(jass_t *j) { G_BotReturnGuardPosts(jass_getcontext(j)->playerState); return 0; }

uint32_t CommandsWaiting(jass_t *j) {
    player_t *player = jass_getcontext(j)->playerState;
    uint32_t waiting = G_BotCommandsWaiting(player);
#ifdef WC3_TRACE_AI
    static uint32_t last_reported[MAX_PLAYERS];
    static bool command_count_seen[MAX_PLAYERS];
    static bool common_check_reported[MAX_PLAYERS];
    uint32_t playernum = player ? PLAYER_NUM(player) : MAX_PLAYERS;
    if (playernum < MAX_PLAYERS &&
        (!command_count_seen[playernum] || last_reported[playernum] != waiting)) {
        command_count_seen[playernum] = true;
        last_reported[playernum] = waiting;
        G_BOT_TRACE(playernum, j, "commands_waiting_changed", "count=%u newest=%d data=%d",
                    waiting, G_BotLastCommand(player), G_BotLastData(player));
    }
    if (playernum < MAX_PLAYERS && !common_check_reported[playernum]) {
        char callchain[512];
        jass_formatcallchain(j, callchain, sizeof(callchain));
        if (strstr(callchain, "CommonSuicideOnPlayer")) {
            common_check_reported[playernum] = true;
            G_BOT_TRACE(playernum, j, "wave_command_check",
                        "count=%u newest=%d data=%d", waiting,
                        G_BotLastCommand(player), G_BotLastData(player));
        }
    }
#endif
    return jass_pushinteger(j, waiting);
}

uint32_t GetLastCommand(jass_t *j) {
    return jass_pushinteger(j, G_BotLastCommand(jass_getcontext(j)->playerState));
}

uint32_t GetLastData(jass_t *j) {
    return jass_pushinteger(j, G_BotLastData(jass_getcontext(j)->playerState));
}

uint32_t PopLastCommand(jass_t *j) {
    G_BotPopCommand(jass_getcontext(j)->playerState);
    return 0;
}

uint32_t StartThread(jass_t *j) {
    jassFunc_t const *func = jass_checkcode(j, 1);
    jassContext_t context = *jass_getcontext(j);
    context.func = func;
    jass_startcoroutine(j, &context);
    return 0;
}

/* Keep the exported JASS name Sleep while avoiding Win32's global Sleep symbol. */
uint32_t JassSleep(jass_t *j) {
    float seconds = jass_checknumber(j, 1);
    jass_sleep(j, (uint32_t)(MAX(0, seconds) * 1000));
    return 0;
}

uint32_t SetCaptainHome(jass_t *j) {
    player_t *player = jass_getcontext(j)->playerState;
    int32_t which = jass_checkinteger(j, 1);
    float x = jass_checknumber(j, 2), y = jass_checknumber(j, 3);
    G_BotSetCaptainHome(player, which, x, y);
    return 0;
}
uint32_t TeleportCaptain(jass_t *j) {
    G_BotTeleportCaptain(jass_getcontext(j)->playerState, jass_checknumber(j, 1), jass_checknumber(j, 2));
    return 0;
}
uint32_t CaptainAttack(jass_t *j) {
    G_BotCaptainAttack(jass_getcontext(j)->playerState, jass_checknumber(j, 1), jass_checknumber(j, 2));
    return 0;
}
uint32_t CaptainGoHome(jass_t *j) { G_BotCaptainGoHome(jass_getcontext(j)->playerState); return 0; }
uint32_t CaptainVsPlayer(jass_t *j) {
    G_BotCaptainVsPlayer(jass_getcontext(j)->playerState, jass_checkhandle(j, 1, "player"));
    return 0;
}
uint32_t CaptainVsUnits(jass_t *j) {
    G_BotCaptainVsUnits(jass_getcontext(j)->playerState, jass_checkhandle(j, 1, "player"));
    return 0;
}
uint32_t ResetCaptainLocs(jass_t *j) { G_BotResetCaptainLocs(jass_getcontext(j)->playerState); return 0; }
uint32_t ClearCaptainTargets(jass_t *j) { G_BotClearCaptainTargets(jass_getcontext(j)->playerState); return 0; }
uint32_t CaptainAtGoal(jass_t *j) { return jass_pushboolean(j, G_BotCaptainAtGoal(jass_getcontext(j)->playerState)); }
uint32_t CaptainIsHome(jass_t *j) { return jass_pushboolean(j, G_BotCaptainIsHome(jass_getcontext(j)->playerState)); }
uint32_t SetStagePoint(jass_t *j) {
    player_t *player = jass_getcontext(j)->playerState;
    float x = jass_checknumber(j, 1), y = jass_checknumber(j, 2);
    G_BotSetStagePoint(player, x, y);
    G_BOT_TRACE(player ? PLAYER_NUM(player) : MAX_PLAYERS, j, "wave_stage_set", "point=(%.1f,%.1f)", x, y);
    return 0;
}
/* SuicideUnit: void in retail; sends qty units of class_id at any hostile enemy. */
uint32_t SuicideUnit(jass_t *j) {
    player_t *player = jass_getcontext(j)->playerState;
    int32_t qty = jass_checkinteger(j, 1);
    uint32_t class_id = (uint32_t)jass_checkinteger(j, 2);
    bool accepted = G_BotSuicideUnits(player, qty, class_id, -1);
    (void)accepted; // Retained for WC3_TRACE_AI diagnostics when tracing is enabled.
    G_BOT_TRACE(player ? PLAYER_NUM(player) : MAX_PLAYERS, j, "wave_send",
               "api=SuicideUnit qty=%d unit=%.4s target=any_hostile accepted=%d group_size=%u",
               qty, (cstring_t)&class_id, (int)accepted, G_BotCaptainGroupSize(player));
    return 0;
}
/* SuicideUnitEx: same but targets a specific player's forces. */
uint32_t SuicideUnitEx(jass_t *j) {
    player_t *player = jass_getcontext(j)->playerState;
    int32_t qty = jass_checkinteger(j, 1);
    uint32_t class_id = (uint32_t)jass_checkinteger(j, 2);
    int32_t target = jass_checkinteger(j, 3);
    bool accepted = G_BotSuicideUnits(player, qty, class_id, target);
    (void)accepted; // Retained for WC3_TRACE_AI diagnostics when tracing is enabled.
    G_BOT_TRACE(player ? PLAYER_NUM(player) : MAX_PLAYERS, j, "wave_send",
               "api=SuicideUnitEx qty=%d unit=%.4s target_player=%d accepted=%d group_size=%u",
               qty, (cstring_t)&class_id, target, (int)accepted, G_BotCaptainGroupSize(player));
    return 0;
}
/* SuicidePlayer: launches the formed assault captain at target player. */
uint32_t SuicidePlayer(jass_t *j) {
    player_t *player = jass_getcontext(j)->playerState;
    bot_t *bot = BotState(j);
    (void)bot; // Retained for WC3_TRACE_AI diagnostics when tracing is enabled.
    player_t *target = jass_checkhandle(j, 1, "player");
    bool check_full = jass_checkboolean(j, 2);
    bool accepted = G_BotSuicidePlayer(player, target ? PLAYER_NUM(target) : 0, check_full);
    G_BOT_TRACE(player ? PLAYER_NUM(player) : MAX_PLAYERS, j, "wave_send",
               "api=SuicidePlayer target_player=%u check_full=%d accepted=%d group_size=%u desired=%u shortfall=%u",
               target ? PLAYER_NUM(target) : 0, (int)check_full, (int)accepted,
               G_BotCaptainGroupSize(player),
               bot ? bot->captains[BOT_CAPTAIN_ATTACK].desired : 0,
               bot && bot->captains[BOT_CAPTAIN_ATTACK].desired > G_BotCaptainGroupSize(player) ?
                   bot->captains[BOT_CAPTAIN_ATTACK].desired - G_BotCaptainGroupSize(player) : 0);
    return jass_pushboolean(j, accepted);
}
uint32_t MergeUnits(jass_t *j) {
    player_t *player = jass_getcontext(j)->playerState;
    int32_t qty = jass_checkinteger(j, 1);
    uint32_t a = (uint32_t)jass_checkinteger(j, 2), b = (uint32_t)jass_checkinteger(j, 3), make = (uint32_t)jass_checkinteger(j, 4);
    return jass_pushboolean(j, G_BotMergeUnits(player, qty, a, b, make));
}
uint32_t ConvertUnits(jass_t *j) {
    return jass_pushboolean(j, G_BotConvertUnits(jass_getcontext(j)->playerState,
        jass_checkinteger(j, 1), (uint32_t)jass_checkinteger(j, 2)));
}
static int32_t BotUpgradeNextLevel(jass_t *j, uint32_t upgrade_id) {
    player_t *player = jass_getcontext(j)->playerState;
    UpgradeData_t const *upgrade = G_UpgradeData(upgrade_id);
    int32_t level;

    if (!player || !upgrade || upgrade->id != upgrade_id || upgrade->maxLevel <= 0) return 0;
    level = G_GetPlayerTechResearchedLevel(PLAYER_CLIENT(player), upgrade_id) + 1;
    return level <= upgrade->maxLevel ? level : 0;
}

uint32_t GetUpgradeGoldCost(jass_t *j) {
    uint32_t upgrade_id = (uint32_t)jass_checkinteger(j, 1);
    return jass_pushinteger(j, G_UpgradeGoldCost(upgrade_id, BotUpgradeNextLevel(j, upgrade_id)));
}
uint32_t GetUpgradeWoodCost(jass_t *j) {
    uint32_t upgrade_id = (uint32_t)jass_checkinteger(j, 1);
    return jass_pushinteger(j, G_UpgradeLumberCost(upgrade_id, BotUpgradeNextLevel(j, upgrade_id)));
}
/* Historical OpenRealm alias; retail common.ai calls this GetUpgradeWoodCost. */
uint32_t GetUpgradeLumberCost(jass_t *j) { return GetUpgradeWoodCost(j); }

uint32_t ShiftTownSpot(jass_t *j) {
    G_BotShiftTownSpot(jass_getcontext(j)->playerState, jass_checknumber(j, 1), jass_checknumber(j, 2));
    return 0;
}

#endif /* api_ai_h */
