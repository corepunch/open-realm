#ifndef api_ai_h
#define api_ai_h

/* common.ai counts queued and constructing units toward desired totals; Done excludes both incomplete states. */
static LONG BotUnitCount(LPPLAYER player, DWORD unitid, BOOL done) {
    LONG count = 0;
    if (!player || !unitid) return 0;
    FILTER_EDICTS(ent, ent->inuse && (ent->svflags & SVF_MONSTER) && ent->class_id == unitid &&
                         ent->s.player == PLAYER_NUM(player) && !(ent->svflags & SVF_DEADMONSTER)) {
        if (!done || (!ent->construction.active && !ent->training)) count++;
    }
    return count;
}

DWORD GetAiPlayer(LPJASS j) {
    LPPLAYER player = jass_getcontext(j)->playerState;
    return jass_pushinteger(j, player ? (LONG)PLAYER_NUM(player) : -1);
}

DWORD GetUnitCount(LPJASS j) {
    return jass_pushinteger(j, BotUnitCount(jass_getcontext(j)->playerState, jass_checkinteger(j, 1), false));
}

DWORD GetPlayerUnitTypeCount(LPJASS j) {
    LPPLAYER player = jass_checkhandle(j, 1, "player");
    return jass_pushinteger(j, BotUnitCount(player, jass_checkinteger(j, 2), false));
}

DWORD GetUnitCountDone(LPJASS j) {
    return jass_pushinteger(j, BotUnitCount(jass_getcontext(j)->playerState, jass_checkinteger(j, 1), true));
}

DWORD GetUnitGoldCost(LPJASS j) {
    return jass_pushinteger(j, MAX(0, G_UnitBalance(jass_checkinteger(j, 1))->goldCost));
}

DWORD GetUnitWoodCost(LPJASS j) {
    return jass_pushinteger(j, MAX(0, G_UnitBalance(jass_checkinteger(j, 1))->lumberCost));
}

DWORD GetUpgradeLevel(LPJASS j) {
    LPPLAYER player = jass_getcontext(j)->playerState;
    return jass_pushinteger(j, player ? G_GetPlayerTechResearchedLevel(PLAYER_CLIENT(player), jass_checkinteger(j, 1)) : 0);
}

DWORD UnitAlive(LPJASS j) {
    LPEDICT unit = jass_checkhandle(j, 1, "unit");
    return jass_pushboolean(j, G_BotUnitAlive(unit));
}

static bot_t *BotState(LPJASS j) {
    LPPLAYER player = jass_getcontext(j)->playerState;
    return player ? level.bots + PLAYER_NUM(player) : NULL;
}

static DWORD BotSetFlag(LPJASS j, botFlag_t flag) {
    bot_t *bot = BotState(j);
    BOOL set = jass_checkboolean(j, 1);
    if (bot) bot->flags = set ? bot->flags | flag : bot->flags & ~flag;
    return 0;
}

DWORD SetCampaignAI(LPJASS j) { bot_t *bot = BotState(j); if (bot) bot->mode = BOT_CAMPAIGN; return 0; }
DWORD SetMeleeAI(LPJASS j) { bot_t *bot = BotState(j); if (bot) bot->mode = BOT_MELEE; return 0; }
DWORD SetTargetHeroes(LPJASS j) { return BotSetFlag(j, BOT_TARGET_HEROES); }
DWORD SetPeonsRepair(LPJASS j) { return BotSetFlag(j, BOT_PEONS_REPAIR); }
DWORD SetHeroesFlee(LPJASS j) { return BotSetFlag(j, BOT_HEROES_FLEE); }
DWORD SetWatchMegaTargets(LPJASS j) { return BotSetFlag(j, BOT_WATCH_MEGA); }
DWORD SetIgnoreInjured(LPJASS j) { return BotSetFlag(j, BOT_IGNORE_INJURED); }
DWORD SetHeroesTakeItems(LPJASS j) { return BotSetFlag(j, BOT_HEROES_TAKE_ITEM); }
DWORD SetUnitsFlee(LPJASS j) { return BotSetFlag(j, BOT_UNITS_FLEE); }
DWORD SetGroupsFlee(LPJASS j) { return BotSetFlag(j, BOT_GROUPS_FLEE); }
DWORD SetSlowChopping(LPJASS j) { return BotSetFlag(j, BOT_SLOW_CHOPPING); }
DWORD SetCaptainChanges(LPJASS j) { return BotSetFlag(j, BOT_CAPTAIN_CHANGES); }
DWORD SetSmartArtillery(LPJASS j) { return BotSetFlag(j, BOT_SMART_ARTILLERY); }
DWORD GroupTimedLife(LPJASS j) { return BotSetFlag(j, BOT_GROUP_TIMED_LIFE); }
DWORD SetNewHeroes(LPJASS j) { return BotSetFlag(j, BOT_NEW_HEROES); }
DWORD SetRandomPaths(LPJASS j) { return BotSetFlag(j, BOT_RANDOM_PATHS); }
DWORD SetDefendPlayer(LPJASS j) { return BotSetFlag(j, BOT_DEFEND_PLAYER); }
DWORD SetHeroesBuyItems(LPJASS j) { return BotSetFlag(j, BOT_HEROES_BUY_ITEMS); }

DWORD SetReplacementCount(LPJASS j) {
    bot_t *bot = BotState(j);
    if (bot) bot->replacement_count = MAX(0, jass_checkinteger(j, 1));
    return 0;
}

DWORD StopGathering(LPJASS j) {
    G_BotStopGathering(jass_getcontext(j)->playerState);
    return 0;
}

DWORD ClearHarvestAI(LPJASS j) { G_BotClearHarvest(jass_getcontext(j)->playerState); return 0; }
DWORD HarvestGold(LPJASS j) {
    G_BotHarvest(jass_getcontext(j)->playerState, jass_checkinteger(j, 1), jass_checkinteger(j, 2), true);
    return 0;
}
DWORD HarvestWood(LPJASS j) {
    G_BotHarvest(jass_getcontext(j)->playerState, jass_checkinteger(j, 1), jass_checkinteger(j, 2), false);
    return 0;
}

DWORD CreateCaptains(LPJASS j) {
    G_BotCreateCaptains(jass_getcontext(j)->playerState);
    return 0;
}

DWORD IgnoredUnits(LPJASS j) {
    return jass_pushinteger(j, G_BotIgnoredUnits(jass_getcontext(j)->playerState, jass_checkinteger(j, 1)));
}

DWORD CaptainInCombat(LPJASS j) {
    return jass_pushboolean(j, G_BotCaptainInCombat(jass_getcontext(j)->playerState, jass_checkboolean(j, 1)));
}

DWORD CommandsWaiting(LPJASS j) {
    return jass_pushinteger(j, G_BotCommandsWaiting(jass_getcontext(j)->playerState));
}

DWORD GetLastCommand(LPJASS j) {
    return jass_pushinteger(j, G_BotLastCommand(jass_getcontext(j)->playerState));
}

DWORD GetLastData(LPJASS j) {
    return jass_pushinteger(j, G_BotLastData(jass_getcontext(j)->playerState));
}

DWORD PopLastCommand(LPJASS j) {
    G_BotPopCommand(jass_getcontext(j)->playerState);
    return 0;
}

DWORD StartThread(LPJASS j) {
    LPCJASSFUNC func = jass_checkcode(j, 1);
    JASSCONTEXT context = *jass_getcontext(j);
    context.func = func;
    jass_startcoroutine(j, &context);
    return 0;
}

DWORD Sleep(LPJASS j) {
    FLOAT seconds = jass_checknumber(j, 1);
    jass_sleep(j, (DWORD)(MAX(0, seconds) * 1000));
    return 0;
}

#endif /* api_ai_h */
