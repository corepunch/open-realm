extern LPPLAYER currentplayer;

static bool TutorialTextDebugEnabledPlayer(void) {
    return WC3_TUTORIAL_DEBUG_ENABLED();
}

static void TutorialTextDebugContextPlayer(LPJASS j, int32_t *trigger_ordinal, cstring_t *caller) {
    LPCJASSCONTEXT context = jass_getcontext(j);
    if (trigger_ordinal)
        *trigger_ordinal = context && context->trigger ? (int32_t)(context->trigger - level.triggers) : -1L;
    if (caller)
        *caller = context && context->func ? jass_functionname(context->func) : NULL;
}

uint32_t SetPlayerTeam(LPJASS j) {
    LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    int32_t whichTeam = jass_checkinteger(j, 2);
    whichPlayer->team = whichTeam;
    return 0;
}
uint32_t SetPlayerStartLocation(LPJASS j) {
    LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    int32_t startLocIndex = jass_checkinteger(j, 2);
    if (whichPlayer) PLAYER_CLIENT(whichPlayer)->ps.start_location = startLocIndex;
    return 0;
}
uint32_t ForcePlayerStartLocation(LPJASS j) {
    LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    int32_t startLocIndex = jass_checkinteger(j, 2);
    if (whichPlayer) PLAYER_CLIENT(whichPlayer)->ps.start_location = startLocIndex;
    return 0;
}
uint32_t SetPlayerColor(LPJASS j) {
    LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    uint32_t *pColor = jass_checkhandle(j, 2, "playercolor");
    if (whichPlayer && pColor) {
        uint32_t const previous_color = whichPlayer->color;
        G_ChangePlayerTeamColor(whichPlayer, previous_color, *pColor);
        whichPlayer->color = *pColor;
    }
    return 0;
}
uint32_t SetPlayerAlliance(LPJASS j) {
    LPPLAYER sourcePlayer, otherPlayer;
    if (!(sourcePlayer = jass_checkhandle(j, 1, "player"))) {
        fprintf(stderr, "SetPlayerAlliance(): sourcePlayer is nil\n");
        return 0;
    }
    if (!(otherPlayer = jass_checkhandle(j, 2, "player"))) {
        fprintf(stderr, "SetPlayerAlliance(): otherPlayer is nil\n");
        return 0;
    }
    PLAYERALLIANCE *whichAllianceSetting = jass_checkhandle(j, 3, "alliancetype");
    bool value = jass_checkboolean(j, 4);
    G_SetPlayerAlliance(sourcePlayer, otherPlayer, *whichAllianceSetting, value);
    return 0;
}
/* Player-configuration natives need server-owned WC3 client state. Race
 * preferences are a mask, tax is directional and resource-keyed, and controller
 * state reflects config()/lobby choices; none belong in the networked PLAYER. */
uint32_t SetPlayerTaxRate(LPJASS j) {
    LPPLAYER source = jass_checkhandle(j, 1, "player"), other = jass_checkhandle(j, 2, "player");
    uint32_t * resource = jass_checkhandle(j, 3, "playerstate");
    int32_t rate = jass_checkinteger(j, 4);
    if (source && other && resource && *resource <= PLAYERSTATE_LUMBER_GATHERED)
        PLAYER_CLIENT(source)->jass.tax[PLAYER_NUM(other)][*resource] = MIN(MAX(0, rate), 100);
    return 0;
}
uint32_t SetPlayerRacePreference(LPJASS j) {
    LPPLAYER player = jass_checkhandle(j, 1, "player");
    uint32_t * pref = jass_checkhandle(j, 2, "racepreference");
    if (player && pref) PLAYER_CLIENT(player)->jass.race_pref |= *pref;
    return 0;
}
uint32_t SetPlayerRaceSelectable(LPJASS j) {
    LPPLAYER player = jass_checkhandle(j, 1, "player");
    if (player) PLAYER_CLIENT(player)->jass.race_selectable = jass_checkboolean(j, 2);
    return 0;
}
uint32_t SetPlayerController(LPJASS j) {
    LPPLAYER player = jass_checkhandle(j, 1, "player");
    uint32_t * control = jass_checkhandle(j, 2, "mapcontrol");
    if (player && control) PLAYER_CLIENT(player)->jass.controller = *control;
    return 0;
}
uint32_t SetPlayerName(LPJASS j) {
    LPPLAYER player = jass_checkhandle(j, 1, "player");
    if (player) {
        LPGAMECLIENT client = PLAYER_CLIENT(player);
        strlcpy(client->jass.name, jass_checkstring(j, 2), sizeof(client->jass.name));
        client->ps.name = client->jass.name;
    }
    return 0;
}
uint32_t SetPlayerOnScoreScreen(LPJASS j) {
    LPPLAYER player = jass_checkhandle(j, 1, "player");
    if (player) PLAYER_CLIENT(player)->jass.on_score_screen = jass_checkboolean(j, 2);
    return 0;
}
uint32_t GetPlayerTeam(LPJASS j) {
    LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    return jass_pushinteger(j, whichPlayer->team);
}
uint32_t GetPlayerStartLocation(LPJASS j) {
    LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    int32_t loc = whichPlayer ? PLAYER_CLIENT(whichPlayer)->ps.start_location : -1;
    return jass_pushinteger(j, loc);
}
uint32_t GetPlayerColor(LPJASS j) {
    LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    uint32_t *playercolor = jass_newhandle(j, sizeof(uint32_t), "playercolor");
    *playercolor = whichPlayer ? whichPlayer->color : 0;
    return 1;
}
uint32_t GetPlayerSelectable(LPJASS j) {
    LPPLAYER player = jass_checkhandle(j, 1, "player");
    return jass_pushboolean(j, player && PLAYER_CLIENT(player)->jass.race_selectable);
}
uint32_t GetPlayerController(LPJASS j) {
    LPPLAYER player = jass_checkhandle(j, 1, "player");
    return JassPushMapControlHandle(j, player ? PLAYER_CLIENT(player)->jass.controller : 5);
}
uint32_t GetPlayerSlotState(LPJASS j) {
    LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    LPGAMECLIENT client = whichPlayer ? PLAYER_CLIENT(whichPlayer) : NULL;
    int32_t state = 0;

    if (client && client->jass.removed) {
        state = 2;
    } else if (client && client->mapplayer &&
        (client->mapplayer->playerType == kPlayerTypeHuman ||
         client->mapplayer->playerType == kPlayerTypeComputer))
    {
        state = 1;
    }
    return JassPushPlayerSlotStateHandle(j, state);
}
uint32_t GetPlayerTaxRate(LPJASS j) {
    LPPLAYER source = jass_checkhandle(j, 1, "player"), other = jass_checkhandle(j, 2, "player");
    uint32_t * resource = jass_checkhandle(j, 3, "playerstate");
    int32_t rate = source && other && resource && *resource <= PLAYERSTATE_LUMBER_GATHERED ?
        PLAYER_CLIENT(source)->jass.tax[PLAYER_NUM(other)][*resource] : 0;
    return jass_pushinteger(j, rate);
}
uint32_t IsPlayerRacePrefSet(LPJASS j) {
    LPPLAYER player = jass_checkhandle(j, 1, "player");
    uint32_t * pref = jass_checkhandle(j, 2, "racepreference");
    return jass_pushboolean(j, player && pref && (PLAYER_CLIENT(player)->jass.race_pref & *pref));
}
uint32_t GetPlayerName(LPJASS j) {
    LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    LPGAMECLIENT client = whichPlayer ? PLAYER_CLIENT(whichPlayer) : NULL;
    cstring_t name = "";
    if (client && client->jass.name[0]) {
        name = client->jass.name;
    } else if (whichPlayer && whichPlayer->name) {
        name = whichPlayer->name;
    }
    return jass_pushstring(j, name);
}
uint32_t IssueNeutralImmediateOrder(LPJASS j) {
    //handle_t forWhichPlayer = jass_checkhandle(j, 1, "player");
    //handle_t neutralStructure = jass_checkhandle(j, 2, "unit");
    //cstring_t unitToBuild = jass_checkstring(j, 3);
    return jass_pushboolean(j, 0);
}
uint32_t IssueNeutralImmediateOrderById(LPJASS j) {
    //handle_t forWhichPlayer = jass_checkhandle(j, 1, "player");
    //handle_t neutralStructure = jass_checkhandle(j, 2, "unit");
    //int32_t unitId = jass_checkinteger(j, 3);
    return jass_pushboolean(j, 0);
}
uint32_t IssueNeutralPointOrder(LPJASS j) {
    //handle_t forWhichPlayer = jass_checkhandle(j, 1, "player");
    //handle_t neutralStructure = jass_checkhandle(j, 2, "unit");
    //cstring_t unitToBuild = jass_checkstring(j, 3);
    //float x = jass_checknumber(j, 4);
    //float y = jass_checknumber(j, 5);
    return jass_pushboolean(j, 0);
}
uint32_t IssueNeutralPointOrderById(LPJASS j) {
    //handle_t forWhichPlayer = jass_checkhandle(j, 1, "player");
    //handle_t neutralStructure = jass_checkhandle(j, 2, "unit");
    //int32_t unitId = jass_checkinteger(j, 3);
    //float x = jass_checknumber(j, 4);
    //float y = jass_checknumber(j, 5);
    return jass_pushboolean(j, 0);
}
uint32_t IssueNeutralTargetOrder(LPJASS j) {
    //handle_t forWhichPlayer = jass_checkhandle(j, 1, "player");
    //handle_t neutralStructure = jass_checkhandle(j, 2, "unit");
    //cstring_t unitToBuild = jass_checkstring(j, 3);
    //handle_t target = jass_checkhandle(j, 4, "widget");
    return jass_pushboolean(j, 0);
}
uint32_t IssueNeutralTargetOrderById(LPJASS j) {
    //handle_t forWhichPlayer = jass_checkhandle(j, 1, "player");
    //handle_t neutralStructure = jass_checkhandle(j, 2, "unit");
    //int32_t unitId = jass_checkinteger(j, 3);
    //handle_t target = jass_checkhandle(j, 4, "widget");
    return jass_pushboolean(j, 0);
}
uint32_t Player(LPJASS j) {
    int32_t number = jass_checkinteger(j, 1);
    LPPLAYER player = G_GetPlayerByNumber(number);
    return jass_pushlighthandle(j, player, "player");
}
uint32_t GetLocalPlayer(LPJASS j) {
    return jass_pushlighthandle(j, (LPMAPPLAYER)currentplayer, "player");
}
uint32_t IsPlayerAlly(LPJASS j) {
    LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    LPPLAYER otherPlayer = jass_checkhandle(j, 2, "player");
    if (!whichPlayer || !otherPlayer) return jass_pushboolean(j, 0);
    return jass_pushboolean(j, G_GetPlayerAlliance(whichPlayer, otherPlayer, ALLIANCE_PASSIVE));
}
uint32_t IsPlayerEnemy(LPJASS j) {
    LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    LPPLAYER otherPlayer = jass_checkhandle(j, 2, "player");
    if (!whichPlayer || !otherPlayer) return jass_pushboolean(j, 0);
    return jass_pushboolean(j, !G_GetPlayerAlliance(whichPlayer, otherPlayer, ALLIANCE_PASSIVE));
}
uint32_t IsPlayerInForce(LPJASS j) {
    LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    uint32_t * whichForce = jass_checkhandle(j, 2, "force");
    return jass_pushboolean(j, whichPlayer && whichForce && ((*whichForce) & (1 << PLAYER_NUM(whichPlayer))));
}
uint32_t IsPlayerObserver(LPJASS j) {
    //LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    return jass_pushboolean(j, 0);
}
uint32_t IsVisibleToPlayer(LPJASS j) {
    //float x = jass_checknumber(j, 1);
    //float y = jass_checknumber(j, 2);
    //handle_t whichPlayer = jass_checkhandle(j, 3, "player");
    return jass_pushboolean(j, 0);
}
uint32_t IsLocationVisibleToPlayer(LPJASS j) {
    //handle_t whichLocation = jass_checkhandle(j, 1, "location");
    //handle_t whichPlayer = jass_checkhandle(j, 2, "player");
    return jass_pushboolean(j, 0);
}
uint32_t IsFoggedToPlayer(LPJASS j) {
    //float x = jass_checknumber(j, 1);
    //float y = jass_checknumber(j, 2);
    //handle_t whichPlayer = jass_checkhandle(j, 3, "player");
    return jass_pushboolean(j, 0);
}
uint32_t IsLocationFoggedToPlayer(LPJASS j) {
    //handle_t whichLocation = jass_checkhandle(j, 1, "location");
    //handle_t whichPlayer = jass_checkhandle(j, 2, "player");
    return jass_pushboolean(j, 0);
}
uint32_t IsMaskedToPlayer(LPJASS j) {
    //float x = jass_checknumber(j, 1);
    //float y = jass_checknumber(j, 2);
    //handle_t whichPlayer = jass_checkhandle(j, 3, "player");
    return jass_pushboolean(j, 0);
}
uint32_t IsLocationMaskedToPlayer(LPJASS j) {
    //handle_t whichLocation = jass_checkhandle(j, 1, "location");
    //handle_t whichPlayer = jass_checkhandle(j, 2, "player");
    return jass_pushboolean(j, 0);
}
uint32_t GetPlayerRace(LPJASS j) {
    LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    int32_t race = whichPlayer ? (int32_t)whichPlayer->race : kPlayerRaceNone;

    return JassPushRaceHandle(j, race);
}
uint32_t GetPlayerId(LPJASS j) {
    LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    return jass_pushinteger(j, whichPlayer ? (int32_t)whichPlayer->number : 0);
}
uint32_t GetPlayerUnitCount(LPJASS j) {
    LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    bool includeIncomplete = jass_checkboolean(j, 2);
    int32_t count = 0;

    if (!whichPlayer) return jass_pushinteger(j, 0);

    FOR_LOOP(i, globals.num_edicts) {
        LPEDICT ent = globals.edicts + i;

        if (!ent->inuse || !ent->class_id ||
            ent->s.player != PLAYER_NUM(whichPlayer) ||
            G_UnitIsBuilding(ent->class_id) || M_IsDead(ent)) {
            continue;
        }
        if (!includeIncomplete && ent->construction.active) {
            continue;
        }
        count++;
    }
    return jass_pushinteger(j, count);
}

static bool PlayerTypedUnitNameMatches(LPEDICT ent, cstring_t unitName) {
    UnitProfile_t const *profile;

    if (!ent || !ent->class_id || !unitName || !*unitName) return false;

    /* UnitId2String() returns the four-character object id, while legacy
     * campaign helpers also pass the unit's authored display/legacy name
     * (for example "Peon"). Accept both representations. */
    if (!strcmp(GetClassName(ent->class_id), unitName)) return true;

    profile = G_UnitProfile(ent->class_id);
    return profile && profile->name && !strcmp(profile->name, unitName);
}

uint32_t GetPlayerTypedUnitCount(LPJASS j) {
    LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    cstring_t unitName = jass_checkstring(j, 2);
    bool includeIncomplete = jass_checkboolean(j, 3);
    bool includeUpgrades = jass_checkboolean(j, 4);
    int32_t count = 0;

    (void)includeUpgrades; /* Unit-type upgrade equivalence is not represented yet. */

    if (!whichPlayer || !unitName || !*unitName) return jass_pushinteger(j, 0);

    FOR_LOOP(i, globals.num_edicts) {
        LPEDICT ent = globals.edicts + i;

        if (!ent->inuse || !ent->class_id ||
            ent->s.player != PLAYER_NUM(whichPlayer) || M_IsDead(ent) ||
            !PlayerTypedUnitNameMatches(ent, unitName)) {
            continue;
        }
        if (!includeIncomplete && ent->construction.active) {
            continue;
        }
        count++;
    }
    return jass_pushinteger(j, count);
}
uint32_t GetPlayerStructureCount(LPJASS j) {
    LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    bool includeIncomplete = jass_checkboolean(j, 2);
    int32_t count = 0;

    if (!whichPlayer) return jass_pushinteger(j, 0);

    FOR_LOOP(i, globals.num_edicts) {
        LPEDICT ent = globals.edicts + i;

        if (!ent->inuse || !ent->class_id ||
            ent->s.player != PLAYER_NUM(whichPlayer) ||
            !G_UnitIsBuilding(ent->class_id) || M_IsDead(ent)) {
            continue;
        }
        if (!includeIncomplete && ent->construction.active) {
            continue;
        }
        count++;
    }
    return jass_pushinteger(j, count);
}
uint32_t GetPlayerState(LPJASS j) {
    LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    PLAYERSTATE *whichPlayerState = jass_checkhandle(j, 2, "playerstate");
    LPGAMECLIENT client = PLAYER_CLIENT(whichPlayer);
    return jass_pushinteger(j, client->ps.stats[*whichPlayerState]);
}
uint32_t GetPlayerAlliance(LPJASS j) {
    LPPLAYER sourcePlayer = jass_checkhandle(j, 1, "player");
    LPPLAYER otherPlayer = jass_checkhandle(j, 2, "player");
    PLAYERALLIANCE *whichAllianceSetting = jass_checkhandle(j, 3, "alliancetype");
    return jass_pushboolean(j, G_GetPlayerAlliance(sourcePlayer, otherPlayer, *whichAllianceSetting));
}
uint32_t GetPlayerHandicap(LPJASS j) {
    LPPLAYER player = jass_checkhandle(j, 1, "player");
    return jass_pushnumber(j, player ? PLAYER_CLIENT(player)->jass.handicap : 100.0f);
}
uint32_t GetPlayerHandicapXP(LPJASS j) {
    LPPLAYER player = jass_checkhandle(j, 1, "player");
    return jass_pushnumber(j, player ? PLAYER_CLIENT(player)->jass.handicap_xp : 100.0f);
}
uint32_t SetPlayerHandicap(LPJASS j) {
    LPPLAYER player = jass_checkhandle(j, 1, "player");
    if (player) PLAYER_CLIENT(player)->jass.handicap = MAX(0, jass_checknumber(j, 2));
    return 0;
}
uint32_t SetPlayerHandicapXP(LPJASS j) {
    LPPLAYER player = jass_checkhandle(j, 1, "player");
    if (player) PLAYER_CLIENT(player)->jass.handicap_xp = MAX(0, jass_checknumber(j, 2));
    return 0;
}
uint32_t SetPlayerTechMaxAllowed(LPJASS j) {
    LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    int32_t techid = jass_checkinteger(j, 2);
    int32_t maximum = jass_checkinteger(j, 3);
    if (whichPlayer) G_SetPlayerTechMaxAllowed(PLAYER_CLIENT(whichPlayer), (uint32_t)techid, maximum);
    return 0;
}
uint32_t GetPlayerTechMaxAllowed(LPJASS j) {
    LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    int32_t techid = jass_checkinteger(j, 2);
    int32_t maximum = whichPlayer ? G_GetPlayerTechMaxAllowed(PLAYER_CLIENT(whichPlayer), (uint32_t)techid) : -1;
    return jass_pushinteger(j, maximum);
}
uint32_t AddPlayerTechResearched(LPJASS j) {
    LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    int32_t techid = jass_checkinteger(j, 2);
    int32_t levels = jass_checkinteger(j, 3);
    if (whichPlayer) G_AddPlayerTechResearched(PLAYER_CLIENT(whichPlayer), (uint32_t)techid, levels);
    return 0;
}
uint32_t SetPlayerTechResearched(LPJASS j) {
    LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    int32_t techid = jass_checkinteger(j, 2);
    int32_t setToLevel = jass_checkinteger(j, 3);
    if (whichPlayer) G_SetPlayerTechResearched(PLAYER_CLIENT(whichPlayer), (uint32_t)techid, setToLevel);
    return 0;
}
uint32_t GetPlayerTechResearched(LPJASS j) {
    LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    int32_t techid = jass_checkinteger(j, 2);
    bool specificonly = jass_checkboolean(j, 3);
    /* TODO: model Warcraft technology-equivalence groups. Until that data is
     * represented, both specificonly modes address the exact rawcode only. */
    (void)specificonly;
    return jass_pushboolean(j, whichPlayer &&
        G_GetPlayerTechResearchedLevel(PLAYER_CLIENT(whichPlayer), (uint32_t)techid) > 0);
}
uint32_t GetPlayerTechCount(LPJASS j) {
    LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    int32_t techid = jass_checkinteger(j, 2);
    bool specificonly = jass_checkboolean(j, 3);
    /* TODO: model Warcraft technology-equivalence groups. Until that data is
     * represented, both specificonly modes address the exact rawcode only. */
    (void)specificonly;
    return jass_pushinteger(j, whichPlayer ? G_GetPlayerTechCountValue(PLAYER_CLIENT(whichPlayer), (uint32_t)techid) : 0);
}
uint32_t SetPlayerAbilityAvailable(LPJASS j) {
    LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    int32_t abilid = jass_checkinteger(j, 2);
    bool avail = jass_checkboolean(j, 3);
    if (whichPlayer) G_SetPlayerAbilityAvailable(PLAYER_CLIENT(whichPlayer), (uint32_t)abilid, avail);
    return 0;
}
uint32_t SetPlayerState(LPJASS j) {
    LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    PLAYERSTATE *whichPlayerState = jass_checkhandle(j, 2, "playerstate");
    int32_t value = jass_checkinteger(j, 3);
    LPGAMECLIENT client;

    if (!whichPlayer || !whichPlayerState || *whichPlayerState >= MAX_STATS) return 0;
    client = PLAYER_CLIENT(whichPlayer);
    if ((*whichPlayerState == PLAYERSTATE_RESOURCE_GOLD ||
         *whichPlayerState == PLAYERSTATE_RESOURCE_LUMBER) &&
        client->mapplayer && client->mapplayer->playerType == kPlayerTypeHuman &&
        gi.CvarString && atoi(gi.CvarString("wc3_cheat_starting_resources", "0"))) {
        fprintf(stderr,
            "WC3_CHEAT_RESOURCES SetPlayerState player=%u state=%ld before=%u requested=%ld\n",
            (unsigned)whichPlayer->number, (long)*whichPlayerState,
            (unsigned)client->ps.stats[*whichPlayerState], (long)value);
    }
    client->ps.stats[*whichPlayerState] = (uint16_t)MIN(MAX(0, value), USHRT_MAX);
    if (*whichPlayerState == PLAYERSTATE_RESOURCE_FOOD_USED) {
        G_RecomputePlayerUpkeep(client);
    }
    if (*whichPlayerState == PLAYERSTATE_RESOURCE_FOOD_USED ||
        *whichPlayerState == PLAYERSTATE_RESOURCE_FOOD_CAP ||
        *whichPlayerState == PLAYERSTATE_FOOD_CAP_CEILING) {
        G_InvalidateCommands(client);
    }
    return 0;
}
uint32_t RemovePlayer(LPJASS j) {
    /* Warcraft records a per-player result and transitions that slot to LEFT.
     * The shared result helper also backs developer win/lose cheats so both
     * routes publish the same player events and use the same result UI path. */
    LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    uint32_t *gameResult = jass_checkhandle(j, 2, "playergameresult");

    G_GameResultDebug("RemovePlayer enter player_handle=%p result_handle=%p result=%ld events=%u/%u",
        (void *)whichPlayer, (void *)gameResult,
        gameResult ? (long)*gameResult : -1L,
        (unsigned)level.events.read, (unsigned)level.events.write);

    if (!whichPlayer || !gameResult || *gameResult > 3) {
        G_GameResultDebug("RemovePlayer ignored reason=invalid_args player=%p result=%ld",
            (void *)whichPlayer, gameResult ? (long)*gameResult : -1L);
        return 0;
    }

    G_RemovePlayerWithResult(PLAYER_NUM(whichPlayer), *gameResult);
    return 0;
}
uint32_t CachePlayerHeroData(LPJASS j) {
    //LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    return 0;
}
uint32_t SetFogStateRect(LPJASS j) {
    LPPLAYER forWhichPlayer = jass_checkhandle(j, 1, "player");
    uint32_t *whichState = jass_checkhandle(j, 2, "fogstate");
    LPCBOX2 where = jass_checkhandle(j, 3, "rect");
    bool useSharedVision = jass_checkboolean(j, 4);
    if (forWhichPlayer && whichState && where) {
        FOGWRITE fog = { PLAYER_NUM(forWhichPlayer), *whichState, useSharedVision };
        G_FowSetStateRect(&fog, where);
    }
    return 0;
}
uint32_t SetFogStateRadius(LPJASS j) {
    LPPLAYER forWhichPlayer = jass_checkhandle(j, 1, "player");
    uint32_t *whichState = jass_checkhandle(j, 2, "fogstate");
    VECTOR2 center = { jass_checknumber(j, 3), jass_checknumber(j, 4) };
    float radius = jass_checknumber(j, 5);
    bool useSharedVision = jass_checkboolean(j, 6);
    if (forWhichPlayer && whichState) {
        FOGWRITE fog = { PLAYER_NUM(forWhichPlayer), *whichState, useSharedVision };
        G_FowSetStateRadius(&fog, &center, radius);
    }
    return 0;
}
uint32_t SetFogStateRadiusLoc(LPJASS j) {
    LPPLAYER forWhichPlayer = jass_checkhandle(j, 1, "player");
    uint32_t *whichState = jass_checkhandle(j, 2, "fogstate");
    LPCVECTOR2 center = jass_checkhandle(j, 3, "location");
    float radius = jass_checknumber(j, 4);
    bool useSharedVision = jass_checkboolean(j, 5);
    if (forWhichPlayer && whichState && center) {
        FOGWRITE fog = { PLAYER_NUM(forWhichPlayer), *whichState, useSharedVision };
        G_FowSetStateRadius(&fog, center, radius);
    }
    return 0;
}
uint32_t FogMaskEnable(LPJASS j) {
    bool enable = jass_checkboolean(j, 1);
    if (currentplayer) {
        LPGAMECLIENT client = PLAYER_CLIENT(currentplayer);
        SET_FLAG(client->ps.rdflags, RDF_NOFOGMASK, !enable);
    } else FOR_LOOP(i, game.max_clients) {
        SET_FLAG(game.clients[i].ps.rdflags, RDF_NOFOGMASK, !enable);
    }
    return 0;
}
uint32_t FogEnable(LPJASS j) {
    bool enable = jass_checkboolean(j, 1);
    if (currentplayer) {
        LPGAMECLIENT client = PLAYER_CLIENT(currentplayer);
        SET_FLAG(client->ps.rdflags, RDF_NOFOG, !enable);
    } else FOR_LOOP(i, game.max_clients) {
        SET_FLAG(game.clients[i].ps.rdflags, RDF_NOFOG, !enable);
    }
    return 0;
}
uint32_t IsFogMaskEnabled(LPJASS j) {
    if (currentplayer) {
        LPGAMECLIENT client = PLAYER_CLIENT(currentplayer);
        return jass_pushboolean(j, !(client->ps.rdflags & RDF_NOFOGMASK));
    } else {
        return jass_pushboolean(j, !(game.clients->ps.rdflags & RDF_NOFOGMASK));
    }
}
uint32_t IsFogEnabled(LPJASS j) {
    if (currentplayer) {
        LPGAMECLIENT client = PLAYER_CLIENT(currentplayer);
        return jass_pushboolean(j, !(client->ps.rdflags & RDF_NOFOG));
    } else {
        return jass_pushboolean(j, !(game.clients->ps.rdflags & RDF_NOFOG));
    }
}
static LPFOGMODIFIER G_NewFogModifier(LPJASS j, LPPLAYER player, uint32_t *state, bool useShared) {
    API_ALLOC(FOGMODIFIER, fogmodifier);
    if (!fogmodifier) {
        return NULL;
    }
    memset(fogmodifier, 0, sizeof(*fogmodifier));
    fogmodifier->player = player ? PLAYER_NUM(player) : 0;
    fogmodifier->state = state ? *state : 0;
    fogmodifier->use_shared_vision = useShared;
    return fogmodifier;
}
uint32_t CreateFogModifierRect(LPJASS j) {
    LPPLAYER forWhichPlayer = jass_checkhandle(j, 1, "player");
    uint32_t *whichState = jass_checkhandle(j, 2, "fogstate");
    LPCBOX2 where = jass_checkhandle(j, 3, "rect");
    bool useSharedVision = jass_checkboolean(j, 4);
    LPFOGMODIFIER mod = G_NewFogModifier(j, forWhichPlayer, whichState, useSharedVision);
    if (mod && where) {
        mod->is_rect = true;
        mod->rect = *where;
    }
    return 1;
}
uint32_t CreateFogModifierRadius(LPJASS j) {
    LPPLAYER forWhichPlayer = jass_checkhandle(j, 1, "player");
    uint32_t *whichState = jass_checkhandle(j, 2, "fogstate");
    float centerx = jass_checknumber(j, 3);
    float centerY = jass_checknumber(j, 4);
    float radius = jass_checknumber(j, 5);
    bool useSharedVision = jass_checkboolean(j, 6);
    LPFOGMODIFIER mod = G_NewFogModifier(j, forWhichPlayer, whichState, useSharedVision);
    if (mod) {
        mod->center = MAKE(VECTOR2, centerx, centerY);
        mod->radius = radius;
    }
    return 1;
}
uint32_t CreateFogModifierRadiusLoc(LPJASS j) {
    LPPLAYER forWhichPlayer = jass_checkhandle(j, 1, "player");
    uint32_t *whichState = jass_checkhandle(j, 2, "fogstate");
    LPCVECTOR2 center = jass_checkhandle(j, 3, "location");
    float radius = jass_checknumber(j, 4);
    bool useSharedVision = jass_checkboolean(j, 5);
    LPFOGMODIFIER mod = G_NewFogModifier(j, forWhichPlayer, whichState, useSharedVision);
    if (mod && center) {
        mod->center = *center;
        mod->radius = radius;
    }
    return 1;
}
uint32_t DestroyFogModifier(LPJASS j) {
    LPFOGMODIFIER whichFogModifier = jass_checkhandle(j, 1, "fogmodifier");
    G_FogModifierStop(whichFogModifier);
    return 0;
}
uint32_t FogModifierStart(LPJASS j) {
    LPFOGMODIFIER whichFogModifier = jass_checkhandle(j, 1, "fogmodifier");
    G_FogModifierStart(whichFogModifier);
    return 0;
}
uint32_t FogModifierStop(LPJASS j) {
    LPFOGMODIFIER whichFogModifier = jass_checkhandle(j, 1, "fogmodifier");
    G_FogModifierStop(whichFogModifier);
    return 0;
}
uint32_t DisplayTextToPlayer(LPJASS j) {
    LPPLAYER toPlayer = jass_checkhandle(j, 1, "player");
    float x = jass_checknumber(j, 2);
    float y = jass_checknumber(j, 3);
    cstring_t message = jass_checkstring(j, 4);
    if (TutorialTextDebugEnabledPlayer()) {
        int32_t trigger_ordinal;
        cstring_t caller;
        TutorialTextDebugContextPlayer(j, &trigger_ordinal, &caller);
        fprintf(stderr,
                "WC3_TUTORIAL_TEXT native=DisplayTextToPlayer trigger=%ld caller=\"%s\" player=%d x=%.3f y=%.3f duration=auto raw=\"%s\" resolved=\"%s\"\n",
                (long)trigger_ordinal, caller ? caller : "(native/root)",
                toPlayer ? (int)PLAYER_NUM(toPlayer) : -1, x, y,
                message ? message : "", G_LevelString(message) ? G_LevelString(message) : "");
    }
    UI_ShowText(PLAYER_ENT(toPlayer), &MAKE(VECTOR2, x, y), message, -1.0f);
    return 0;
}
uint32_t DisplayTimedTextToPlayer(LPJASS j) {
    LPPLAYER toPlayer = jass_checkhandle(j, 1, "player");
    float x = jass_checknumber(j, 2);
    float y = jass_checknumber(j, 3);
    float duration = jass_checknumber(j, 4);
    cstring_t message = jass_checkstring(j, 5);
    if (TutorialTextDebugEnabledPlayer()) {
        int32_t trigger_ordinal;
        cstring_t caller;
        TutorialTextDebugContextPlayer(j, &trigger_ordinal, &caller);
        fprintf(stderr,
                "WC3_TUTORIAL_TEXT native=DisplayTimedTextToPlayer trigger=%ld caller=\"%s\" player=%d x=%.3f y=%.3f duration=%.3f raw=\"%s\" resolved=\"%s\"\n",
                (long)trigger_ordinal, caller ? caller : "(native/root)",
                toPlayer ? (int)PLAYER_NUM(toPlayer) : -1, x, y, duration,
                message ? message : "", G_LevelString(message) ? G_LevelString(message) : "");
    }
    UI_ShowText(PLAYER_ENT(toPlayer), &MAKE(VECTOR2, x, y), message, duration);
    return 0;
}
uint32_t DisplayTimedTextFromPlayer(LPJASS j) {
    LPPLAYER toPlayer = jass_checkhandle(j, 1, "player");
    float x = jass_checknumber(j, 2);
    float y = jass_checknumber(j, 3);
    float duration = jass_checknumber(j, 4);
    cstring_t message = jass_checkstring(j, 5);
    if (TutorialTextDebugEnabledPlayer()) {
        int32_t trigger_ordinal;
        cstring_t caller;
        TutorialTextDebugContextPlayer(j, &trigger_ordinal, &caller);
        fprintf(stderr,
                "WC3_TUTORIAL_TEXT native=DisplayTimedTextFromPlayer trigger=%ld caller=\"%s\" player=%d x=%.3f y=%.3f duration=%.3f raw=\"%s\" resolved=\"%s\"\n",
                (long)trigger_ordinal, caller ? caller : "(native/root)",
                toPlayer ? (int)PLAYER_NUM(toPlayer) : -1, x, y, duration,
                message ? message : "", G_LevelString(message) ? G_LevelString(message) : "");
    }
    UI_ShowText(PLAYER_ENT(toPlayer), &MAKE(VECTOR2, x, y), message, duration);
    return 0;
}
uint32_t ClearTextMessages(LPJASS j) {
    if (currentplayer) {
        UI_ClearTextMessages(PLAYER_ENT(currentplayer));
    } else {
        FOR_LOOP(i, game.max_clients) {
            LPEDICT ent = G_GetPlayerEntityByNumber(i);
            if (ent && ent->client) UI_ClearTextMessages(ent);
        }
    }
    return 0;
}
uint32_t StartMeleeAI(LPJASS j) {
    LPPLAYER player = jass_checkhandle(j, 1, "player");
    cstring_t script = jass_checkstring(j, 2);
    G_BotStart(player, script, BOT_MELEE);
    return 0;
}
uint32_t StartCampaignAI(LPJASS j) {
    LPPLAYER player = jass_checkhandle(j, 1, "player");
    cstring_t script = jass_checkstring(j, 2);
    G_BotStart(player, script, BOT_CAMPAIGN);
    return 0;
}
uint32_t CommandAI(LPJASS j) {
    LPPLAYER player = jass_checkhandle(j, 1, "player");
    G_BotPushCommand(player, jass_checkinteger(j, 2), jass_checkinteger(j, 3));
    return 0;
}
uint32_t PauseCompAI(LPJASS j) {
    LPPLAYER player = jass_checkhandle(j, 1, "player");
    bool pause = jass_checkboolean(j, 2);
    if (player) G_BotPause(PLAYER_NUM(player), pause);
    return 0;
}
uint32_t RemoveAllGuardPositions(LPJASS j) {
    //handle_t num = jass_checkhandle(j, 1, "player");
    return 0;
}
uint32_t SetBlight(LPJASS j) {
    LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    VECTOR2 point = { jass_checknumber(j, 2), jass_checknumber(j, 3) };
    float radius = jass_checknumber(j, 4);
    bool addBlight = jass_checkboolean(j, 5);
    (void)whichPlayer; /* Blight is global terrain state; player is API-compatible ownership context. */
    G_SetBlightRadius(&point, radius, addBlight);
    return 0;
}
uint32_t SetBlightRect(LPJASS j) {
    LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    LPCBOX2 r = jass_checkhandle(j, 2, "rect");
    bool addBlight = jass_checkboolean(j, 3);
    (void)whichPlayer;
    if (r) G_SetBlightRect(r, addBlight);
    return 0;
}
uint32_t SetBlightPoint(LPJASS j) {
    LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    VECTOR2 point = { jass_checknumber(j, 2), jass_checknumber(j, 3) };
    bool addBlight = jass_checkboolean(j, 4);
    (void)whichPlayer;
    G_SetBlightPoint(&point, addBlight);
    return 0;
}
uint32_t SetBlightLoc(LPJASS j) {
    LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    LPCVECTOR2 whichLocation = jass_checkhandle(j, 2, "location");
    float radius = jass_checknumber(j, 3);
    bool addBlight = jass_checkboolean(j, 4);
    (void)whichPlayer;
    if (whichLocation) G_SetBlightRadius(whichLocation, radius, addBlight);
    return 0;
}

static void JassMarkSelectionDirty(LPPLAYER player) {
    if (player) {
        PLAYER_CLIENT(player)->selection_dirty = true;
    } else {
        FOR_LOOP(i, game.max_clients) game.clients[i].selection_dirty = true;
    }
}

uint32_t ClearSelection(LPJASS j) {
    FOR_LOOP(i, globals.num_edicts) {
        if (currentplayer) {
            /* Selection is a per-player bitmask; clear only this player's bit. */
            g_edicts[i].selected &= ~(1u << PLAYER_NUM(currentplayer));
        } else {
            g_edicts[i].selected = 0;
        }
    }
    JassMarkSelectionDirty(currentplayer);
    return 0;
}
uint32_t SelectUnit(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    bool flag = jass_checkboolean(j, 2);
    if (!whichUnit) {
        return 0;
    }
    if (flag) {
        if (currentplayer) {
            whichUnit->selected |= 1 << PLAYER_NUM(currentplayer);
        } else {
            whichUnit->selected = -1;
        }
    } else {
        if (currentplayer) {
            /* SelectUnit(false) must preserve every other player's selection bit. */
            whichUnit->selected &= ~(1u << PLAYER_NUM(currentplayer));
        } else {
            whichUnit->selected = 0;
        }
    }
    JassMarkSelectionDirty(currentplayer);
    return 0;
}
