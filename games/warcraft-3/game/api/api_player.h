extern LPPLAYER currentplayer;

DWORD SetPlayerTeam(LPJASS j) {
    LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    LONG whichTeam = jass_checkinteger(j, 2);
    whichPlayer->team = whichTeam;
    return 0;
}
DWORD SetPlayerStartLocation(LPJASS j) {
    LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    LONG startLocIndex = jass_checkinteger(j, 2);
    if (whichPlayer) PLAYER_CLIENT(whichPlayer)->ps.start_location = startLocIndex;
    return 0;
}
DWORD ForcePlayerStartLocation(LPJASS j) {
    LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    LONG startLocIndex = jass_checkinteger(j, 2);
    if (whichPlayer) PLAYER_CLIENT(whichPlayer)->ps.start_location = startLocIndex;
    return 0;
}
DWORD SetPlayerColor(LPJASS j) {
    LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    DWORD *pColor = jass_checkhandle(j, 2, "playercolor");
    if (whichPlayer && pColor) {
        whichPlayer->color = *pColor;
    }
    return 0;
}
DWORD SetPlayerAlliance(LPJASS j) {
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
    BOOL value = jass_checkboolean(j, 4);
    G_SetPlayerAlliance(sourcePlayer, otherPlayer, *whichAllianceSetting, value);
    return 0;
}
/* Player-configuration natives need server-owned WC3 client state. Race
 * preferences are a mask, tax is directional and resource-keyed, and controller
 * state reflects config()/lobby choices; none belong in the networked PLAYER. */
DWORD SetPlayerTaxRate(LPJASS j) {
    LPPLAYER source = jass_checkhandle(j, 1, "player"), other = jass_checkhandle(j, 2, "player");
    LPDWORD resource = jass_checkhandle(j, 3, "playerstate");
    LONG rate = jass_checkinteger(j, 4);
    if (source && other && resource && *resource <= PLAYERSTATE_LUMBER_GATHERED)
        PLAYER_CLIENT(source)->jass.tax[PLAYER_NUM(other)][*resource] = MIN(MAX(0, rate), 100);
    return 0;
}
DWORD SetPlayerRacePreference(LPJASS j) {
    LPPLAYER player = jass_checkhandle(j, 1, "player");
    LPDWORD pref = jass_checkhandle(j, 2, "racepreference");
    if (player && pref) PLAYER_CLIENT(player)->jass.race_pref |= *pref;
    return 0;
}
DWORD SetPlayerRaceSelectable(LPJASS j) {
    LPPLAYER player = jass_checkhandle(j, 1, "player");
    if (player) PLAYER_CLIENT(player)->jass.race_selectable = jass_checkboolean(j, 2);
    return 0;
}
DWORD SetPlayerController(LPJASS j) {
    LPPLAYER player = jass_checkhandle(j, 1, "player");
    LPDWORD control = jass_checkhandle(j, 2, "mapcontrol");
    if (player && control) PLAYER_CLIENT(player)->jass.controller = *control;
    return 0;
}
DWORD SetPlayerName(LPJASS j) {
    LPPLAYER player = jass_checkhandle(j, 1, "player");
    if (player) {
        LPGAMECLIENT client = PLAYER_CLIENT(player);
        strlcpy(client->jass.name, jass_checkstring(j, 2), sizeof(client->jass.name));
        client->ps.name = client->jass.name;
    }
    return 0;
}
DWORD SetPlayerOnScoreScreen(LPJASS j) {
    LPPLAYER player = jass_checkhandle(j, 1, "player");
    if (player) PLAYER_CLIENT(player)->jass.on_score_screen = jass_checkboolean(j, 2);
    return 0;
}
DWORD GetPlayerTeam(LPJASS j) {
    LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    return jass_pushinteger(j, whichPlayer->team);
}
DWORD GetPlayerStartLocation(LPJASS j) {
    LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    LONG loc = whichPlayer ? PLAYER_CLIENT(whichPlayer)->ps.start_location : -1;
    return jass_pushinteger(j, loc);
}
DWORD GetPlayerColor(LPJASS j) {
    LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    DWORD *playercolor = jass_newhandle(j, sizeof(DWORD), "playercolor");
    *playercolor = whichPlayer ? whichPlayer->color : 0;
    return 1;
}
DWORD GetPlayerSelectable(LPJASS j) {
    LPPLAYER player = jass_checkhandle(j, 1, "player");
    return jass_pushboolean(j, player && PLAYER_CLIENT(player)->jass.race_selectable);
}
DWORD GetPlayerController(LPJASS j) {
    LPPLAYER player = jass_checkhandle(j, 1, "player");
    return JassPushMapControlHandle(j, player ? PLAYER_CLIENT(player)->jass.controller : 5);
}
DWORD GetPlayerSlotState(LPJASS j) {
    LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    LPGAMECLIENT client = whichPlayer ? PLAYER_CLIENT(whichPlayer) : NULL;
    LONG state = 0;

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
DWORD GetPlayerTaxRate(LPJASS j) {
    LPPLAYER source = jass_checkhandle(j, 1, "player"), other = jass_checkhandle(j, 2, "player");
    LPDWORD resource = jass_checkhandle(j, 3, "playerstate");
    LONG rate = source && other && resource && *resource <= PLAYERSTATE_LUMBER_GATHERED ?
        PLAYER_CLIENT(source)->jass.tax[PLAYER_NUM(other)][*resource] : 0;
    return jass_pushinteger(j, rate);
}
DWORD IsPlayerRacePrefSet(LPJASS j) {
    LPPLAYER player = jass_checkhandle(j, 1, "player");
    LPDWORD pref = jass_checkhandle(j, 2, "racepreference");
    return jass_pushboolean(j, player && pref && (PLAYER_CLIENT(player)->jass.race_pref & *pref));
}
DWORD GetPlayerName(LPJASS j) {
    LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    LPGAMECLIENT client = whichPlayer ? PLAYER_CLIENT(whichPlayer) : NULL;
    LPCSTR name = "";
    if (client && client->jass.name[0]) {
        name = client->jass.name;
    } else if (whichPlayer && whichPlayer->name) {
        name = whichPlayer->name;
    }
    return jass_pushstring(j, name);
}
DWORD IssueNeutralImmediateOrder(LPJASS j) {
    //HANDLE forWhichPlayer = jass_checkhandle(j, 1, "player");
    //HANDLE neutralStructure = jass_checkhandle(j, 2, "unit");
    //LPCSTR unitToBuild = jass_checkstring(j, 3);
    return jass_pushboolean(j, 0);
}
DWORD IssueNeutralImmediateOrderById(LPJASS j) {
    //HANDLE forWhichPlayer = jass_checkhandle(j, 1, "player");
    //HANDLE neutralStructure = jass_checkhandle(j, 2, "unit");
    //LONG unitId = jass_checkinteger(j, 3);
    return jass_pushboolean(j, 0);
}
DWORD IssueNeutralPointOrder(LPJASS j) {
    //HANDLE forWhichPlayer = jass_checkhandle(j, 1, "player");
    //HANDLE neutralStructure = jass_checkhandle(j, 2, "unit");
    //LPCSTR unitToBuild = jass_checkstring(j, 3);
    //FLOAT x = jass_checknumber(j, 4);
    //FLOAT y = jass_checknumber(j, 5);
    return jass_pushboolean(j, 0);
}
DWORD IssueNeutralPointOrderById(LPJASS j) {
    //HANDLE forWhichPlayer = jass_checkhandle(j, 1, "player");
    //HANDLE neutralStructure = jass_checkhandle(j, 2, "unit");
    //LONG unitId = jass_checkinteger(j, 3);
    //FLOAT x = jass_checknumber(j, 4);
    //FLOAT y = jass_checknumber(j, 5);
    return jass_pushboolean(j, 0);
}
DWORD IssueNeutralTargetOrder(LPJASS j) {
    //HANDLE forWhichPlayer = jass_checkhandle(j, 1, "player");
    //HANDLE neutralStructure = jass_checkhandle(j, 2, "unit");
    //LPCSTR unitToBuild = jass_checkstring(j, 3);
    //HANDLE target = jass_checkhandle(j, 4, "widget");
    return jass_pushboolean(j, 0);
}
DWORD IssueNeutralTargetOrderById(LPJASS j) {
    //HANDLE forWhichPlayer = jass_checkhandle(j, 1, "player");
    //HANDLE neutralStructure = jass_checkhandle(j, 2, "unit");
    //LONG unitId = jass_checkinteger(j, 3);
    //HANDLE target = jass_checkhandle(j, 4, "widget");
    return jass_pushboolean(j, 0);
}
DWORD Player(LPJASS j) {
    LONG number = jass_checkinteger(j, 1);
    LPPLAYER player = G_GetPlayerByNumber(number);
    return jass_pushlighthandle(j, player, "player");
}
DWORD GetLocalPlayer(LPJASS j) {
    return jass_pushlighthandle(j, (LPMAPPLAYER)currentplayer, "player");
}
DWORD IsPlayerAlly(LPJASS j) {
    LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    LPPLAYER otherPlayer = jass_checkhandle(j, 2, "player");
    if (!whichPlayer || !otherPlayer) return jass_pushboolean(j, 0);
    return jass_pushboolean(j, G_GetPlayerAlliance(whichPlayer, otherPlayer, ALLIANCE_PASSIVE));
}
DWORD IsPlayerEnemy(LPJASS j) {
    LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    LPPLAYER otherPlayer = jass_checkhandle(j, 2, "player");
    if (!whichPlayer || !otherPlayer) return jass_pushboolean(j, 0);
    return jass_pushboolean(j, !G_GetPlayerAlliance(whichPlayer, otherPlayer, ALLIANCE_PASSIVE));
}
DWORD IsPlayerInForce(LPJASS j) {
    LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    LPDWORD whichForce = jass_checkhandle(j, 2, "force");
    return jass_pushboolean(j, whichPlayer && whichForce && ((*whichForce) & (1 << PLAYER_NUM(whichPlayer))));
}
DWORD IsPlayerObserver(LPJASS j) {
    //LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    return jass_pushboolean(j, 0);
}
DWORD IsVisibleToPlayer(LPJASS j) {
    //FLOAT x = jass_checknumber(j, 1);
    //FLOAT y = jass_checknumber(j, 2);
    //HANDLE whichPlayer = jass_checkhandle(j, 3, "player");
    return jass_pushboolean(j, 0);
}
DWORD IsLocationVisibleToPlayer(LPJASS j) {
    //HANDLE whichLocation = jass_checkhandle(j, 1, "location");
    //HANDLE whichPlayer = jass_checkhandle(j, 2, "player");
    return jass_pushboolean(j, 0);
}
DWORD IsFoggedToPlayer(LPJASS j) {
    //FLOAT x = jass_checknumber(j, 1);
    //FLOAT y = jass_checknumber(j, 2);
    //HANDLE whichPlayer = jass_checkhandle(j, 3, "player");
    return jass_pushboolean(j, 0);
}
DWORD IsLocationFoggedToPlayer(LPJASS j) {
    //HANDLE whichLocation = jass_checkhandle(j, 1, "location");
    //HANDLE whichPlayer = jass_checkhandle(j, 2, "player");
    return jass_pushboolean(j, 0);
}
DWORD IsMaskedToPlayer(LPJASS j) {
    //FLOAT x = jass_checknumber(j, 1);
    //FLOAT y = jass_checknumber(j, 2);
    //HANDLE whichPlayer = jass_checkhandle(j, 3, "player");
    return jass_pushboolean(j, 0);
}
DWORD IsLocationMaskedToPlayer(LPJASS j) {
    //HANDLE whichLocation = jass_checkhandle(j, 1, "location");
    //HANDLE whichPlayer = jass_checkhandle(j, 2, "player");
    return jass_pushboolean(j, 0);
}
DWORD GetPlayerRace(LPJASS j) {
    LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    LONG race = whichPlayer ? (LONG)whichPlayer->race : kPlayerRaceNone;

    return JassPushRaceHandle(j, race);
}
DWORD GetPlayerId(LPJASS j) {
    LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    return jass_pushinteger(j, whichPlayer ? (LONG)whichPlayer->number : 0);
}
DWORD GetPlayerUnitCount(LPJASS j) {
    //LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    //BOOL includeIncomplete = jass_checkboolean(j, 2);
    return jass_pushinteger(j, 0);
}
DWORD GetPlayerTypedUnitCount(LPJASS j) {
    //LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    //LPCSTR unitName = jass_checkstring(j, 2);
    //BOOL includeIncomplete = jass_checkboolean(j, 3);
    //BOOL includeUpgrades = jass_checkboolean(j, 4);
    return jass_pushinteger(j, 0);
}
DWORD GetPlayerStructureCount(LPJASS j) {
    LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    BOOL includeIncomplete = jass_checkboolean(j, 2);
    LONG count = 0;

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
DWORD GetPlayerState(LPJASS j) {
    LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    PLAYERSTATE *whichPlayerState = jass_checkhandle(j, 2, "playerstate");
    LPGAMECLIENT client = PLAYER_CLIENT(whichPlayer);
    return jass_pushinteger(j, client->ps.stats[*whichPlayerState]);
}
DWORD GetPlayerAlliance(LPJASS j) {
    LPPLAYER sourcePlayer = jass_checkhandle(j, 1, "player");
    LPPLAYER otherPlayer = jass_checkhandle(j, 2, "player");
    PLAYERALLIANCE *whichAllianceSetting = jass_checkhandle(j, 3, "alliancetype");
    return jass_pushboolean(j, G_GetPlayerAlliance(sourcePlayer, otherPlayer, *whichAllianceSetting));
}
DWORD GetPlayerHandicap(LPJASS j) {
    LPPLAYER player = jass_checkhandle(j, 1, "player");
    return jass_pushnumber(j, player ? PLAYER_CLIENT(player)->jass.handicap : 100.0f);
}
DWORD GetPlayerHandicapXP(LPJASS j) {
    LPPLAYER player = jass_checkhandle(j, 1, "player");
    return jass_pushnumber(j, player ? PLAYER_CLIENT(player)->jass.handicap_xp : 100.0f);
}
DWORD SetPlayerHandicap(LPJASS j) {
    LPPLAYER player = jass_checkhandle(j, 1, "player");
    if (player) PLAYER_CLIENT(player)->jass.handicap = MAX(0, jass_checknumber(j, 2));
    return 0;
}
DWORD SetPlayerHandicapXP(LPJASS j) {
    LPPLAYER player = jass_checkhandle(j, 1, "player");
    if (player) PLAYER_CLIENT(player)->jass.handicap_xp = MAX(0, jass_checknumber(j, 2));
    return 0;
}
DWORD SetPlayerTechMaxAllowed(LPJASS j) {
    LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    LONG techid = jass_checkinteger(j, 2);
    LONG maximum = jass_checkinteger(j, 3);
    if (whichPlayer) G_SetPlayerTechMaxAllowed(PLAYER_CLIENT(whichPlayer), (DWORD)techid, maximum);
    return 0;
}
DWORD GetPlayerTechMaxAllowed(LPJASS j) {
    LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    LONG techid = jass_checkinteger(j, 2);
    LONG maximum = whichPlayer ? G_GetPlayerTechMaxAllowed(PLAYER_CLIENT(whichPlayer), (DWORD)techid) : -1;
    return jass_pushinteger(j, maximum);
}
DWORD AddPlayerTechResearched(LPJASS j) {
    LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    LONG techid = jass_checkinteger(j, 2);
    LONG levels = jass_checkinteger(j, 3);
    if (whichPlayer) G_AddPlayerTechResearched(PLAYER_CLIENT(whichPlayer), (DWORD)techid, levels);
    return 0;
}
DWORD SetPlayerTechResearched(LPJASS j) {
    LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    LONG techid = jass_checkinteger(j, 2);
    LONG setToLevel = jass_checkinteger(j, 3);
    if (whichPlayer) G_SetPlayerTechResearched(PLAYER_CLIENT(whichPlayer), (DWORD)techid, setToLevel);
    return 0;
}
DWORD GetPlayerTechResearched(LPJASS j) {
    LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    LONG techid = jass_checkinteger(j, 2);
    BOOL specificonly = jass_checkboolean(j, 3);
    /* TODO: model Warcraft technology-equivalence groups. Until that data is
     * represented, both specificonly modes address the exact rawcode only. */
    (void)specificonly;
    return jass_pushboolean(j, whichPlayer &&
        G_GetPlayerTechResearchedLevel(PLAYER_CLIENT(whichPlayer), (DWORD)techid) > 0);
}
DWORD GetPlayerTechCount(LPJASS j) {
    LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    LONG techid = jass_checkinteger(j, 2);
    BOOL specificonly = jass_checkboolean(j, 3);
    /* TODO: model Warcraft technology-equivalence groups. Until that data is
     * represented, both specificonly modes address the exact rawcode only. */
    (void)specificonly;
    return jass_pushinteger(j, whichPlayer ? G_GetPlayerTechCountValue(PLAYER_CLIENT(whichPlayer), (DWORD)techid) : 0);
}
DWORD SetPlayerAbilityAvailable(LPJASS j) {
    //LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    //LONG abilid = jass_checkinteger(j, 2);
    //BOOL avail = jass_checkboolean(j, 3);
    return 0;
}
DWORD SetPlayerState(LPJASS j) {
    LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    PLAYERSTATE *whichPlayerState = jass_checkhandle(j, 2, "playerstate");
    LONG value = jass_checkinteger(j, 3);
    LPGAMECLIENT client;

    if (!whichPlayer || !whichPlayerState || *whichPlayerState >= MAX_STATS) return 0;
    client = PLAYER_CLIENT(whichPlayer);
    client->ps.stats[*whichPlayerState] = (USHORT)MIN(MAX(0, value), USHRT_MAX);
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
DWORD RemovePlayer(LPJASS j) {
    /* Warcraft records a per-player result and transitions that slot to LEFT.
     * Victory/defeat are player events; TIE/NEUTRAL still remove the slot but do
     * not synthesize victory/defeat events. The temporary native result overlay
     * is deferred until queued JASS events have had a chance to enter cinematic
     * mode, so an ending cinematic is not covered by the fallback UI. */
    LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    DWORD *gameResult = jass_checkhandle(j, 2, "playergameresult");
    LPGAMECLIENT client;
    LPEDICT pent;
    DWORD player_num;

    G_GameResultDebug("RemovePlayer enter player_handle=%p result_handle=%p result=%ld events=%u/%u",
        (void *)whichPlayer, (void *)gameResult,
        gameResult ? (long)*gameResult : -1L,
        (unsigned)level.events.read, (unsigned)level.events.write);

    if (!whichPlayer || !gameResult || *gameResult > 3) {
        G_GameResultDebug("RemovePlayer ignored reason=invalid_args player=%p result=%ld",
            (void *)whichPlayer, gameResult ? (long)*gameResult : -1L);
        return 0;
    }

    player_num = PLAYER_NUM(whichPlayer);
    client = PLAYER_CLIENT(whichPlayer);
    G_GameResultDebug("RemovePlayer resolved player=%u client_index=%ld connected=%u removed=%u ui=%u",
        (unsigned)player_num,
        client ? (long)(client - game.clients) : -1L,
        client ? (unsigned)client->connected : 0u,
        client ? (unsigned)client->jass.removed : 0u,
        client ? (unsigned)client->ps.client_ui_state : 0u);

    if (!client) {
        G_GameResultDebug("RemovePlayer ignored player=%u reason=no_client", (unsigned)player_num);
        return 0;
    }
    if (client->jass.removed) {
        G_GameResultDebug("RemovePlayer ignored player=%u reason=already_removed stored_result=%u",
            (unsigned)player_num, (unsigned)client->ps.stats[PLAYERSTATE_GAME_RESULT]);
        return 0;
    }

    client->ps.stats[PLAYERSTATE_GAME_RESULT] = (USHORT)*gameResult;
    client->jass.removed = true;
    client->jass.pending_game_result = 0;
    client->jass.pending_game_result_event = level.events.read;
    G_BotRequestStop(player_num);

    pent = PLAYER_ENT(whichPlayer);
    G_GameResultDebug("RemovePlayer state player=%u result=%u pent=%p ent=%ld inuse=%u owner=%u",
        (unsigned)player_num, (unsigned)*gameResult, (void *)pent,
        pent ? (long)pent->s.number : -1L,
        pent ? (unsigned)pent->inuse : 0u,
        pent ? (unsigned)pent->s.player : 0u);
    if (!pent) {
        G_GameResultDebug("RemovePlayer player=%u result=%u has no player edict; no result event/UI queued",
            (unsigned)player_num, (unsigned)*gameResult);
        return 0;
    }

    if (*gameResult == 0) {
        G_PublishEvent(pent, EVENT_PLAYER_VICTORY);
        client->jass.pending_game_result = 1;
        client->jass.pending_game_result_event = level.events.write;
        G_GameResultDebug("RemovePlayer queued VICTORY player=%u wait_event=%u events=%u/%u",
            (unsigned)player_num, (unsigned)client->jass.pending_game_result_event,
            (unsigned)level.events.read, (unsigned)level.events.write);
    } else if (*gameResult == 1) {
        G_PublishEvent(pent, EVENT_PLAYER_DEFEAT);
        client->jass.pending_game_result = 2;
        client->jass.pending_game_result_event = level.events.write;
        G_GameResultDebug("RemovePlayer queued DEFEAT player=%u wait_event=%u events=%u/%u",
            (unsigned)player_num, (unsigned)client->jass.pending_game_result_event,
            (unsigned)level.events.read, (unsigned)level.events.write);
    } else {
        G_GameResultDebug("RemovePlayer player=%u result=%u records removal only; no victory/defeat UI",
            (unsigned)player_num, (unsigned)*gameResult);
    }
    return 0;
}
DWORD CachePlayerHeroData(LPJASS j) {
    //LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    return 0;
}
DWORD SetFogStateRect(LPJASS j) {
    LPPLAYER forWhichPlayer = jass_checkhandle(j, 1, "player");
    DWORD *whichState = jass_checkhandle(j, 2, "fogstate");
    LPCBOX2 where = jass_checkhandle(j, 3, "rect");
    BOOL useSharedVision = jass_checkboolean(j, 4);
    if (forWhichPlayer && whichState && where) {
        FOGWRITE fog = { PLAYER_NUM(forWhichPlayer), *whichState, useSharedVision };
        G_FowSetStateRect(&fog, where);
    }
    return 0;
}
DWORD SetFogStateRadius(LPJASS j) {
    LPPLAYER forWhichPlayer = jass_checkhandle(j, 1, "player");
    DWORD *whichState = jass_checkhandle(j, 2, "fogstate");
    VECTOR2 center = { jass_checknumber(j, 3), jass_checknumber(j, 4) };
    FLOAT radius = jass_checknumber(j, 5);
    BOOL useSharedVision = jass_checkboolean(j, 6);
    if (forWhichPlayer && whichState) {
        FOGWRITE fog = { PLAYER_NUM(forWhichPlayer), *whichState, useSharedVision };
        G_FowSetStateRadius(&fog, &center, radius);
    }
    return 0;
}
DWORD SetFogStateRadiusLoc(LPJASS j) {
    LPPLAYER forWhichPlayer = jass_checkhandle(j, 1, "player");
    DWORD *whichState = jass_checkhandle(j, 2, "fogstate");
    LPCVECTOR2 center = jass_checkhandle(j, 3, "location");
    FLOAT radius = jass_checknumber(j, 4);
    BOOL useSharedVision = jass_checkboolean(j, 5);
    if (forWhichPlayer && whichState && center) {
        FOGWRITE fog = { PLAYER_NUM(forWhichPlayer), *whichState, useSharedVision };
        G_FowSetStateRadius(&fog, center, radius);
    }
    return 0;
}
DWORD FogMaskEnable(LPJASS j) {
    BOOL enable = jass_checkboolean(j, 1);
    if (currentplayer) {
        LPGAMECLIENT client = PLAYER_CLIENT(currentplayer);
        SET_FLAG(client->ps.rdflags, RDF_NOFOGMASK, !enable);
    } else FOR_LOOP(i, game.max_clients) {
        SET_FLAG(game.clients[i].ps.rdflags, RDF_NOFOGMASK, !enable);
    }
    return 0;
}
DWORD FogEnable(LPJASS j) {
    BOOL enable = jass_checkboolean(j, 1);
    if (currentplayer) {
        LPGAMECLIENT client = PLAYER_CLIENT(currentplayer);
        SET_FLAG(client->ps.rdflags, RDF_NOFOG, !enable);
    } else FOR_LOOP(i, game.max_clients) {
        SET_FLAG(game.clients[i].ps.rdflags, RDF_NOFOG, !enable);
    }
    return 0;
}
DWORD IsFogMaskEnabled(LPJASS j) {
    if (currentplayer) {
        LPGAMECLIENT client = PLAYER_CLIENT(currentplayer);
        return jass_pushboolean(j, !(client->ps.rdflags & RDF_NOFOGMASK));
    } else {
        return jass_pushboolean(j, !(game.clients->ps.rdflags & RDF_NOFOGMASK));
    }
}
DWORD IsFogEnabled(LPJASS j) {
    if (currentplayer) {
        LPGAMECLIENT client = PLAYER_CLIENT(currentplayer);
        return jass_pushboolean(j, !(client->ps.rdflags & RDF_NOFOG));
    } else {
        return jass_pushboolean(j, !(game.clients->ps.rdflags & RDF_NOFOG));
    }
}
static LPFOGMODIFIER G_NewFogModifier(LPJASS j, LPPLAYER player, DWORD *state, BOOL useShared) {
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
DWORD CreateFogModifierRect(LPJASS j) {
    LPPLAYER forWhichPlayer = jass_checkhandle(j, 1, "player");
    DWORD *whichState = jass_checkhandle(j, 2, "fogstate");
    LPCBOX2 where = jass_checkhandle(j, 3, "rect");
    BOOL useSharedVision = jass_checkboolean(j, 4);
    LPFOGMODIFIER mod = G_NewFogModifier(j, forWhichPlayer, whichState, useSharedVision);
    if (mod && where) {
        mod->is_rect = true;
        mod->rect = *where;
    }
    return 1;
}
DWORD CreateFogModifierRadius(LPJASS j) {
    LPPLAYER forWhichPlayer = jass_checkhandle(j, 1, "player");
    DWORD *whichState = jass_checkhandle(j, 2, "fogstate");
    FLOAT centerx = jass_checknumber(j, 3);
    FLOAT centerY = jass_checknumber(j, 4);
    FLOAT radius = jass_checknumber(j, 5);
    BOOL useSharedVision = jass_checkboolean(j, 6);
    LPFOGMODIFIER mod = G_NewFogModifier(j, forWhichPlayer, whichState, useSharedVision);
    if (mod) {
        mod->center = MAKE(VECTOR2, centerx, centerY);
        mod->radius = radius;
    }
    return 1;
}
DWORD CreateFogModifierRadiusLoc(LPJASS j) {
    LPPLAYER forWhichPlayer = jass_checkhandle(j, 1, "player");
    DWORD *whichState = jass_checkhandle(j, 2, "fogstate");
    LPCVECTOR2 center = jass_checkhandle(j, 3, "location");
    FLOAT radius = jass_checknumber(j, 4);
    BOOL useSharedVision = jass_checkboolean(j, 5);
    LPFOGMODIFIER mod = G_NewFogModifier(j, forWhichPlayer, whichState, useSharedVision);
    if (mod && center) {
        mod->center = *center;
        mod->radius = radius;
    }
    return 1;
}
DWORD DestroyFogModifier(LPJASS j) {
    LPFOGMODIFIER whichFogModifier = jass_checkhandle(j, 1, "fogmodifier");
    G_FogModifierStop(whichFogModifier);
    return 0;
}
DWORD FogModifierStart(LPJASS j) {
    LPFOGMODIFIER whichFogModifier = jass_checkhandle(j, 1, "fogmodifier");
    G_FogModifierStart(whichFogModifier);
    return 0;
}
DWORD FogModifierStop(LPJASS j) {
    LPFOGMODIFIER whichFogModifier = jass_checkhandle(j, 1, "fogmodifier");
    G_FogModifierStop(whichFogModifier);
    return 0;
}
DWORD DisplayTextToPlayer(LPJASS j) {
    LPPLAYER toPlayer = jass_checkhandle(j, 1, "player");
    FLOAT x = jass_checknumber(j, 2);
    FLOAT y = jass_checknumber(j, 3);
    LPCSTR message = jass_checkstring(j, 4);
    UI_ShowText(PLAYER_ENT(toPlayer), &MAKE(VECTOR2, x, y), message, -1.0f);
    return 0;
}
DWORD DisplayTimedTextToPlayer(LPJASS j) {
    LPPLAYER toPlayer = jass_checkhandle(j, 1, "player");
    FLOAT x = jass_checknumber(j, 2);
    FLOAT y = jass_checknumber(j, 3);
    FLOAT duration = jass_checknumber(j, 4);
    LPCSTR message = jass_checkstring(j, 5);
    UI_ShowText(PLAYER_ENT(toPlayer), &MAKE(VECTOR2, x, y), message, duration);
    return 0;
}
DWORD DisplayTimedTextFromPlayer(LPJASS j) {
    LPPLAYER toPlayer = jass_checkhandle(j, 1, "player");
    FLOAT x = jass_checknumber(j, 2);
    FLOAT y = jass_checknumber(j, 3);
    FLOAT duration = jass_checknumber(j, 4);
    LPCSTR message = jass_checkstring(j, 5);
    UI_ShowText(PLAYER_ENT(toPlayer), &MAKE(VECTOR2, x, y), message, duration);
    return 0;
}
DWORD ClearTextMessages(LPJASS j) {
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
DWORD StartMeleeAI(LPJASS j) {
    LPPLAYER player = jass_checkhandle(j, 1, "player");
    LPCSTR script = jass_checkstring(j, 2);
    G_BotStart(player, script, BOT_MELEE);
    return 0;
}
DWORD StartCampaignAI(LPJASS j) {
    LPPLAYER player = jass_checkhandle(j, 1, "player");
    LPCSTR script = jass_checkstring(j, 2);
    G_BotStart(player, script, BOT_CAMPAIGN);
    return 0;
}
DWORD CommandAI(LPJASS j) {
    LPPLAYER player = jass_checkhandle(j, 1, "player");
    G_BotPushCommand(player, jass_checkinteger(j, 2), jass_checkinteger(j, 3));
    return 0;
}
DWORD PauseCompAI(LPJASS j) {
    LPPLAYER player = jass_checkhandle(j, 1, "player");
    BOOL pause = jass_checkboolean(j, 2);
    if (player) G_BotPause(PLAYER_NUM(player), pause);
    return 0;
}
DWORD RemoveAllGuardPositions(LPJASS j) {
    //HANDLE num = jass_checkhandle(j, 1, "player");
    return 0;
}
DWORD SetBlight(LPJASS j) {
    //LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    //FLOAT x = jass_checknumber(j, 2);
    //FLOAT y = jass_checknumber(j, 3);
    //FLOAT radius = jass_checknumber(j, 4);
    //BOOL addBlight = jass_checkboolean(j, 5);
    return 0;
}
DWORD SetBlightRect(LPJASS j) {
    //LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    //HANDLE r = jass_checkhandle(j, 2, "rect");
    //BOOL addBlight = jass_checkboolean(j, 3);
    return 0;
}
DWORD SetBlightPoint(LPJASS j) {
    //LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    //FLOAT x = jass_checknumber(j, 2);
    //FLOAT y = jass_checknumber(j, 3);
    //BOOL addBlight = jass_checkboolean(j, 4);
    return 0;
}
DWORD SetBlightLoc(LPJASS j) {
    //LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    //HANDLE whichLocation = jass_checkhandle(j, 2, "location");
    //FLOAT radius = jass_checknumber(j, 3);
    //BOOL addBlight = jass_checkboolean(j, 4);
    return 0;
}
DWORD ClearSelection(LPJASS j) {
    FOR_LOOP(i, globals.num_edicts) {
        if (currentplayer) {
            g_edicts[i].selected &= 1 << PLAYER_NUM(currentplayer);
        } else {
            g_edicts[i].selected = 0;
        }
    }
    return 0;
}
DWORD SelectUnit(LPJASS j) {
    LPEDICT whichUnit = jass_checkhandle(j, 1, "unit");
    BOOL flag = jass_checkboolean(j, 2);
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
            whichUnit->selected &= 1 << PLAYER_NUM(currentplayer);
        } else {
            whichUnit->selected = 0;
        }
    }
    return 0;
}
