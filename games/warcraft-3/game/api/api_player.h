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
DWORD SetPlayerTaxRate(LPJASS j) {
    //HANDLE sourcePlayer = jass_checkhandle(j, 1, "player");
    //HANDLE otherPlayer = jass_checkhandle(j, 2, "player");
    //HANDLE whichResource = jass_checkhandle(j, 3, "playerstate");
    //LONG rate = jass_checkinteger(j, 4);
    return 0;
}
DWORD SetPlayerRacePreference(LPJASS j) {
    //LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    //HANDLE whichRacePreference = jass_checkhandle(j, 2, "racepreference");
    return 0;
}
DWORD SetPlayerRaceSelectable(LPJASS j) {
    //LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    //BOOL value = jass_checkboolean(j, 2);
    return 0;
}
DWORD SetPlayerController(LPJASS j) {
    //LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    //HANDLE controlType = jass_checkhandle(j, 2, "mapcontrol");
    return 0;
}
DWORD SetPlayerOnScoreScreen(LPJASS j) {
    //LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    //BOOL flag = jass_checkboolean(j, 2);
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
    //LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    return jass_pushboolean(j, 0);
}
DWORD GetPlayerController(LPJASS j) {
    //LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    return jass_pushnullhandle(j, "mapcontrol");
}
DWORD GetPlayerSlotState(LPJASS j) {
    LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    LPGAMECLIENT client = whichPlayer ? PLAYER_CLIENT(whichPlayer) : NULL;
    LONG state = 0;

    if (client && client->mapplayer &&
        (client->mapplayer->playerType == kPlayerTypeHuman ||
         client->mapplayer->playerType == kPlayerTypeComputer))
    {
        state = 1;
    }
    return JassPushPlayerSlotStateHandle(j, state);
}
DWORD GetPlayerTaxRate(LPJASS j) {
    //HANDLE sourcePlayer = jass_checkhandle(j, 1, "player");
    //HANDLE otherPlayer = jass_checkhandle(j, 2, "player");
    //HANDLE whichResource = jass_checkhandle(j, 3, "playerstate");
    return jass_pushinteger(j, 0);
}
DWORD IsPlayerRacePrefSet(LPJASS j) {
    //LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    //HANDLE pref = jass_checkhandle(j, 2, "racepreference");
    return jass_pushboolean(j, 0);
}
DWORD GetPlayerName(LPJASS j) {
    LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    LPGAMECLIENT client = whichPlayer ? PLAYER_CLIENT(whichPlayer) : NULL;
    LPCSTR name = "";
    if (client && client->mapplayer && client->mapplayer->playerName) {
        name = client->mapplayer->playerName;
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
    return jass_pushboolean(j, (*whichForce) & (1 << PLAYER_NUM(whichPlayer)));
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
    //LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    //BOOL includeIncomplete = jass_checkboolean(j, 2);
    return jass_pushinteger(j, 0);
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
    //LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    return jass_pushnumber(j, 0);
}
DWORD GetPlayerHandicapXP(LPJASS j) {
    //LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    return jass_pushnumber(j, 0);
}
DWORD SetPlayerHandicap(LPJASS j) {
    //LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    //FLOAT handicap = jass_checknumber(j, 2);
    return 0;
}
DWORD SetPlayerHandicapXP(LPJASS j) {
    //LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    //FLOAT handicap = jass_checknumber(j, 2);
    return 0;
}
DWORD SetPlayerTechMaxAllowed(LPJASS j) {
    //LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    //LONG techid = jass_checkinteger(j, 2);
    //LONG maximum = jass_checkinteger(j, 3);
    return 0;
}
DWORD GetPlayerTechMaxAllowed(LPJASS j) {
    //LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    //LONG techid = jass_checkinteger(j, 2);
    return jass_pushinteger(j, 0);
}
DWORD AddPlayerTechResearched(LPJASS j) {
    //LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    //LONG techid = jass_checkinteger(j, 2);
    //LONG levels = jass_checkinteger(j, 3);
    return 0;
}
DWORD SetPlayerTechResearched(LPJASS j) {
    //LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    //LONG techid = jass_checkinteger(j, 2);
    //LONG setToLevel = jass_checkinteger(j, 3);
    return 0;
}
DWORD GetPlayerTechResearched(LPJASS j) {
    //LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    //LONG techid = jass_checkinteger(j, 2);
    //BOOL specificonly = jass_checkboolean(j, 3);
    return jass_pushboolean(j, 0);
}
DWORD GetPlayerTechCount(LPJASS j) {
    //LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    //LONG techid = jass_checkinteger(j, 2);
    //BOOL specificonly = jass_checkboolean(j, 3);
    return jass_pushinteger(j, 0);
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
    LPGAMECLIENT client = PLAYER_CLIENT(whichPlayer);
    client->ps.stats[*whichPlayerState] = MAX(0, value);
    return 0;
}
DWORD RemovePlayer(LPJASS j) {
    /* Ghidra: RemovePlayer (FUN_00401220) -> worker FUN_003b32c0 marks the
     * player removed and records their game result (PLAYER_GAME_RESULT_*:
     * 0=VICTORY, 1=DEFEAT, 2=TIE, 3=NEUTRAL), driving the victory/defeat
     * notification.  Our equivalent of that notification is the player victory/
     * defeat game event the map registers via TriggerRegisterPlayerEventVictory
     * /Defeat (EVENT_PLAYER_VICTORY=14 / EVENT_PLAYER_DEFEAT=13), so publishing
     * it here fires the mission's end-of-level triggers (e.g. CustomVictoryBJ
     * -> RemovePlayer -> the victory dialog/quest completion). */
    LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    DWORD *gameResult = jass_checkhandle(j, 2, "playergameresult");
    if (whichPlayer && gameResult) {
        LPEDICT pent = PLAYER_ENT(whichPlayer);
        if (pent) {
            if (*gameResult == 0) {
                G_PublishEvent(pent, EVENT_PLAYER_VICTORY);
            } else if (*gameResult == 1) {
                G_PublishEvent(pent, EVENT_PLAYER_DEFEAT);
            }
        }
    }
    return 0;
}
DWORD CachePlayerHeroData(LPJASS j) {
    //LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    return 0;
}
DWORD SetFogStateRect(LPJASS j) {
    //HANDLE forWhichPlayer = jass_checkhandle(j, 1, "player");
    //HANDLE whichState = jass_checkhandle(j, 2, "fogstate");
    //HANDLE where = jass_checkhandle(j, 3, "rect");
    //BOOL useSharedVision = jass_checkboolean(j, 4);
    return 0;
}
DWORD SetFogStateRadius(LPJASS j) {
    //HANDLE forWhichPlayer = jass_checkhandle(j, 1, "player");
    //HANDLE whichState = jass_checkhandle(j, 2, "fogstate");
    //FLOAT centerx = jass_checknumber(j, 3);
    //FLOAT centerY = jass_checknumber(j, 4);
    //FLOAT radius = jass_checknumber(j, 5);
    //BOOL useSharedVision = jass_checkboolean(j, 6);
    return 0;
}
DWORD SetFogStateRadiusLoc(LPJASS j) {
    //HANDLE forWhichPlayer = jass_checkhandle(j, 1, "player");
    //HANDLE whichState = jass_checkhandle(j, 2, "fogstate");
    //HANDLE center = jass_checkhandle(j, 3, "location");
    //FLOAT radius = jass_checknumber(j, 4);
    //BOOL useSharedVision = jass_checkboolean(j, 5);
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
    UI_ShowText(PLAYER_ENT(toPlayer), &MAKE(VECTOR2, x, y), message, 0);
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
    return 0;
}
DWORD StartMeleeAI(LPJASS j) {
    //HANDLE num = jass_checkhandle(j, 1, "player");
    //LPCSTR script = jass_checkstring(j, 2);
    return 0;
}
DWORD StartCampaignAI(LPJASS j) {
    //HANDLE num = jass_checkhandle(j, 1, "player");
    //LPCSTR script = jass_checkstring(j, 2);
    return 0;
}
DWORD CommandAI(LPJASS j) {
    //HANDLE num = jass_checkhandle(j, 1, "player");
    //LONG command = jass_checkinteger(j, 2);
    //LONG data = jass_checkinteger(j, 3);
    return 0;
}
DWORD PauseCompAI(LPJASS j) {
    //HANDLE p = jass_checkhandle(j, 1, "player");
    //BOOL pause = jass_checkboolean(j, 2);
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
