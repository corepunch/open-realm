extern LPPLAYER currentplayer;

static BOOL TutorialTextDebugEnabledMisc(void) {
    return WC3_TUTORIAL_DEBUG_ENABLED();
}

static void TutorialTextDebugContextMisc(LPJASS j, LONG *trigger_ordinal, LPCSTR *caller) {
    LPCJASSCONTEXT context = jass_getcontext(j);
    if (trigger_ordinal)
        *trigger_ordinal = context && context->trigger ? (LONG)(context->trigger - level.triggers) : -1L;
    if (caller)
        *caller = context && context->func ? jass_functionname(context->func) : NULL;
}

DWORD class_id(LPCSTR str) { return *(DWORD *)str; }

/* Converted enums are owned JASS handles; enum equality compares their DWORD payload. */
static DWORD JassPushEnumHandle(LPJASS j, LPCSTR type, LONG value) {
    LPDWORD handle = jass_newhandle(j, sizeof(*handle), type);
    *handle = (DWORD)value;
    return 1;
}

static DWORD JassPushRaceHandle(LPJASS j, LONG value) {
    return JassPushEnumHandle(j, "race", value);
}

static DWORD JassPushPlayerSlotStateHandle(LPJASS j, LONG value) {
    return JassPushEnumHandle(j, "playerslotstate", value);
}

#define CONVERT_FUNC(NAME, TYPE) \
static DWORD JassPush##NAME##Handle(LPJASS j, LONG value) { return JassPushEnumHandle(j, #TYPE, value); } \
DWORD Convert##NAME(LPJASS j) { \
    return JassPush##NAME##Handle(j, jass_checkinteger(j, 1)); \
}

#define MATH_FUNC(NAME, FUNC, INPUT, OUTPUT) \
DWORD NAME(LPJASS j) { \
    return jass_push##OUTPUT(j, FUNC(jass_check##INPUT(j, 1))); \
}

#define MATH_FUNC2(NAME, FUNC, OUTPUT) \
DWORD NAME(LPJASS j) { \
    FLOAT arg1 = jass_checknumber(j, 1); \
    FLOAT arg2 = jass_checknumber(j, 2); \
    return jass_push##OUTPUT(j, FUNC(arg1, arg2)); \
}

DWORD ConvertRace(LPJASS j) {
    return JassPushRaceHandle(j, jass_checkinteger(j, 1));
}
CONVERT_FUNC(AllianceType, alliancetype);
CONVERT_FUNC(RacePref, racepreference);
CONVERT_FUNC(IGameState, igamestate);
CONVERT_FUNC(FGameState, fgamestate);
CONVERT_FUNC(PlayerState, playerstate);
CONVERT_FUNC(PlayerGameResult, playergameresult);
CONVERT_FUNC(UnitState, unitstate);
CONVERT_FUNC(GameEvent, gameevent);
CONVERT_FUNC(PlayerEvent, playerevent);
CONVERT_FUNC(PlayerUnitEvent, playerunitevent);
CONVERT_FUNC(WidgetEvent, widgetevent);
CONVERT_FUNC(DialogEvent, dialogevent);
CONVERT_FUNC(UnitEvent, unitevent);
CONVERT_FUNC(LimitOp, limitop);
CONVERT_FUNC(UnitType, unittype);
CONVERT_FUNC(GameSpeed, gamespeed);
CONVERT_FUNC(Placement, placement);
CONVERT_FUNC(StartLocPrio, startlocprio);
CONVERT_FUNC(GameDifficulty, gamedifficulty);
CONVERT_FUNC(GameType, gametype);
CONVERT_FUNC(MapFlag, mapflag);
CONVERT_FUNC(MapVisibility, mapvisibility);
CONVERT_FUNC(MapSetting, mapsetting);
CONVERT_FUNC(MapDensity, mapdensity);
CONVERT_FUNC(MapControl, mapcontrol);
CONVERT_FUNC(PlayerColor, playercolor);
DWORD ConvertPlayerSlotState(LPJASS j) {
    return JassPushPlayerSlotStateHandle(j, jass_checkinteger(j, 1));
}
CONVERT_FUNC(VolumeGroup, volumegroup);
CONVERT_FUNC(CameraField, camerafield);
CONVERT_FUNC(BlendMode, blendmode);
CONVERT_FUNC(RarityControl, raritycontrol);
CONVERT_FUNC(TexMapFlags, texmapflags);
CONVERT_FUNC(FogState, fogstate);
CONVERT_FUNC(EffectType, effecttype);

MATH_FUNC(Deg2Rad, DEG2RAD, number, number);
MATH_FUNC(Rad2Deg, RAD2DEG, number, number);
MATH_FUNC(Sin, sin, number, number);
MATH_FUNC(Cos, cos, number, number);
MATH_FUNC(Tan, tan, number, number);
MATH_FUNC(Asin, asin, number, number);
MATH_FUNC(Acos, acos, number, number);
MATH_FUNC(Atan, atan, number, number);
MATH_FUNC(SquareRoot, sqrt, number, number);
MATH_FUNC(I2R, (FLOAT), integer, number);
MATH_FUNC(R2I, (LONG), number, integer);
MATH_FUNC2(Pow, pow, number);
MATH_FUNC2(Atan2, atan2, number);
DWORD OrderId(LPJASS j) {
    return jass_pushinteger(j, (LONG)G_OrderId(jass_checkstring(j, 1)));
}
DWORD OrderId2String(LPJASS j) {
    return jass_pushstring(j, G_OrderId2String((DWORD)jass_checkinteger(j, 1)));
}
MATH_FUNC(UnitId, class_id, string, integer);
MATH_FUNC(AbilityId, class_id, string, integer);
MATH_FUNC(UnitId2String, GetClassName, integer, string);
MATH_FUNC(AbilityId2String, GetClassName, integer, string);
MATH_FUNC(S2I, atoi, string, integer);
MATH_FUNC(S2R, atoi, string, number);

DWORD I2S(LPJASS j) {
    LONG i = jass_checkinteger(j, 1);
    char buffer[64] = { 0 };
    snprintf(buffer, sizeof(buffer), "%d", i);
    return jass_pushstring(j, buffer);
}
DWORD R2S(LPJASS j) {
    FLOAT r = jass_checknumber(j, 1);
    char buffer[64] = { 0 };
    snprintf(buffer, sizeof(buffer), "%f", r);
    return jass_pushstring(j, buffer);
}
DWORD R2SW(LPJASS j) {
    FLOAT r = jass_checknumber(j, 1);
    LONG width = jass_checkinteger(j, 2);
    LONG precision = jass_checkinteger(j, 3);
    /* Clamp to safe values so the formatted float fits in the 64-byte buffer.
     * A floating-point number needs at most ~25 chars; add width up to 32
     * and precision up to 16 for a safe upper bound well within 64 bytes. */
    if (width < 0 || width > 32) width = 0;
    if (precision < 0 || precision > 16) precision = 6;
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%*.*f", (int)width, (int)precision, (double)r);
    return jass_pushstring(j, buffer);
}
DWORD SubString(LPJASS j) {
    LPCSTR source = jass_checkstring(j, 1);
    LONG start = jass_checkinteger(j, 2);
    LONG end = jass_checkinteger(j, 3);
    if (!source) return jass_pushstring(j, "");
    LONG len = (LONG)strlen(source);
    if (start < 0) start = 0;
    if (end > len) end = len;
    if (start >= end) return jass_pushstring(j, "");
    LONG n = end - start;
    char *buf = gi.MemAlloc(n + 1);
    memcpy(buf, source + start, (size_t)n);
    buf[n] = '\0';
    DWORD result = jass_pushstring(j, buf);
    gi.MemFree(buf);
    return result;
}
DWORD GetLocalizedString(LPJASS j) {
    LPCSTR source = jass_checkstring(j, 1);
    return jass_pushstring(j, source ? source : "");
}
DWORD GetLocalizedHotkey(LPJASS j) {
    //LPCSTR source = jass_checkstring(j, 1);
    return jass_pushinteger(j, 0);
}
/* Map-configuration natives run from config() before main(). They must mutate a
 * per-level setup snapshot initialized from war3map.w3i; level.mapinfo is
 * authoritative input and must not remain the writable runtime store. */
DWORD SetMapName(LPJASS j) {
    strlcpy(level.setup.name, jass_checkstring(j, 1), sizeof(level.setup.name));
    return 0;
}
DWORD SetMapDescription(LPJASS j) {
    strlcpy(level.setup.description, jass_checkstring(j, 1), sizeof(level.setup.description));
    return 0;
}
DWORD SetTeams(LPJASS j) {
    level.setup.teams = MIN(MAX(0, jass_checkinteger(j, 1)), MAX_PLAYERS);
    return 0;
}
DWORD SetPlayers(LPJASS j) {
    level.setup.players = MIN(MAX(0, jass_checkinteger(j, 1)), MAX_PLAYERS);
    return 0;
}
DWORD DefineStartLocation(LPJASS j) {
    LONG whichStartLoc = jass_checkinteger(j, 1);
    FLOAT x = jass_checknumber(j, 2);
    FLOAT y = jass_checknumber(j, 3);

    if (level.mapinfo && whichStartLoc >= 0 && whichStartLoc < MAX_PLAYERS) {
        ((LPMAPINFO)level.mapinfo)->players[whichStartLoc].startingPosition = (VECTOR2){ x, y };
    }
    return 0;
}
DWORD DefineStartLocationLoc(LPJASS j) {
    LONG whichStartLoc = jass_checkinteger(j, 1);
    LPCVECTOR2 whichLocation = jass_checkhandle(j, 2, "location");

    if (level.mapinfo && whichLocation &&
        whichStartLoc >= 0 && whichStartLoc < MAX_PLAYERS) {
        ((LPMAPINFO)level.mapinfo)->players[whichStartLoc].startingPosition = *whichLocation;
    }
    return 0;
}
DWORD SetStartLocPrioCount(LPJASS j) {
    LONG loc = jass_checkinteger(j, 1), count = jass_checkinteger(j, 2);
    if (loc >= 0 && loc < MAX_PLAYERS) level.setup.start_prio[loc].count = MIN(MAX(0, count), MAX_START_PRIO);
    return 0;
}
DWORD SetStartLocPrio(LPJASS j) {
    LONG loc = jass_checkinteger(j, 1), slot = jass_checkinteger(j, 2), other = jass_checkinteger(j, 3);
    LPDWORD priority = jass_checkhandle(j, 4, "startlocprio");
    if (loc >= 0 && loc < MAX_PLAYERS && slot >= 0 && slot < (LONG)level.setup.start_prio[loc].count && priority)
        level.setup.start_prio[loc].slots[slot] = (typeof(*level.setup.start_prio[loc].slots)){ other, *priority };
    return 0;
}
DWORD GetStartLocPrioSlot(LPJASS j) {
    LONG loc = jass_checkinteger(j, 1), slot = jass_checkinteger(j, 2);
    LONG value = loc >= 0 && loc < MAX_PLAYERS && slot >= 0 && slot < (LONG)level.setup.start_prio[loc].count ?
        level.setup.start_prio[loc].slots[slot].location : 0;
    return jass_pushinteger(j, value);
}
DWORD GetStartLocPrio(LPJASS j) {
    LONG loc = jass_checkinteger(j, 1), slot = jass_checkinteger(j, 2);
    LONG value = loc >= 0 && loc < MAX_PLAYERS && slot >= 0 && slot < (LONG)level.setup.start_prio[loc].count ?
        level.setup.start_prio[loc].slots[slot].priority : 0;
    return JassPushStartLocPrioHandle(j, value);
}
DWORD SetGameTypeSupported(LPJASS j) {
    LPDWORD type = jass_checkhandle(j, 1, "gametype");
    BOOL value = jass_checkboolean(j, 2);
    if (type) {
        SET_FLAG(level.setup.game_types, *type, value);
    }
    return 0;
}
DWORD SetMapFlag(LPJASS j) {
    LPDWORD flag = jass_checkhandle(j, 1, "mapflag");
    BOOL value = jass_checkboolean(j, 2);
    if (flag) {
        SET_FLAG(level.setup.map_flags, *flag, value);
    }
    return 0;
}
DWORD SetGamePlacement(LPJASS j) {
    LPDWORD value = jass_checkhandle(j, 1, "placement");
    if (value) level.setup.placement = *value;
    return 0;
}
DWORD SetGameSpeed(LPJASS j) {
    LPDWORD value = jass_checkhandle(j, 1, "gamespeed");
    if (value) level.setup.speed = *value;
    return 0;
}
DWORD SetGameDifficulty(LPJASS j) {
    LPDWORD value = jass_checkhandle(j, 1, "gamedifficulty");
    if (value) level.setup.difficulty = *value;
    return 0;
}
DWORD SetResourceDensity(LPJASS j) {
    LPDWORD value = jass_checkhandle(j, 1, "mapdensity");
    if (value) level.setup.resource_density = *value;
    return 0;
}
DWORD SetCreatureDensity(LPJASS j) {
    LPDWORD value = jass_checkhandle(j, 1, "mapdensity");
    if (value) level.setup.creature_density = *value;
    return 0;
}
DWORD GetTeams(LPJASS j) {
    return jass_pushinteger(j, level.setup.teams);
}
DWORD GetPlayers(LPJASS j) {
    return jass_pushinteger(j, level.setup.players);
}
DWORD IsGameTypeSupported(LPJASS j) {
    LPDWORD type = jass_checkhandle(j, 1, "gametype");
    return jass_pushboolean(j, type && (level.setup.game_types & *type));
}
DWORD GetGameTypeSelected(LPJASS j) {
    return JassPushGameTypeHandle(j, level.setup.game_type);
}
DWORD IsMapFlagSet(LPJASS j) {
    LPDWORD flag = jass_checkhandle(j, 1, "mapflag");
    return jass_pushboolean(j, flag && (level.setup.map_flags & *flag));
}
DWORD GetGamePlacement(LPJASS j) {
    return JassPushPlacementHandle(j, level.setup.placement);
}
DWORD GetGameSpeed(LPJASS j) {
    return JassPushGameSpeedHandle(j, level.setup.speed);
}
DWORD GetGameDifficulty(LPJASS j) {
    return JassPushGameDifficultyHandle(j, level.setup.difficulty);
}
DWORD GetResourceDensity(LPJASS j) {
    return JassPushMapDensityHandle(j, level.setup.resource_density);
}
DWORD GetCreatureDensity(LPJASS j) {
    return JassPushMapDensityHandle(j, level.setup.creature_density);
}
DWORD GetStartLocationX(LPJASS j) {
    LONG whichStartLocation = jass_checkinteger(j, 1);

    if (!level.mapinfo || whichStartLocation < 0 || whichStartLocation >= MAX_PLAYERS) {
        return jass_pushnumber(j, 0);
    }
    return jass_pushnumber(j, level.mapinfo->players[whichStartLocation].startingPosition.x);
}
DWORD GetStartLocationY(LPJASS j) {
    LONG whichStartLocation = jass_checkinteger(j, 1);

    if (!level.mapinfo || whichStartLocation < 0 || whichStartLocation >= MAX_PLAYERS) {
        return jass_pushnumber(j, 0);
    }
    return jass_pushnumber(j, level.mapinfo->players[whichStartLocation].startingPosition.y);
}
DWORD GetStartLocationLoc(LPJASS j) {
    LONG whichStartLocation = jass_checkinteger(j, 1);
    API_ALLOC(VECTOR2, location);

    if (level.mapinfo && whichStartLocation >= 0 && whichStartLocation < MAX_PLAYERS) {
        *location = level.mapinfo->players[whichStartLocation].startingPosition;
    }
    return 1;
}

DWORD CreateTimer(LPJASS j) {
    LPGTIMER timer = G_AllocJassTimer();
    if (!timer) { jass_rterror(j, "CreateTimer: timer registry is full"); return 0; }
    return jass_pushlighthandle(j, timer, "timer");
}
DWORD DestroyTimer(LPJASS j) {
    LPGTIMER whichTimer = jass_checkhandle(j, 1, "timer");
    if (whichTimer) whichTimer->running = false;
    return 0;
}
DWORD TimerStart(LPJASS j) {
    LPGTIMER whichTimer = jass_checkhandle(j, 1, "timer");
    FLOAT timeout = jass_checknumber(j, 2);
    BOOL periodic = jass_checkboolean(j, 3);
    /* Warcraft accepts null to start/reset a timer without an expiration callback. */
    LPCJASSFUNC handlerFunc = jass_toboolean(j, 4) ? jass_checkcode(j, 4) : NULL;
    if (whichTimer) G_TimerStart(whichTimer, (DWORD)(MAX(0.0f, timeout) * 1000.0f), periodic, handlerFunc);
    return 0;
}
DWORD TimerGetElapsed(LPJASS j) {
    LPGTIMER whichTimer = jass_checkhandle(j, 1, "timer");
    return jass_pushnumber(j, whichTimer ? (whichTimer->duration - G_TimerRemaining(whichTimer)) / 1000.0f : 0.0f);
}
DWORD TimerGetRemaining(LPJASS j) {
    LPGTIMER whichTimer = jass_checkhandle(j, 1, "timer");
    return jass_pushnumber(j, G_TimerRemaining(whichTimer) / 1000.0f);
}
DWORD TimerGetTimeout(LPJASS j) {
    LPGTIMER whichTimer = jass_checkhandle(j, 1, "timer");
    return jass_pushnumber(j, whichTimer ? whichTimer->duration / 1000.0f : 0.0f);
}
DWORD PauseTimer(LPJASS j) {
    G_TimerPause(jass_checkhandle(j, 1, "timer")); return 0;
}
DWORD ResumeTimer(LPJASS j) {
    G_TimerResume(jass_checkhandle(j, 1, "timer")); return 0;
}
DWORD GetExpiredTimer(LPJASS j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->timer, "timer");
}
DWORD CreateForce(LPJASS j) {
    API_ALLOC(DWORD, force);
    (void)force;
    return 1;
}
DWORD ForceAddPlayer(LPJASS j) {
    LPDWORD whichForce = jass_checkhandle(j, 1, "force");
    LPPLAYER whichPlayer = jass_checkhandle(j, 2, "player");
    if (whichForce && whichPlayer) *whichForce |= 1 << PLAYER_NUM(whichPlayer);
    return 0;
}
DWORD ForceRemovePlayer(LPJASS j) {
    LPDWORD whichForce = jass_checkhandle(j, 1, "force");
    LPPLAYER whichPlayer = jass_checkhandle(j, 2, "player");
    if (whichForce && whichPlayer) *whichForce &= ~(1 << PLAYER_NUM(whichPlayer));
    return 0;
}
DWORD ForceClear(LPJASS j) {
    LPDWORD whichForce = jass_checkhandle(j, 1, "force");
    if (whichForce) *whichForce = 0;
    return 0;
}
DWORD DestroyForce(LPJASS j) {
    LPDWORD whichForce = jass_checkhandle(j, 1, "force");
    if (whichForce) *whichForce = 0;
    return 0;
}
/* Force filters bind each candidate as GetFilterPlayer(); limits count accepted
 * players, matching group enumeration rather than limiting candidates tested. */
DWORD ForceEnumPlayers(LPJASS j) {
    LPDWORD whichForce = jass_checkhandle(j, 1, "force");
    LPCJASSFUNC filter = jass_checkhandle(j, 2, "boolexpr");
    if (!whichForce) return 0;
    FOR_LOOP(i, game.max_clients)
        if (jass_evaluateplayerexpr(j, filter, &game.clients[i].ps)) *whichForce |= 1 << game.clients[i].ps.number;
    return 0;
}
DWORD ForceEnumPlayersCounted(LPJASS j) {
    LPDWORD whichForce = jass_checkhandle(j, 1, "force");
    LPCJASSFUNC filter = jass_checkhandle(j, 2, "boolexpr");
    LONG countLimit = jass_checkinteger(j, 3);
    if (!whichForce || countLimit <= 0) return 0;
    FOR_LOOP(i, game.max_clients) {
        LPPLAYER player = &game.clients[i].ps;
        if (jass_evaluateplayerexpr(j, filter, player)) *whichForce |= 1 << player->number, countLimit--;
        if (!countLimit) break;
    }
    return 0;
}
DWORD ForceEnumAllies(LPJASS j) {
    LPDWORD whichForce = jass_checkhandle(j, 1, "force");
    LPPLAYER whichPlayer = jass_checkhandle(j, 2, "player");
    LPCJASSFUNC filter = jass_checkhandle(j, 3, "boolexpr");
    if (!whichForce || !whichPlayer) return 0;
    FOR_LOOP(i, game.max_clients) {
        LPPLAYER player = &game.clients[i].ps;
        if (G_GetPlayerAlliance(whichPlayer, player, ALLIANCE_PASSIVE) && jass_evaluateplayerexpr(j, filter, player))
            *whichForce |= 1 << player->number;
    }
    return 0;
}
DWORD ForceEnumEnemies(LPJASS j) {
    LPDWORD whichForce = jass_checkhandle(j, 1, "force");
    LPPLAYER whichPlayer = jass_checkhandle(j, 2, "player");
    LPCJASSFUNC filter = jass_checkhandle(j, 3, "boolexpr");
    if (!whichForce || !whichPlayer) return 0;
    FOR_LOOP(i, game.max_clients) {
        LPPLAYER player = &game.clients[i].ps;
        if (!G_GetPlayerAlliance(whichPlayer, player, ALLIANCE_PASSIVE) && jass_evaluateplayerexpr(j, filter, player))
            *whichForce |= 1 << player->number;
    }
    return 0;
}
DWORD ForForce(LPJASS j) {
    extern LPPLAYER currentenumplayer;
    LPDWORD whichForce = jass_checkhandle(j, 1, "force");
    LPCJASSFUNC callback = jass_checkcode(j, 2);
    LPPLAYER previous = currentenumplayer;

    if (!whichForce || !callback) {
        return 0;
    }
    FOR_LOOP(i, MAX_PLAYERS) {
        if (!(*whichForce & (1 << i))) {
            continue;
        }
        currentenumplayer = G_GetPlayerByNumber(i);
        if (!currentenumplayer) {
            continue;
        }
        jass_pushfunction(j, callback);
        jass_call(j, 0);
    }
    currentenumplayer = previous;
    return 0;
}
DWORD IsUnitInRegion(LPJASS j) {
    LPCREGION whichRegion = jass_checkhandle(j, 1, "region");
    LPCEDICT whichUnit = jass_checkhandle(j, 2, "unit");
    return jass_pushboolean(j, whichRegion && whichUnit && G_RegionContains(whichRegion, &whichUnit->s.origin2));
}
DWORD IsPointInRegion(LPJASS j) {
    LPCREGION whichRegion = jass_checkhandle(j, 1, "region");
    VECTOR2 point = { jass_checknumber(j, 2), jass_checknumber(j, 3) };
    return jass_pushboolean(j, whichRegion && G_RegionContains(whichRegion, &point));
}
DWORD IsLocationInRegion(LPJASS j) {
    LPCREGION whichRegion = jass_checkhandle(j, 1, "region");
    LPCVECTOR2 whichLocation = jass_checkhandle(j, 2, "location");
    return jass_pushboolean(j, whichRegion && whichLocation && G_RegionContains(whichRegion, whichLocation));
}
/* Return the loaded terrain bounds so Blizzard.j's GetEntireMapRect can enumerate every map unit. */
DWORD GetWorldBounds(LPJASS j) {
    API_ALLOC(BOX2, rect);
    *rect = CM_GetWorldBounds();
    return 1;
}
DWORD GetFilterUnit(LPJASS j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->unit, "unit");
}
DWORD GetEnumUnit(LPJASS j) {
    extern LPEDICT currentunit;
    return jass_pushlighthandle(j, currentunit, "unit");
}
DWORD GetFilterDestructable(LPJASS j) {
    return jass_pushnullhandle(j, "destructable");
}
DWORD GetEnumDestructable(LPJASS j) {
    extern LPEDICT currentdestructable;
    return jass_pushlighthandle(j, currentdestructable, "destructable");
}
DWORD GetFilterPlayer(LPJASS j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->playerState, "player");
}
DWORD GetEnumPlayer(LPJASS j) {
    extern LPPLAYER currentenumplayer;
    return jass_pushlighthandle(j, currentenumplayer, "player");
}
DWORD ExecuteFunc(LPJASS j) {
    LPCSTR funcName = jass_checkstring(j, 1);
    (void)jass_startcoroutinebyname(j, funcName);
    return 0;
}
DWORD newthread(LPJASS j) {
    LPCJASSFUNC func = jass_checkcode(j, 1);
    JASSCONTEXT context = *jass_getcontext(j);
    context.func = func;
    jass_startcoroutine(j, &context);
    return 0;
}
DWORD And(LPJASS j) {
    //HANDLE operandA = jass_checkhandle(j, 1, "boolexpr");
    //HANDLE operandB = jass_checkhandle(j, 2, "boolexpr");
    return jass_pushnullhandle(j, "boolexpr");
}
DWORD Or(LPJASS j) {
    //HANDLE operandA = jass_checkhandle(j, 1, "boolexpr");
    //HANDLE operandB = jass_checkhandle(j, 2, "boolexpr");
    return jass_pushnullhandle(j, "boolexpr");
}
DWORD Not(LPJASS j) {
    //HANDLE operand = jass_checkhandle(j, 1, "boolexpr");
    return jass_pushnullhandle(j, "boolexpr");
}
DWORD Condition(LPJASS j) {
    LPCJASSFUNC func = jass_checkcode(j, 1);
    return jass_pushlighthandle(j, (HANDLE)func, "conditionfunc");
}
DWORD DestroyCondition(LPJASS j) {
    //HANDLE c = jass_checkhandle(j, 1, "conditionfunc");
    return 0;
}
DWORD Filter(LPJASS j) {
    /* Like Condition(): wrap the code as a boolexpr handle so enumeration
     * natives (GroupEnumUnitsInRect, ForceEnum*, etc.) can evaluate it per
     * candidate via jass_evaluateboolexpr.  Was a stub returning null, which
     * made every Filter()-based enum match everything (e.g. GetUnitsInRectOf-
     * Player returned all players' units, polluting victory/kill-count groups). */
    LPCJASSFUNC func = jass_checkcode(j, 1);
    return jass_pushlighthandle(j, (HANDLE)func, "filterfunc");
}
DWORD DestroyFilter(LPJASS j) {
    //HANDLE f = jass_checkhandle(j, 1, "filterfunc");
    return 0;
}
DWORD DestroyBoolExpr(LPJASS j) {
    //HANDLE e = jass_checkhandle(j, 1, "boolexpr");
    return 0;
}
DWORD GetEventGameState(LPJASS j) {
    return jass_pushnullhandle(j, "gamestate");
}
DWORD GetWinningPlayer(LPJASS j) {
    return jass_pushnullhandle(j, "player");
}
DWORD GetEnteringUnit(LPJASS j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->unit, "unit");
}
DWORD GetLeavingUnit(LPJASS j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->unit, "unit");
}
DWORD GetTriggeringTrackable(LPJASS j) {
    return jass_pushnullhandle(j, "trackable");
}
DWORD GetClickedButton(LPJASS j) {
    return jass_pushnullhandle(j, "button");
}
DWORD GetClickedDialog(LPJASS j) {
    return jass_pushnullhandle(j, "dialog");
}
DWORD GetLevelingUnit(LPJASS j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->unit, "unit");
}
DWORD GetLearningUnit(LPJASS j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->unit, "unit");
}
DWORD GetLearnedSkill(LPJASS j) {
    return jass_pushinteger(j, 0);
}
DWORD GetLearnedSkillLevel(LPJASS j) {
    return jass_pushinteger(j, 0);
}
DWORD GetRevivableUnit(LPJASS j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->unit, "unit");
}
DWORD GetRevivingUnit(LPJASS j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->unit, "unit");
}
DWORD GetAttacker(LPJASS j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->unit, "unit");
}
DWORD GetRescuer(LPJASS j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->unit, "unit");
}
DWORD GetDyingUnit(LPJASS j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->unit, "unit");
}
DWORD GetKillingUnit(LPJASS j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->source, "unit");
}
DWORD GetDecayingUnit(LPJASS j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->unit, "unit");
}
DWORD GetConstructingStructure(LPJASS j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->unit, "unit");
}
DWORD GetCancelledStructure(LPJASS j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->unit, "unit");
}
DWORD GetConstructedStructure(LPJASS j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->unit, "unit");
}
DWORD GetResearchingUnit(LPJASS j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->unit, "unit");
}
DWORD GetResearched(LPJASS j) {
    return jass_pushinteger(j, jass_getcontext(j)->eventValue);
}
DWORD GetTrainedUnitType(LPJASS j) {
    LPCJASSCONTEXT context = jass_getcontext(j);
    LPEDICT trained = context->source ? context->source : context->unit;
    return jass_pushinteger(j, trained ? (LONG)trained->class_id : 0);
}
DWORD GetTrainedUnit(LPJASS j) {
    LPCJASSCONTEXT context = jass_getcontext(j);
    LPEDICT trained = context->source ? context->source : context->unit;
    return jass_pushlighthandle(j, trained, "unit");
}
DWORD GetDetectedUnit(LPJASS j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->unit, "unit");
}
DWORD GetSummoningUnit(LPJASS j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->unit, "unit");
}
DWORD GetSummonedUnit(LPJASS j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->source, "unit");
}
DWORD GetTransportUnit(LPJASS j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->unit, "unit");
}
DWORD GetLoadedUnit(LPJASS j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->unit, "unit");
}
/* GetChangingUnit/GetChangingUnitPrevOwner read ownership-change event context.
 * eventValue carries prev_owner+1; zero means no change context. */
DWORD GetChangingUnit(LPJASS j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->unit, "unit");
}
DWORD GetChangingUnitPrevOwner(LPJASS j) {
    LONG val = jass_getcontext(j)->eventValue;
    LPPLAYER player = val > 0 ? G_GetPlayerByNumber((DWORD)(val - 1)) : NULL;
    return jass_pushlighthandle(j, player, "player");
}
DWORD GetManipulatingUnit(LPJASS j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->unit, "unit");
}
DWORD GetOrderedUnit(LPJASS j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->unit, "unit");
}
DWORD GetIssuedOrderId(LPJASS j) {
    return jass_pushinteger(j, G_GetIssuedOrderId(jass_getcontext(j)->unit));
}
DWORD GetOrderPointX(LPJASS j) {
    VECTOR2 point = { 0.0f, 0.0f };
    G_GetIssuedOrderPoint(jass_getcontext(j)->unit, &point);
    return jass_pushnumber(j, point.x);
}
DWORD GetOrderPointY(LPJASS j) {
    VECTOR2 point = { 0.0f, 0.0f };
    G_GetIssuedOrderPoint(jass_getcontext(j)->unit, &point);
    return jass_pushnumber(j, point.y);
}
DWORD GetOrderPointLoc(LPJASS j) {
    VECTOR2 point = { 0.0f, 0.0f };
    API_ALLOC(VECTOR2, location);
    G_GetIssuedOrderPoint(jass_getcontext(j)->unit, &point);
    *location = point;
    return 1;
}
DWORD GetOrderTarget(LPJASS j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->source, "widget");
}
DWORD GetOrderTargetDestructable(LPJASS j) {
    LPEDICT target = jass_getcontext(j)->source;
    return target && G_IsDestructable(target) ?
        jass_pushlighthandle(j, target, "destructable") : jass_pushnullhandle(j, "destructable");
}
DWORD GetOrderTargetUnit(LPJASS j) {
    LPEDICT target = jass_getcontext(j)->source;
    return target && (target->svflags & SVF_MONSTER) ?
        jass_pushlighthandle(j, target, "unit") : jass_pushnullhandle(j, "unit");
}

DWORD GetSpellAbilityUnit(LPJASS j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->unit, "unit");
}
DWORD GetSpellAbilityId(LPJASS j) {
    return jass_pushinteger(j, jass_getcontext(j)->eventValue);
}
DWORD GetSpellTargetUnit(LPJASS j) {
    LPEDICT target = jass_getcontext(j)->source;
    return target && (target->svflags & SVF_MONSTER) ?
        jass_pushlighthandle(j, target, "unit") : jass_pushnullhandle(j, "unit");
}
DWORD GetSpellTargetDestructable(LPJASS j) {
    LPEDICT target = jass_getcontext(j)->source;
    return target && G_IsDestructable(target) ?
        jass_pushlighthandle(j, target, "destructable") : jass_pushnullhandle(j, "destructable");
}
DWORD GetSpellTargetItem(LPJASS j) {
    LPEDICT target = jass_getcontext(j)->source;
    return target && G_IsItem(target) ?
        jass_pushlighthandle(j, target, "item") : jass_pushnullhandle(j, "item");
}
DWORD GetSpellTargetX(LPJASS j) {
    JASSCONTEXT const *ctx = jass_getcontext(j);
    return jass_pushnumber(j, ctx->hasPoint ? ctx->point.x : 0.0f);
}
DWORD GetSpellTargetY(LPJASS j) {
    JASSCONTEXT const *ctx = jass_getcontext(j);
    return jass_pushnumber(j, ctx->hasPoint ? ctx->point.y : 0.0f);
}
DWORD GetSpellTargetLoc(LPJASS j) {
    JASSCONTEXT const *ctx = jass_getcontext(j);
    API_ALLOC(VECTOR2, location);
    *location = ctx->hasPoint ? ctx->point : (VECTOR2){ 0.0f, 0.0f };
    return 1;
}
DWORD GetEventPlayerState(LPJASS j) {
    return jass_pushnullhandle(j, "playerstate");
}
DWORD GetEventPlayerChatString(LPJASS j) {
    return jass_pushstring(j, 0);
}
DWORD GetEventPlayerChatStringMatched(LPJASS j) {
    return jass_pushstring(j, 0);
}
DWORD GetEventUnitState(LPJASS j) {
    return jass_pushnullhandle(j, "unitstate");
}
DWORD GetEventDamage(LPJASS j) {
    return jass_pushnumber(j, (FLOAT)jass_getcontext(j)->eventValue);
}
DWORD GetEventDamageSource(LPJASS j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->source, "unit");
}
DWORD GetObjectName(LPJASS j) {
    LONG objectId = jass_checkinteger(j, 1);
    return jass_pushstring(j, G_ObjectName((DWORD)objectId));
}
DWORD GetSellingUnit(LPJASS j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->unit, "unit");
}
DWORD GetBuyingUnit(LPJASS j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->source, "unit");
}
DWORD GetSoldUnit(LPJASS j) {
    return jass_pushlighthandle(j, eventsoldunit, "unit");
}
DWORD GetSoldItem(LPJASS j) {
    return jass_pushlighthandle(j, eventsolditem, "item");
}
DWORD StringLength(LPJASS j) {
    LPCSTR s = jass_checkstring(j, 1);
    return jass_pushinteger(j, s ? (LONG)strlen(s) : 0);
}
DWORD StringCase(LPJASS j) {
    LPCSTR source = jass_checkstring(j, 1);
    BOOL upper = jass_checkboolean(j, 2);
    char buf[1024];
    DWORD i, n;
    if (!source) return jass_pushstring(j, "");
    n = (DWORD)strlen(source);
    if (n >= sizeof(buf)) n = sizeof(buf) - 1;
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)source[i];
        buf[i] = (char)(upper ? toupper(c) : tolower(c));
    }
    buf[n] = '\0';
    return jass_pushstring(j, buf);
}
DWORD GetEventDetectingPlayer(LPJASS j) {
    return jass_pushnullhandle(j, "player");
}
DWORD GetEventTargetUnit(LPJASS j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->unit, "unit");
}
DWORD GetWidgetLife(LPJASS j) {
    LPEDICT whichWidget = jass_checkhandle(j, 1, "widget");
    return jass_pushnumber(j, whichWidget ? whichWidget->health.value : 0);
}
DWORD SetWidgetLife(LPJASS j) {
    LPEDICT whichWidget = jass_checkhandle(j, 1, "widget");
    FLOAT newLife = jass_checknumber(j, 2);
    if (whichWidget) {
        BOOL const was_dead = M_IsDead(whichWidget);
        G_SetHealth(whichWidget, newLife);
        if ((whichWidget->s.flags & EF_FOW_BLOCKER) && was_dead != M_IsDead(whichWidget)) G_FowMarkBlockersDirty();
    }
    return 0;
}
DWORD GetWidgetX(LPJASS j) {
    LPEDICT whichWidget = jass_checkhandle(j, 1, "widget");
    return jass_pushnumber(j, whichWidget ? whichWidget->s.origin.x : 0);
}
DWORD GetWidgetY(LPJASS j) {
    LPEDICT whichWidget = jass_checkhandle(j, 1, "widget");
    return jass_pushnumber(j, whichWidget ? whichWidget->s.origin.y : 0);
}
DWORD GetFoodMade(LPJASS j) {
    LONG unitId = jass_checkinteger(j, 1);
    UnitBalance_t const *balance = G_UnitBalance((DWORD)unitId);
    return jass_pushinteger(j, balance ? balance->foodMade : 0);
}
DWORD GetFoodUsed(LPJASS j) {
    LONG unitId = jass_checkinteger(j, 1);
    UnitBalance_t const *balance = G_UnitBalance((DWORD)unitId);
    return jass_pushinteger(j, balance ? balance->foodUsed : 0);
}

DWORD EndGame(LPJASS j) {
    BOOL doScoreScreen = jass_checkboolean(j, 1);
    G_RequestEndGame(doScoreScreen);
    return 0;
}
DWORD ChangeLevel(LPJASS j) {
    LPCSTR newLevel = jass_checkstring(j, 1);
    BOOL doScoreScreen = jass_checkboolean(j, 2);
    G_RequestChangeLevel(newLevel, doScoreScreen);
    return 0;
}
DWORD RestartGame(LPJASS j) {
    BOOL doScoreScreen = jass_checkboolean(j, 1);
    G_RequestRestartGame(doScoreScreen);
    return 0;
}
DWORD ReloadGame(LPJASS j) {
    return 0;
}
DWORD DoNotSaveReplay(LPJASS j) { /* TODO: replay recording not yet implemented */ return 0; }
DWORD SaveGame(LPJASS j) {
    LPCSTR name = jass_checkstring(j, 1);
    PATHSTR path;

    if (!name || !*name || strchr(name, '/') || strchr(name, '\\') || !gi.SavePath) {
        fprintf(stderr, "WC3 SaveGame: invalid save name\n");
        return 0;
    }
    gi.SavePath(name, path, sizeof(path));
    if (!WriteGame(path)) fprintf(stderr, "WC3 SaveGame: could not write %s\n", path);
    return 0;
}
DWORD LoadGame(LPJASS j) {
    LPCSTR name = jass_checkstring(j, 1);
    PATHSTR path;

    if (!name || !*name || strchr(name, '/') || strchr(name, '\\') || !gi.SavePath) {
        fprintf(stderr, "WC3 LoadGame: invalid save name\n");
        return 0;
    }
    gi.SavePath(name, path, sizeof(path));
    if (!ReadGame(path)) fprintf(stderr, "WC3 LoadGame: could not read %s\n", path);
    return 0;
}
DWORD SetCampaignMenuRace(LPJASS j) {
    //HANDLE r = jass_checkhandle(j, 1, "race");
    return 0;
}
static LONG ally_color_filter_state;
static BOOL creep_camp_filter_state = true;
DWORD GetAllyColorFilterState(LPJASS j) { (void)j; return jass_pushinteger(j, ally_color_filter_state); }
DWORD SetAllyColorFilterState(LPJASS j) { ally_color_filter_state = jass_checkinteger(j, 1); return 0; }
DWORD GetCreepCampFilterState(LPJASS j) { (void)j; return jass_pushboolean(j, creep_camp_filter_state); }
DWORD SetCreepCampFilterState(LPJASS j) { creep_camp_filter_state = jass_checkboolean(j, 1); return 0; }
DWORD EnableMinimapFilterButtons(LPJASS j) { (void)jass_checkboolean(j, 1); (void)jass_checkboolean(j, 2); return 0; }
DWORD EnableDragSelect(LPJASS j) { (void)jass_checkboolean(j, 1); (void)jass_checkboolean(j, 2); return 0; }
DWORD EnablePreSelect(LPJASS j) { (void)jass_checkboolean(j, 1); (void)jass_checkboolean(j, 2); return 0; }
DWORD EnableSelect(LPJASS j) { (void)jass_checkboolean(j, 1); (void)jass_checkboolean(j, 2); return 0; }
DWORD SetReservedLocalHeroButtons(LPJASS j) { (void)jass_checkinteger(j, 1); return 0; }
DWORD CopySaveGame(LPJASS j) {
    (void)jass_checkstring(j, 1);
    (void)jass_checkstring(j, 2);
    return jass_pushboolean(j, false); /* PMV Lua bridge; no save-file copy */
}
DWORD GetTerrainType(LPJASS j) { (void)jass_checknumber(j, 1); (void)jass_checknumber(j, 2); return jass_pushinteger(j, 0); }
DWORD GetTerrainVariance(LPJASS j) { (void)jass_checknumber(j, 1); (void)jass_checknumber(j, 2); return jass_pushinteger(j, 0); }
DWORD IsPointBlighted(LPJASS j) { (void)jass_checknumber(j, 1); (void)jass_checknumber(j, 2); return jass_pushboolean(j, false); }
DWORD IsTerrainPathable(LPJASS j) {
    (void)jass_checknumber(j, 1); (void)jass_checknumber(j, 2); (void)jass_checkhandle(j, 3, "pathingtype");
    return jass_pushboolean(j, true);
}
DWORD SetTerrainPathable(LPJASS j) {
    (void)jass_checknumber(j, 1); (void)jass_checknumber(j, 2); (void)jass_checkhandle(j, 3, "pathingtype");
    (void)jass_checkboolean(j, 4);
    return 0;
}
DWORD TerrainDeformRipple(LPJASS j) {
    (void)jass_checknumber(j, 1); (void)jass_checknumber(j, 2); (void)jass_checknumber(j, 3); (void)jass_checknumber(j, 4);
    (void)jass_checkinteger(j, 5); (void)jass_checkinteger(j, 6);
    (void)jass_checknumber(j, 7); (void)jass_checknumber(j, 8); (void)jass_checknumber(j, 9);
    (void)jass_checkboolean(j, 10);
    return jass_pushnullhandle(j, "terraindeformation");
}
DWORD SetCampaignMenuRaceEx(LPJASS j) {
    //LONG campaignIndex = jass_checkinteger(j, 1); /* TODO: wire to campaign UI */
    return 0;
}
DWORD ForceCampaignSelectScreen(LPJASS j) {
    G_RequestCampaignSelect();
    return 0;
}
DWORD SyncSelections(LPJASS j) {
    return 0;
}
DWORD SetFloatGameState(LPJASS j) {
    LPDWORD whichFloatGameState = jass_checkhandle(j, 1, "fgamestate");
    FLOAT value = jass_checknumber(j, 2);
    if (whichFloatGameState && *whichFloatGameState == WC3_GAME_STATE_TIME_OF_DAY)
        G_SetTimeOfDay(value);
    return 0;
}
DWORD GetFloatGameState(LPJASS j) {
    LPDWORD whichFloatGameState = jass_checkhandle(j, 1, "fgamestate");
    if (whichFloatGameState && *whichFloatGameState == WC3_GAME_STATE_TIME_OF_DAY)
        return jass_pushnumber(j, G_GetTimeOfDay());
    return jass_pushnumber(j, 0);
}
DWORD SetIntegerGameState(LPJASS j) {
    //HANDLE whichIntegerGameState = jass_checkhandle(j, 1, "igamestate");
    //LONG value = jass_checkinteger(j, 2);
    return 0;
}
DWORD GetIntegerGameState(LPJASS j) {
    //HANDLE whichIntegerGameState = jass_checkhandle(j, 1, "igamestate");
    return jass_pushinteger(j, 0);
}
DWORD SetTutorialCleared(LPJASS j) {
    BOOL cleared = jass_checkboolean(j, 1);
    G_CampaignProgressSetTutorialCleared(cleared);
    return 0;
}
DWORD SetMissionAvailable(LPJASS j) {
    LONG campaignNumber = jass_checkinteger(j, 1);
    LONG missionNumber = jass_checkinteger(j, 2);
    BOOL available = jass_checkboolean(j, 3);
    G_CampaignProgressSetMissionAvailable(campaignNumber, missionNumber, available);
    return 0;
}
DWORD SetCampaignAvailable(LPJASS j) {
    LONG campaignNumber = jass_checkinteger(j, 1);
    BOOL available = jass_checkboolean(j, 2);
    G_CampaignProgressSetCampaignAvailable(campaignNumber, available);
    return 0;
}
DWORD SetOpCinematicAvailable(LPJASS j) {
    //LONG campaignNumber = jass_checkinteger(j, 1);
    //BOOL available = jass_checkboolean(j, 2);
    return 0;
}
DWORD SetEdCinematicAvailable(LPJASS j) {
    //LONG campaignNumber = jass_checkinteger(j, 1);
    //BOOL available = jass_checkboolean(j, 2);
    return 0;
}
DWORD GetDefaultDifficulty(LPJASS j) {
    return JassPushGameDifficultyHandle(j, level.setup.default_difficulty);
}
DWORD SetDefaultDifficulty(LPJASS j) {
    DWORD *difficulty = jass_checkhandle(j, 1, "gamedifficulty");
    if (difficulty) level.setup.default_difficulty = MIN(*difficulty, 3);
    return 0;
}
DWORD DialogCreate(LPJASS j) {
    return jass_pushnullhandle(j, "dialog");
}
DWORD DialogDestroy(LPJASS j) {
    //HANDLE whichDialog = jass_checkhandle(j, 1, "dialog");
    return 0;
}
DWORD DialogSetAsync(LPJASS j) {
    //HANDLE whichDialog = jass_checkhandle(j, 1, "dialog");
    return 0;
}
DWORD DialogClear(LPJASS j) {
    //HANDLE whichDialog = jass_checkhandle(j, 1, "dialog");
    return 0;
}
DWORD DialogSetMessage(LPJASS j) {
    //HANDLE whichDialog = jass_checkhandle(j, 1, "dialog");
    //LPCSTR messageText = jass_checkstring(j, 2);
    return 0;
}
DWORD DialogAddButton(LPJASS j) {
    //HANDLE whichDialog = jass_checkhandle(j, 1, "dialog");
    //LPCSTR buttonText = jass_checkstring(j, 2);
    //LONG hotkey = jass_checkinteger(j, 3);
    return jass_pushnullhandle(j, "button");
}
DWORD DialogDisplay(LPJASS j) {
    //LPPLAYER whichPlayer = jass_checkhandle(j, 1, "player");
    //HANDLE whichDialog = jass_checkhandle(j, 2, "dialog");
    //BOOL flag = jass_checkboolean(j, 3);
    return 0;
}
DWORD InitGameCache(LPJASS j) {
    API_ALLOC(ggamecache_t, gamecache);
    G_GameCacheInit(gamecache, jass_checkstring(j, 1));
    return 1;
}
DWORD SaveGameCache(LPJASS j) {
    ggamecache_t *cache = jass_checkhandle(j, 1, "gamecache");
    return jass_pushboolean(j, G_GameCacheSave(cache));
}
DWORD StoreInteger(LPJASS j) {
    ggamecache_t *cache = jass_checkhandle(j, 1, "gamecache");
    G_GameCacheStoreInteger(cache, jass_checkstring(j, 2), jass_checkstring(j, 3), jass_checkinteger(j, 4));
    return 0;
}
DWORD StoreReal(LPJASS j) {
    ggamecache_t *cache = jass_checkhandle(j, 1, "gamecache");
    G_GameCacheStoreReal(cache, jass_checkstring(j, 2), jass_checkstring(j, 3), jass_checknumber(j, 4));
    return 0;
}
DWORD StoreBoolean(LPJASS j) {
    ggamecache_t *cache = jass_checkhandle(j, 1, "gamecache");
    G_GameCacheStoreBoolean(cache, jass_checkstring(j, 2), jass_checkstring(j, 3), jass_checkboolean(j, 4));
    return 0;
}
DWORD StoreUnit(LPJASS j) {
    ggamecache_t *cache = jass_checkhandle(j, 1, "gamecache");
    LPEDICT unit = jass_checkhandle(j, 4, "unit");
    return jass_pushboolean(j, G_GameCacheStoreUnit(cache, jass_checkstring(j, 2), jass_checkstring(j, 3), unit));
}
DWORD StoreString(LPJASS j) {
    ggamecache_t *cache = jass_checkhandle(j, 1, "gamecache");
    return jass_pushboolean(j, G_GameCacheStoreString(cache, jass_checkstring(j, 2), jass_checkstring(j, 3), jass_checkstring(j, 4)));
}
DWORD SyncStoredInteger(LPJASS j) {
    (void)jass_checkhandle(j, 1, "gamecache");
    (void)jass_checkstring(j, 2);
    (void)jass_checkstring(j, 3);
    return 0;
}
DWORD SyncStoredReal(LPJASS j) {
    (void)jass_checkhandle(j, 1, "gamecache");
    (void)jass_checkstring(j, 2);
    (void)jass_checkstring(j, 3);
    return 0;
}
DWORD SyncStoredBoolean(LPJASS j) {
    (void)jass_checkhandle(j, 1, "gamecache");
    (void)jass_checkstring(j, 2);
    (void)jass_checkstring(j, 3);
    return 0;
}
DWORD SyncStoredUnit(LPJASS j) {
    (void)jass_checkhandle(j, 1, "gamecache");
    (void)jass_checkstring(j, 2);
    (void)jass_checkstring(j, 3);
    return 0;
}
DWORD SyncStoredString(LPJASS j) {
    (void)jass_checkhandle(j, 1, "gamecache");
    (void)jass_checkstring(j, 2);
    (void)jass_checkstring(j, 3);
    return 0;
}
DWORD HaveStoredInteger(LPJASS j) {
    ggamecache_t *cache = jass_checkhandle(j, 1, "gamecache");
    return jass_pushboolean(j, G_GameCacheHave(cache, jass_checkstring(j, 2), jass_checkstring(j, 3), GAMECACHE_INTEGER));
}
DWORD HaveStoredReal(LPJASS j) {
    ggamecache_t *cache = jass_checkhandle(j, 1, "gamecache");
    return jass_pushboolean(j, G_GameCacheHave(cache, jass_checkstring(j, 2), jass_checkstring(j, 3), GAMECACHE_REAL));
}
DWORD HaveStoredBoolean(LPJASS j) {
    ggamecache_t *cache = jass_checkhandle(j, 1, "gamecache");
    return jass_pushboolean(j, G_GameCacheHave(cache, jass_checkstring(j, 2), jass_checkstring(j, 3), GAMECACHE_BOOLEAN));
}
DWORD HaveStoredUnit(LPJASS j) {
    ggamecache_t *cache = jass_checkhandle(j, 1, "gamecache");
    return jass_pushboolean(j, G_GameCacheHave(cache, jass_checkstring(j, 2), jass_checkstring(j, 3), GAMECACHE_UNIT));
}
DWORD HaveStoredString(LPJASS j) {
    ggamecache_t *cache = jass_checkhandle(j, 1, "gamecache");
    return jass_pushboolean(j, G_GameCacheHave(cache, jass_checkstring(j, 2), jass_checkstring(j, 3), GAMECACHE_STRING));
}
DWORD FlushGameCache(LPJASS j) {
    G_GameCacheFlush(jass_checkhandle(j, 1, "gamecache"));
    return 0;
}
DWORD FlushStoredMission(LPJASS j) {
    G_GameCacheFlushMission(jass_checkhandle(j, 1, "gamecache"), jass_checkstring(j, 2));
    return 0;
}
DWORD FlushStoredInteger(LPJASS j) {
    G_GameCacheFlushEntry(jass_checkhandle(j, 1, "gamecache"), jass_checkstring(j, 2), jass_checkstring(j, 3), GAMECACHE_INTEGER);
    return 0;
}
DWORD FlushStoredReal(LPJASS j) {
    G_GameCacheFlushEntry(jass_checkhandle(j, 1, "gamecache"), jass_checkstring(j, 2), jass_checkstring(j, 3), GAMECACHE_REAL);
    return 0;
}
DWORD FlushStoredBoolean(LPJASS j) {
    G_GameCacheFlushEntry(jass_checkhandle(j, 1, "gamecache"), jass_checkstring(j, 2), jass_checkstring(j, 3), GAMECACHE_BOOLEAN);
    return 0;
}
DWORD FlushStoredUnit(LPJASS j) {
    G_GameCacheFlushEntry(jass_checkhandle(j, 1, "gamecache"), jass_checkstring(j, 2), jass_checkstring(j, 3), GAMECACHE_UNIT);
    return 0;
}
DWORD FlushStoredString(LPJASS j) {
    G_GameCacheFlushEntry(jass_checkhandle(j, 1, "gamecache"), jass_checkstring(j, 2), jass_checkstring(j, 3), GAMECACHE_STRING);
    return 0;
}
DWORD GetStoredInteger(LPJASS j) {
    ggamecache_t *cache = jass_checkhandle(j, 1, "gamecache");
    return jass_pushinteger(j, G_GameCacheGetInteger(cache, jass_checkstring(j, 2), jass_checkstring(j, 3)));
}
DWORD GetStoredReal(LPJASS j) {
    ggamecache_t *cache = jass_checkhandle(j, 1, "gamecache");
    return jass_pushnumber(j, G_GameCacheGetReal(cache, jass_checkstring(j, 2), jass_checkstring(j, 3)));
}
DWORD GetStoredBoolean(LPJASS j) {
    ggamecache_t *cache = jass_checkhandle(j, 1, "gamecache");
    return jass_pushboolean(j, G_GameCacheGetBoolean(cache, jass_checkstring(j, 2), jass_checkstring(j, 3)));
}
DWORD GetStoredString(LPJASS j) {
    ggamecache_t *cache = jass_checkhandle(j, 1, "gamecache");
    return jass_pushstring(j, G_GameCacheGetString(cache, jass_checkstring(j, 2), jass_checkstring(j, 3)));
}
DWORD RestoreUnit(LPJASS j) {
    ggamecache_t *cache = jass_checkhandle(j, 1, "gamecache");
    LPCSTR mission = jass_checkstring(j, 2);
    LPCSTR key = jass_checkstring(j, 3);
    LPPLAYER player = jass_checkhandle(j, 4, "player");
    VECTOR2 location = { jass_checknumber(j, 5), jass_checknumber(j, 6) };
    FLOAT facing = jass_checknumber(j, 7);
    LPEDICT unit;

    if (!cache || !player) return jass_pushnullhandle(j, "unit");
    unit = G_GameCacheRestoreUnit(cache, mission, key, PLAYER_NUM(player), &location, facing);
    return unit ? jass_pushlighthandle(j, unit, "unit") : jass_pushnullhandle(j, "unit");
}
DWORD GetRandomInt(LPJASS j) {
    LONG lowBound = jass_checkinteger(j, 1);
    LONG highBound = jass_checkinteger(j, 2);
    if (lowBound >= highBound) return jass_pushinteger(j, lowBound);
    return jass_pushinteger(j, lowBound + rand() % (highBound - lowBound + 1));
}
DWORD GetRandomReal(LPJASS j) {
    FLOAT lowBound = jass_checknumber(j, 1);
    FLOAT highBound = jass_checknumber(j, 2);
    if (lowBound >= highBound) return jass_pushnumber(j, lowBound);
    FLOAT t = (FLOAT)rand() / (FLOAT)RAND_MAX;
    return jass_pushnumber(j, lowBound + t * (highBound - lowBound));
}
DWORD CreateUnitPool(LPJASS j) {
    return jass_pushnullhandle(j, "unitpool");
}
DWORD DestroyUnitPool(LPJASS j) {
    //HANDLE whichPool = jass_checkhandle(j, 1, "unitpool");
    return 0;
}
DWORD UnitPoolAddUnitType(LPJASS j) {
    //HANDLE whichPool = jass_checkhandle(j, 1, "unitpool");
    //LONG unitId = jass_checkinteger(j, 2);
    //FLOAT weight = jass_checknumber(j, 3);
    return 0;
}
DWORD UnitPoolRemoveUnitType(LPJASS j) {
    //HANDLE whichPool = jass_checkhandle(j, 1, "unitpool");
    //LONG unitId = jass_checkinteger(j, 2);
    return 0;
}
DWORD PlaceRandomUnit(LPJASS j) {
    //HANDLE whichPool = jass_checkhandle(j, 1, "unitpool");
    //LPMAPPLAYER forWhichPlayer = jass_checkhandle(j, 2, "player");
    //FLOAT x = jass_checknumber(j, 3);
    //FLOAT y = jass_checknumber(j, 4);
    //FLOAT facing = jass_checknumber(j, 5);
    return jass_pushnullhandle(j, "unit");
}
DWORD CreateItemPool(LPJASS j) {
    return jass_pushnullhandle(j, "itempool");
}
DWORD DestroyItemPool(LPJASS j) {
    //HANDLE whichItemPool = jass_checkhandle(j, 1, "itempool");
    return 0;
}
DWORD ItemPoolAddItemType(LPJASS j) {
    //HANDLE whichItemPool = jass_checkhandle(j, 1, "itempool");
    //LONG itemId = jass_checkinteger(j, 2);
    //FLOAT weight = jass_checknumber(j, 3);
    return 0;
}
DWORD ItemPoolRemoveItemType(LPJASS j) {
    //HANDLE whichItemPool = jass_checkhandle(j, 1, "itempool");
    //LONG itemId = jass_checkinteger(j, 2);
    return 0;
}
DWORD PlaceRandomItem(LPJASS j) {
    //HANDLE whichItemPool = jass_checkhandle(j, 1, "itempool");
    //FLOAT x = jass_checknumber(j, 2);
    //FLOAT y = jass_checknumber(j, 3);
    return jass_pushnullhandle(j, "item");
}
DWORD ChooseRandomCreep(LPJASS j) {
    //LONG level = jass_checkinteger(j, 1);
    return jass_pushinteger(j, 0);
}
DWORD ChooseRandomNPBuilding(LPJASS j) {
    return jass_pushinteger(j, 0);
}

DWORD SetAllItemTypeSlots(LPJASS j) {
    G_SetAllStockSlots(true, jass_checkinteger(j, 1));
    return 0;
}

DWORD SetAllUnitTypeSlots(LPJASS j) {
    G_SetAllStockSlots(false, jass_checkinteger(j, 1));
    return 0;
}

DWORD SetItemTypeSlots(LPJASS j) {
    G_SetStockSlots(jass_checkhandle(j, 1, "unit"), true, jass_checkinteger(j, 2));
    return 0;
}

DWORD SetUnitTypeSlots(LPJASS j) {
    G_SetStockSlots(jass_checkhandle(j, 1, "unit"), false, jass_checkinteger(j, 2));
    return 0;
}

DWORD AddItemToStock(LPJASS j) {
    G_AddItemStock(jass_checkhandle(j, 1, "unit"), (DWORD)jass_checkinteger(j, 2),
                   jass_checkinteger(j, 3), jass_checkinteger(j, 4));
    return 0;
}

DWORD AddItemToAllStock(LPJASS j) {
    G_AddItemStockAll((DWORD)jass_checkinteger(j, 1), jass_checkinteger(j, 2), jass_checkinteger(j, 3));
    return 0;
}

DWORD RemoveItemFromStock(LPJASS j) {
    G_RemoveItemStock(jass_checkhandle(j, 1, "unit"), (DWORD)jass_checkinteger(j, 2));
    return 0;
}

DWORD RemoveItemFromAllStock(LPJASS j) {
    G_RemoveItemStockAll((DWORD)jass_checkinteger(j, 1));
    return 0;
}

DWORD AddUnitToStock(LPJASS j) {
    G_AddUnitStock(jass_checkhandle(j, 1, "unit"), (DWORD)jass_checkinteger(j, 2),
                   jass_checkinteger(j, 3), jass_checkinteger(j, 4));
    return 0;
}

DWORD AddUnitToAllStock(LPJASS j) {
    G_AddUnitStockAll((DWORD)jass_checkinteger(j, 1), jass_checkinteger(j, 2), jass_checkinteger(j, 3));
    return 0;
}

DWORD RemoveUnitFromStock(LPJASS j) {
    G_RemoveUnitStock(jass_checkhandle(j, 1, "unit"), (DWORD)jass_checkinteger(j, 2));
    return 0;
}

DWORD RemoveUnitFromAllStock(LPJASS j) {
    G_RemoveUnitStockAll((DWORD)jass_checkinteger(j, 1));
    return 0;
}

static BOOL JassRandomItemEligible(ItemData_t const *row, LONG level, DWORD type) {
    if (!row->pickRandom || row->level != level) return false;
    return type == 8 || G_ItemTypeFromClass(row->itemClass) == type;
}

static DWORD JassChooseRandomItem(LONG requested_level, DWORD requested_type) {
    DWORD item_count, count = 0;
    ItemData_t const *items = G_ItemDataRows(&item_count);
    DWORD selected_index;

    if (!items) {
        fprintf(stderr, "JassChooseRandomItem: Units\\ItemData.slk is not loaded\n");
        return 0;
    }

    /*
     * First pass counts candidates. This deliberately consumes no random
     * values so SetRandomSeed() remains predictable.
     */
    FOR_LOOP(i, item_count)
        if (JassRandomItemEligible(items + i, requested_level, requested_type)) count++;

    if (!count) return 0;

    selected_index = (DWORD)(rand() % count);

    /*
     * Second pass returns the selected candidate.
     */
    FOR_LOOP(i, item_count) {
        if (!JassRandomItemEligible(items + i, requested_level, requested_type)) continue;
        if (selected_index--) continue;
        return items[i].id;
    }

    fprintf(stderr, "JassChooseRandomItem: candidate count changed during selection\n");
    return 0;
}

DWORD ChooseRandomItem(LPJASS j) {
    LONG level = jass_checkinteger(j, 1);
    DWORD item_id = JassChooseRandomItem(level, 8);

    return jass_pushinteger(j, (LONG)item_id);
}

DWORD ChooseRandomItemEx(LPJASS j) {
    DWORD *whichType = jass_checkhandle(j, 1, "itemtype");
    LONG level = jass_checkinteger(j, 2);
    DWORD type = whichType ? *whichType : 8;
    DWORD item_id = JassChooseRandomItem(level, type);

    return jass_pushinteger(j, (LONG)item_id);
}

DWORD SetRandomSeed(LPJASS j) {
    LONG seed = jass_checkinteger(j, 1);
    srand((unsigned int)seed);
    return 0;
}
DWORD SetTerrainFog(LPJASS j) {
    //FLOAT a = jass_checknumber(j, 1);
    //FLOAT b = jass_checknumber(j, 2);
    //FLOAT c = jass_checknumber(j, 3);
    //FLOAT d = jass_checknumber(j, 4);
    //FLOAT e = jass_checknumber(j, 5);
    return 0;
}
DWORD ResetTerrainFog(LPJASS j) {
    G_EnvironmentFogReset();
    return 0;
}
DWORD SetUnitFog(LPJASS j) {
    //FLOAT a = jass_checknumber(j, 1);
    //FLOAT b = jass_checknumber(j, 2);
    //FLOAT c = jass_checknumber(j, 3);
    //FLOAT d = jass_checknumber(j, 4);
    //FLOAT e = jass_checknumber(j, 5);
    return 0;
}
DWORD SetTerrainFogEx(LPJASS j) {
    LONG style = jass_checkinteger(j, 1);
    FLOAT zstart = jass_checknumber(j, 2);
    FLOAT zend = jass_checknumber(j, 3);
    FLOAT density = jass_checknumber(j, 4);
    FLOAT red = jass_checknumber(j, 5);
    FLOAT green = jass_checknumber(j, 6);
    FLOAT blue = jass_checknumber(j, 7);

    G_EnvironmentFogSet(&(wc3EnvironmentFogParams_t){
        .style = style, .start = zstart, .end = zend, .density = density,
        .color = { red, green, blue } });
    return 0;
}
DWORD SetWaterBaseColor(LPJASS j) {
    //LONG red = jass_checkinteger(j, 1);
    //LONG green = jass_checkinteger(j, 2);
    //LONG blue = jass_checkinteger(j, 3);
    //LONG alpha = jass_checkinteger(j, 4);
    return 0;
}
DWORD SetWaterDeforms(LPJASS j) {
    //BOOL val = jass_checkboolean(j, 1);
    return 0;
}
DWORD SetDayNightModels(LPJASS j) {
    LPCSTR terrainDNCFile = jass_checkstring(j, 1);
    LPCSTR unitDNCFile = jass_checkstring(j, 2);
    int terrain_model = 0, unit_model = 0;
    char value[16];

    /* The map script owns the DNC asset choice. Register both models through
     * the ordinary model configstring pool, then publish only their compact
     * indices. The generic client turns those indices back into model handles
     * and the WC3 renderer samples their first animated lights. */
    if (gi.ModelIndex) {
        if (terrainDNCFile && *terrainDNCFile) terrain_model = gi.ModelIndex(terrainDNCFile);
        if (unitDNCFile && *unitDNCFile) unit_model = gi.ModelIndex(unitDNCFile);
    }
    if (gi.configstring) {
        snprintf(value, sizeof(value), "%d", terrain_model);
        gi.configstring(CS_TERRAIN_LIGHT_MODEL, value);
        snprintf(value, sizeof(value), "%d", unit_model);
        gi.configstring(CS_ENTITY_LIGHT_MODEL, value);
    }
    return 0;
}
DWORD SetSkyModel(LPJASS j) {
    LPCSTR skyModelFile = jass_checkstring(j, 1);
    int sky_model = skyModelFile && *skyModelFile ? gi.ModelIndex(skyModelFile) : 0;
    char value[16];
    snprintf(value, sizeof(value), "%d", sky_model);
    gi.configstring(CS_SKY, value);
    return 0;
}
DWORD EnableUserControl(LPJASS j) {
    BOOL b = jass_checkboolean(j, 1);
    /* Fast-forwarding must preserve the script's input lock; early edge scrolling overwrote its final camera snap. */
    if (currentplayer) {
        PLAYER_CLIENT(currentplayer)->no_control = !b;
    }
    return 0;
}
DWORD EnableUserUI(LPJASS j) {
    BOOL enabled = jass_checkboolean(j, 1);
    /* Warcraft keeps this separate from EnableUserControl: it suppresses UI
     * affordances such as hover/tooltips, but does not make world selection or
     * gameplay orders inert.  Keep the state for the client presentation path;
     * command authorization must not key off it. */
    if (currentplayer) PLAYER_CLIENT(currentplayer)->no_ui = !enabled;
    return 0;
}
DWORD SuspendTimeOfDay(LPJASS j) {
    G_SuspendTimeOfDay(jass_checkboolean(j, 1));
    return 0;
}
DWORD SetFalseTimeOfDay(LPJASS j) {
    LONG hour = jass_checkinteger(j, 1);
    LONG minute = jass_checkinteger(j, 2);
    FLOAT duration = jass_checknumber(j, 3);
    G_SetFalseTimeOfDay(hour, minute, duration);
    return 0;
}
DWORD SetTimeOfDayScale(LPJASS j) {
    //FLOAT r = jass_checknumber(j, 1);
    return 0;
}
DWORD GetTimeOfDayScale(LPJASS j) {
    return jass_pushnumber(j, 0);
}
DWORD ShowInterface(LPJASS j) {
    BOOL flag = jass_checkboolean(j, 1);
    FLOAT fadeDuration = jass_checknumber(j, 2);
    LPPLAYER player = currentplayer;
    /* Fast-forwarding compresses time, but the script still owns the cinematic-to-game UI transition. */
    if (player)
        UI_ShowInterface(PLAYER_ENT(player), flag, fadeDuration);
    return 0;
}
DWORD PauseGame(LPJASS j) {
    BOOL flag = jass_checkboolean(j, 1);
    G_SetScriptPaused(flag);
    return 0;
}
DWORD AddIndicator(LPJASS j) {
    LPEDICT whichWidget = jass_checkhandle(j, 1, "widget");
    LONG red = jass_checkinteger(j, 2);
    LONG green = jass_checkinteger(j, 3);
    LONG blue = jass_checkinteger(j, 4);
    LONG alpha = jass_checkinteger(j, 5);
    COLOR32 color = MAKE(COLOR32,
        (BYTE)MAX(0, MIN(255, red)),
        (BYTE)MAX(0, MIN(255, green)),
        (BYTE)MAX(0, MIN(255, blue)),
        (BYTE)MAX(0, MIN(255, alpha)));

    G_SendWidgetIndicator(whichWidget, color, currentplayer);
    return 0;
}
DWORD PingMinimap(LPJASS j) {
    FLOAT x = jass_checknumber(j, 1);
    FLOAT y = jass_checknumber(j, 2);
    FLOAT duration = jass_checknumber(j, 3);
    VECTOR2 position = { x, y };

    if (duration <= 0.0f) return 0;
    if (currentplayer) {
        G_SendMinimapPing(PLAYER_CLIENT(currentplayer), &position, duration, COLOR32_WHITE, 0);
    } else {
        FOR_LOOP(i, game.max_clients)
            G_SendMinimapPing(&game.clients[i], &position, duration, COLOR32_WHITE, 0);
    }
    return 0;
}
DWORD PingMinimapEx(LPJASS j) {
    FLOAT x = jass_checknumber(j, 1);
    FLOAT y = jass_checknumber(j, 2);
    FLOAT duration = jass_checknumber(j, 3);
    LONG red = jass_checkinteger(j, 4);
    LONG green = jass_checkinteger(j, 5);
    LONG blue = jass_checkinteger(j, 6);
    BOOL extraEffects = jass_checkboolean(j, 7);
    VECTOR2 position = { x, y };
    COLOR32 color = MAKE(COLOR32,
        (BYTE)MAX(0, MIN(255, red)),
        (BYTE)MAX(0, MIN(255, green)),
        (BYTE)MAX(0, MIN(255, blue)), 255);

    if (duration <= 0.0f) return 0;
    if (currentplayer) {
        G_SendMinimapPing(PLAYER_CLIENT(currentplayer), &position, duration, color,
                          extraEffects ? MINIMAP_PING_EXTRA_EFFECTS : 0);
    } else {
        FOR_LOOP(i, game.max_clients)
            G_SendMinimapPing(&game.clients[i], &position, duration, color,
                              extraEffects ? MINIMAP_PING_EXTRA_EFFECTS : 0);
    }
    return 0;
}
DWORD SetAltMinimapIcon(LPJASS j) {
    //LPCSTR iconPath = jass_checkstring(j, 1); /* TODO: minimap icon override not yet implemented */
    return 0;
}
DWORD EnableOcclusion(LPJASS j) {
    //BOOL flag = jass_checkboolean(j, 1);
    return 0;
}
DWORD SetIntroShotText(LPJASS j) {
    //LPCSTR introText = jass_checkstring(j, 1);
    return 0;
}
DWORD SetIntroShotModel(LPJASS j) {
    //LPCSTR introModelPath = jass_checkstring(j, 1);
    return 0;
}
DWORD EnableWorldFogBoundary(LPJASS j) {
    //BOOL b = jass_checkboolean(j, 1);
    return 0;
}
DWORD PlayCinematic(LPJASS j) {
    LPCSTR movieName = jass_checkstring(j, 1);
    PATHSTR path;

    if (!movieName || !*movieName) return 0;
    /* Retail campaign scripts pass logical names such as HumanOp/HumanEd.
     * Classic stores the pre-rendered AVI payloads under Movies\*.mpq. */
    snprintf(path, sizeof(path), "Movies\\%.*s.mpq",
             (int)(sizeof(path) - sizeof("Movies\\.mpq")),
             movieName);
    gi.QueueMovie(path);
    return 0;
}
DWORD ForceUIKey(LPJASS j) {
    //LPCSTR key = jass_checkstring(j, 1);
    return 0;
}
DWORD ForceUICancel(LPJASS j) {
    return 0;
}
DWORD DisplayLoadDialog(LPJASS j) {
    G_RequestLoadGameMenu();
    return 0;
}
DWORD CreateTrackable(LPJASS j) {
    //LPCSTR trackableModelPath = jass_checkstring(j, 1);
    //FLOAT x = jass_checknumber(j, 2);
    //FLOAT y = jass_checknumber(j, 3);
    //FLOAT facing = jass_checknumber(j, 4);
    return jass_pushnullhandle(j, "trackable");
}
DWORD CreateTimerDialog(LPJASS j) {
    LPGTIMER timer = jass_checkhandle(j, 1, "timer");
    LPTIMERDIALOG dialog = G_AllocTimerDialog(timer);
    if (!dialog) {
        jass_rterror(j, "CreateTimerDialog: timer-dialog registry is full");
        return jass_pushnullhandle(j, "timerdialog");
    }
    return jass_pushlighthandle(j, dialog, "timerdialog");
}
DWORD DestroyTimerDialog(LPJASS j) {
    G_FreeTimerDialog(jass_checkhandle(j, 1, "timerdialog"));
    return 0;
}
DWORD TimerDialogSetTitle(LPJASS j) {
    LPTIMERDIALOG dialog = jass_checkhandle(j, 1, "timerdialog");
    LPCSTR title = jass_checkstring(j, 2);
    if (!dialog || !dialog->inuse) return 0;
    strlcpy(dialog->title, G_LevelString(title ? title : ""), sizeof(dialog->title));
    dialog->title_set = true;
    G_MarkTimerDialogDirty(dialog);
    return 0;
}
DWORD TimerDialogSetTitleColor(LPJASS j) {
    LPTIMERDIALOG dialog = jass_checkhandle(j, 1, "timerdialog");
    LONG red = jass_checkinteger(j, 2);
    LONG green = jass_checkinteger(j, 3);
    LONG blue = jass_checkinteger(j, 4);
    LONG alpha = jass_checkinteger(j, 5);
    if (!dialog || !dialog->inuse) return 0;
    dialog->title_color = MAKE(COLOR32,
        (BYTE)MAX(0, MIN(255, red)), (BYTE)MAX(0, MIN(255, green)),
        (BYTE)MAX(0, MIN(255, blue)), (BYTE)MAX(0, MIN(255, alpha)));
    dialog->title_color_set = true;
    G_MarkTimerDialogDirty(dialog);
    return 0;
}
DWORD TimerDialogSetTimeColor(LPJASS j) {
    LPTIMERDIALOG dialog = jass_checkhandle(j, 1, "timerdialog");
    LONG red = jass_checkinteger(j, 2);
    LONG green = jass_checkinteger(j, 3);
    LONG blue = jass_checkinteger(j, 4);
    LONG alpha = jass_checkinteger(j, 5);
    if (!dialog || !dialog->inuse) return 0;
    dialog->time_color = MAKE(COLOR32,
        (BYTE)MAX(0, MIN(255, red)), (BYTE)MAX(0, MIN(255, green)),
        (BYTE)MAX(0, MIN(255, blue)), (BYTE)MAX(0, MIN(255, alpha)));
    dialog->time_color_set = true;
    G_MarkTimerDialogDirty(dialog);
    return 0;
}
DWORD TimerDialogSetSpeed(LPJASS j) {
    /* Deliberately left unsupported until retail display-rate semantics are
     * pinned down.  Do not alter the authoritative gameplay timer here. */
    (void)jass_checkhandle(j, 1, "timerdialog");
    (void)jass_checknumber(j, 2);
    return 0;
}
DWORD TimerDialogDisplay(LPJASS j) {
    LPTIMERDIALOG dialog = jass_checkhandle(j, 1, "timerdialog");
    BOOL display = jass_checkboolean(j, 2);
    G_SetTimerDialogVisible(dialog, currentplayer, display);
    return 0;
}
DWORD IsTimerDialogDisplayed(LPJASS j) {
    LPTIMERDIALOG dialog = jass_checkhandle(j, 1, "timerdialog");
    return jass_pushboolean(j, G_IsTimerDialogVisible(dialog, currentplayer));
}
DWORD SetCinematicScene(LPJASS j) {
    LONG portraitUnitId = jass_checkinteger(j, 1);
    DWORD *color = jass_checkhandle(j, 2, "playercolor");
    LPCSTR speakerTitle = jass_checkstring(j, 3);
    LPCSTR text = jass_checkstring(j, 4);
    FLOAT sceneDuration = jass_checknumber(j, 5);
    FLOAT voiceoverDuration = jass_checknumber(j, 6);
    if (TutorialTextDebugEnabledMisc()) {
        LONG trigger_ordinal;
        LPCSTR caller;
        LPCSTR resolved_speaker = G_LevelString(speakerTitle);
        LPCSTR resolved_text = G_LevelString(text);
        TutorialTextDebugContextMisc(j, &trigger_ordinal, &caller);
        fprintf(stderr,
                "WC3_TUTORIAL_TEXT native=SetCinematicScene trigger=%ld caller=\"%s\" player=%d portrait=%.4s scene=%.3f voice=%.3f speaker_raw=\"%s\" speaker=\"%s\" text_raw=\"%s\" text=\"%s\"\n",
                (long)trigger_ordinal, caller ? caller : "(native/root)",
                currentplayer ? (int)PLAYER_NUM(currentplayer) : -1,
                portraitUnitId ? (LPCSTR)&portraitUnitId : "----", sceneDuration, voiceoverDuration,
                speakerTitle ? speakerTitle : "", resolved_speaker ? resolved_speaker : "",
                text ? text : "", resolved_text ? resolved_text : "");
    }
    if (G_SkipCutscene()) return 0;
    if (currentplayer) {
        LPGAMECLIENT gc = PLAYER_CLIENT(currentplayer);
        DWORD now = G_Time();
        G_SetPlayerText(gc, PLAYERTEXT_SPEAKER, G_LevelString(speakerTitle));
        G_SetPlayerText(gc, PLAYERTEXT_DIALOGUE, G_LevelString(text));
        /* Only gameplay transmissions are tutorial prompts; cutscene dialogue
         * belongs to the cinematic presentation and must not enter F12 history. */
        if (gc && gc->ps.client_ui_state == CLIENT_UI_GAME)
            UI_RecordTransmissionMessage(PLAYER_ENT(currentplayer));
        currentplayer->cinematic_portrait = 0;
        /* The renderer currently owns 16 replaceable team-color textures.
         * Keep unsupported extended player colors deterministic instead of
         * allowing the renderer's bit mask to wrap them onto another color. */
        currentplayer->stats[UI_PLAYERSTAT_CINEMATIC_PORTRAIT_COLOR] =
            color && *color < MAX_PLAYERS ? *color : 0;
        if (portraitUnitId) {
            LPCSTR model = G_UnitUI((DWORD)portraitUnitId)->modelFile;
            if (model && *model) {
                PATHSTR mf;
                G_NormalizeModelFilename(model, mf, sizeof(mf));
                currentplayer->cinematic_portrait = G_RegisterModel(mf);
            }
        }
        if (gc) {
            gc->cinematic_end_time = sceneDuration > 0 ? now + (DWORD)(sceneDuration * 1000.0f) : 0;
            gc->cinematic_voice_end_time = voiceoverDuration > 0 ? now + (DWORD)(voiceoverDuration * 1000.0f) : 0;
        }
        UI_InvalidateDialoguePresentation(PLAYER_ENT(currentplayer));
    }
    return 0;
}
DWORD EndCinematicScene(LPJASS j) {
    if (TutorialTextDebugEnabledMisc()) {
        LONG trigger_ordinal;
        LPCSTR caller;
        TutorialTextDebugContextMisc(j, &trigger_ordinal, &caller);
        fprintf(stderr,
                "WC3_TUTORIAL_TEXT native=EndCinematicScene trigger=%ld caller=\"%s\" player=%d\n",
                (long)trigger_ordinal, caller ? caller : "(native/root)",
                currentplayer ? (int)PLAYER_NUM(currentplayer) : -1);
    }
    if (currentplayer) {
        LPGAMECLIENT gc = PLAYER_CLIENT(currentplayer);
        G_SetPlayerText(gc, PLAYERTEXT_SPEAKER, "");
        G_SetPlayerText(gc, PLAYERTEXT_DIALOGUE, "");
        currentplayer->cinematic_portrait = 0;
        currentplayer->stats[UI_PLAYERSTAT_CINEMATIC_PORTRAIT_COLOR] = 0;
        if (gc) {
            gc->cinematic_end_time = 0;
            gc->cinematic_voice_end_time = 0;
        }
        UI_InvalidateDialoguePresentation(PLAYER_ENT(currentplayer));
    }
    return 0;
}
DWORD ForceCinematicSubtitles(LPJASS j) {
    /* Current Warsmash always renders transmission subtitles even when this
     * override is false. Consume the native so campaign scripts remain valid
     * without pretending OpenRealm has a separate subtitle preference yet. */
    (void)jass_checkboolean(j, 1);
    return 0;
}
DWORD NewSoundEnvironment(LPJASS j) {
    //LPCSTR environmentName = jass_checkstring(j, 1);
    return 0;
}
DWORD SetDoodadAnimation(LPJASS j) {
    FLOAT x = jass_checknumber(j, 1);
    FLOAT y = jass_checknumber(j, 2);
    FLOAT radius = jass_checknumber(j, 3);
    LONG doodadID = jass_checkinteger(j, 4);
    BOOL nearestOnly = jass_checkboolean(j, 5);
    LPCSTR animName = jass_checkstring(j, 6);
    BOOL animRandom = jass_checkboolean(j, 7);
    doodadAnimationRadiusParams_t const params = {
        .x = x, .y = y, .radius = radius, .doodad_id = (DWORD)doodadID,
        .nearest_only = nearestOnly, .anim_name = animName, .random_animation = animRandom
    };

    G_SetDoodadAnimationRadius(&params);
    return 0;
}
DWORD SetDoodadAnimationRect(LPJASS j) {
    LPCBOX2 r = jass_checkhandle(j, 1, "rect");
    LONG doodadID = jass_checkinteger(j, 2);
    LPCSTR animName = jass_checkstring(j, 3);
    BOOL animRandom = jass_checkboolean(j, 4);

    G_SetDoodadAnimationRect(r, (DWORD)doodadID, animName, animRandom);
    return 0;
}
DWORD Cheat(LPJASS j) {
    //LPCSTR cheatStr = jass_checkstring(j, 1);
    return 0;
}
DWORD IsNoVictoryCheat(LPJASS j) {
    return jass_pushboolean(j, 0);
}
DWORD IsNoDefeatCheat(LPJASS j) {
    return jass_pushboolean(j, 0);
}
DWORD Preload(LPJASS j) {
    //LPCSTR filename = jass_checkstring(j, 1);
    return 0;
}
DWORD PreloadEnd(LPJASS j) {
    //FLOAT timeout = jass_checknumber(j, 1);
    return 0;
}
DWORD PreloadGenClear(LPJASS j) {
    return 0;
}
DWORD PreloadGenStart(LPJASS j) {
    return 0;
}
DWORD PreloadGenEnd(LPJASS j) {
    //LPCSTR filename = jass_checkstring(j, 1);
    return 0;
}
DWORD Preloader(LPJASS j) {
    //LPCSTR filename = jass_checkstring(j, 1);
    return 0;
}

// **************
// 1.29 additions
// **************

DWORD GetPlayerNeutralPassive(LPJASS j) {
    return jass_pushinteger(j, PLAYER_NEUTRAL_PASSIVE);
}
DWORD GetPlayerNeutralAggressive(LPJASS j) {
    return jass_pushinteger(j, PLAYER_NEUTRAL_AGGRESSIVE);
}
DWORD GetBJMaxPlayers(LPJASS j) {
    return jass_pushinteger(j, game.max_clients);
}
DWORD GetBJPlayerNeutralVictim(LPJASS j) {
    return jass_pushinteger(j, PLAYER_NEUTRAL_VICTIM);
}
DWORD GetBJPlayerNeutralExtra(LPJASS j) {
    return jass_pushinteger(j, PLAYER_NEUTRAL_EXTRA);
}
DWORD GetBJMaxPlayerSlots(LPJASS j) {
    return jass_pushinteger(j, 12);
}
DWORD ConvertVersion(LPJASS j) {
    API_ALLOC(DWORD, version);
    *version = jass_checkinteger(j, 1);
    return 1;
}
DWORD ConvertItemType(LPJASS j) {
    API_ALLOC(DWORD, itemtype);
    *itemtype = jass_checkinteger(j, 1);
    return 1;
}
DWORD ConvertAttackType(LPJASS j) {
    API_ALLOC(DWORD, attacktype);
    *attacktype = jass_checkinteger(j, 1);
    return 1;
}
DWORD ConvertDamageType(LPJASS j) {
    API_ALLOC(DWORD, damagetype);
    *damagetype = jass_checkinteger(j, 1);
    return 1;
}
DWORD ConvertWeaponType(LPJASS j) {
    API_ALLOC(DWORD, weapontype);
    *weapontype = jass_checkinteger(j, 1);
    return 1;
}
DWORD ConvertSoundType(LPJASS j) {
    API_ALLOC(DWORD, soundtype);
    *soundtype = jass_checkinteger(j, 1);
    return 1;
}
DWORD ConvertPathingType(LPJASS j) {
    API_ALLOC(DWORD, pathingtype);
    *pathingtype = jass_checkinteger(j, 1);
    return 1;
}
DWORD ConvertMouseButtonType(LPJASS j) {
    API_ALLOC(DWORD, mousebuttontype);
    *mousebuttontype = jass_checkinteger(j, 1);
    return 1;
}
DWORD ConvertAIDifficulty(LPJASS j) {
    API_ALLOC(DWORD, aidifficulty);
    *aidifficulty = jass_checkinteger(j, 1);
    return 1;
}
DWORD ConvertPlayerScore(LPJASS j) {
    API_ALLOC(DWORD, playerscore);
    *playerscore = jass_checkinteger(j, 1);
    return 1;
}
/* Minimal lightning/image/ubersplat handles — presentation is partial; scripts need create/destroy/move/color/show. */
typedef struct {
    VECTOR3 a, b;
    FLOAT color[4];
    BOOL shown;
    char code[8];
} jassLightning_t;
typedef struct {
    VECTOR3 pos, size, origin;
    FLOAT color[4];
    BOOL shown, render, render_always;
    char file[128];
} jassImage_t;
typedef struct {
    VECTOR2 pos;
    FLOAT color[4];
    BOOL shown, render, render_always, finished;
    char name[64];
} jassUbersplat_t;

DWORD AddLightningEx(LPJASS j) {
    LPCSTR codeName = jass_checkstring(j, 1);
    jassLightning_t *bolt = jass_newhandle(j, sizeof(*bolt), "lightning");
    (void)jass_checkboolean(j, 2);
    if (!bolt) return jass_pushnullhandle(j, "lightning");
    memset(bolt, 0, sizeof(*bolt));
    bolt->a = MAKE(VECTOR3, jass_checknumber(j, 3), jass_checknumber(j, 4), jass_checknumber(j, 5));
    bolt->b = MAKE(VECTOR3, jass_checknumber(j, 6), jass_checknumber(j, 7), jass_checknumber(j, 8));
    bolt->color[0] = bolt->color[1] = bolt->color[2] = bolt->color[3] = 1.0f;
    bolt->shown = true;
    if (codeName) strlcpy(bolt->code, codeName, sizeof(bolt->code));
    return 1;
}
DWORD AddLightning(LPJASS j) {
    LPCSTR codeName = jass_checkstring(j, 1);
    BOOL checkVisibility = jass_checkboolean(j, 2);
    FLOAT x1 = jass_checknumber(j, 3), y1 = jass_checknumber(j, 4);
    FLOAT x2 = jass_checknumber(j, 5), y2 = jass_checknumber(j, 6);
    jassLightning_t *bolt = jass_newhandle(j, sizeof(*bolt), "lightning");
    (void)checkVisibility;
    if (!bolt) return jass_pushnullhandle(j, "lightning");
    memset(bolt, 0, sizeof(*bolt));
    bolt->a = MAKE(VECTOR3, x1, y1, 0); bolt->b = MAKE(VECTOR3, x2, y2, 0);
    bolt->color[0] = bolt->color[1] = bolt->color[2] = bolt->color[3] = 1.0f;
    bolt->shown = true;
    if (codeName) strlcpy(bolt->code, codeName, sizeof(bolt->code));
    return 1;
}
DWORD DestroyLightning(LPJASS j) {
    jassLightning_t *bolt = jass_checkhandle(j, 1, "lightning");
    if (bolt) bolt->shown = false;
    return jass_pushboolean(j, bolt != NULL);
}
DWORD MoveLightningEx(LPJASS j) {
    jassLightning_t *bolt = jass_checkhandle(j, 1, "lightning");
    (void)jass_checkboolean(j, 2);
    if (!bolt) return jass_pushboolean(j, 0);
    bolt->a = MAKE(VECTOR3, jass_checknumber(j, 3), jass_checknumber(j, 4), jass_checknumber(j, 5));
    bolt->b = MAKE(VECTOR3, jass_checknumber(j, 6), jass_checknumber(j, 7), jass_checknumber(j, 8));
    return jass_pushboolean(j, 1);
}
DWORD MoveLightning(LPJASS j) {
    jassLightning_t *bolt = jass_checkhandle(j, 1, "lightning");
    (void)jass_checkboolean(j, 2);
    if (!bolt) return jass_pushboolean(j, 0);
    bolt->a.x = jass_checknumber(j, 3); bolt->a.y = jass_checknumber(j, 4);
    bolt->b.x = jass_checknumber(j, 5); bolt->b.y = jass_checknumber(j, 6);
    return jass_pushboolean(j, 1);
}
DWORD SetLightningColor(LPJASS j) {
    jassLightning_t *bolt = jass_checkhandle(j, 1, "lightning");
    if (!bolt) return jass_pushboolean(j, 0);
    bolt->color[0] = jass_checknumber(j, 2); bolt->color[1] = jass_checknumber(j, 3);
    bolt->color[2] = jass_checknumber(j, 4); bolt->color[3] = jass_checknumber(j, 5);
    return jass_pushboolean(j, 1);
}
DWORD GetLightningColorR(LPJASS j) { jassLightning_t *b = jass_checkhandle(j, 1, "lightning"); return jass_pushnumber(j, b ? b->color[0] : 0); }
DWORD GetLightningColorG(LPJASS j) { jassLightning_t *b = jass_checkhandle(j, 1, "lightning"); return jass_pushnumber(j, b ? b->color[1] : 0); }
DWORD GetLightningColorB(LPJASS j) { jassLightning_t *b = jass_checkhandle(j, 1, "lightning"); return jass_pushnumber(j, b ? b->color[2] : 0); }
DWORD GetLightningColorA(LPJASS j) { jassLightning_t *b = jass_checkhandle(j, 1, "lightning"); return jass_pushnumber(j, b ? b->color[3] : 0); }

DWORD CreateImage(LPJASS j) {
    LPCSTR file = jass_checkstring(j, 1);
    jassImage_t *img = jass_newhandle(j, sizeof(*img), "image");
    if (!img) return jass_pushnullhandle(j, "image");
    memset(img, 0, sizeof(*img));
    img->size = MAKE(VECTOR3, jass_checknumber(j, 2), jass_checknumber(j, 3), jass_checknumber(j, 4));
    img->pos = MAKE(VECTOR3, jass_checknumber(j, 5), jass_checknumber(j, 6), jass_checknumber(j, 7));
    img->origin = MAKE(VECTOR3, jass_checknumber(j, 8), jass_checknumber(j, 9), jass_checknumber(j, 10));
    (void)jass_checkinteger(j, 11);
    img->color[0] = img->color[1] = img->color[2] = img->color[3] = 1.0f;
    img->shown = true;
    if (file) strlcpy(img->file, file, sizeof(img->file));
    return 1;
}
DWORD DestroyImage(LPJASS j) { jassImage_t *img = jass_checkhandle(j, 1, "image"); if (img) img->shown = false; return 0; }
DWORD ShowImage(LPJASS j) { jassImage_t *img = jass_checkhandle(j, 1, "image"); if (img) img->shown = jass_checkboolean(j, 2); return 0; }
DWORD SetImagePosition(LPJASS j) {
    jassImage_t *img = jass_checkhandle(j, 1, "image");
    if (img) img->pos = MAKE(VECTOR3, jass_checknumber(j, 2), jass_checknumber(j, 3), jass_checknumber(j, 4));
    return 0;
}
DWORD SetImageColor(LPJASS j) {
    jassImage_t *img = jass_checkhandle(j, 1, "image");
    if (img) {
        img->color[0] = jass_checkinteger(j, 2) / 255.0f; img->color[1] = jass_checkinteger(j, 3) / 255.0f;
        img->color[2] = jass_checkinteger(j, 4) / 255.0f; img->color[3] = jass_checkinteger(j, 5) / 255.0f;
    }
    return 0;
}
DWORD SetImageRender(LPJASS j) { jassImage_t *img = jass_checkhandle(j, 1, "image"); if (img) img->render = jass_checkboolean(j, 2); return 0; }
DWORD SetImageRenderAlways(LPJASS j) { jassImage_t *img = jass_checkhandle(j, 1, "image"); if (img) img->render_always = jass_checkboolean(j, 2); return 0; }
DWORD SetImageConstantHeight(LPJASS j) { (void)j; return 0; }
DWORD SetImageAboveWater(LPJASS j) { (void)j; return 0; }
DWORD SetImageType(LPJASS j) { (void)j; return 0; }

DWORD CreateUbersplat(LPJASS j) {
    jassUbersplat_t *u = jass_newhandle(j, sizeof(*u), "ubersplat");
    LPCSTR name = jass_checkstring(j, 3);
    if (!u) return jass_pushnullhandle(j, "ubersplat");
    memset(u, 0, sizeof(*u));
    u->pos = MAKE(VECTOR2, jass_checknumber(j, 1), jass_checknumber(j, 2));
    u->color[0] = jass_checkinteger(j, 4) / 255.0f; u->color[1] = jass_checkinteger(j, 5) / 255.0f;
    u->color[2] = jass_checkinteger(j, 6) / 255.0f; u->color[3] = jass_checkinteger(j, 7) / 255.0f;
    (void)jass_checkboolean(j, 8); (void)jass_checkboolean(j, 9);
    u->shown = true;
    if (name) strlcpy(u->name, name, sizeof(u->name));
    return 1;
}
DWORD DestroyUbersplat(LPJASS j) { jassUbersplat_t *u = jass_checkhandle(j, 1, "ubersplat"); if (u) u->shown = false; return 0; }
DWORD ResetUbersplat(LPJASS j) { jassUbersplat_t *u = jass_checkhandle(j, 1, "ubersplat"); if (u) u->finished = false; return 0; }
DWORD FinishUbersplat(LPJASS j) { jassUbersplat_t *u = jass_checkhandle(j, 1, "ubersplat"); if (u) u->finished = true; return 0; }
DWORD ShowUbersplat(LPJASS j) { jassUbersplat_t *u = jass_checkhandle(j, 1, "ubersplat"); if (u) u->shown = jass_checkboolean(j, 2); return 0; }
DWORD SetUbersplatRender(LPJASS j) { jassUbersplat_t *u = jass_checkhandle(j, 1, "ubersplat"); if (u) u->render = jass_checkboolean(j, 2); return 0; }
DWORD SetUbersplatRenderAlways(LPJASS j) { jassUbersplat_t *u = jass_checkhandle(j, 1, "ubersplat"); if (u) u->render_always = jass_checkboolean(j, 2); return 0; }

DWORD VersionGet(LPJASS j) {
    API_ALLOC(DWORD, version);
    *version = 0;
    return 1;
}
/* Version enums are typed handles from ConvertVersion, not integer arguments. */
DWORD VersionCompatible(LPJASS j) {
    LPDWORD version = jass_checkhandle(j, 1, "version");
    return jass_pushboolean(j, version && *version == 0);
}
DWORD VersionSupported(LPJASS j) {
    LPDWORD version = jass_checkhandle(j, 1, "version");
    return jass_pushboolean(j, version && *version == 0);
}
