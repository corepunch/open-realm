extern LPPLAYER currentplayer;

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
MATH_FUNC(OrderId, class_id, string, integer);
MATH_FUNC(UnitId, class_id, string, integer);
MATH_FUNC(AbilityId, class_id, string, integer);
MATH_FUNC(OrderId2String, GetClassName, integer, string);
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
    API_ALLOC(GTIMER, timer);
    if (!G_RegisterJassTimer(timer)) jass_rterror(j, "CreateTimer: timer registry is full");
    return 1;
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
DWORD GetWorldBounds(LPJASS j) {
    return jass_pushnullhandle(j, "rect");
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
    return jass_pushinteger(j, 0);
}
DWORD GetTrainedUnitType(LPJASS j) {
    return jass_pushinteger(j, 0);
}
DWORD GetTrainedUnit(LPJASS j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->unit, "unit");
}
DWORD GetDetectedUnit(LPJASS j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->unit, "unit");
}
DWORD GetSummoningUnit(LPJASS j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->unit, "unit");
}
DWORD GetSummonedUnit(LPJASS j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->unit, "unit");
}
DWORD GetTransportUnit(LPJASS j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->unit, "unit");
}
DWORD GetLoadedUnit(LPJASS j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->unit, "unit");
}
DWORD GetManipulatingUnit(LPJASS j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->unit, "unit");
}
DWORD GetOrderedUnit(LPJASS j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->unit, "unit");
}
DWORD GetIssuedOrderId(LPJASS j) {
    return jass_pushinteger(j, 0);
}
DWORD GetOrderPointX(LPJASS j) {
    return jass_pushnumber(j, 0);
}
DWORD GetOrderPointY(LPJASS j) {
    return jass_pushnumber(j, 0);
}
DWORD GetOrderPointLoc(LPJASS j) {
    return jass_pushnullhandle(j, "location");
}
DWORD GetOrderTarget(LPJASS j) {
    return jass_pushnullhandle(j, "widget");
}
DWORD GetOrderTargetDestructable(LPJASS j) {
    return jass_pushnullhandle(j, "destructable");
}
DWORD GetOrderTargetUnit(LPJASS j) {
    return jass_pushlighthandle(j, jass_getcontext(j)->unit, "unit");
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
    return jass_pushnumber(j, 0);
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
        whichWidget->health.value = newLife;
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
    //BOOL cleared = jass_checkboolean(j, 1);
    return 0;
}
DWORD SetMissionAvailable(LPJASS j) {
    //LONG campaignNumber = jass_checkinteger(j, 1);
    //LONG missionNumber = jass_checkinteger(j, 2);
    //BOOL available = jass_checkboolean(j, 3);
    return 0;
}
DWORD SetCampaignAvailable(LPJASS j) {
    //LONG campaignNumber = jass_checkinteger(j, 1);
    //BOOL available = jass_checkboolean(j, 2);
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
    //LONG style = jass_checkinteger(j, 1);
    //FLOAT zstart = jass_checknumber(j, 2);
    //FLOAT zend = jass_checknumber(j, 3);
    //FLOAT density = jass_checknumber(j, 4);
    //FLOAT red = jass_checknumber(j, 5);
    //FLOAT green = jass_checknumber(j, 6);
    //FLOAT blue = jass_checknumber(j, 7);
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
    (void)terrainDNCFile;
    (void)unitDNCFile;
    return 0;
}
DWORD SetSkyModel(LPJASS j) {
    //LPCSTR skyModelFile = jass_checkstring(j, 1);
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
    //HANDLE whichWidget = jass_checkhandle(j, 1, "widget");
    //LONG red = jass_checkinteger(j, 2);
    //LONG green = jass_checkinteger(j, 3);
    //LONG blue = jass_checkinteger(j, 4);
    //LONG alpha = jass_checkinteger(j, 5);
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
    //LPCSTR movieName = jass_checkstring(j, 1);
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
    //HANDLE t = jass_checkhandle(j, 1, "timer");
    return jass_pushnullhandle(j, "timerdialog");
}
DWORD DestroyTimerDialog(LPJASS j) {
    //HANDLE whichDialog = jass_checkhandle(j, 1, "timerdialog");
    return 0;
}
DWORD TimerDialogSetTitle(LPJASS j) {
    //HANDLE whichDialog = jass_checkhandle(j, 1, "timerdialog");
    //LPCSTR title = jass_checkstring(j, 2);
    return 0;
}
DWORD TimerDialogSetTitleColor(LPJASS j) {
    //HANDLE whichDialog = jass_checkhandle(j, 1, "timerdialog");
    //LONG red = jass_checkinteger(j, 2);
    //LONG green = jass_checkinteger(j, 3);
    //LONG blue = jass_checkinteger(j, 4);
    //LONG alpha = jass_checkinteger(j, 5);
    return 0;
}
DWORD TimerDialogSetTimeColor(LPJASS j) {
    //HANDLE whichDialog = jass_checkhandle(j, 1, "timerdialog");
    //LONG red = jass_checkinteger(j, 2);
    //LONG green = jass_checkinteger(j, 3);
    //LONG blue = jass_checkinteger(j, 4);
    //LONG alpha = jass_checkinteger(j, 5);
    return 0;
}
DWORD TimerDialogSetSpeed(LPJASS j) {
    //HANDLE whichDialog = jass_checkhandle(j, 1, "timerdialog");
    //FLOAT speedMultFactor = jass_checknumber(j, 2);
    return 0;
}
DWORD TimerDialogDisplay(LPJASS j) {
    //HANDLE whichDialog = jass_checkhandle(j, 1, "timerdialog");
    //BOOL display = jass_checkboolean(j, 2);
    return 0;
}
DWORD IsTimerDialogDisplayed(LPJASS j) {
    //HANDLE whichDialog = jass_checkhandle(j, 1, "timerdialog");
    return jass_pushboolean(j, 0);
}
DWORD SetCinematicScene(LPJASS j) {
    LONG portraitUnitId = jass_checkinteger(j, 1);
    //HANDLE color = jass_checkhandle(j, 2, "playercolor");
    LPCSTR speakerTitle = jass_checkstring(j, 3);
    LPCSTR text = jass_checkstring(j, 4);
    FLOAT sceneDuration = jass_checknumber(j, 5);
    FLOAT voiceoverDuration = jass_checknumber(j, 6);
    if (G_SkipCutscene()) return 0;
    if (currentplayer) {
        LPGAMECLIENT gc = PLAYER_CLIENT(currentplayer);
        DWORD now = gi.GetTime();
        G_SetPlayerText(gc, PLAYERTEXT_SPEAKER, G_LevelString(speakerTitle));
        G_SetPlayerText(gc, PLAYERTEXT_DIALOGUE, G_LevelString(text));
        currentplayer->cinematic_portrait = 0;
        if (portraitUnitId) {
            LPCSTR model = G_UnitUI((DWORD)portraitUnitId)->modelFile;
            if (model && *model) {
                PATHSTR mf;
                snprintf(mf, sizeof(mf), "%s.mdx", model);
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
    if (currentplayer) {
        LPGAMECLIENT gc = PLAYER_CLIENT(currentplayer);
        G_SetPlayerText(gc, PLAYERTEXT_SPEAKER, "");
        G_SetPlayerText(gc, PLAYERTEXT_DIALOGUE, "");
        currentplayer->cinematic_portrait = 0;
        if (gc) {
            gc->cinematic_end_time = 0;
            gc->cinematic_voice_end_time = 0;
        }
        UI_InvalidateDialoguePresentation(PLAYER_ENT(currentplayer));
    }
    return 0;
}
DWORD NewSoundEnvironment(LPJASS j) {
    //LPCSTR environmentName = jass_checkstring(j, 1);
    return 0;
}
DWORD SetDoodadAnimation(LPJASS j) {
    //FLOAT x = jass_checknumber(j, 1);
    //FLOAT y = jass_checknumber(j, 2);
    //FLOAT radius = jass_checknumber(j, 3);
    //LONG doodadID = jass_checkinteger(j, 4);
    //BOOL nearestOnly = jass_checkboolean(j, 5);
    //LPCSTR animName = jass_checkstring(j, 6);
    //BOOL animRandom = jass_checkboolean(j, 7);
    return 0;
}
DWORD SetDoodadAnimationRect(LPJASS j) {
    //HANDLE r = jass_checkhandle(j, 1, "rect");
    //LONG doodadID = jass_checkinteger(j, 2);
    //LPCSTR animName = jass_checkstring(j, 3);
    //BOOL animRandom = jass_checkboolean(j, 4);
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
DWORD VersionGet(LPJASS j) {
    API_ALLOC(DWORD, version);
    *version = 0;
    return 1;
}
DWORD VersionCompatible(LPJASS j) {
    return jass_pushboolean(j, jass_checkinteger(j, 1) == 0);
}
DWORD VersionSupported(LPJASS j) {
    return jass_pushboolean(j, jass_checkinteger(j, 1) == 0);
}
