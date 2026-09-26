#ifndef api_ai_h
#define api_ai_h

/* Blizzard AI traces use %d substitution, but the script string must never become a C format string. */
static void BotDisplayFormat(string_t dst, size_t size, cstring_t format, const int32_t *values, uint32_t count) {
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

static uint32_t BotDisplayText(LPJASS j, uint32_t count) {
    int32_t player = jass_checkinteger(j, 1), values[3] = {0};
    cstring_t format = jass_checkstring(j, 2);
    char message[1024];
    FOR_LOOP(i, count) values[i] = jass_checkinteger(j, 3 + i);
    BotDisplayFormat(message, sizeof(message), format, values, count);
    fprintf(stderr, "WC3 AI[%d]: %s", player, message);
    return 0;
}

uint32_t DisplayText(LPJASS j) { return BotDisplayText(j, 0); }
uint32_t DisplayTextI(LPJASS j) { return BotDisplayText(j, 1); }
uint32_t DisplayTextII(LPJASS j) { return BotDisplayText(j, 2); }
uint32_t DisplayTextIII(LPJASS j) { return BotDisplayText(j, 3); }

/* common.ai counts queued and constructing units toward desired totals; Done excludes both incomplete states. */
static int32_t BotUnitCount(LPPLAYER player, uint32_t unitid, bool done) {
    int32_t count = 0;
    if (!player || !unitid) return 0;
    FILTER_EDICTS(ent, ent->inuse && (ent->svflags & SVF_MONSTER) && ent->class_id == unitid &&
                         ent->s.player == PLAYER_NUM(player) && !(ent->svflags & SVF_DEADMONSTER)) {
        if (!done || (!ent->construction.active && !ent->training)) count++;
    }
    if (!done) FILTER_EDICTS(builder, G_BotUnitAlive(builder) && builder->s.player == PLAYER_NUM(player) &&
                                      builder->build_project == unitid) count++;
    return count;
}

uint32_t GetAiPlayer(LPJASS j) {
    LPPLAYER player = jass_getcontext(j)->playerState;
    return jass_pushinteger(j, player ? (int32_t)PLAYER_NUM(player) : -1);
}

uint32_t GetAIDifficulty(LPJASS j) {
    LPPLAYER player = jass_checkhandle(j, 1, "player");
    uint32_t * difficulty = jass_newhandle(j, sizeof(*difficulty), "aidifficulty");
    *difficulty = player ? 1 : 0; /* Lobby slots currently expose WC3's normal AI difficulty only. */
    return 1;
}

uint32_t GetUnitCount(LPJASS j) {
    LPPLAYER player = jass_getcontext(j)->playerState;
    uint32_t class_id = jass_checkinteger(j, 1);
    int32_t count = BotUnitCount(player, class_id, false);
#ifdef WC3_DEBUG_AI
    if (class_id == MAKEFOURCC('h','p','e','a'))
        fprintf(stderr, "WC3_DEBUG_AI count id=%.4s value=%d gold=%d lumber=%d\n", (cstring_t)&class_id, count,
            player->stats[PLAYERSTATE_RESOURCE_GOLD], player->stats[PLAYERSTATE_RESOURCE_LUMBER]);
#endif
    return jass_pushinteger(j, count);
}

uint32_t GetPlayerUnitTypeCount(LPJASS j) {
    LPPLAYER player = jass_checkhandle(j, 1, "player");
    return jass_pushinteger(j, BotUnitCount(player, jass_checkinteger(j, 2), false));
}

uint32_t GetUnitCountDone(LPJASS j) {
    return jass_pushinteger(j, BotUnitCount(jass_getcontext(j)->playerState, jass_checkinteger(j, 1), true));
}

uint32_t GetMinesOwned(LPJASS j) { return jass_pushinteger(j, G_BotMinesOwned(jass_getcontext(j)->playerState)); }
uint32_t GetGoldOwned(LPJASS j) { return jass_pushinteger(j, G_BotGoldOwned(jass_getcontext(j)->playerState)); }
uint32_t TownWithMine(LPJASS j) { return jass_pushinteger(j, G_BotTownWithMine(jass_getcontext(j)->playerState)); }
uint32_t TownHasMine(LPJASS j) {
    return jass_pushboolean(j, G_BotTownMine(jass_getcontext(j)->playerState, jass_checkinteger(j, 1)) != NULL);
}
uint32_t TownHasHall(LPJASS j) {
    return jass_pushboolean(j, G_BotUnitAlive(G_BotTown(jass_getcontext(j)->playerState, jass_checkinteger(j, 1))));
}

uint32_t SetProduce(LPJASS j) {
    return jass_pushboolean(j, G_BotProduce(jass_getcontext(j)->playerState, jass_checkinteger(j, 1),
                                            jass_checkinteger(j, 2), jass_checkinteger(j, 3)));
}

uint32_t GetUnitGoldCost(LPJASS j) {
    return jass_pushinteger(j, MAX(0, G_UnitBalance(jass_checkinteger(j, 1))->goldCost));
}

uint32_t GetUnitWoodCost(LPJASS j) {
    return jass_pushinteger(j, MAX(0, G_UnitBalance(jass_checkinteger(j, 1))->lumberCost));
}

uint32_t GetUnitBuildTime(LPJASS j) {
    return jass_pushinteger(j, MAX(0, G_UnitBalance(jass_checkinteger(j, 1))->buildTime));
}

uint32_t GetUpgradeLevel(LPJASS j) {
    LPPLAYER player = jass_getcontext(j)->playerState;
    return jass_pushinteger(j, player ? G_GetPlayerTechResearchedLevel(PLAYER_CLIENT(player), jass_checkinteger(j, 1)) : 0);
}

uint32_t UnitAlive(LPJASS j) {
    LPEDICT unit = jass_checkhandle(j, 1, "unit");
    return jass_pushboolean(j, G_BotUnitAlive(unit));
}

static bot_t *BotState(LPJASS j) {
    LPPLAYER player = jass_getcontext(j)->playerState;
    return player ? level.bots + PLAYER_NUM(player) : NULL;
}

static uint32_t BotSetFlag(LPJASS j, botFlag_t flag) {
    bot_t *bot = BotState(j);
    bool set = jass_checkboolean(j, 1);
    if (bot) bot->flags = set ? bot->flags | flag : bot->flags & ~flag;
    return 0;
}

uint32_t SetCampaignAI(LPJASS j) { bot_t *bot = BotState(j); if (bot) bot->mode = BOT_CAMPAIGN; return 0; }
uint32_t SetMeleeAI(LPJASS j) { bot_t *bot = BotState(j); if (bot) bot->mode = BOT_MELEE; return 0; }
uint32_t SetHeroLevels(LPJASS j) { bot_t *bot = BotState(j); if (bot) bot->hero_levels = jass_checkcode(j, 1); return 0; }
uint32_t SetTargetHeroes(LPJASS j) { return BotSetFlag(j, BOT_TARGET_HEROES); }
uint32_t SetPeonsRepair(LPJASS j) { return BotSetFlag(j, BOT_PEONS_REPAIR); }
uint32_t SetHeroesFlee(LPJASS j) { return BotSetFlag(j, BOT_HEROES_FLEE); }
uint32_t SetWatchMegaTargets(LPJASS j) { return BotSetFlag(j, BOT_WATCH_MEGA); }
uint32_t SetIgnoreInjured(LPJASS j) { return BotSetFlag(j, BOT_IGNORE_INJURED); }
uint32_t SetHeroesTakeItems(LPJASS j) { return BotSetFlag(j, BOT_HEROES_TAKE_ITEM); }
uint32_t SetUnitsFlee(LPJASS j) { return BotSetFlag(j, BOT_UNITS_FLEE); }
uint32_t SetGroupsFlee(LPJASS j) { return BotSetFlag(j, BOT_GROUPS_FLEE); }
uint32_t SetSlowChopping(LPJASS j) { return BotSetFlag(j, BOT_SLOW_CHOPPING); }
uint32_t SetCaptainChanges(LPJASS j) { return BotSetFlag(j, BOT_CAPTAIN_CHANGES); }
uint32_t SetSmartArtillery(LPJASS j) { return BotSetFlag(j, BOT_SMART_ARTILLERY); }
uint32_t GroupTimedLife(LPJASS j) { return BotSetFlag(j, BOT_GROUP_TIMED_LIFE); }
uint32_t SetNewHeroes(LPJASS j) { return BotSetFlag(j, BOT_NEW_HEROES); }
uint32_t SetRandomPaths(LPJASS j) { return BotSetFlag(j, BOT_RANDOM_PATHS); }
uint32_t SetDefendPlayer(LPJASS j) { return BotSetFlag(j, BOT_DEFEND_PLAYER); }
uint32_t SetHeroesBuyItems(LPJASS j) { return BotSetFlag(j, BOT_HEROES_BUY_ITEMS); }

uint32_t SetReplacementCount(LPJASS j) {
    bot_t *bot = BotState(j);
    if (bot) bot->replacement_count = MAX(0, jass_checkinteger(j, 1));
    return 0;
}

uint32_t StopGathering(LPJASS j) {
    G_BotStopGathering(jass_getcontext(j)->playerState);
    return 0;
}

uint32_t ClearHarvestAI(LPJASS j) { G_BotClearHarvest(jass_getcontext(j)->playerState); return 0; }
uint32_t HarvestGold(LPJASS j) {
    G_BotHarvest(jass_getcontext(j)->playerState, jass_checkinteger(j, 1), jass_checkinteger(j, 2), true);
    return 0;
}
uint32_t HarvestWood(LPJASS j) {
    G_BotHarvest(jass_getcontext(j)->playerState, jass_checkinteger(j, 1), jass_checkinteger(j, 2), false);
    return 0;
}

uint32_t CreateCaptains(LPJASS j) {
    G_BotCreateCaptains(jass_getcontext(j)->playerState);
    return 0;
}

uint32_t IgnoredUnits(LPJASS j) {
    return jass_pushinteger(j, G_BotIgnoredUnits(jass_getcontext(j)->playerState, jass_checkinteger(j, 1)));
}

uint32_t CaptainInCombat(LPJASS j) {
    return jass_pushboolean(j, G_BotCaptainInCombat(jass_getcontext(j)->playerState, jass_checkboolean(j, 1)));
}

uint32_t InitAssault(LPJASS j) { G_BotInitAssault(jass_getcontext(j)->playerState); return 0; }
uint32_t AddAssault(LPJASS j) {
    return jass_pushboolean(j, G_BotAddAssault(jass_getcontext(j)->playerState, jass_checkinteger(j, 1), jass_checkinteger(j, 2)));
}
uint32_t CaptainGroupSize(LPJASS j) { return jass_pushinteger(j, G_BotCaptainGroupSize(jass_getcontext(j)->playerState)); }
uint32_t CaptainIsFull(LPJASS j) { return jass_pushboolean(j, G_BotCaptainIsFull(jass_getcontext(j)->playerState)); }
uint32_t CaptainIsEmpty(LPJASS j) { return jass_pushboolean(j, !G_BotCaptainGroupSize(jass_getcontext(j)->playerState)); }
uint32_t CaptainReadiness(LPJASS j) { return jass_pushinteger(j, G_BotCaptainReadiness(jass_getcontext(j)->playerState, false)); }
uint32_t CaptainReadinessHP(LPJASS j) { return jass_pushinteger(j, G_BotCaptainReadiness(jass_getcontext(j)->playerState, false)); }
uint32_t CaptainReadinessMa(LPJASS j) { return jass_pushinteger(j, G_BotCaptainReadiness(jass_getcontext(j)->playerState, true)); }

uint32_t AddDefenders(LPJASS j) {
    return jass_pushboolean(j, G_BotAddDefenders(jass_getcontext(j)->playerState, jass_checkinteger(j, 1), jass_checkinteger(j, 2)));
}

uint32_t AddGuardPost(LPJASS j) {
    G_BotAddGuardPost(jass_getcontext(j)->playerState, jass_checkinteger(j, 1), jass_checknumber(j, 2), jass_checknumber(j, 3));
    return 0;
}
uint32_t FillGuardPosts(LPJASS j) { G_BotFillGuardPosts(jass_getcontext(j)->playerState); return 0; }
uint32_t ReturnGuardPosts(LPJASS j) { G_BotReturnGuardPosts(jass_getcontext(j)->playerState); return 0; }

uint32_t CommandsWaiting(LPJASS j) {
    return jass_pushinteger(j, G_BotCommandsWaiting(jass_getcontext(j)->playerState));
}

uint32_t GetLastCommand(LPJASS j) {
    return jass_pushinteger(j, G_BotLastCommand(jass_getcontext(j)->playerState));
}

uint32_t GetLastData(LPJASS j) {
    return jass_pushinteger(j, G_BotLastData(jass_getcontext(j)->playerState));
}

uint32_t PopLastCommand(LPJASS j) {
    G_BotPopCommand(jass_getcontext(j)->playerState);
    return 0;
}

uint32_t StartThread(LPJASS j) {
    LPCJASSFUNC func = jass_checkcode(j, 1);
    JASSCONTEXT context = *jass_getcontext(j);
    context.func = func;
    jass_startcoroutine(j, &context);
    return 0;
}

/* Keep the exported JASS name Sleep while avoiding Win32's global Sleep symbol. */
uint32_t JassSleep(LPJASS j) {
    float seconds = jass_checknumber(j, 1);
    jass_sleep(j, (uint32_t)(MAX(0, seconds) * 1000));
    return 0;
}

uint32_t SetCaptainHome(LPJASS j) {
    LPPLAYER player = jass_getcontext(j)->playerState;
    int32_t which = jass_checkinteger(j, 1);
    float x = jass_checknumber(j, 2), y = jass_checknumber(j, 3);
    G_BotSetCaptainHome(player, which, x, y);
    return 0;
}
uint32_t SetStagePoint(LPJASS j) {
    LPPLAYER player = jass_getcontext(j)->playerState;
    float x = jass_checknumber(j, 1), y = jass_checknumber(j, 2);
    G_BotSetStagePoint(player, x, y);
    return 0;
}
/* SuicideUnit: void in retail; sends qty units of class_id at any hostile enemy. */
uint32_t SuicideUnit(LPJASS j) {
    LPPLAYER player = jass_getcontext(j)->playerState;
    int32_t qty = jass_checkinteger(j, 1);
    uint32_t class_id = (uint32_t)jass_checkinteger(j, 2);
    G_BotSuicideUnits(player, qty, class_id, -1);
    return 0;
}
/* SuicideUnitEx: same but targets a specific player's forces. */
uint32_t SuicideUnitEx(LPJASS j) {
    LPPLAYER player = jass_getcontext(j)->playerState;
    int32_t qty = jass_checkinteger(j, 1);
    uint32_t class_id = (uint32_t)jass_checkinteger(j, 2);
    int32_t target = jass_checkinteger(j, 3);
    G_BotSuicideUnits(player, qty, class_id, target);
    return 0;
}
/* SuicidePlayer: launches the formed assault captain at target player. */
uint32_t SuicidePlayer(LPJASS j) {
    LPPLAYER player = jass_getcontext(j)->playerState;
    LPPLAYER target = jass_checkhandle(j, 1, "player");
    bool check_full = jass_checkboolean(j, 2);
    return jass_pushboolean(j, G_BotSuicidePlayer(player, target ? PLAYER_NUM(target) : 0, check_full));
}
uint32_t MergeUnits(LPJASS j) {
    LPPLAYER player = jass_getcontext(j)->playerState;
    int32_t qty = jass_checkinteger(j, 1);
    uint32_t a = (uint32_t)jass_checkinteger(j, 2), b = (uint32_t)jass_checkinteger(j, 3), make = (uint32_t)jass_checkinteger(j, 4);
    return jass_pushboolean(j, G_BotMergeUnits(player, qty, a, b, make));
}
uint32_t GetUpgradeGoldCost(LPJASS j) {
    return jass_pushinteger(j, G_UpgradeGoldCost((uint32_t)jass_checkinteger(j, 1), 0));
}
uint32_t GetUpgradeLumberCost(LPJASS j) {
    return jass_pushinteger(j, G_UpgradeLumberCost((uint32_t)jass_checkinteger(j, 1), 0));
}

#endif /* api_ai_h */
